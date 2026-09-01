# Power Budget

The evaluation plan requires the battery to stay **above 30 % through a full
night of continuous operation**. That criterion is not won by the firmware's
threshold check - it is won or lost here, in the load arithmetic. This page
shows the numbers the firmware defaults are set from, so they can be re-derived
for a different UV array or blower.

## 1. Measured/assumed loads

| Load | Rail | Power | 12 V bus current | Note |
|---|---|---|---|---|
| ESP32 DevKit (Wi-Fi, modem sleep) | 5 V | 0.45 W | - | `WiFi.setSleep(true)` is on |
| ESP32-CAM at ~4 fps grayscale | 5 V | 0.80 W | - | the single biggest continuous load |
| DHT22 + HC-SR04 + LDR | 5 V/3.3 V | 0.05 W | - | |
| **Subtotal on the 5 V rail** | | **1.30 W** | **0.128 A** | through the buck at 85 % |
| UV-A LED array | 12 V | 6.0 W | 0.50 A | while lit |
| Centrifugal blower | 12 V | 21.6 W | 1.80 A | while running |

Buck converter efficiency is taken as 85 %, which is typical for a 6-24 V to
5 V 3 A module at a ~0.3 A output. Substitute your own measurements: the
firmware constants are `LOAD_A_BASE`, `LOAD_A_UV` and `LOAD_A_FAN` in
`arduino/PestTrapController/config.h`, and they feed both this budget and
the IR compensation in the state-of-charge estimate.

## 2. Night (18:00-05:00, 11 hours)

| Item | Duty | Current | Energy |
|---|---|---|---|
| Electronics (both boards) | 100 % | 0.128 A | 1.41 Ah |
| UV-A array | 40 % (2 min on / 3 min off) | 0.50 A | 2.20 Ah |
| Blower | ~410 s total | 1.80 A | 0.21 Ah |
| **Night total** | | | **3.82 Ah (45.8 Wh)** |

The blower figure assumes 33 scheduled purges of 10 s plus about 10 detection
captures of 8 s. The firmware caps it at `FAN_MAX_SEC_PER_HOUR` = 300 s, so
even a pathological detection storm cannot exceed 0.92 Ah in a night.

**Depth of discharge: 3.82 / 9.0 = 42 %.** Starting from a full battery the
night ends near **58 % state of charge**, comfortably clear of the 30 %
criterion.

## 3. Day (05:00-18:00, 13 hours)

The trap is idle and the camera's 5 V rail is switched off at GPIO23, because
there is nothing for it to see.

| Item | Current | Energy |
|---|---|---|
| ESP32 + sensors, camera powered down | 0.049 A | 0.64 Ah |

**Daily total: 4.46 Ah (53.5 Wh).**

## 4. Why the UV array is duty-cycled

Running a 6 W UV-A array continuously is the configuration most people build
first, and it does not work on this battery:

| UV duty | Night consumption | Depth of discharge | SoC at dawn | Meets the 30 % criterion? |
|---|---|---|---|---|
| 100 % | 7.12 Ah | 79 % | ~21 % | **No** |
| 60 % | 5.47 Ah | 61 % | ~39 % | Yes, no margin |
| **40 % (default)** | **3.82 Ah** | **42 %** | **~58 %** | **Yes** |
| 40 %, 3 W array | 2.72 Ah | 30 % | ~70 % | Yes, comfortable |

If continuous UV is scientifically important to the trial, use a 3 W array
rather than duty-cycling a 6 W one, and set `UV_DUTY_CYCLE_ENABLE = false`.
That lands at 4.7 Ah/night: workable, but with less reserve for a cloudy week.

Note the coupling documented in `trap.cpp`: at night the UV array is the
camera's only illumination, so detection only happens during the ON phase. The
cycle is deliberately long (2 min / 3 min) so the detector's background model
has time to re-stabilise after each transition rather than spending its life
relearning.

## 5. Solar sizing

To replace 53.5 Wh/day, allowing ~80 % round-trip charge efficiency into a
lead-acid battery, the panel must deliver about **67 Wh/day**.

Davao City averages roughly 4.5-5 peak sun hours, but a fixed, un-cleaned panel
in a lettuce plot through the wet season should be derated hard. Using **3.5
effective peak sun hours**:

| Controller | Derate | Minimum panel |
|---|---|---|
| PWM (panel clamped to battery voltage) | 0.75 | **~26 W** |
| MPPT | 0.90 | **~21 W** |

**Specify at least a 30 W panel with a PWM controller, or 20-25 W with MPPT.**
A 10 W "12 V" panel - a common default at this price point - harvests roughly
26 Wh/day here and will not keep up; the trap would ratchet down night by
night until it sits in `LOW_BATTERY`. Confirm the wattage on your panel's
label before the first trial, because this is the single assumption most
likely to invalidate the power-autonomy result.

## 6. Autonomy with no sun

Usable capacity down to the 30 % shed threshold is 9.0 x 0.70 = 6.3 Ah
(75.6 Wh), which is **about 1.4 days** of operation with zero harvest. One
fully overcast day is survivable; two consecutive ones will trip the shed
threshold before dawn on the second night. That is the intended behaviour -
the trap protects the battery and keeps logging - but plan trial scheduling
around it, and record cloud cover alongside the trial data.

## 7. Cabling and protection

* 5 A fuse between battery positive and the distribution point; 2 A on the UV branch.
* Blower feed in at least 20 AWG; the 1.8 A start surge on thin wire shows up
  as a voltage dip that resets the buck converter.
* 470 uF/25 V across the 12 V bus at the blower, plus the flyback diode from
  `docs/WIRING.md`. Without them the fan's inrush is the most likely cause of
  unexplained reboots mid-trial.
