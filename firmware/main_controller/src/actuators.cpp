#include "actuators.h"
#include "config.h"

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

void PwmOut::begin() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin_, freq_, 8);
  ledcWrite(pin_, 0);
#else
  ledcSetup(channel_, freq_, 8);
  ledcAttachPin(pin_, channel_);
  ledcWrite(channel_, 0);
#endif
  pct_ = 0;
}

void PwmOut::setPercent(uint8_t pct) {
  if (pct > 100) pct = 100;
  pct_ = pct;
  const uint32_t duty = (static_cast<uint32_t>(pct) * 255u) / 100u;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin_, duty);
#else
  ledcWrite(channel_, duty);
#endif
}

namespace {
// 1 kHz for the LED array (well above flicker, easy on the MOSFET); 20 kHz for
// the blower so its PWM whine sits above the audible band.
PwmOut uv_(PIN_UV_GATE, 0, 1000);
PwmOut fan_(PIN_FAN_GATE, 1, 20000);

uint32_t fan_started_ms_ = 0;
uint32_t fan_total_run_s_ = 0;

// Rolling-hour budget kept as 60 one-minute buckets of fan-on milliseconds.
uint16_t fan_bucket_ms_[60] = {0};
uint8_t  fan_bucket_idx_ = 0;
uint32_t fan_bucket_started_ms_ = 0;
uint32_t fan_accum_this_bucket_ms_ = 0;
}  // namespace

namespace actuators {

void begin() {
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_CAM_POWER, OUTPUT);
  digitalWrite(PIN_CAM_POWER, HIGH);   // camera powered by default
  uv_.begin();
  fan_.begin();
  fan_bucket_started_ms_ = millis();
}

void uvSet(uint8_t pct) { uv_.setPercent(pct); }
bool uvOn() { return uv_.on(); }

void fanStart(uint8_t pct) {
  if (fan_.on()) { fan_.setPercent(pct); return; }
  fan_started_ms_ = millis();
  // Ramp from the lowest duty that reliably breaks the rotor free (below ~30 %
  // a 12 V centrifugal blower can stall and just sit there heating the winding).
  const uint8_t kMinSpin = 30;
  const uint32_t step_ms = FAN_SOFTSTART_MS / 10;
  for (int i = 1; i <= 10; ++i) {
    const uint8_t p = kMinSpin + ((pct - kMinSpin) * i) / 10;
    fan_.setPercent(p);
    delay(step_ms);
  }
  fan_.setPercent(pct);
}

void fanStop() {
  if (fan_.on()) {
    const uint32_t run_ms = millis() - fan_started_ms_;
    fan_total_run_s_ += run_ms / 1000;
    fan_accum_this_bucket_ms_ += run_ms;
  }
  fan_.setPercent(0);
}

bool fanOn() { return fan_.on(); }

void fanBudgetTick() {
  const uint32_t now = millis();
  if (now - fan_bucket_started_ms_ < 60000UL) return;

  uint32_t closed = fan_accum_this_bucket_ms_;
  if (fan_.on()) {                       // count the part of the minute it ran
    closed += now - max(fan_started_ms_, fan_bucket_started_ms_);
  }
  fan_bucket_ms_[fan_bucket_idx_] = static_cast<uint16_t>(min(closed, 60000UL));
  fan_bucket_idx_ = (fan_bucket_idx_ + 1) % 60;
  fan_accum_this_bucket_ms_ = 0;
  fan_bucket_started_ms_ = now;
}

uint32_t fanBudgetRemainingSec() {
  uint32_t used_ms = fan_accum_this_bucket_ms_;
  for (int i = 0; i < 60; ++i) used_ms += fan_bucket_ms_[i];
  if (fan_.on()) used_ms += millis() - max(fan_started_ms_, fan_bucket_started_ms_);
  const uint32_t used_s = used_ms / 1000;
  return (used_s >= FAN_MAX_SEC_PER_HOUR) ? 0 : (FAN_MAX_SEC_PER_HOUR - used_s);
}

uint32_t fanTotalRunSec() { return fan_total_run_s_; }

void statusLed(bool on) { digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW); }

void statusBlink(uint8_t times, uint16_t period_ms) {
  for (uint8_t i = 0; i < times; ++i) {
    digitalWrite(PIN_STATUS_LED, HIGH);
    delay(period_ms / 2);
    digitalWrite(PIN_STATUS_LED, LOW);
    delay(period_ms / 2);
  }
}

void camPower(bool on) { digitalWrite(PIN_CAM_POWER, on ? HIGH : LOW); }

void camPowerCycle() {
  camPower(false);
  delay(CAM_REBOOT_HOLD_MS);
  camPower(true);
}

}  // namespace actuators
