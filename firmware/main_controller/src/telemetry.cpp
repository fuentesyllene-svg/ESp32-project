#include "telemetry.h"
#include "config.h"
#include "datalog.h"
#include "secrets.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

namespace {
WiFiClient net;
PubSubClient mqtt(net);
telemetry::CommandHandler cmd_handler_ = nullptr;
DeliveryStats stats_{};

uint32_t last_wifi_try_ = 0;
uint32_t last_mqtt_try_ = 0;
bool     ap_mode_ = false;

char topic_telemetry_[64];
char topic_event_[64];
char topic_status_[64];
char topic_cmd_[64];
uint32_t seq_ = 0;

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  String s;
  s.reserve(len);
  for (unsigned int i = 0; i < len; ++i) s += static_cast<char>(payload[i]);
  if (cmd_handler_) cmd_handler_(s);
}

// Wraps a record so the receiver can verify it arrived intact:
//   {"seq":12,"d":{...},"crc":"a1b2c3d4"}
// The CRC is computed over the exact serialised bytes of "d", so a subscriber
// re-serialising that sub-object gets the same value.
String frame(const JsonDocument& doc) {
  String inner;
  serializeJson(doc, inner);
  const uint32_t crc = telemetry::crc32(
      reinterpret_cast<const uint8_t*>(inner.c_str()), inner.length());
  char tail[32];
  snprintf(tail, sizeof(tail), ",\"crc\":\"%08x\"}", crc);
  String out;
  out.reserve(inner.length() + 48);
  out += "{\"seq\":";
  out += String(++seq_);
  out += ",\"d\":";
  out += inner;
  out += tail;
  return out;
}

bool publishRaw(const String& line) {
  if (!mqtt.connected()) return false;
  // A spooled record keeps its original topic in a "_t" hint if present;
  // everything else goes to the telemetry topic.
  const char* topic = topic_telemetry_;
  if (line.indexOf("\"_t\":\"event\"") >= 0) topic = topic_event_;
  return mqtt.publish(topic, line.c_str(), false);
}

bool sendOrSpool(const String& line) {
  ++stats_.generated;
  if (publishRaw(line)) { ++stats_.published; return true; }
  if (datalog::spoolPush(line)) { ++stats_.spooled; return false; }
  ++stats_.dropped;
  return false;
}

// Called by the spool drain; a success here means a previously spooled record
// finally made it, so it counts toward "published".
bool resend(const String& line) {
  if (publishRaw(line)) {
    ++stats_.published;
    if (stats_.spooled) --stats_.spooled;
    return true;
  }
  return false;
}

void startAccessPoint() {
  // Falls back to a local hotspot so the operator can still read status and
  // download the trial log in the field with no internet at all.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_FALLBACK_SSID, AP_FALLBACK_PASS);
  ap_mode_ = true;
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  const uint32_t now = millis();
  if (now - last_wifi_try_ < WIFI_RETRY_MS) return;
  last_wifi_try_ = now;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void ensureMqtt() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const uint32_t now = millis();
  if (now - last_mqtt_try_ < MQTT_RETRY_MS) return;
  last_mqtt_try_ = now;

  const bool ok = (strlen(MQTT_USER) > 0)
      ? mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASSWORD,
                     topic_status_, 0, true, "{\"online\":false}")
      : mqtt.connect(DEVICE_ID, topic_status_, 0, true, "{\"online\":false}");
  if (ok) {
    mqtt.publish(topic_status_,
                 "{\"online\":true,\"fw\":\"" FIRMWARE_VERSION
                 "\",\"site\":\"" SITE_NAME "\"}", true);
    mqtt.subscribe(topic_cmd_);
  }
}
}  // namespace

namespace telemetry {

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

void begin(CommandHandler handler) {
  cmd_handler_ = handler;
  snprintf(topic_telemetry_, sizeof(topic_telemetry_), "pest/%s/telemetry", DEVICE_ID);
  snprintf(topic_event_, sizeof(topic_event_), "pest/%s/event", DEVICE_ID);
  snprintf(topic_status_, sizeof(topic_status_), "pest/%s/status", DEVICE_ID);
  snprintf(topic_cmd_, sizeof(topic_cmd_), "pest/%s/cmd", DEVICE_ID);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_ID);
  WiFi.setSleep(true);              // modem sleep; meaningful on a 9 Ah battery
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  last_wifi_try_ = millis();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(60);
}

void loop() {
  ensureWifi();
  ensureMqtt();
  if (mqtt.connected()) {
    mqtt.loop();
    if (datalog::spoolCount() > 0) datalog::spoolDrain(resend, 5);
    if (ap_mode_ && WiFi.status() == WL_CONNECTED) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      ap_mode_ = false;
    }
  } else if (!ap_mode_ && WiFi.status() != WL_CONNECTED &&
             millis() > 2 * WIFI_RETRY_MS) {
    startAccessPoint();
  }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool mqttConnected() { return mqtt.connected(); }
bool apModeActive()  { return ap_mode_; }

String ipAddress() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (ap_mode_) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

void publishSample(const TelemetrySample& s, const String& iso_time) {
  JsonDocument doc;
  doc["_t"]      = "telemetry";
  doc["id"]      = DEVICE_ID;
  doc["site"]    = SITE_NAME;
  doc["ts"]      = iso_time;
  doc["up"]      = s.uptime_s;
  doc["state"]   = s.state;

  JsonObject pw  = doc["power"].to<JsonObject>();
  pw["vbat"]     = roundf(s.vbat * 100) / 100.0f;
  pw["vocv"]     = roundf(s.vocv * 100) / 100.0f;
  pw["vpv"]      = roundf(s.vpv * 100) / 100.0f;
  pw["soc"]      = roundf(s.soc_pct * 10) / 10.0f;
  pw["load_a"]   = roundf(s.load_a * 100) / 100.0f;
  pw["charging"] = s.charging;

  JsonObject env = doc["env"].to<JsonObject>();
  if (s.env_valid) {
    env["t"]  = roundf(s.temp_c * 10) / 10.0f;
    env["rh"] = roundf(s.rh_pct * 10) / 10.0f;
  } else {
    env["t"]  = nullptr;
    env["rh"] = nullptr;
  }
  env["ldr"] = s.ldr;

  JsonObject det = doc["det"].to<JsonObject>();
  det["cat"]  = s.caterpillars;
  det["aph"]  = s.aphids;
  det["non"]  = s.nontarget;
  det["conf"] = s.confidence;
  det["captures"] = s.captures_total;
  det["fan_s"]    = s.fan_run_s;

  JsonObject sys = doc["sys"].to<JsonObject>();
  sys["cam_state"]  = s.cam_state;
  sys["cam_online"] = s.cam_online;
  sys["cam_lost"]   = s.cam_lost;
  sys["rssi"]       = s.rssi;
  sys["heap"]       = s.free_heap;
  sys["spool"]      = static_cast<uint32_t>(datalog::spoolCount());
  sys["deliv"]      = roundf(stats_.ratio_pct() * 10) / 10.0f;

  sendOrSpool(frame(doc));
}

void publishEvent(const char* type, const String& detail) {
  JsonDocument doc;
  doc["_t"]     = "event";
  doc["id"]     = DEVICE_ID;
  doc["up"]     = millis() / 1000;
  doc["type"]   = type;
  doc["detail"] = detail;
  sendOrSpool(frame(doc));
}

const DeliveryStats& stats() { return stats_; }

String csvRow(const TelemetrySample& s, const String& iso_time) {
  char row[320];
  snprintf(row, sizeof(row),
           "%s,%lu,%s,%.2f,%.2f,%.1f,%.2f,%d,%.1f,%.1f,%d,%u,%u,%u,%u,%lu,%lu,"
           "%u,%d,%lu,%lu",
           iso_time.c_str(), static_cast<unsigned long>(s.uptime_s), s.state,
           s.vbat, s.vocv, s.soc_pct, s.vpv, s.charging ? 1 : 0,
           s.env_valid ? s.temp_c : NAN, s.env_valid ? s.rh_pct : NAN, s.ldr,
           s.caterpillars, s.aphids, s.nontarget, s.confidence,
           static_cast<unsigned long>(s.captures_total),
           static_cast<unsigned long>(s.fan_run_s), s.cam_state, s.rssi,
           static_cast<unsigned long>(stats_.published),
           static_cast<unsigned long>(stats_.spooled + stats_.dropped));
  return String(row);
}

}  // namespace telemetry
