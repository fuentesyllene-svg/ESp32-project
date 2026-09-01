// -----------------------------------------------------------------------------
// Host-side test harness.
//
// The ESP32 toolchain is not available in every environment, and a firmware
// bug found on a pole in a lettuce plot at 2 a.m. is expensive. These tests
// compile the real firmware sources against small shims and exercise the parts
// that carry the research's claims:
//
//   * the state-of-charge model behind the 30 % power-autonomy criterion
//   * the trap state machine (load shedding, hysteresis, UV duty cycle)
//   * the controller <-> camera sentence parser, including corruption
//   * the detection pipeline, driven with synthetic frames
//
// Build and run:  make -C test/host
// -----------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#include "Arduino.h"
#include "esp_camera.h"

#include "power.h"
#include "trap.h"
#include "cam_link.h"
#include "detector.h"
#include "config.h"

namespace hw {
extern uint8_t  uv_pct;
extern bool     fan;
extern uint32_t fan_budget_s;
extern uint32_t fan_starts;
void reset();
}

// ------------------------------------------------------------- test runner --
namespace {
int checks_ = 0, failures_ = 0;
const char* group_ = "";

void group(const char* g) { group_ = g; printf("\n== %s ==\n", g); }

void check(bool cond, const char* what) {
  ++checks_;
  if (cond) {
    printf("  ok   %s\n", what);
  } else {
    ++failures_;
    printf("  FAIL %s  [%s]\n", what, group_);
  }
}

void checkNear(float got, float want, float tol, const char* what) {
  const bool ok = fabsf(got - want) <= tol;
  ++checks_;
  if (ok) {
    printf("  ok   %s (%.3f)\n", what, got);
  } else {
    ++failures_;
    printf("  FAIL %s: got %.3f want %.3f +-%.3f  [%s]\n", what, got, want, tol,
           group_);
  }
}

// -------------------------------------------------------------- frame maker --
const int SRC_W = 320, SRC_H = 240;
std::vector<uint8_t> frame_(SRC_W * SRC_H);

void frameBase(uint8_t level) {
  for (int i = 0; i < SRC_W * SRC_H; ++i) {
    // A little deterministic texture, so the background model is not being fed
    // a mathematically perfect constant it could never see in a lettuce bed.
    frame_[i] = static_cast<uint8_t>(level + ((i * 7919) % 5) - 2);
  }
}

void frameRect(int x, int y, int w, int h, uint8_t v) {
  for (int yy = y; yy < y + h; ++yy) {
    if (yy < 0 || yy >= SRC_H) continue;
    for (int xx = x; xx < x + w; ++xx) {
      if (xx < 0 || xx >= SRC_W) continue;
      frame_[yy * SRC_W + xx] = v;
    }
  }
}

void pushFrame() { shim_camera_set_frame(frame_.data(), SRC_W, SRC_H); }

// Runs n frames of plain background so the model settles.
DetectionResult settle(int n, uint8_t level = 100) {
  DetectionResult r{};
  for (int i = 0; i < n; ++i) {
    frameBase(level);
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  return r;
}

// ------------------------------------------------------- link test helpers --
uint8_t nmeaChecksum(const char* s) {
  uint8_t cs = 0;
  for (const char* p = s; *p; ++p) cs ^= static_cast<uint8_t>(*p);
  return cs;
}

std::string sentence(const char* body) {
  char out[160];
  snprintf(out, sizeof(out), "$%s*%02X\r\n", body, nmeaChecksum(body));
  return out;
}

void injectLink(const std::string& s) {
  HardwareSerial* p = shim_serial(2);
  if (p) p->shim_inject(s);
}

// ------------------------------------------------------------ trap helpers --
TrapInputs mkInputs(bool night, float soc, bool cam = false, bool sonar = false) {
  TrapInputs in;
  in.night = night;
  in.soc_pct = soc;
  in.cam_detection = cam;
  in.sonar_detection = sonar;
  return in;
}

// Steps the state machine forward in wall-clock time, holding the inputs.
void runFor(uint32_t ms, const TrapInputs& in, uint32_t step = 100) {
  for (uint32_t t = 0; t < ms; t += step) {
    shim_advance_ms(step);
    trap::update(in);
  }
}

int events_ = 0;
char last_event_[64] = {0};
void onEvent(const char* type, const String&) {
  ++events_;
  snprintf(last_event_, sizeof(last_event_), "%s", type);
}

// ================================================================== tests ====

void testSoc() {
  group("State-of-charge model (12 V SLA)");

  checkNear(power::socFromOcv(12.73f), 100.0f, 0.1f, "full at 12.73 V");
  checkNear(power::socFromOcv(12.10f), 50.0f, 0.1f, "half at 12.10 V");
  checkNear(power::socFromOcv(11.81f), 30.0f, 0.1f, "shed point at 11.81 V");
  checkNear(power::socFromOcv(11.30f), 0.0f, 0.1f, "empty at 11.30 V");
  check(power::socFromOcv(15.0f) == 100.0f, "clamps above the curve");
  check(power::socFromOcv(9.0f) == 0.0f, "clamps below the curve");

  bool monotonic = true;
  float prev = -1.0f;
  for (float v = 11.0f; v <= 13.0f; v += 0.01f) {
    const float s = power::socFromOcv(v);
    if (s < prev - 0.001f) monotonic = false;
    prev = s;
  }
  check(monotonic, "monotonic across the whole curve");

  // The divider maths: 12.10 V behind an 11:1 divider is 1100 mV at the pin.
  power::begin();
  shim_set_pin_mv(PIN_VBAT_SENSE, 1100);
  shim_set_pin_mv(PIN_VPV_SENSE, 0);
  PowerReading r = power::read(0.0f);
  checkNear(r.battery_volts, 12.10f, 0.05f, "divider reconstructs 12.10 V");
  checkNear(r.soc_pct, 50.0f, 1.5f, "reads 50 % at rest");
  check(!r.charging, "not charging with no panel voltage");

  // Same battery, but a 2 A load is pulling the terminals down. Without IR
  // compensation this would read as a flat battery and shed load early.
  shim_set_pin_mv(PIN_VBAT_SENSE, 1094);          // ~12.04 V terminal
  PowerReading loaded = power::read(2.0f);
  checkNear(loaded.battery_ocv, 12.10f, 0.03f, "load compensation recovers OCV");
  check(loaded.soc_pct > 45.0f, "loaded battery still reads ~50 %, not flat");

  // Charging clamp: a bulk-charging bus must not read as full.
  shim_set_pin_mv(PIN_VBAT_SENSE, 1300);          // 14.3 V
  shim_set_pin_mv(PIN_VPV_SENSE, 1700);           // 18.7 V panel
  PowerReading chg = power::read(0.2f);
  check(chg.charging, "detects charging when PV is above the battery");
  check(chg.soc_pct <= 95.0f, "charging voltage is clamped below 100 %");
}

void testTrapStateMachine() {
  group("Trap state machine");

  shim_reset_time();
  hw::reset();
  events_ = 0;
  trap::begin(onEvent);
  trap::setEnabled(true);
  trap::setUvOverride(-1);

  // Daytime: everything off.
  trap::update(mkInputs(false, 90.0f));
  trap::update(mkInputs(false, 90.0f));
  check(trap::state() == TrapState::DAY_IDLE, "daytime -> DAY_IDLE");
  check(hw::uv_pct == 0 && !hw::fan, "daytime leaves both loads off");

  // Night opens: UV lights.
  shim_advance_ms(1000);
  trap::update(mkInputs(true, 90.0f));
  check(trap::state() == TrapState::LURE, "night -> LURE");
  check(hw::uv_pct == UV_DUTY_PCT, "UV array lit while luring");

  // A camera detection fires a capture burst.
  shim_advance_ms(DETECT_HOLDOFF_MS + 100);
  trap::update(mkInputs(true, 90.0f, /*cam=*/true));
  check(trap::state() == TrapState::CAPTURE, "detection -> CAPTURE");
  check(hw::fan, "blower runs during capture");
  check(trap::capturesByCam() == 1, "capture attributed to the camera");

  // ...and stops again after CAPTURE_MS, then cools down and re-arms.
  runFor(CAPTURE_MS + 500, mkInputs(true, 90.0f));
  check(!hw::fan, "blower stops when the burst ends");
  check(trap::state() == TrapState::COOLDOWN, "capture -> COOLDOWN");
  runFor(COOLDOWN_MS + 500, mkInputs(true, 90.0f));
  check(trap::state() == TrapState::LURE, "cooldown -> LURE");

  // Ultrasonic path is attributed separately.
  shim_advance_ms(DETECT_HOLDOFF_MS + 100);
  trap::update(mkInputs(true, 90.0f, false, /*sonar=*/true));
  check(trap::capturesBySonar() == 1, "capture attributed to the ultrasonic");
  runFor(CAPTURE_MS + COOLDOWN_MS + 1000, mkInputs(true, 90.0f));

  group("Battery protection");

  // Below the 30 % shed threshold everything must stop.
  trap::update(mkInputs(true, 25.0f));
  check(trap::state() == TrapState::LOW_BATTERY, "SoC 25 % -> LOW_BATTERY");
  check(hw::uv_pct == 0 && !hw::fan, "low battery sheds both loads");

  // Recovery needs hysteresis: 35 % is above the shed point but must not
  // re-arm, or the trap oscillates around the threshold at dawn.
  trap::update(mkInputs(true, 35.0f));
  check(trap::state() == TrapState::LOW_BATTERY, "35 % does not re-arm (hysteresis)");
  trap::update(mkInputs(true, 45.0f));
  check(trap::state() == TrapState::LURE, "45 % re-arms");

  // Between SOC_SHED_PCT and SOC_FAN_MIN_PCT the lure may run but the blower
  // may not: the trap keeps observing without risking the battery.
  shim_advance_ms(DETECT_HOLDOFF_MS + 100);
  trap::update(mkInputs(true, 32.0f, true));
  check(trap::state() == TrapState::LURE, "SoC 32 %: stays in LURE");
  check(!hw::fan, "SoC 32 %: blower suppressed");
  check(strcmp(last_event_, "capture_skipped") == 0, "skip is recorded as an event");

  // An exhausted fan budget must also block a capture.
  hw::fan_budget_s = 2;
  shim_advance_ms(DETECT_HOLDOFF_MS + 100);
  trap::update(mkInputs(true, 90.0f, true));
  check(!hw::fan, "exhausted rolling-hour budget blocks the blower");
  hw::fan_budget_s = FAN_MAX_SEC_PER_HOUR;

  group("UV duty cycle");

  shim_reset_time();
  hw::reset();
  trap::begin(onEvent);
  trap::update(mkInputs(false, 90.0f));           // BOOT -> DAY_IDLE
  shim_advance_ms(1000);
  trap::update(mkInputs(true, 90.0f));            // -> LURE, phase anchored
  check(hw::uv_pct > 0, "UV on at the start of the cycle");

  runFor(UV_ON_MS + 2000, mkInputs(true, 90.0f), 1000);
  check(hw::uv_pct == 0, "UV off during the dark phase");

  // A camera detection in the dark phase must be ignored: with the array off
  // there is no illumination, so the camera cannot have seen anything.
  const uint32_t before = trap::captures();
  trap::update(mkInputs(true, 90.0f, /*cam=*/true));
  check(trap::captures() == before, "camera detection ignored while UV is dark");

  // The ultrasonic sensor does not depend on light, so it still fires.
  shim_advance_ms(DETECT_HOLDOFF_MS + 100);
  trap::update(mkInputs(true, 90.0f, false, /*sonar=*/true));
  check(trap::captures() == before + 1, "ultrasonic still fires in the dark phase");
  runFor(CAPTURE_MS + COOLDOWN_MS + 1000, mkInputs(true, 90.0f), 500);

  runFor(UV_OFF_MS, mkInputs(true, 90.0f), 1000);
  check(hw::uv_pct > 0, "UV returns on the next cycle");

  // A manual override pins the array on regardless of the cycle.
  trap::setUvOverride(100);
  runFor(UV_ON_MS + 5000, mkInputs(true, 90.0f), 1000);
  check(hw::uv_pct == 100, "override holds the UV on through the dark phase");
  trap::setUvOverride(-1);

  group("Remote disable");
  trap::setEnabled(false);
  trap::update(mkInputs(true, 90.0f));
  check(trap::state() == TrapState::DISABLED, "disable command stops the trap");
  check(hw::uv_pct == 0 && !hw::fan, "disabled means both loads off");
  trap::setEnabled(true);
  trap::update(mkInputs(true, 90.0f));
  trap::update(mkInputs(true, 90.0f));
  check(trap::state() == TrapState::LURE, "enable command resumes");
}

void testCamLink() {
  group("Controller <-> camera link");

  cam_link::begin();
  check(cam_link::health().sentences_ok == 0, "starts with a clean counter");

  injectLink(sentence("PEST,DET,1,120,2,7,1,84,350,38"));
  const bool got = cam_link::poll();
  const CamReport& r = cam_link::lastReport();
  check(got, "a valid sentence is reported as fresh");
  check(r.seq == 1, "sequence parsed");
  check(r.caterpillars == 2, "caterpillar count parsed");
  check(r.aphids == 7, "aphid count parsed");
  check(r.nontarget == 1, "non-target count parsed");
  check(r.confidence_pct == 84, "confidence parsed");
  check(r.motion_px == 350, "motion pixels parsed");
  checkNear(r.fps, 3.8f, 0.01f, "fps parsed as tenths");

  // A corrupted sentence must be rejected, not half-applied. This is the
  // mechanism behind the "without localized transmission corruption" criterion.
  const uint32_t ok_before = cam_link::health().sentences_ok;
  injectLink("$PEST,DET,2,130,99,99,99,99,999,99*00\r\n");
  cam_link::poll();
  check(cam_link::health().sentences_bad == 1, "bad checksum counted");
  check(cam_link::health().sentences_ok == ok_before, "bad sentence not accepted");
  check(cam_link::lastReport().caterpillars == 2, "corrupt data did not overwrite");

  // Debug prints sharing the wire must not disturb the parser.
  injectLink("some stray debug output\r\n");
  injectLink("$GPGGA,not,ours*00\r\n");
  cam_link::poll();
  check(cam_link::lastReport().caterpillars == 2, "foreign lines ignored");

  // A gap in the sequence numbers is a measurable loss.
  const uint32_t lost_before = cam_link::health().sentences_lost;
  injectLink(sentence("PEST,DET,5,140,0,0,0,0,10,40"));   // 2,3,4 missing
  cam_link::poll();
  check(cam_link::health().sentences_lost == lost_before + 3,
        "sequence gap counted as three lost sentences");

  // Status sentences update health without touching the detection report.
  injectLink(sentence("PEST,STA,6,150,2,180000,900,3"));
  cam_link::poll();
  check(cam_link::health().state == 2, "status state parsed");
  check(cam_link::health().frames == 900, "status frame count parsed");
  check(cam_link::health().errors == 3, "status error count parsed");

  // Outbound sentences must carry a checksum the camera will accept.
  HardwareSerial* port = shim_serial(2);
  port->shim_clear_tx();
  cam_link::sendConfig(true, 60);
  const std::string tx = port->shim_tx();
  check(tx.find("$PEST,CFG,1,60*") == 0, "config sentence formatted");
  const size_t star = tx.find('*');
  const uint8_t want =
      static_cast<uint8_t>(strtoul(tx.substr(star + 1, 2).c_str(), nullptr, 16));
  check(nmeaChecksum(tx.substr(1, star - 1).c_str()) == want,
        "outbound checksum is correct");
}

void testDetector() {
  group("Detection pipeline");

  check(detector::begin(), "camera initialises");
  detector::setSensitivity(50);
  detector::resetBackground();

  DetectionResult r = settle(BG_LEARN_FRAMES + 10);
  check(!r.learning, "background model stabilises");
  check(r.caterpillars == 0 && r.aphids == 0, "quiet scene reports nothing");

  // --- caterpillar: elongated, slow-moving ---------------------------------
  // 40x10 source px -> ~10x2.5 grid px: area ~30, elongation ~4.
  for (int i = 0; i < 8; ++i) {
    frameBase(100);
    frameRect(120 + i, 100, 40, 10, 210);        // creeps 1 px per frame
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  check(r.caterpillars >= 1, "elongated slow blob classified as caterpillar");
  check(r.best_confidence >= DETECT_CONF_MIN_PCT,
        "caterpillar confidence clears the capture threshold");

  // The key property of the selective background update: a larva that stops
  // moving must stay visible instead of dissolving into the background.
  for (int i = 0; i < 120; ++i) {
    frameBase(100);
    frameRect(128, 100, 40, 10, 210);            // completely stationary
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  check(r.caterpillars >= 1, "stationary caterpillar still detected 120 frames on");

  // --- aphid colony: small blobs, clustered ---------------------------------
  detector::resetBackground();
  settle(BG_LEARN_FRAMES + 10);
  for (int i = 0; i < 8; ++i) {
    frameBase(100);
    for (int k = 0; k < 5; ++k) frameRect(100 + k * 20, 120, 8, 8, 210);
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  check(r.aphids >= 3, "clustered specks classified as an aphid colony");

  // A lone speck of the same size is noise, not a colony.
  detector::resetBackground();
  settle(BG_LEARN_FRAMES + 10);
  for (int i = 0; i < 8; ++i) {
    frameBase(100);
    frameRect(150, 120, 8, 8, 210);
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  check(r.aphids == 0, "isolated speck is not reported as an aphid");

  // --- non-target: fast mover ----------------------------------------------
  detector::resetBackground();
  settle(BG_LEARN_FRAMES + 10);
  int fast_nontarget = 0;
  for (int i = 0; i < 6; ++i) {
    frameBase(100);
    frameRect(40 + i * 40, 60, 24, 16, 210);     // ~10 grid px per frame
    pushFrame();
    r = detector::process();
    if (r.nontarget > 0) ++fast_nontarget;
    shim_advance_ms(150);
  }
  check(fast_nontarget > 0, "fast mover counted as a non-target");
  check(r.caterpillars == 0, "fast mover is not counted as a caterpillar");

  // --- oversized object ------------------------------------------------------
  detector::resetBackground();
  settle(BG_LEARN_FRAMES + 10);
  for (int i = 0; i < 6; ++i) {
    frameBase(100);
    frameRect(60, 40, 200, 150, 210);            // a leaf, a hand, a shadow
    pushFrame();
    r = detector::process();
    shim_advance_ms(150);
  }
  check(r.caterpillars == 0 && r.aphids == 0, "oversized blob is not a target");

  // --- global illumination change -------------------------------------------
  // This is the UV array switching on. It must force a relearn, not report a
  // swarm; getting this wrong would fire the blower every duty cycle.
  detector::resetBackground();
  settle(BG_LEARN_FRAMES + 10);
  frameBase(200);                                 // whole scene brightens
  pushFrame();
  r = detector::process();
  check(r.scene_reset, "global lighting change forces a background reset");
  check(r.caterpillars == 0 && r.aphids == 0, "lighting change reports no targets");

  // --- camera failure --------------------------------------------------------
  shim_camera_fail_next(true);
  const uint32_t err_before = detector::frameErrors();
  r = detector::process();
  check(detector::frameErrors() == err_before + 1, "dropped frame is counted");
  check(r.caterpillars == 0, "dropped frame reports nothing");
  shim_camera_fail_next(false);
}

}  // namespace

int main() {
  printf("IoT pest trap - host test suite\n");
  testSoc();
  testTrapStateMachine();
  testCamLink();
  testDetector();

  printf("\n----------------------------------------\n");
  printf("%d checks, %d failures\n", checks_, failures_);
  return failures_ == 0 ? 0 : 1;
}
