#pragma once
// -----------------------------------------------------------------------------
// ESP32-CAM detection node - configuration.
//
// The thresholds below are in units of the 80x60 working grid, not the raw
// camera frame. They therefore depend on how far the camera sits from the
// intake and must be calibrated on the assembled prototype - the procedure is
// in docs/TESTING_PROTOCOL.md section 3.
// -----------------------------------------------------------------------------

#define CAM_NODE_VERSION "1.0.0"

// ------------------------------------------------------------------- link ----
// UART1 to the main controller. GPIO12 is deliberately avoided: it is the
// flash-voltage strapping pin and a UART line idling high there stops the
// module from booting. See docs/WIRING.md section 5.
static const int PIN_UPLINK_TX = 13;   // -> ESP32 GPIO16
static const int PIN_UPLINK_RX = 14;   // <- ESP32 GPIO17
static const uint32_t LINK_BAUD = 115200;
static const uint32_t STATUS_INTERVAL_MS = 10000;

// ------------------------------------------------------------ working grid ---
// QVGA (320x240) grayscale, box-downsampled by 4 in each axis.
static const int GRID_W = 80;
static const int GRID_H = 60;
static const int GRID_N = GRID_W * GRID_H;

// ------------------------------------------------ background / foreground ----
// Exponential background model. Lower alpha = slower to absorb a stationary
// insect, which is what lets the node report static targets and not just
// moving ones.
static const float BG_ALPHA        = 0.02f;   // normal update rate
static const float BG_ALPHA_BLOB   = 0.001f;  // inside an active blob
static const int   BG_LEARN_FRAMES = 40;      // frames before arming

// Foreground threshold in grey levels, scaled by the sensitivity the
// controller sends (0-100, 50 = nominal).
static const int FG_THRESHOLD_BASE = 18;
static const int FG_THRESHOLD_MIN  = 8;
static const int FG_THRESHOLD_MAX  = 45;

// A frame where this much of the scene changed is a lighting change (UV
// switching, headlights, dawn), not an infestation. It forces a model reset.
static const int GLOBAL_CHANGE_PCT = 35;

// --------------------------------------------------------------- blob size ---
// Areas in working-grid pixels. At 20 cm with QVGA, one grid pixel is roughly
// 1 mm across, so these correspond to ~4-60 mm2 targets.
static const int BLOB_MIN_AREA = 4;
static const int BLOB_MAX_AREA = 900;    // bigger = leaf, hand, or shadow

static const int CAT_MIN_AREA  = 12;     // caterpillar: elongated, slow
static const int CAT_MAX_AREA  = 400;
static const float CAT_MIN_ELONGATION = 2.2f;
// Area at which a blob is unambiguously large enough to be a larva rather than
// a noise cluster; the confidence score plateaus from here up.
static const int   CAT_CONFIDENT_AREA = 20;

static const int APH_MIN_AREA  = 4;      // aphid: tiny, appears in clusters
static const int APH_MAX_AREA  = 30;
static const float APH_CLUSTER_RADIUS = 12.0f;
static const int   APH_MIN_CLUSTER = 3;

// ---------------------------------------------------------------- tracking ---
static const int   MAX_BLOBS = 24;
// The match radius must exceed the largest displacement we want to *measure*,
// not the largest we want to accept. Set it below FAST_SPEED_PX and a flying
// insect simply fails to associate between frames, so it is scored as a new
// blob every frame and never counted as a non-target - which would quietly
// destroy the selectivity figure the trial reports.
static const float TRACK_MATCH_RADIUS = 24.0f;   // grid px between frames
// Anything crossing the frame faster than this is a flying non-target
// (bees, moths in transit). Caterpillars and settled aphids barely move.
static const float FAST_SPEED_PX = 6.0f;
static const uint8_t MIN_AGE_FRAMES = 3;         // persistence before counting

// ------------------------------------------------------------- diagnostics ---
// Optional: bring up Wi-Fi on the camera node and serve a snapshot at
// http://<ip>/jpg for aiming and threshold calibration. Leave this OFF during
// field trials - it roughly doubles the node's average current.
#define CAM_WIFI_DIAGNOSTICS 0
#if CAM_WIFI_DIAGNOSTICS
#define CAM_WIFI_SSID "your-field-hotspot"
#define CAM_WIFI_PASS "your-password"
#endif
