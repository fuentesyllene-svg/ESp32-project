#pragma once
// -----------------------------------------------------------------------------
// IoT-Enabled Caterpillar and Aphid Detection and Removal System
// Solar-Powered UV-Light and Fan Trap for Lactuca sativa
//
// Main controller (ESP32 DevKit v1) - compile-time configuration.
// Every tunable that a field trial might need to change lives in this file.
// Pin rationale and wiring are documented in docs/WIRING.md.
// -----------------------------------------------------------------------------

// ---------------------------------------------------------------- identity ---
#define DEVICE_ID          "trap-01"      // unique per unit; used in MQTT topics
#define FIRMWARE_VERSION   "1.0.0"
#define SITE_NAME          "Malamba-Salaysay"

// -------------------------------------------------------------------- pins ---
static const int PIN_UV_GATE      = 18;   // MOSFET gate, UV-A LED array
static const int PIN_FAN_GATE     = 19;   // MOSFET gate, 12 V centrifugal blower
static const int PIN_CAM_POWER    = 23;   // high-side switch for the ESP32-CAM 5 V
static const int PIN_LINK_RX      = 16;   // UART2 RX  <- ESP32-CAM GPIO13
static const int PIN_LINK_TX      = 17;   // UART2 TX  -> ESP32-CAM GPIO14
static const int PIN_VBAT_SENSE   = 34;   // ADC1_CH6, 100k/10k divider
static const int PIN_VPV_SENSE    = 35;   // ADC1_CH7, 100k/10k divider
static const int PIN_LDR          = 33;   // ADC1_CH5, ambient light
static const int PIN_DHT          = 26;   // DHT22 data
static const int PIN_SONAR_TRIG   = 25;   // HC-SR04 trigger
static const int PIN_SONAR_ECHO   = 39;   // HC-SR04 echo (through 10k/20k divider)
static const int PIN_STATUS_LED   = 2;    // onboard LED

// ------------------------------------------------------------ power module ---
// Divider ratio (R1 + R2) / R2 with R1 = 100k, R2 = 10k. Trim per unit against
// a bench meter: measured_battery_volts / reported_volts * current_ratio.
static const float VBAT_DIVIDER_RATIO = 11.00f;
static const float VPV_DIVIDER_RATIO  = 11.00f;

// 12 V SLA internal resistance, used to compensate the terminal voltage back to
// an open-circuit estimate while loads are drawing. 9 Ah AGM/SLA is ~25-35 mOhm
// when healthy; raise it as the battery ages.
static const float BATT_INTERNAL_OHMS = 0.030f;

// Current each load draws from the 12 V bus, for load compensation and for the
// energy accounting in the telemetry payload. Measure yours and update.
static const float LOAD_A_BASE = 0.18f;   // both ESP32s + sensors via the buck
static const float LOAD_A_UV   = 0.50f;   // UV-A array at full duty (~6 W)
static const float LOAD_A_FAN  = 1.80f;   // centrifugal blower at full duty

// State-of-charge thresholds. The research protocol requires the battery to
// stay above 30 % through a night of continuous operation, so the firmware
// sheds load at 30 % rather than discharging into the SLA's damage region.
static const float SOC_SHED_PCT    = 30.0f;  // -> LOW_BATTERY, all loads off
static const float SOC_RECOVER_PCT = 40.0f;  // hysteresis before re-arming
static const float SOC_FAN_MIN_PCT = 35.0f;  // below this the blower stays off
                                             // but the UV lure may continue

// ---------------------------------------------------------- night operation --
// Local time window for active trapping (Asia/Manila, UTC+8, no DST).
static const int NIGHT_START_HOUR = 18;
static const int NIGHT_END_HOUR   = 5;
#define TIMEZONE_POSIX "PHT-8"
#define NTP_SERVER_1   "time.google.com"
#define NTP_SERVER_2   "ph.pool.ntp.org"

// LDR fallback used when NTP has never synced (raw ADC counts, 0-4095).
// Below this = dark. Calibrate on site at dusk.
static const int LDR_DARK_COUNTS = 900;

// ------------------------------------------------------------- trap timing ---
static const uint8_t  UV_DUTY_PCT        = 100;    // UV brightness while luring

// UV duty cycling. A 6 W UV-A array run continuously costs ~5.5 Ah over an
// 11-hour night, which a 9 Ah SLA cannot supply without going below the 30 %
// floor this study requires - see docs/POWER_BUDGET.md. The array is therefore
// pulsed. Note that at night the UV array is the camera's only illumination,
// so detection is only possible during the ON phase; the cycle is kept long so
// the background model has time to stabilise after each transition.
static const bool     UV_DUTY_CYCLE_ENABLE = true;
static const uint32_t UV_ON_MS  = 120000;   // 2 min lit
static const uint32_t UV_OFF_MS = 180000;   // 3 min dark  -> 40 % duty
static const uint8_t  FAN_DUTY_PCT       = 100;    // blower duty during capture
static const uint32_t FAN_SOFTSTART_MS   = 600;    // ramp, limits inrush sag
static const uint32_t CAPTURE_MS         = 8000;   // suction burst per detection
static const uint32_t COOLDOWN_MS        = 5000;   // settle before re-arming
static const uint32_t PURGE_INTERVAL_MS  = 20UL * 60UL * 1000UL;  // scheduled sweep
static const uint32_t PURGE_MS           = 10000;  // duration of that sweep

// Duty-cycle guard: the blower may not run more than this many seconds in any
// rolling hour. Protects the 9 Ah battery from a detection storm.
static const uint32_t FAN_MAX_SEC_PER_HOUR = 300;

// ---------------------------------------------------------------- detection --
// A capture fires when the camera node reports target insects with at least
// this confidence, or when the ultrasonic sensor sees an object hovering in the
// intake throat. Both paths are logged separately so their contribution to the
// capture count can be separated during analysis.
static const uint8_t  DETECT_CONF_MIN_PCT   = 60;
static const uint8_t  DETECT_MIN_TARGETS    = 1;
static const uint32_t DETECT_HOLDOFF_MS     = 3000;  // ignore repeats within
static const uint32_t CAM_LINK_TIMEOUT_MS   = 30000; // no frame -> cam fault
static const uint32_t CAM_REBOOT_HOLD_MS    = 2000;  // power-cycle pulse width
// Boot + background-learning time to allow after powering the camera up,
// before its silence counts as a fault.
static const uint32_t CAM_BOOT_GRACE_MS    = 20000;

// Ultrasonic trigger: an object between these distances from the intake mouth.
static const float SONAR_MIN_CM = 3.0f;
static const float SONAR_MAX_CM = 25.0f;
static const uint8_t SONAR_CONFIRM_HITS = 3;   // consecutive reads to confirm

// -------------------------------------------------------------- telemetry ----
// The evaluation plan logs ambient readings and battery voltage every 15 min.
static const uint32_t TELEMETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;
static const uint32_t SENSOR_POLL_MS        = 2000;
static const uint32_t WIFI_RETRY_MS         = 30000;
static const uint32_t MQTT_RETRY_MS         = 15000;

// Store-and-forward spool. Records that cannot be published are queued here and
// re-sent on reconnect; this is what makes the >=95 % delivery target reachable
// over an intermittent rural link.
#define SPOOL_PATH        "/spool.jsonl"
#define LOGFILE_PATH      "/trial.csv"
static const size_t SPOOL_MAX_BYTES   = 256UL * 1024UL;
static const size_t LOGFILE_MAX_BYTES = 512UL * 1024UL;

// ------------------------------------------------------------------ system ---
static const uint32_t WDT_TIMEOUT_S = 30;
#define AP_FALLBACK_SSID  "PestTrap-" DEVICE_ID
// AP password must be at least 8 characters.
#define AP_FALLBACK_PASS  "lactuca2026"
