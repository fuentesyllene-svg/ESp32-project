#pragma once
#include <Arduino.h>

// Operating state machine for the UV lure / blower trap.
enum class TrapState : uint8_t {
  BOOT = 0,
  DAY_IDLE,      // outside the night window: everything off, still logging
  LURE,          // UV array lit, waiting for a detection
  CAPTURE,       // blower running because a target was detected
  PURGE,         // scheduled blower sweep, no detection required
  COOLDOWN,      // blower off, letting the scene settle before re-arming
  LOW_BATTERY,   // below the 30 % shed threshold: all loads off
  DISABLED       // switched off remotely
};

struct TrapInputs {
  bool  night;             // inside the operating window
  float soc_pct;
  bool  cam_detection;     // confirmed target from the camera node
  bool  sonar_detection;   // object confirmed in the intake throat
};

namespace trap {
using EventSink = void (*)(const char* type, const String& detail);

void begin(EventSink sink);
void update(const TrapInputs& in);

TrapState   state();
const char* stateName();

uint32_t captures();          // total capture cycles
uint32_t capturesByCam();
uint32_t capturesBySonar();
uint32_t purges();

// Estimated draw on the 12 V bus right now, fed back into the SoC
// calculation so the battery reading is compensated for the load.
float estimatedLoadAmps();

// Remote commands.
void setEnabled(bool on);
bool enabled();
void forceCapture(uint32_t ms);
void setUvOverride(int pct);   // -1 clears the override
}  // namespace trap
