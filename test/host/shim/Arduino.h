// Minimal Arduino.h substitute so the algorithmic firmware sources can be
// compiled and exercised on a workstation. Used only by test/host.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

// ------------------------------------------------------------------ timing --
uint32_t millis();
void     shim_advance_ms(uint32_t ms);
void     shim_reset_time();
inline void delay(uint32_t ms) { shim_advance_ms(ms); }
inline void delayMicroseconds(uint32_t) {}

// -------------------------------------------------------------------- misc --
#define PROGMEM
#define F(x) x
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int  digitalRead(int) { return 0; }
inline unsigned long pulseIn(int, int, unsigned long) { return 0; }

#define constrain(amt, low, high) \
  ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
using std::min;
using std::max;

// --------------------------------------------------------------------- ADC --
#define ADC_11db 3
void analogSetPinAttenuation(int pin, int atten);
void analogReadResolution(int bits);
uint32_t analogReadMilliVolts(int pin);
int analogRead(int pin);
// Test hook: pretend this pin sits at the given millivolts.
void shim_set_pin_mv(int pin, uint32_t mv);

// ------------------------------------------------------------------ String --
class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
  String(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
  String(long v) { char b[24]; snprintf(b, sizeof(b), "%ld", v); s_ = b; }
  String(unsigned long v) { char b[24]; snprintf(b, sizeof(b), "%lu", v); s_ = b; }
  String(float v, int digits = 2) {
    char b[32]; snprintf(b, sizeof(b), "%.*f", digits, v); s_ = b;
  }
  String(double v, int digits = 2) {
    char b[32]; snprintf(b, sizeof(b), "%.*f", digits, v); s_ = b;
  }
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  void reserve(size_t n) { s_.reserve(n); }
  bool endsWith(const String& o) const {
    return s_.size() >= o.s_.size() &&
           s_.compare(s_.size() - o.s_.size(), o.s_.size(), o.s_) == 0;
  }
  int indexOf(const char* n) const {
    const size_t p = s_.find(n);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  void trim() {
    const char* ws = " \t\r\n";
    const size_t a = s_.find_first_not_of(ws);
    if (a == std::string::npos) { s_.clear(); return; }
    s_ = s_.substr(a, s_.find_last_not_of(ws) - a + 1);
  }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* o) { s_ += o; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  friend String operator+(String a, const String& b) { a.s_ += b.s_; return a; }
  friend String operator+(String a, const char* b) { a.s_ += b; return a; }
  bool operator==(const char* o) const { return s_ == o; }
  const std::string& std_str() const { return s_; }

 private:
  std::string s_;
};

// ----------------------------------------------------------- HardwareSerial --
#define SERIAL_8N1 0x800001c
class HardwareSerial;
void shim_register_serial(int num, HardwareSerial* s);
HardwareSerial* shim_serial(int num);
class HardwareSerial {
 public:
  explicit HardwareSerial(int num) : num_(num) { shim_register_serial(num, this); }
  void begin(unsigned long baud, uint32_t cfg = SERIAL_8N1, int rx = -1,
             int tx = -1) {
    (void)baud; (void)cfg; (void)rx; (void)tx;
  }
  void setRxBufferSize(size_t) {}
  int available() { return static_cast<int>(rx_.size()) - rx_pos_; }
  int read() {
    if (rx_pos_ >= static_cast<int>(rx_.size())) return -1;
    return static_cast<unsigned char>(rx_[rx_pos_++]);
  }
  void print(const char* s) { tx_ += s; }
  void println(const char* s) { tx_ += s; tx_ += "\n"; }
  void println() { tx_ += "\n"; }
  int printf(const char* fmt, ...);

  // ---- test hooks
  void shim_inject(const std::string& data) { rx_ += data; }
  std::string shim_tx() const { return tx_; }
  void shim_clear_tx() { tx_.clear(); }

 private:
  int num_;
  std::string rx_;
  int rx_pos_ = 0;
  std::string tx_;
};
extern HardwareSerial Serial;
