// Host stand-in for actuators.cpp: records what the state machine asked for,
// so the control logic can be tested without hardware.
#include "actuators.h"
#include "config.h"

namespace hw {
uint8_t  uv_pct = 0;
bool     fan = false;
uint8_t  fan_pct = 0;
uint32_t fan_budget_s = FAN_MAX_SEC_PER_HOUR;
uint32_t fan_run_s = 0;
uint32_t fan_starts = 0;
void reset() {
  uv_pct = 0; fan = false; fan_pct = 0;
  fan_budget_s = FAN_MAX_SEC_PER_HOUR; fan_run_s = 0; fan_starts = 0;
}
}  // namespace hw

namespace actuators {
void begin() {}
void uvSet(uint8_t pct) { hw::uv_pct = pct; }
bool uvOn() { return hw::uv_pct > 0; }
void fanStart(uint8_t pct) { hw::fan = true; hw::fan_pct = pct; ++hw::fan_starts; }
void fanStop() { hw::fan = false; hw::fan_pct = 0; }
bool fanOn() { return hw::fan; }
void fanBudgetTick() {}
uint32_t fanBudgetRemainingSec() { return hw::fan_budget_s; }
uint32_t fanTotalRunSec() { return hw::fan_run_s; }
void statusLed(bool) {}
void statusBlink(uint8_t, uint16_t) {}
void camPower(bool) {}
void camPowerCycle() {}
}  // namespace actuators
