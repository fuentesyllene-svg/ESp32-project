#include "trap.h"
#include "config.h"
#include "actuators.h"

namespace {
TrapState state_ = TrapState::BOOT;
trap::EventSink sink_ = nullptr;

uint32_t state_since_ = 0;
uint32_t last_capture_ms_ = 0;
uint32_t last_purge_ms_ = 0;
uint32_t capture_len_ms_ = CAPTURE_MS;

uint32_t lure_phase_start_ = 0;   // anchors the UV duty cycle
bool     uv_phase_on_ = true;

uint32_t captures_ = 0, cap_cam_ = 0, cap_sonar_ = 0, purges_ = 0;
bool enabled_ = true;
int  uv_override_ = -1;
bool pending_manual_capture_ = false;
uint32_t manual_capture_ms_ = 0;

void emit(const char* type, const String& detail) {
  if (sink_) sink_(type, detail);
}

void enter(TrapState s) {
  if (state_ == s) return;
  state_ = s;
  state_since_ = millis();
}

uint32_t inState() { return millis() - state_since_; }

void allLoadsOff() {
  actuators::fanStop();
  actuators::uvSet(0);
}

uint8_t uvTarget() {
  return (uv_override_ >= 0) ? static_cast<uint8_t>(uv_override_) : UV_DUTY_PCT;
}

// The blower may only run if the battery can afford it and the rolling-hour
// budget has room for a whole burst. Starting a burst we cannot finish would
// leave insects half-drawn into the throat.
bool fanAllowed(float soc_pct, uint32_t want_ms) {
  if (soc_pct < SOC_FAN_MIN_PCT) return false;
  return actuators::fanBudgetRemainingSec() >= (want_ms / 1000);
}
}  // namespace

namespace trap {

void begin(EventSink sink) {
  sink_ = sink;
  state_ = TrapState::BOOT;
  state_since_ = millis();
  last_purge_ms_ = millis();
}

void update(const TrapInputs& in) {
  const uint32_t now = millis();

  // --- unconditional guards, evaluated before the normal state flow --------
  if (!enabled_) {
    if (state_ != TrapState::DISABLED) {
      allLoadsOff();
      emit("disabled", "remote command");
      enter(TrapState::DISABLED);
    }
    return;
  }

  if (in.soc_pct < SOC_SHED_PCT && state_ != TrapState::LOW_BATTERY) {
    allLoadsOff();
    emit("low_battery", String("soc=") + String(in.soc_pct, 1) + "%");
    enter(TrapState::LOW_BATTERY);
    return;
  }

  // A transition is re-dispatched once, so the state we land in applies its
  // outputs in the same control cycle. Without this, entering LURE leaves the
  // UV array dark until the next pass through the loop.
  for (int pass = 0; pass < 2; ++pass) {
  const TrapState before_pass = state_;
  switch (state_) {
    case TrapState::BOOT:
      allLoadsOff();
      lure_phase_start_ = now;
      enter(in.night ? TrapState::LURE : TrapState::DAY_IDLE);
      break;

    case TrapState::LOW_BATTERY:
      // Hysteresis: recover well above the shed point so the trap does not
      // oscillate on and off around the threshold at dawn.
      if (in.soc_pct >= SOC_RECOVER_PCT) {
        emit("recovered", String("soc=") + String(in.soc_pct, 1) + "%");
        lure_phase_start_ = now;
        enter(in.night ? TrapState::LURE : TrapState::DAY_IDLE);
      }
      break;

    case TrapState::DAY_IDLE:
      allLoadsOff();
      if (in.night) {
        emit("armed", "night window open");
        enter(TrapState::LURE);
        last_purge_ms_ = now;
        lure_phase_start_ = now;
      }
      break;

    case TrapState::LURE: {
      // UV duty cycle. An explicit override (from the dashboard, during
      // calibration) pins the array on and bypasses the cycle.
      if (UV_DUTY_CYCLE_ENABLE && uv_override_ < 0) {
        const uint32_t period = UV_ON_MS + UV_OFF_MS;
        const uint32_t phase = (now - lure_phase_start_) % period;
        uv_phase_on_ = phase < UV_ON_MS;
      } else {
        uv_phase_on_ = true;
      }
      actuators::uvSet(uv_phase_on_ ? uvTarget() : 0);

      if (!in.night) {
        emit("disarmed", "night window closed");
        allLoadsOff();
        enter(TrapState::DAY_IDLE);
        break;
      }
      if (pending_manual_capture_) {
        pending_manual_capture_ = false;
        capture_len_ms_ = manual_capture_ms_;
        actuators::fanStart(FAN_DUTY_PCT);
        ++captures_;
        emit("capture", "trigger=manual");
        enter(TrapState::CAPTURE);
        break;
      }
      // While the array is dark the camera has no illumination, so a
      // "detection" in that phase cannot be trusted. The ultrasonic sensor is
      // unaffected by light and still counts.
      const bool detected =
          (in.cam_detection && uv_phase_on_) || in.sonar_detection;
      const bool holdoff_clear = (now - last_capture_ms_) > DETECT_HOLDOFF_MS;
      if (detected && holdoff_clear) {
        if (fanAllowed(in.soc_pct, CAPTURE_MS)) {
          capture_len_ms_ = CAPTURE_MS;
          actuators::fanStart(FAN_DUTY_PCT);
          ++captures_;
          if (in.cam_detection) ++cap_cam_; else ++cap_sonar_;
          emit("capture", String("trigger=") +
                              (in.cam_detection ? "camera" : "ultrasonic"));
          enter(TrapState::CAPTURE);
        } else {
          // Detected but cannot act: worth recording, because a trial that
          // shows many of these is power-limited, not detection-limited.
          emit("capture_skipped",
               String("soc=") + String(in.soc_pct, 1) +
                   "% budget_s=" + String(actuators::fanBudgetRemainingSec()));
          last_capture_ms_ = now;
        }
        break;
      }
      if ((now - last_purge_ms_) > PURGE_INTERVAL_MS &&
          fanAllowed(in.soc_pct, PURGE_MS)) {
        actuators::fanStart(FAN_DUTY_PCT);
        ++purges_;
        emit("purge", "scheduled sweep");
        enter(TrapState::PURGE);
      }
      break;
    }

    case TrapState::CAPTURE:
      if (inState() >= capture_len_ms_) {
        actuators::fanStop();
        last_capture_ms_ = now;
        enter(TrapState::COOLDOWN);
      }
      break;

    case TrapState::PURGE:
      if (inState() >= PURGE_MS) {
        actuators::fanStop();
        last_purge_ms_ = now;
        last_capture_ms_ = now;
        enter(TrapState::COOLDOWN);
      }
      break;

    case TrapState::COOLDOWN:
      if (inState() >= COOLDOWN_MS) {
        enter(in.night ? TrapState::LURE : TrapState::DAY_IDLE);
      }
      break;

    case TrapState::DISABLED:
      if (enabled_) enter(TrapState::BOOT);
      break;
  }
  if (state_ == before_pass) break;
  }
}

TrapState state() { return state_; }

const char* stateName() {
  switch (state_) {
    case TrapState::BOOT:        return "BOOT";
    case TrapState::DAY_IDLE:    return "DAY_IDLE";
    case TrapState::LURE:        return "LURE";
    case TrapState::CAPTURE:     return "CAPTURE";
    case TrapState::PURGE:       return "PURGE";
    case TrapState::COOLDOWN:    return "COOLDOWN";
    case TrapState::LOW_BATTERY: return "LOW_BATTERY";
    case TrapState::DISABLED:    return "DISABLED";
  }
  return "?";
}

uint32_t captures()      { return captures_; }
uint32_t capturesByCam() { return cap_cam_; }
uint32_t capturesBySonar(){ return cap_sonar_; }
uint32_t purges()        { return purges_; }

float estimatedLoadAmps() {
  float a = LOAD_A_BASE;
  a += LOAD_A_UV * (actuators::uvOn() ? 1.0f : 0.0f);
  a += LOAD_A_FAN * (actuators::fanOn() ? 1.0f : 0.0f);
  return a;
}

void setEnabled(bool on) {
  enabled_ = on;
  if (on && state_ == TrapState::DISABLED) enter(TrapState::BOOT);
}
bool enabled() { return enabled_; }

void forceCapture(uint32_t ms) {
  manual_capture_ms_ = constrain(ms, 500UL, 30000UL);
  pending_manual_capture_ = true;
}

void setUvOverride(int pct) {
  uv_override_ = (pct < 0) ? -1 : constrain(pct, 0, 100);
}

}  // namespace trap
