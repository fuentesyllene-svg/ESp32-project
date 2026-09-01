// -----------------------------------------------------------------------------
// IoT-Enabled Caterpillar and Aphid Detection and Removal System Using a
// Solar-Powered UV-Light and Fan Trap for Lactuca sativa
//
// Main controller firmware - ESP32 DevKit v1.
//
// Subsystem map (matching the conceptual framework):
//   power supply        -> power.cpp        (PV + 12 V 9 Ah SLA monitoring)
//   sensing/detection   -> cam_link.cpp, sensors.cpp
//   processing/comms    -> this file, trap.cpp, telemetry.cpp, webui.cpp
//   pest removal        -> actuators.cpp    (UV-A array + centrifugal blower)
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>

#include "config.h"
#include "actuators.h"
#include "cam_link.h"
#include "datalog.h"
#include "power.h"
#include "sensors.h"
#include "telemetry.h"
#include "trap.h"
#include "webui.h"

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

namespace {

uint32_t last_sensor_ms_ = 0;
uint32_t last_telemetry_ms_ = 0;
uint32_t last_cam_ping_ms_ = 0;
uint32_t last_cam_cycle_ms_ = 0;
uint32_t last_cfg_push_ms_ = 0;
uint32_t cam_wake_ms_ = 0;
bool     cam_powered_ = true;
bool     last_uv_state_ = false;
bool     time_synced_ = false;
uint16_t last_det_seq_ = 0;
bool     have_det_seq_ = false;
uint8_t  cam_sensitivity_ = 50;

PowerReading power_{};
EnvReading   env_{};
bool         sonar_hit_ = false;

// Cumulative detection counters, so a 15-minute row shows what happened during
// the interval rather than only the instant it was sampled.
uint32_t det_cat_total_ = 0, det_aph_total_ = 0, det_non_total_ = 0;

// ---------------------------------------------------------------- watchdog ---
void wdtBegin(uint32_t seconds) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_deinit();                 // core 3 initialises one at boot
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

// -------------------------------------------------------------------- time ---
String isoTime() {
  struct tm t;
  if (time_synced_ && getLocalTime(&t, 50)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+08:00", &t);
    return String(buf);
  }
  // No NTP yet: an unambiguous relative stamp beats a fake 1970 date, and the
  // analysis script recognises it.
  return String("uptime+") + String(millis() / 1000);
}

bool nightNow() {
  struct tm t;
  if (time_synced_ && getLocalTime(&t, 50)) {
    const int h = t.tm_hour;
    return (NIGHT_START_HOUR > NIGHT_END_HOUR)
               ? (h >= NIGHT_START_HOUR || h < NIGHT_END_HOUR)
               : (h >= NIGHT_START_HOUR && h < NIGHT_END_HOUR);
  }
  // Fallback while the clock is unset: trust the light sensor.
  return power::readLdrCounts() >= LDR_DARK_COUNTS;
}

void trySyncTime() {
  if (time_synced_ || !telemetry::wifiConnected()) return;
  configTzTime(TIMEZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2);
  struct tm t;
  if (getLocalTime(&t, 3000) && t.tm_year > 120) time_synced_ = true;
}

// ------------------------------------------------------------------ events ---
void onTrapEvent(const char* type, const String& detail) {
  Serial.printf("[event] %s %s\n", type, detail.c_str());
  telemetry::publishEvent(type, detail);
  // Capture events also land in the CSV, so the physical chamber audit at the
  // end of a 72-hour trial can be aligned against the timestamps.
  datalog::logCsv(isoTime() + ",EVENT," + type + "," + detail);
}

// ---------------------------------------------------------------- commands ---
void onCommand(const String& payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) {
    Serial.println("[cmd] malformed payload ignored");
    return;
  }
  const char* cmd = doc["cmd"] | "";
  Serial.printf("[cmd] %s\n", cmd);

  if (!strcmp(cmd, "capture")) {
    trap::forceCapture((doc["sec"] | 8) * 1000UL);
  } else if (!strcmp(cmd, "uv")) {
    trap::setUvOverride(doc["pct"] | -1);
  } else if (!strcmp(cmd, "enable")) {
    trap::setEnabled(doc["on"] | true);
  } else if (!strcmp(cmd, "sensitivity")) {
    cam_sensitivity_ = constrain(static_cast<int>(doc["pct"] | 50), 0, 100);
    cam_link::sendConfig(actuators::uvOn(), cam_sensitivity_);
  } else if (!strcmp(cmd, "camcycle")) {
    actuators::camPowerCycle();
  } else if (!strcmp(cmd, "wipe")) {
    datalog::formatAll();
  } else if (!strcmp(cmd, "reboot")) {
    delay(200);
    ESP.restart();
  }
}

// ------------------------------------------------------------- status JSON ---
String statusJson() {
  const CamReport& r = cam_link::lastReport();
  const CamHealth& h = cam_link::health();
  JsonDocument doc;
  doc["id"]        = DEVICE_ID;
  doc["site"]      = SITE_NAME;
  doc["fw"]        = FIRMWARE_VERSION;
  doc["time"]      = isoTime();
  doc["uptime"]    = millis() / 1000;
  doc["state"]     = trap::stateName();
  doc["enabled"]   = trap::enabled();
  doc["vbat"]      = power_.battery_volts;
  doc["vocv"]      = power_.battery_ocv;
  doc["soc"]       = power_.soc_pct;
  doc["vpv"]       = power_.pv_volts;
  doc["charging"]  = power_.charging;
  doc["load_a"]    = power_.load_amps;
  doc["env_ok"]    = env_.valid;
  doc["temp"]      = env_.valid ? env_.temperature_c : 0.0f;
  doc["rh"]        = env_.valid ? env_.humidity_pct : 0.0f;
  doc["ldr"]       = power::readLdrCounts();
  doc["night"]     = nightNow();
  doc["cam_online"]= h.online && !cam_link::linkStale();
  doc["cam_state"] = h.state;
  doc["cam_powered"] = cam_powered_;
  doc["cam_bad"]   = h.sentences_bad;
  doc["cam_lost"]  = h.sentences_lost;
  doc["cat"]       = r.caterpillars;
  doc["aph"]       = r.aphids;
  doc["non"]       = r.nontarget;
  doc["conf"]      = r.confidence_pct;
  doc["fps"]       = r.fps;
  doc["captures"]  = trap::captures();
  doc["cap_cam"]   = trap::capturesByCam();
  doc["cap_sonar"] = trap::capturesBySonar();
  doc["purges"]    = trap::purges();
  doc["fan_s"]     = actuators::fanTotalRunSec();
  doc["fan_budget"]= actuators::fanBudgetRemainingSec();
  doc["wifi"]      = telemetry::wifiConnected();
  doc["ap"]        = telemetry::apModeActive();
  doc["ip"]        = telemetry::ipAddress();
  doc["mqtt"]      = telemetry::mqttConnected();
  doc["rssi"]      = WiFi.RSSI();
  doc["delivery"]  = telemetry::stats().ratio_pct();
  doc["spool"]     = static_cast<uint32_t>(datalog::spoolCount());
  doc["fs_free"]   = static_cast<uint32_t>(datalog::freeBytes());
  doc["heap"]      = ESP.getFreeHeap();
  String out;
  serializeJson(doc, out);
  return out;
}

// --------------------------------------------------------------- detection ---
// A camera report only counts once, and only when it clears both the target
// count and the confidence floor from config.h.
bool freshCamDetection() {
  const CamReport& r = cam_link::lastReport();
  if (r.received_ms == 0) return false;
  if (have_det_seq_ && r.seq == last_det_seq_) return false;
  last_det_seq_ = r.seq;
  have_det_seq_ = true;

  det_cat_total_ += r.caterpillars;
  det_aph_total_ += r.aphids;
  det_non_total_ += r.nontarget;

  const uint16_t targets = r.caterpillars + r.aphids;
  return targets >= DETECT_MIN_TARGETS &&
         r.confidence_pct >= DETECT_CONF_MIN_PCT;
}

// The camera node is the single biggest continuous load on the 5 V rail
// (~0.8 W). There is nothing for it to see during the day, so its supply is
// cut through GPIO23 whenever the trap is not armed. Over a 13-hour daylight
// period that is worth about 1 Ah on a 9 Ah battery - see docs/POWER_BUDGET.md.
bool cameraShouldBePowered() {
  switch (trap::state()) {
    case TrapState::LURE:
    case TrapState::CAPTURE:
    case TrapState::PURGE:
    case TrapState::COOLDOWN:
      return true;
    default:
      return false;
  }
}

void manageCameraPower() {
  const bool want = cameraShouldBePowered();
  if (want == cam_powered_) return;
  cam_powered_ = want;
  actuators::camPower(want);
  if (want) {
    // Give the node its boot and background-learning time before the link is
    // judged stale, otherwise it gets power-cycled the moment it comes up.
    cam_wake_ms_ = millis();
    cam_link::sendConfig(actuators::uvOn(), cam_sensitivity_);
  }
  Serial.printf("[cam] power %s\n", want ? "on" : "off");
}

void serviceCameraLink() {
  const uint32_t now = millis();
  // Do not chase a camera that is intentionally unpowered, and give a freshly
  // powered one time to boot.
  if (!cam_powered_ || (now - cam_wake_ms_) < CAM_BOOT_GRACE_MS) return;
  if (!cam_link::linkStale()) return;

  if (now - last_cam_ping_ms_ > 5000) {
    cam_link::sendPing();
    last_cam_ping_ms_ = now;
  }
  // Still silent well past the timeout: cut its 5 V and let it come back.
  if (now - cam_link::health().last_rx_ms > 2 * CAM_LINK_TIMEOUT_MS &&
      now - last_cam_cycle_ms_ > 60000) {
    Serial.println("[cam] link dead, power cycling");
    telemetry::publishEvent("cam_reset", "link timeout");
    actuators::camPowerCycle();
    last_cam_cycle_ms_ = now;
  }
}

void pushCamConfigIfNeeded() {
  const bool uv = actuators::uvOn();
  const uint32_t now = millis();
  // Push on any UV transition (the camera must reset its background model),
  // and refresh every 30 s so a rebooted camera picks the context back up.
  if (uv != last_uv_state_ || now - last_cfg_push_ms_ > 30000) {
    cam_link::sendConfig(uv, cam_sensitivity_);
    last_uv_state_ = uv;
    last_cfg_push_ms_ = now;
  }
}

void recordTelemetry() {
  const CamReport& r = cam_link::lastReport();
  const CamHealth& h = cam_link::health();

  TelemetrySample s{};
  s.uptime_s      = millis() / 1000;
  s.state         = trap::stateName();
  s.vbat          = power_.battery_volts;
  s.vocv          = power_.battery_ocv;
  s.vpv           = power_.pv_volts;
  s.soc_pct       = power_.soc_pct;
  s.load_a        = power_.load_amps;
  s.charging      = power_.charging;
  s.temp_c        = env_.temperature_c;
  s.rh_pct        = env_.humidity_pct;
  s.env_valid     = env_.valid;
  s.ldr           = power::readLdrCounts();
  s.caterpillars  = r.caterpillars;
  s.aphids        = r.aphids;
  s.nontarget     = r.nontarget;
  s.confidence    = r.confidence_pct;
  s.captures_total= trap::captures();
  s.fan_run_s     = actuators::fanTotalRunSec();
  s.cam_state     = h.state;
  s.cam_lost      = h.sentences_lost;
  s.cam_online    = h.online && !cam_link::linkStale();
  s.rssi          = WiFi.RSSI();
  s.free_heap     = ESP.getFreeHeap();

  const String stamp = isoTime();
  datalog::logCsv(telemetry::csvRow(s, stamp));
  telemetry::publishSample(s, stamp);

  Serial.printf("[log] %s %s vbat=%.2f soc=%.0f%% cat=%u aph=%u caps=%lu "
                "deliv=%.1f%%\n",
                stamp.c_str(), s.state, s.vbat, s.soc_pct, s.caterpillars,
                s.aphids, static_cast<unsigned long>(s.captures_total),
                telemetry::stats().ratio_pct());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("=== UV-Light and Fan Trap for Lactuca sativa ==="));
  Serial.printf("device=%s site=%s fw=%s\n", DEVICE_ID, SITE_NAME,
                FIRMWARE_VERSION);

  actuators::begin();
  power::begin();
  sensors::begin();
  cam_link::begin();
  trap::begin(onTrapEvent);

  if (!datalog::begin()) {
    Serial.println("[fs] LittleFS mount FAILED - logging and spooling disabled");
  }

  telemetry::begin(onCommand);
  webui::begin(statusJson, onCommand);

  // Prime the first readings so the state machine does not act on zeroes.
  power_ = power::read(trap::estimatedLoadAmps());
  env_   = sensors::readEnv();

  wdtBegin(WDT_TIMEOUT_S);
  actuators::statusBlink(3, 200);
  Serial.println("[sys] ready");
}

void loop() {
  esp_task_wdt_reset();

  cam_link::poll();
  telemetry::loop();
  webui::loop();
  trySyncTime();
  actuators::fanBudgetTick();
  manageCameraPower();
  pushCamConfigIfNeeded();
  serviceCameraLink();

  const uint32_t now = millis();
  if (now - last_sensor_ms_ >= SENSOR_POLL_MS) {
    last_sensor_ms_ = now;
    power_ = power::read(trap::estimatedLoadAmps());
    env_   = sensors::readEnv();
    // Only ping the ultrasonic sensor while the trap is actually luring; each
    // ping burst costs ~90 ms of blocking time and there is nothing to detect
    // during the day.
    sonar_hit_ = (trap::state() == TrapState::LURE)
                     ? sensors::sonarObjectConfirmed()
                     : false;
    actuators::statusLed(trap::state() == TrapState::LURE ||
                         trap::state() == TrapState::CAPTURE);
  }

  TrapInputs in;
  in.night           = nightNow();
  in.soc_pct         = power_.soc_pct;
  in.cam_detection   = freshCamDetection();
  in.sonar_detection = sonar_hit_;
  if (in.sonar_detection) sonar_hit_ = false;   // consume the one-shot
  trap::update(in);

  if (now - last_telemetry_ms_ >= TELEMETRY_INTERVAL_MS ||
      last_telemetry_ms_ == 0) {
    last_telemetry_ms_ = now;
    recordTelemetry();
  }

  delay(10);
}
