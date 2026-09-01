#pragma once
#include <Arduino.h>

struct EnvReading {
  float temperature_c;   // NAN when the DHT22 did not answer
  float humidity_pct;    // NAN when the DHT22 did not answer
  bool  valid;
};

namespace sensors {
void begin();

// The DHT22 needs ~2 s between conversions; this caches and only re-reads when
// the sensor is ready, so it is safe to call from the main loop.
EnvReading readEnv();

// Median of three HC-SR04 pings, in centimetres. Returns NAN on timeout
// (nothing in range, or the sensor is unplugged).
float readDistanceCm();

// True when an object has been within [SONAR_MIN_CM, SONAR_MAX_CM] for
// SONAR_CONFIRM_HITS consecutive calls. Call at a steady rate from the loop.
bool sonarObjectConfirmed();
}  // namespace sensors
