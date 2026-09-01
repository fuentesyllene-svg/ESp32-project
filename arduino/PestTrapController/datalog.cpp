#include "datalog.h"
#include "config.h"
#include <LittleFS.h>

namespace {
const char* kCsvHeader =
    "iso_time,uptime_s,state,vbat,vocv,soc_pct,vpv,charging,temp_c,rh_pct,"
    "ldr,caterpillars,aphids,nontarget,conf,captures,fan_run_s,cam_state,"
    "rssi,pub_ok,pub_fail\n";

size_t spool_count_ = 0;

size_t countLines(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  size_t n = 0;
  while (f.available()) {
    if (f.read() == '\n') ++n;
  }
  f.close();
  return n;
}

// Keeps a file bounded by dropping the oldest half. Flash on an ESP32 is small
// and a 72-hour trial at one row per 15 minutes plus capture events will
// otherwise fill it; halving keeps the most recent data without a stall.
void trimHalf(const char* path) {
  File in = LittleFS.open(path, "r");
  if (!in) return;
  const size_t total = in.size();
  in.seek(total / 2);
  while (in.available() && in.read() != '\n') {}   // align to a line boundary

  File out = LittleFS.open("/trim.tmp", "w");
  if (!out) { in.close(); return; }
  uint8_t chunk[256];
  while (in.available()) {
    const size_t n = in.read(chunk, sizeof(chunk));
    out.write(chunk, n);
  }
  in.close();
  out.close();
  LittleFS.remove(path);
  LittleFS.rename("/trim.tmp", path);
}
}  // namespace

namespace datalog {

bool begin() {
  if (!LittleFS.begin(true)) return false;      // true = format on first boot
  if (!LittleFS.exists(LOGFILE_PATH)) {
    File f = LittleFS.open(LOGFILE_PATH, "w");
    if (f) { f.print(kCsvHeader); f.close(); }
  }
  spool_count_ = countLines(SPOOL_PATH);
  return true;
}

void logCsv(const String& row) {
  File f = LittleFS.open(LOGFILE_PATH, "a");
  if (!f) return;
  f.print(row);
  if (!row.endsWith("\n")) f.print('\n');
  const size_t sz = f.size();
  f.close();
  if (sz > LOGFILE_MAX_BYTES) trimHalf(LOGFILE_PATH);
}

size_t logSize() {
  File f = LittleFS.open(LOGFILE_PATH, "r");
  if (!f) return 0;
  const size_t s = f.size();
  f.close();
  return s;
}

String logTail(size_t max_bytes) {
  File f = LittleFS.open(LOGFILE_PATH, "r");
  if (!f) return String();
  if (f.size() > max_bytes) {
    f.seek(f.size() - max_bytes);
    while (f.available() && f.read() != '\n') {}
  }
  String out;
  out.reserve(max_bytes);
  while (f.available()) out += static_cast<char>(f.read());
  f.close();
  return out;
}

bool spoolPush(const String& json_line) {
  if (spoolBytes() > SPOOL_MAX_BYTES) trimHalf(SPOOL_PATH);
  File f = LittleFS.open(SPOOL_PATH, "a");
  if (!f) return false;
  f.print(json_line);
  f.print('\n');
  f.close();
  ++spool_count_;
  return true;
}

size_t spoolCount() { return spool_count_; }

size_t spoolBytes() {
  File f = LittleFS.open(SPOOL_PATH, "r");
  if (!f) return 0;
  const size_t s = f.size();
  f.close();
  return s;
}

size_t spoolDrain(bool (*sender)(const String&), size_t max_records) {
  File in = LittleFS.open(SPOOL_PATH, "r");
  if (!in) return 0;

  File out = LittleFS.open("/spool.tmp", "w");
  if (!out) { in.close(); return 0; }

  size_t sent = 0, kept = 0;
  bool still_sending = true;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (still_sending && sent < max_records) {
      if (sender(line)) { ++sent; continue; }
      still_sending = false;               // preserve chronological order
    }
    out.print(line);
    out.print('\n');
    ++kept;
  }
  in.close();
  out.close();
  LittleFS.remove(SPOOL_PATH);
  LittleFS.rename("/spool.tmp", SPOOL_PATH);
  spool_count_ = kept;
  return sent;
}

void formatAll() {
  LittleFS.remove(SPOOL_PATH);
  LittleFS.remove(LOGFILE_PATH);
  spool_count_ = 0;
  File f = LittleFS.open(LOGFILE_PATH, "w");
  if (f) { f.print(kCsvHeader); f.close(); }
}

size_t freeBytes() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

}  // namespace datalog
