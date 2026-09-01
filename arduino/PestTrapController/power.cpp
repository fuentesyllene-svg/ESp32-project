#include "power.h"
#include "config.h"

namespace {

// Resting open-circuit voltage -> state of charge for a 12 V (6-cell) SLA at
// ~25 C. Values are the flooded/AGM consensus curve; they are deliberately
// conservative at the bottom end so the 30 % shed threshold trips early rather
// than late.
struct SocPoint { float volts; float pct; };
const SocPoint kSocCurve[] = {
  {12.73f, 100.0f}, {12.62f, 90.0f}, {12.50f, 80.0f}, {12.37f, 70.0f},
  {12.24f, 60.0f},  {12.10f, 50.0f}, {11.96f, 40.0f}, {11.81f, 30.0f},
  {11.66f, 20.0f},  {11.51f, 10.0f}, {11.30f, 0.0f},
};
const size_t kSocPoints = sizeof(kSocCurve) / sizeof(kSocCurve[0]);

// Median-of-N sampling. analogReadMilliVolts() applies the chip's factory ADC
// calibration, which removes most of the ESP32's notorious ADC nonlinearity.
float readDividerVolts(int pin, float ratio) {
  const int kSamples = 15;
  uint32_t s[kSamples];
  for (int i = 0; i < kSamples; ++i) {
    s[i] = analogReadMilliVolts(pin);
    delayMicroseconds(200);
  }
  for (int i = 1; i < kSamples; ++i) {          // insertion sort, N is tiny
    uint32_t key = s[i];
    int j = i - 1;
    while (j >= 0 && s[j] > key) { s[j + 1] = s[j]; --j; }
    s[j + 1] = key;
  }
  return (s[kSamples / 2] / 1000.0f) * ratio;
}

}  // namespace

namespace power {

void begin() {
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VPV_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_LDR, ADC_11db);
  analogReadResolution(12);
}

float socFromOcv(float ocv) {
  if (ocv >= kSocCurve[0].volts) return 100.0f;
  if (ocv <= kSocCurve[kSocPoints - 1].volts) return 0.0f;
  for (size_t i = 1; i < kSocPoints; ++i) {
    if (ocv >= kSocCurve[i].volts) {
      const float span = kSocCurve[i - 1].volts - kSocCurve[i].volts;
      const float frac = (ocv - kSocCurve[i].volts) / span;
      return kSocCurve[i].pct + frac * (kSocCurve[i - 1].pct - kSocCurve[i].pct);
    }
  }
  return 0.0f;
}

PowerReading read(float load_amps) {
  PowerReading r;
  r.battery_volts = readDividerVolts(PIN_VBAT_SENSE, VBAT_DIVIDER_RATIO);
  r.pv_volts      = readDividerVolts(PIN_VPV_SENSE, VPV_DIVIDER_RATIO);
  r.load_amps     = load_amps;

  // Terminal voltage sags by I*R under load; add it back before consulting the
  // resting-voltage curve, otherwise a running blower reads as a flat battery.
  r.battery_ocv = r.battery_volts + load_amps * BATT_INTERNAL_OHMS;

  // While the charge controller is bulk-charging, terminal voltage is pinned
  // near the absorption setpoint and says nothing about SoC. Report the raw
  // curve value but clamp it so a 14.4 V charging bus does not read as 100 %
  // and immediately re-enable loads on a still-empty battery.
  r.charging = (r.pv_volts > r.battery_volts + 0.5f) && (r.battery_volts > 13.0f);
  r.soc_pct  = socFromOcv(r.battery_ocv);
  if (r.charging && r.soc_pct > 95.0f) r.soc_pct = 95.0f;
  return r;
}

int readLdrCounts() {
  uint32_t acc = 0;
  for (int i = 0; i < 8; ++i) acc += analogRead(PIN_LDR);
  return static_cast<int>(acc / 8);
}

}  // namespace power
