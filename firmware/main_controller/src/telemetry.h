#pragma once
#include <Arduino.h>

// One row of the trial record: everything the dashboard shows and everything
// the analysis in tools/analyze_trial.py needs.
struct TelemetrySample {
  uint32_t uptime_s;
  const char* state;
  float  vbat, vocv, vpv, soc_pct, load_a;
  bool   charging;
  float  temp_c, rh_pct;
  int    ldr;
  bool   env_valid;
  uint16_t caterpillars, aphids, nontarget;
  uint8_t  confidence;
  uint32_t captures_total;
  uint32_t fan_run_s;
  uint8_t  cam_state;
  uint32_t cam_lost;
  bool     cam_online;
  int      rssi;
  uint32_t free_heap;
};

// Delivery accounting for the ">=95 % of logged packets transmitted without
// corruption" success criterion.
struct DeliveryStats {
  uint32_t generated;      // records created
  uint32_t published;      // accepted by the broker (live or from the spool)
  uint32_t spooled;        // written to flash because the link was down
  uint32_t dropped;        // lost to a full spool / filesystem error
  float    ratio_pct() const {
    return generated ? (100.0f * published) / generated : 100.0f;
  }
};

namespace telemetry {
// handler receives the raw JSON payload of any message on <base>/cmd.
using CommandHandler = void (*)(const String& payload);

void begin(CommandHandler handler);
void loop();

bool wifiConnected();
bool mqttConnected();
bool apModeActive();
String ipAddress();

// Publishes a periodic record, or spools it when offline.
void publishSample(const TelemetrySample& s, const String& iso_time);
// Publishes a capture/fault event. Events are spooled too.
void publishEvent(const char* type, const String& detail);

const DeliveryStats& stats();
String csvRow(const TelemetrySample& s, const String& iso_time);

uint32_t crc32(const uint8_t* data, size_t len);
}  // namespace telemetry
