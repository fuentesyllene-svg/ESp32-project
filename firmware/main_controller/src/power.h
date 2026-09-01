#pragma once
#include <Arduino.h>

// Battery / PV monitoring and state-of-charge estimation for a 12 V SLA.
struct PowerReading {
  float battery_volts;      // terminal voltage, as measured
  float battery_ocv;        // load-compensated open-circuit estimate
  float pv_volts;           // panel / charge-controller output voltage
  float soc_pct;            // 0-100, from the SLA discharge curve
  float load_amps;          // estimated present draw on the 12 V bus
  bool  charging;           // PV is above battery -> harvesting
};

namespace power {
void begin();

// load_amps is the caller's estimate of what is switched on right now; it is
// used to undo the IR drop before the voltage is mapped to state of charge.
PowerReading read(float load_amps);

// Coulomb-free SoC from the resting voltage of a 12 V lead-acid battery.
float socFromOcv(float ocv);

// Ambient light, raw ADC counts (higher = darker with the divider in
// docs/WIRING.md, where the LDR sits on the high side).
int readLdrCounts();
}  // namespace power
