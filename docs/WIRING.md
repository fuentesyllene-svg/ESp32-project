# Wiring and Pin Assignment

Hardware as specified for this study:

| Subsystem | Part |
|---|---|
| Controller | ESP32 DevKit v1 (30/38-pin, WROOM-32) |
| Detection sensor | ESP32-CAM (AI-Thinker, OV2640) |
| Storage battery | 12 V 9 Ah/20 HR sealed lead-acid (SLA) |
| Harvesting | 12 V solar panel + PWM/MPPT charge controller |
| Logic supply | DC 6-24 V in to 5 V 3 A USB buck step-down module |
| Removal mechanism | 12 V high-RPM centrifugal blower |
| Attractant | 12 V UV-A LED array (365-395 nm) |

---

## 1. Power tree

```
 12 V Solar Panel
        |
        v
 Charge Controller  ----->  12 V 9 Ah SLA Battery  (system DC bus, 10.5-14.6 V)
   (PV / BAT / LOAD)                 |
                                     +--> Buck 6-24V -> 5V 3A --> 5 V rail
                                     |                              |-- ESP32 DevKit (5V/VIN pin)
                                     |                              |-- ESP32-CAM (5V pin)
                                     |                              '-- HC-SR04 (VCC)
                                     +--> UV-A LED array  (switched, low-side MOSFET Q1)
                                     '--> Centrifugal blower (switched, low-side MOSFET Q2)
```

Rules that must be observed on the bench and in the field:

1. **Single star ground.** Battery negative is the one ground reference. The
   buck converter output ground, both ESP32 grounds, and both MOSFET sources
   tie back to it. A floating ground between the ESP32 and the fan MOSFET is
   the most common cause of phantom resets on this kind of build.
2. **Take the DC bus for the loads from the battery/LOAD terminal, not from
   the 5 V rail.** The 3 A buck cannot start a 12 V blower.
3. **Fuse the bus.** 5 A blade fuse between battery positive and the
   distribution point; 2 A inline fuse on the UV branch.
4. Keep the blower's supply pair twisted and away from the ADC sense wiring.

## 2. Load switching

Both 12 V loads are switched **low-side** with logic-level N-channel MOSFETs
(IRLZ44N or IRLB8721 - a standard IRF540N will *not* fully enhance at 3.3 V):

```
 12 V bus ---- LOAD (+) ... LOAD (-) ---- Q drain
                                   |
                          D1 (1N5822) across the load, cathode to 12 V
 ESP32 GPIO --- 220R --- Q gate
                          |
                        100k  (gate pulldown, holds the load off during boot/reset)
                          |
 GND ------------------ Q source
```

* **D1 flyback diode across the blower is mandatory.** A centrifugal blower is
  an inductive load; without the diode the switch-off spike will eventually
  destroy Q2 and can crash or brick the ESP32.
* The 100 k gate pulldown matters because the ESP32 floats its GPIOs during
  reset and the first ~200 ms of boot. Without it the fan twitches on every
  reset.
* Add a 470 uF/25 V electrolytic across the 12 V bus close to the blower to
  absorb its inrush, otherwise the bus sag on fan start can brown out the buck
  converter and reset both boards.

## 3. Voltage sensing

Two identical dividers feed ADC1 (ADC2 is unusable while Wi-Fi is on):

```
 Sense point ---[ 100k ]---+---[ 10k ]--- GND
                           |
                           +--- ESP32 ADC pin   (+ 100 nF to GND)
```

Divider ratio 11.0: 14.6 V battery -> 1.33 V, 22 V panel open-circuit -> 2.0 V,
both comfortably inside the ESP32's usable ADC range. The exact ratio is
trimmed per unit during calibration - see `docs/TESTING_PROTOCOL.md` section 1.

## 4. ESP32 DevKit (main controller) pin map

| GPIO | Direction | Connected to | Notes |
|---|---|---|---|
| 18 | out | Q1 gate (UV-A array) | LEDC PWM, 1 kHz |
| 19 | out | Q2 gate (blower) | LEDC PWM, 20 kHz (above audible), soft-start |
| 23 | out | ESP32-CAM 5 V enable (high-side switch/relay) | lets the controller power-cycle a hung camera |
| 16 | in  | ESP32-CAM GPIO13 (its TX) | UART2 RX |
| 17 | out | ESP32-CAM GPIO14 (its RX) | UART2 TX |
| 34 | analog in | Battery divider | ADC1_CH6, input-only pin |
| 35 | analog in | Solar/PV divider | ADC1_CH7, input-only pin |
| 33 | analog in | LDR divider (LDR to 3V3, 10 k to GND) | ADC1_CH5, day/night backup |
| 26 | in/out | DHT22 DATA | 4.7 k pull-up to 3V3 |
| 25 | out | HC-SR04 TRIG | |
| 39 | in  | HC-SR04 ECHO | **via 10 k/20 k divider** - ECHO is 5 V and will damage the pin |
| 2  | out | Status LED | onboard LED |
| 21/22 | - | reserved I2C (SDA/SCL) | optional OLED / BME280 |

Do not move the ADC functions to GPIO 0/2/4/12-15/25-27: those are ADC2 and
stop converting the moment Wi-Fi starts.

## 5. ESP32-CAM (AI-Thinker) pin map

The camera module keeps almost nothing free. This build uses only the pins
that are safe when no SD card is fitted:

| GPIO | Direction | Connected to | Notes |
|---|---|---|---|
| 13 | out | ESP32 GPIO16 | UART1 TX (link to controller) |
| 14 | in  | ESP32 GPIO17 | UART1 RX (link to controller) |
| 5 V, GND | - | switched 5 V rail from GPIO23 | do **not** feed it from the 3.3 V pin |

* **GPIO12 is deliberately unused.** It is the flash-voltage strapping pin; a
  UART line idling high on GPIO12 stops the module from booting.
* GPIO0 must be free (or pulled low only while flashing).
* GPIO4 drives the onboard white flash LED. It is left off - white light
  attracts non-target insects and would confound the selectivity measurement.
* To reflash the camera, unplug the two link wires first; the programmer needs
  U0TXD/U0RXD, which the runtime link no longer occupies.

## 6. Physical placement notes

* Mount the camera looking **across** the UV array's face, not into it, at
  15-25 cm. Pointing the lens at the UV source saturates the sensor and the
  background model never stabilises.
* The UV array sits at the blower intake so that attracted insects are inside
  the suction cone; `CAPTURE_MS` in `config.h` assumes that geometry.
* The collection chamber goes on the blower **outlet** with a fine mesh
  (0.5-1 mm) retaining screen, so trapped specimens stay countable and intact
  for the manual audit at the end of each 72-hour trial.
* House the electronics in an IP65 enclosure with the vent facing down. The
  DHT22 goes outside the enclosure under a rain shield; inside it reads the
  enclosure, not the canopy.
