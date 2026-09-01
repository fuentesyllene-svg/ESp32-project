#include "Arduino.h"
#include <cstdarg>
#include <map>

namespace {
uint32_t now_ms_ = 0;
std::map<int, uint32_t> pin_mv_;
std::map<int, HardwareSerial*> serials_;
}  // namespace

uint32_t millis() { return now_ms_; }
void shim_advance_ms(uint32_t ms) { now_ms_ += ms; }
void shim_reset_time() { now_ms_ = 0; }

void analogSetPinAttenuation(int, int) {}
void analogReadResolution(int) {}

uint32_t analogReadMilliVolts(int pin) {
  auto it = pin_mv_.find(pin);
  return it == pin_mv_.end() ? 0 : it->second;
}

int analogRead(int pin) {
  // 12-bit over the ~3.1 V full-scale range of the 11 dB attenuator.
  const uint32_t mv = analogReadMilliVolts(pin);
  const uint32_t counts = mv * 4095 / 3100;
  return static_cast<int>(counts > 4095 ? 4095 : counts);
}

void shim_set_pin_mv(int pin, uint32_t mv) { pin_mv_[pin] = mv; }

int HardwareSerial::printf(const char* fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  tx_ += buf;
  return n;
}

void shim_register_serial(int num, HardwareSerial* s) { serials_[num] = s; }
HardwareSerial* shim_serial(int num) {
  auto it = serials_.find(num);
  return it == serials_.end() ? nullptr : it->second;
}

HardwareSerial Serial(0);
