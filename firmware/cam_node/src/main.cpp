// -----------------------------------------------------------------------------
// IoT-Enabled Caterpillar and Aphid Detection and Removal System
//
// Detection node firmware - ESP32-CAM (AI-Thinker).
//
// This board does one job: look at the scene in front of the UV lure, decide
// whether what it sees is a caterpillar, an aphid colony, or something the
// trap should leave alone, and report that over the UART to the controller.
// All actuation and all networking live on the main controller.
//
// Flashing: disconnect the two link wires (GPIO13/14 are free, but the USB
// programmer needs the board's own U0TXD/U0RXD and GPIO0 pulled low).
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "detector.h"
#include "link.h"

#if CAM_WIFI_DIAGNOSTICS
#include <WiFi.h>
#include <WebServer.h>
#include <esp_camera.h>
static WebServer diag(80);
#endif

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

namespace {

enum NodeState : uint8_t {
  STATE_BOOT = 0,
  STATE_LEARNING = 1,
  STATE_ARMED = 2,
  STATE_ERROR = 3
};

NodeState state_ = STATE_BOOT;
bool camera_ok_ = false;
bool uv_on_ = false;
uint32_t last_status_ms_ = 0;
float fps_ = 0.0f;

void wdtBegin(uint32_t seconds) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_deinit();
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = seconds * 1000;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  esp_task_wdt_init(&cfg);
#else
  esp_task_wdt_init(seconds, true);
#endif
  esp_task_wdt_add(NULL);
}

void onConfig(bool uv_on, uint8_t sensitivity) {
  detector::setSensitivity(sensitivity);
  if (uv_on != uv_on_) {
    // The UV array changing state rewrites the whole scene. Relearn rather
    // than report the illumination step as a detection.
    uv_on_ = uv_on;
    detector::resetBackground();
    state_ = STATE_LEARNING;
  }
}

void onPing() {
  link::sendStatus(state_, ESP.getFreeHeap(), detector::framesProcessed(),
                   detector::frameErrors());
}

#if CAM_WIFI_DIAGNOSTICS
void serveJpeg() {
  // Calibration aid only: lets you aim the lens and check framing from a
  // phone. Uses a separate JPEG grab, so it does not disturb the grayscale
  // detection pipeline's frame buffers beyond one frame time.
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { diag.send(503, "text/plain", "no frame"); return; }
  diag.sendHeader("Content-Type", "image/x-portable-graymap");
  String hdr = "P5\n" + String(fb->width) + " " + String(fb->height) + "\n255\n";
  diag.setContentLength(hdr.length() + fb->len);
  diag.send(200, "image/x-portable-graymap", "");
  diag.client().print(hdr);
  diag.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void serveBlobs() {
  uint8_t n = 0;
  const Blob* b = detector::blobs(&n);
  String out = "[";
  for (uint8_t i = 0; i < n; ++i) {
    if (i) out += ",";
    out += "{\"a\":" + String(b[i].area) +
           ",\"x\":" + String(b[i].cx, 1) +
           ",\"y\":" + String(b[i].cy, 1) +
           ",\"e\":" + String(b[i].elongation, 2) +
           ",\"f\":" + String(b[i].fill, 2) +
           ",\"v\":" + String(b[i].speed, 1) +
           ",\"age\":" + String(b[i].age) +
           ",\"cls\":" + String(b[i].cls) +
           ",\"conf\":" + String(b[i].confidence) + "}";
  }
  out += "]";
  diag.send(200, "application/json", out);
}
#endif

}  // namespace

void setup() {
  // The link shares no pins with U0, so the USB console stays usable for
  // debugging while the sentence link runs on UART1.
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[cam] detection node v%s\n", CAM_NODE_VERSION);

  link::begin(onConfig, onPing);

  camera_ok_ = detector::begin();
  if (!camera_ok_) {
    Serial.println("[cam] camera init FAILED");
    state_ = STATE_ERROR;
  } else {
    detector::setSensitivity(50);
    state_ = STATE_LEARNING;
  }

#if CAM_WIFI_DIAGNOSTICS
  WiFi.begin(CAM_WIFI_SSID, CAM_WIFI_PASS);
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[cam] diagnostics at http://%s/\n",
                  WiFi.localIP().toString().c_str());
    diag.on("/pgm", serveJpeg);
    diag.on("/blobs", serveBlobs);
    diag.begin();
  }
#endif

  wdtBegin(30);
  Serial.println("[cam] ready");
}

void loop() {
  esp_task_wdt_reset();
  link::poll();
#if CAM_WIFI_DIAGNOSTICS
  diag.handleClient();
#endif

  if (!camera_ok_) {
    // Nothing useful to do; keep answering the controller so it can decide to
    // power-cycle this board.
    state_ = STATE_ERROR;
    if (millis() - last_status_ms_ > 2000) {
      last_status_ms_ = millis();
      link::sendStatus(state_, ESP.getFreeHeap(), 0, detector::frameErrors());
    }
    delay(100);
    return;
  }

  const uint32_t t0 = millis();
  const DetectionResult r = detector::process();
  const uint32_t dt = millis() - t0;
  if (dt > 0) {
    const float inst = 1000.0f / dt;
    fps_ = (fps_ == 0.0f) ? inst : (fps_ * 0.8f + inst * 0.2f);
  }

  state_ = r.learning ? STATE_LEARNING : STATE_ARMED;

  // Every processed frame produces a DET sentence, whether or not anything was
  // found. A steady stream is how the controller knows the node is alive, and
  // the zero rows are what make the false-positive rate measurable afterwards.
  if (!r.learning) {
    link::sendDetection(r.caterpillars, r.aphids, r.nontarget,
                        r.best_confidence, r.motion_px, fps_);
  }

  if (millis() - last_status_ms_ > STATUS_INTERVAL_MS) {
    last_status_ms_ = millis();
    link::sendStatus(state_, ESP.getFreeHeap(), detector::framesProcessed(),
                     detector::frameErrors());
    Serial.printf("[cam] state=%u fps=%.1f cat=%u aph=%u non=%u motion=%lu\n",
                  state_, fps_, r.caterpillars, r.aphids, r.nontarget,
                  static_cast<unsigned long>(r.motion_px));
  }

  // ~4 frames/s is plenty for insects that walk, and it keeps the node's own
  // draw down, which matters on a 9 Ah battery running all night.
  delay(150);
}
