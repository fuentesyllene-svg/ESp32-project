#include "detector.h"
#include "camera_pins.h"
#include <esp_camera.h>
#include <math.h>

// -----------------------------------------------------------------------------
// Detection pipeline
//
//   QVGA grayscale frame
//     -> 4x4 box downsample to an 80x60 working grid
//     -> per-pixel background subtraction (selective exponential model)
//     -> connected-component labelling of the foreground
//     -> per-blob shape features (area, elongation, solidity, contrast)
//     -> frame-to-frame tracking for speed and persistence
//     -> classification: caterpillar / aphid / non-target
//
// The selective background update is the part that matters biologically: a
// plain moving-average model absorbs a stationary caterpillar within a few
// seconds and the trap goes blind to exactly the pest it is looking for. Here
// the model updates 20x slower inside an active blob, so a settled larva stays
// visible while the leaf behind it still adapts to wind and cloud.
// -----------------------------------------------------------------------------

namespace {

uint16_t bg_[GRID_N];          // background model, grey level << 6 fixed point
uint8_t  cur_[GRID_N];         // current downsampled frame
uint8_t  fg_[GRID_N];          // foreground mask, 0/1
uint8_t  visited_[GRID_N];
int16_t  stack_[GRID_N];       // flood-fill stack, indices into the grid

Blob blobs_[MAX_BLOBS];
uint8_t blob_count_ = 0;

// Previous-frame centroids, for tracking.
struct Track { float cx, cy; uint8_t age; uint8_t id; bool used; };
Track tracks_[MAX_BLOBS];
uint8_t track_count_ = 0;
uint8_t next_id_ = 1;

uint8_t sensitivity_ = 50;
int     frames_since_reset_ = 0;
uint32_t frames_ok_ = 0, frame_errors_ = 0;
detector::ExternalClassifier external_ = nullptr;

int fgThreshold() {
  // sensitivity 0 -> least sensitive (high threshold), 100 -> most sensitive.
  const float scale = 1.0f + (50 - static_cast<int>(sensitivity_)) / 50.0f;
  int t = static_cast<int>(FG_THRESHOLD_BASE * scale);
  if (t < FG_THRESHOLD_MIN) t = FG_THRESHOLD_MIN;
  if (t > FG_THRESHOLD_MAX) t = FG_THRESHOLD_MAX;
  return t;
}

// 4x4 box downsample of a QVGA grayscale frame into cur_.
void downsample(const uint8_t* src, int src_w, int src_h) {
  const int bx = src_w / GRID_W;      // 4
  const int by = src_h / GRID_H;      // 4
  for (int gy = 0; gy < GRID_H; ++gy) {
    for (int gx = 0; gx < GRID_W; ++gx) {
      uint32_t acc = 0;
      for (int y = 0; y < by; ++y) {
        const uint8_t* row = src + (gy * by + y) * src_w + gx * bx;
        for (int x = 0; x < bx; ++x) acc += row[x];
      }
      cur_[gy * GRID_W + gx] = static_cast<uint8_t>(acc / (bx * by));
    }
  }
}

void seedBackground() {
  for (int i = 0; i < GRID_N; ++i) bg_[i] = static_cast<uint16_t>(cur_[i]) << 6;
  frames_since_reset_ = 0;
  track_count_ = 0;
}

// Flood fill from a seed, 8-connected, collecting the blob's raw moments.
void growBlob(int seed, Blob& b) {
  int sp = 0;
  stack_[sp++] = seed;
  visited_[seed] = 1;

  uint32_t n = 0;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  int32_t sum_cur = 0, sum_bg = 0;
  int x0 = GRID_W, y0 = GRID_H, x1 = 0, y1 = 0;

  while (sp > 0) {
    const int idx = stack_[--sp];
    const int x = idx % GRID_W;
    const int y = idx / GRID_W;

    ++n;
    sx += x; sy += y;
    sxx += static_cast<double>(x) * x;
    syy += static_cast<double>(y) * y;
    sxy += static_cast<double>(x) * y;
    sum_cur += cur_[idx];
    sum_bg  += bg_[idx] >> 6;
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;

    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue;
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) continue;
        const int nidx = ny * GRID_W + nx;
        if (fg_[nidx] && !visited_[nidx] && sp < GRID_N) {
          visited_[nidx] = 1;
          stack_[sp++] = nidx;
        }
      }
    }
  }

  b.area = static_cast<uint16_t>(n);
  b.cx = static_cast<float>(sx / n);
  b.cy = static_cast<float>(sy / n);
  b.x0 = x0; b.y0 = y0; b.x1 = x1; b.y1 = y1;
  b.contrast = static_cast<int16_t>((sum_cur - sum_bg) / static_cast<int32_t>(n));

  const float bbox_area = static_cast<float>((x1 - x0 + 1) * (y1 - y0 + 1));
  b.fill = n / bbox_area;

  // Second-order central moments -> principal axes. Orientation-independent,
  // unlike a bounding-box ratio, so a diagonal caterpillar still reads as
  // elongated.
  const double mu20 = sxx / n - (sx / n) * (sx / n);
  const double mu02 = syy / n - (sy / n) * (sy / n);
  const double mu11 = sxy / n - (sx / n) * (sy / n);
  const double tr = mu20 + mu02;
  const double det = sqrt((mu20 - mu02) * (mu20 - mu02) + 4.0 * mu11 * mu11);
  const double l1 = (tr + det) / 2.0;
  const double l2 = (tr - det) / 2.0;
  b.elongation = (l2 > 0.05) ? static_cast<float>(sqrt(l1 / l2)) : 6.0f;

  b.speed = 0;
  b.age = 1;
  b.cls = CLASS_UNKNOWN;
  b.confidence = 0;
  b.id = 0;
}

void segment(int threshold, uint32_t* motion_px_out) {
  uint32_t motion = 0;
  for (int i = 0; i < GRID_N; ++i) {
    const int diff = abs(static_cast<int>(cur_[i]) - static_cast<int>(bg_[i] >> 6));
    fg_[i] = (diff >= threshold) ? 1 : 0;
    visited_[i] = 0;
    motion += fg_[i];
  }
  *motion_px_out = motion;

  blob_count_ = 0;
  for (int i = 0; i < GRID_N && blob_count_ < MAX_BLOBS; ++i) {
    if (!fg_[i] || visited_[i]) continue;
    Blob b{};
    growBlob(i, b);
    if (b.area < BLOB_MIN_AREA) continue;     // sensor noise
    blobs_[blob_count_++] = b;
  }
}

// Nearest-centroid association with the previous frame. Gives every blob a
// speed and an age; both are needed to separate a settled larva from a bee
// passing through the beam.
void track() {
  for (uint8_t i = 0; i < track_count_; ++i) tracks_[i].used = false;

  for (uint8_t i = 0; i < blob_count_; ++i) {
    Blob& b = blobs_[i];
    int best = -1;
    float best_d = TRACK_MATCH_RADIUS;
    for (uint8_t t = 0; t < track_count_; ++t) {
      if (tracks_[t].used) continue;
      const float dx = b.cx - tracks_[t].cx;
      const float dy = b.cy - tracks_[t].cy;
      const float d = sqrtf(dx * dx + dy * dy);
      if (d < best_d) { best_d = d; best = t; }
    }
    if (best >= 0) {
      tracks_[best].used = true;
      b.speed = best_d;
      b.age = (tracks_[best].age < 255) ? tracks_[best].age + 1 : 255;
      b.id = tracks_[best].id;
    } else {
      b.speed = 0;
      b.age = 1;
      b.id = next_id_++;
      if (next_id_ == 0) next_id_ = 1;
    }
  }

  track_count_ = blob_count_;
  for (uint8_t i = 0; i < blob_count_; ++i) {
    tracks_[i].cx = blobs_[i].cx;
    tracks_[i].cy = blobs_[i].cy;
    tracks_[i].age = blobs_[i].age;
    tracks_[i].id = blobs_[i].id;
    tracks_[i].used = false;
  }
}

// Trapezoidal membership: 0 outside [lo, hi], 1 across the [lo_full, hi_full]
// plateau, linear on the shoulders. Used to grade the geometric gates instead
// of scoring them as a hard yes/no.
//
// A plateau rather than a distance-from-centre score matters here: the accepted
// caterpillar area spans 12-400 grid px, but real larvae at this working
// distance sit near the bottom of that range. Scoring by centrality would give
// a textbook 30 px larva about 9 % confidence and it would never clear the
// capture threshold, while a 200 px blob - far more likely to be a leaf edge -
// would score highest.
float plateau(float v, float lo, float lo_full, float hi_full, float hi) {
  if (v <= lo || v >= hi) return 0.0f;
  if (v >= lo_full && v <= hi_full) return 1.0f;
  if (v < lo_full) return (v - lo) / (lo_full - lo);
  return (hi - v) / (hi - hi_full);
}

void classify() {
  // Pass 1: independent per-blob decisions.
  for (uint8_t i = 0; i < blob_count_; ++i) {
    Blob& b = blobs_[i];

    if (external_) {
      uint8_t conf = 0;
      const uint8_t verdict = external_(b, &conf);
      if (verdict != CLASS_UNKNOWN) {
        b.cls = verdict;
        b.confidence = conf;
        continue;
      }
    }

    if (b.area > BLOB_MAX_AREA) {            // leaf, hand, shadow front
      b.cls = CLASS_NONTARGET;
      b.confidence = 0;
      continue;
    }
    if (b.speed > FAST_SPEED_PX) {           // in flight: not our target
      b.cls = CLASS_NONTARGET;
      b.confidence = 0;
      continue;
    }
    if (b.age < MIN_AGE_FRAMES) {            // not persistent enough yet
      b.cls = CLASS_UNKNOWN;
      b.confidence = 0;
      continue;
    }

    const bool cat_size  = b.area >= CAT_MIN_AREA && b.area <= CAT_MAX_AREA;
    const bool cat_shape = b.elongation >= CAT_MIN_ELONGATION;
    if (cat_size && cat_shape) {
      float c = 0.45f * plateau(b.area, CAT_MIN_AREA, CAT_CONFIDENT_AREA,
                                CAT_MAX_AREA * 0.65f, CAT_MAX_AREA);
      c += 0.35f * fminf(1.0f, (b.elongation - CAT_MIN_ELONGATION) / 2.0f + 0.4f);
      c += 0.10f * fminf(1.0f, b.fill * 1.4f);        // solid body, not a twig
      c += 0.10f * fminf(1.0f, b.age / 8.0f);         // rewards persistence
      b.cls = CLASS_CATERPILLAR;
      b.confidence = static_cast<uint8_t>(fminf(100.0f, c * 100.0f));
      continue;
    }

    if (b.area >= APH_MIN_AREA && b.area <= APH_MAX_AREA) {
      b.cls = CLASS_APHID;                   // provisional; pass 2 confirms
      b.confidence = 0;
      continue;
    }

    b.cls = CLASS_NONTARGET;
    b.confidence = 0;
  }

  // Pass 2: aphids only count as aphids in a colony. A single 5-pixel speck is
  // far more likely to be sensor noise or a dust mote than an aphid, but
  // aphids on lettuce arrive in dense clusters, and that clustering is the
  // most reliable signal available at this resolution.
  for (uint8_t i = 0; i < blob_count_; ++i) {
    if (blobs_[i].cls != CLASS_APHID) continue;
    uint8_t neighbours = 1;
    for (uint8_t j = 0; j < blob_count_; ++j) {
      if (i == j || blobs_[j].cls != CLASS_APHID) continue;
      const float dx = blobs_[i].cx - blobs_[j].cx;
      const float dy = blobs_[i].cy - blobs_[j].cy;
      if (sqrtf(dx * dx + dy * dy) <= APH_CLUSTER_RADIUS) ++neighbours;
    }
    if (neighbours >= APH_MIN_CLUSTER) {
      float c = 0.5f + 0.1f * (neighbours - APH_MIN_CLUSTER);
      c += 0.1f * fminf(1.0f, blobs_[i].age / 6.0f);
      blobs_[i].confidence = static_cast<uint8_t>(fminf(100.0f, c * 100.0f));
    } else {
      blobs_[i].cls = CLASS_NONTARGET;
      blobs_[i].confidence = 0;
    }
  }
}

void updateBackground() {
  // Mark which pixels sit inside a blob that is currently classified as a
  // target, so those regions age into the background far more slowly.
  static uint8_t protect[GRID_N];
  memset(protect, 0, sizeof(protect));
  for (uint8_t i = 0; i < blob_count_; ++i) {
    const Blob& b = blobs_[i];
    if (b.cls != CLASS_CATERPILLAR && b.cls != CLASS_APHID &&
        b.cls != CLASS_UNKNOWN) {
      continue;
    }
    for (int y = b.y0; y <= b.y1; ++y) {
      for (int x = b.x0; x <= b.x1; ++x) protect[y * GRID_W + x] = 1;
    }
  }

  for (int i = 0; i < GRID_N; ++i) {
    const float alpha = protect[i] ? BG_ALPHA_BLOB : BG_ALPHA;
    const float bgv = (bg_[i] >> 6);
    const float nv = bgv + alpha * (static_cast<float>(cur_[i]) - bgv);
    bg_[i] = static_cast<uint16_t>(lroundf(nv * 64.0f));
  }
}

}  // namespace

namespace detector {

bool begin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;   c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;   c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;   c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;   c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  // Grayscale: the pipeline is intensity-based, and a QVGA grey frame is a
  // quarter the memory and about three times faster to process than JPEG
  // decode + colour conversion on this part.
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size   = FRAMESIZE_QVGA;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  if (!psramFound()) {                    // fall back gracefully
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count = 1;
  }

  if (esp_camera_init(&c) != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // Night scene under a UV-A array: fix the gain ceiling so auto-exposure
    // does not pump the background between frames, which would swamp the
    // difference image.
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, GAINCEILING_16X);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_whitebal(s, 0);                // irrelevant in grayscale, saves work
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
  }

  memset(bg_, 0, sizeof(bg_));
  frames_since_reset_ = 0;
  return true;
}

void setExternalClassifier(ExternalClassifier fn) { external_ = fn; }

void setSensitivity(uint8_t pct) { sensitivity_ = (pct > 100) ? 100 : pct; }

void resetBackground() { frames_since_reset_ = -1; }   // reseed on next frame

DetectionResult process() {
  DetectionResult r{};

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    ++frame_errors_;
    r.learning = true;
    return r;
  }
  if (fb->width < GRID_W * 4 || fb->height < GRID_H * 4) {
    esp_camera_fb_return(fb);
    ++frame_errors_;
    r.learning = true;
    return r;
  }

  downsample(fb->buf, fb->width, fb->height);
  esp_camera_fb_return(fb);
  ++frames_ok_;

  if (frames_since_reset_ <= 0) {
    seedBackground();
    r.learning = true;
    r.scene_reset = true;
    ++frames_since_reset_;
    return r;
  }

  uint32_t motion = 0;
  segment(fgThreshold(), &motion);
  r.motion_px = motion;

  // A global change is a lighting event, not an infestation. Reseeding here is
  // what stops the UV array switching on from being reported as a swarm.
  if (motion > static_cast<uint32_t>(GRID_N * GLOBAL_CHANGE_PCT / 100)) {
    seedBackground();
    r.learning = true;
    r.scene_reset = true;
    return r;
  }

  ++frames_since_reset_;
  if (frames_since_reset_ < BG_LEARN_FRAMES) {
    updateBackground();
    r.learning = true;
    return r;
  }

  track();
  classify();

  for (uint8_t i = 0; i < blob_count_; ++i) {
    switch (blobs_[i].cls) {
      case CLASS_CATERPILLAR: ++r.caterpillars; break;
      case CLASS_APHID:       ++r.aphids; break;
      case CLASS_NONTARGET:   ++r.nontarget; break;
      default: break;                        // UNKNOWN: still stabilising
    }
    if (blobs_[i].cls == CLASS_CATERPILLAR || blobs_[i].cls == CLASS_APHID) {
      if (blobs_[i].confidence > r.best_confidence) {
        r.best_confidence = blobs_[i].confidence;
      }
    }
  }
  r.blob_count = blob_count_;

  updateBackground();
  return r;
}

const Blob* blobs(uint8_t* count) {
  if (count) *count = blob_count_;
  return blobs_;
}

uint32_t framesProcessed() { return frames_ok_; }
uint32_t frameErrors() { return frame_errors_; }

}  // namespace detector
