#pragma once
#include <Arduino.h>

// PWM output that works on both Arduino-ESP32 2.x (ledcSetup/ledcAttachPin)
// and 3.x (ledcAttach), so the project builds on whichever core is installed.
class PwmOut {
 public:
  PwmOut(int pin, int channel, uint32_t freq_hz)
      : pin_(pin), channel_(channel), freq_(freq_hz) {}
  void begin();
  void setPercent(uint8_t pct);
  uint8_t percent() const { return pct_; }
  bool on() const { return pct_ > 0; }

 private:
  int pin_;
  int channel_;
  uint32_t freq_;
  uint8_t pct_ = 0;
};

namespace actuators {
void begin();

// UV-A lure array.
void uvSet(uint8_t pct);
bool uvOn();

// Centrifugal blower. fanStart() ramps up over FAN_SOFTSTART_MS so the inrush
// does not sag the 12 V bus enough to reset the buck converter.
void fanStart(uint8_t pct);
void fanStop();
bool fanOn();

// Rolling-hour run-time budget for the blower. fanBudgetRemainingSec() returns
// how many more seconds it may run in the current window.
void     fanBudgetTick();          // call from the main loop
uint32_t fanBudgetRemainingSec();
uint32_t fanTotalRunSec();

void statusLed(bool on);
void statusBlink(uint8_t times, uint16_t period_ms);

// Cuts and restores the ESP32-CAM's 5 V rail; used when the camera link has
// gone quiet for CAM_LINK_TIMEOUT_MS.
void camPower(bool on);
void camPowerCycle();
}  // namespace actuators
