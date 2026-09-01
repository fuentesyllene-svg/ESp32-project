#include "sensors.h"
#include "config.h"
#include <DHT.h>

namespace {
DHT dht(PIN_DHT, DHT22);
EnvReading cached_{NAN, NAN, false};
uint32_t last_env_ms_ = 0;
uint8_t sonar_hits_ = 0;

float pingOnceCm() {
  digitalWrite(PIN_SONAR_TRIG, LOW);
  delayMicroseconds(3);
  digitalWrite(PIN_SONAR_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_SONAR_TRIG, LOW);
  // 30 ms ceiling ~ 5 m; anything further is not our insect.
  const unsigned long us = pulseIn(PIN_SONAR_ECHO, HIGH, 30000UL);
  if (us == 0) return NAN;
  return us / 58.0f;                       // speed of sound, round trip
}
}  // namespace

namespace sensors {

void begin() {
  dht.begin();
  pinMode(PIN_SONAR_TRIG, OUTPUT);
  digitalWrite(PIN_SONAR_TRIG, LOW);
  pinMode(PIN_SONAR_ECHO, INPUT);
}

EnvReading readEnv() {
  const uint32_t now = millis();
  if (cached_.valid && (now - last_env_ms_) < 2200) return cached_;

  const float h = dht.readHumidity();
  const float t = dht.readTemperature();
  last_env_ms_ = now;
  if (isnan(h) || isnan(t)) {
    cached_.valid = false;                 // keep the stale values visible but
    return cached_;                        // flagged, so the log shows the gap
  }
  cached_.temperature_c = t;
  cached_.humidity_pct = h;
  cached_.valid = true;
  return cached_;
}

float readDistanceCm() {
  float a = pingOnceCm(); delay(30);
  float b = pingOnceCm(); delay(30);
  float c = pingOnceCm();
  // Median of three, treating a timeout as "far away" rather than discarding
  // the sample, so one dropped echo cannot fake a detection.
  if (isnan(a)) a = 999.0f;
  if (isnan(b)) b = 999.0f;
  if (isnan(c)) c = 999.0f;
  const float hi = max(a, max(b, c));
  const float lo = min(a, min(b, c));
  const float med = a + b + c - hi - lo;
  return (med >= 999.0f) ? NAN : med;
}

bool sonarObjectConfirmed() {
  const float d = readDistanceCm();
  if (!isnan(d) && d >= SONAR_MIN_CM && d <= SONAR_MAX_CM) {
    if (sonar_hits_ < 255) ++sonar_hits_;
  } else {
    sonar_hits_ = 0;
  }
  if (sonar_hits_ >= SONAR_CONFIRM_HITS) {
    sonar_hits_ = 0;                       // consume, so one object = one event
    return true;
  }
  return false;
}

}  // namespace sensors
