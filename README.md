# IoT-Enabled Caterpillar and Aphid Detection and Removal System

Firmware and supporting tools for a **solar-powered UV-light and fan trap for
*Lactuca sativa***, built around an ESP32 controller and an ESP32-CAM detection
node.

The system attracts target insects with a UV-A array, decides from the camera
whether what it sees is a caterpillar, an aphid colony, or something it should
leave alone, removes confirmed targets into a collection chamber with a
centrifugal blower, and reports everything to a cloud dashboard and a local
status page while running entirely off a solar-charged battery.

## Hardware

| Subsystem | Part |
|---|---|
| Controller | ESP32 DevKit v1 (WROOM-32) |
| Detection sensor | ESP32-CAM (AI-Thinker, OV2640) |
| Storage | 12 V 9 Ah/20 HR sealed lead-acid battery |
| Harvesting | 12 V solar panel + charge controller |
| Logic supply | DC 6-24 V to 5 V 3 A USB buck converter |
| Removal | 12 V high-RPM centrifugal blower |
| Attractant | 12 V UV-A LED array (365-395 nm) |
| Secondary sensing | HC-SR04 ultrasonic, DHT22, LDR |

Wiring, the full pin map, and the MOSFET/protection circuits are in
[`docs/WIRING.md`](docs/WIRING.md). **Read section 2 before powering the
blower** - the flyback diode and bulk capacitor are not optional.

## Repository layout

```
arduino/PestTrapController/  sketch 1 - ESP32 DevKit: control, power, IoT, logging
arduino/PestTrapCamera/      sketch 2 - ESP32-CAM: vision and classification
test/host/                   workstation test suite for the algorithmic code
tools/analyze_trial.py       turns a trial log into the success-criteria numbers
docs/                        wiring, power budget, protocols, testing plan
```

Both are ordinary Arduino IDE sketches: open the `.ino` and the rest of the
files appear as tabs. Each folder also carries a `platformio.ini`, so the same
files build under PlatformIO if you prefer it; the Arduino IDE ignores that
file. There is only one copy of the source either way.

The two boards map onto the study's conceptual framework like this:

| Subsystem in the framework | Where it lives |
|---|---|
| Power supply | `power.cpp` (PV + SLA monitoring, state of charge) |
| Sensing and detection | `PestTrapCamera/detector.cpp`, `sensors.cpp` |
| Processing and communication | `trap.cpp`, `telemetry.cpp`, `webui.cpp` |
| Pest removal | `actuators.cpp` (UV array + blower) |

Each sketch's `.ino` only calls into `app.cpp`. That is deliberate: the Arduino
IDE generates function prototypes at file scope for anything in the `.ino`, and
gets it wrong for the namespaced functions this project uses throughout.

## Quick start (Arduino IDE)

**1. Install ESP32 board support.** File > Preferences > *Additional boards
manager URLs*:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then Tools > Board > Boards Manager, search **esp32**, install *esp32 by
Espressif Systems*.

**2. Install the libraries** the controller needs (Tools > Manage Libraries):

| Library | Author | Note |
|---|---|---|
| PubSubClient | Nick O'Leary | MQTT |
| ArduinoJson | Benoit Blanchon | **version 7.x** |
| DHT sensor library | Adafruit | |
| Adafruit Unified Sensor | Adafruit | pulled in by the DHT library |

The camera sketch needs no extra libraries; its driver ships with the board
package.

**3. Upload the controller.** Open `arduino/PestTrapController/PestTrapController.ino`.
Fill in the **secrets.h** tab with your Wi-Fi and MQTT details, then:

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| Partition Scheme | Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) |
| Upload Speed | 921600 |

**4. Upload the camera node.** Open `arduino/PestTrapCamera/PestTrapCamera.ino`.

| Setting | Value |
|---|---|
| Board | AI Thinker ESP32-CAM |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** - required, it will not fit otherwise |
| PSRAM | Enabled |

The ESP32-CAM has no USB. To flash it:

1. Disconnect the two link wires on GPIO13 and GPIO14.
2. Wire an FTDI adapter: 5V, GND, U0T to RX, U0R to TX.
3. Jumper GPIO0 to GND, press RESET, then Upload.
4. Remove the jumper, press RESET, reconnect the link wires.

**5. Open the dashboard.** The controller prints its IP on boot (Serial
Monitor, 115200). With no field Wi-Fi it raises its own hotspot,
`PestTrap-trap-01`, password `lactuca2026`, and serves the same page at
`http://192.168.4.1/` including a CSV download of the trial log.

**6. Calibrate before trusting any data** - see
[`docs/TESTING_PROTOCOL.md`](docs/TESTING_PROTOCOL.md) sections 1-4.

### If you would rather use PlatformIO

```bash
cd arduino/PestTrapController && pio run -t upload && pio device monitor
cd arduino/PestTrapCamera     && pio run -t upload
```

## How it operates

```
 DAY_IDLE ---- 18:00 ----> LURE <----------------+
    ^                       |                    |
    |                  detection             COOLDOWN
  05:00                     |                    ^
    |                       v                    |
    +----------------- CAPTURE (blower, 8 s) ----+

 any state --- SoC < 30 % ---> LOW_BATTERY --- SoC >= 40 % ---> resume
```

While armed, the UV array runs a **2 min on / 3 min off** cycle. That is a
power decision with a detection consequence: at night the array is the
camera's only illumination, so the controller ignores camera detections during
the dark phase, and the camera resets its background model on every transition
instead of reporting the illumination change as a swarm. The ultrasonic sensor
does not depend on light and keeps working throughout.

Every 15 minutes the controller logs a row to on-board flash and publishes it
to MQTT. Records that cannot be sent are spooled to flash and re-sent in order
on reconnect, each carrying a CRC-32 so the receiver can prove it arrived
intact - see [`docs/MQTT_API.md`](docs/MQTT_API.md).

## Detection method

The camera node runs a classical vision pipeline, chosen so it fits in the
ESP32-CAM's budget and so every decision it makes is inspectable:

```
QVGA grayscale -> 80x60 grid -> selective background subtraction
  -> connected components -> shape features -> tracking -> classification
```

* **Caterpillars** are elongated (second-moment axis ratio >= 2.2), of larval
  size, and slow.
* **Aphids** are tiny and, decisively, **clustered** - a lone speck is scored
  as noise, because at this resolution a single 5-pixel blob is far more likely
  to be a dust mote than an aphid.
* **Non-targets** are oversized, or moving too fast to be anything but a flying
  insect in transit.

The background model updates 20x more slowly inside an active blob. That one
detail is what lets the trap see a caterpillar that has stopped moving; a plain
moving-average model absorbs a stationary larva within seconds and goes blind
to exactly the pest it is looking for.

A trained model can be dropped in without touching the rest of the pipeline via
`detector::setExternalClassifier()` - it receives the same per-blob features
and overrides the geometric verdict. The heuristic then remains as a fallback.

## Tests

The algorithmic code compiles and runs on a workstation against small shims, so
the logic can be checked without flashing hardware:

```bash
make -C test/host
```

76 checks cover the state-of-charge model, the trap state machine (load
shedding, hysteresis, the UV duty cycle, budget limits), the controller-camera
link including corrupted and foreign sentences, and the full detection pipeline
driven with synthetic frames - a slow elongated blob, a clustered colony, a
lone speck, a fast mover, an oversized object, and a global lighting change.

## Things worth knowing before the first trial

* **Check your panel's wattage.** A 30 W panel (PWM controller) or 20-25 W
  (MPPT) is needed to sustain the daily 53.5 Wh. A 10 W panel, common at this
  price point, harvests roughly half that and the trap will ratchet down night
  after night. The arithmetic is in [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md).
* **The `nontarget` counter is not a selectivity measurement.** It counts what
  the *camera rejected*; the blower captures whatever is in the suction cone
  regardless. Only the physical chamber audit measures selectivity. Report them
  separately.
* **Detection thresholds are geometry-dependent.** The blob areas in the
  camera sketch's `config.h` tab are meaningless until calibrated at your
  actual camera-to-intake distance.
* **Run a control plot and a UV-only night.** Without them, "attracted" and
  "removed" cannot be separated in the results.

## Licence

Released for academic use in connection with this study.
