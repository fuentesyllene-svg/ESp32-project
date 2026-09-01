#pragma once
#include <Arduino.h>

// On-board persistence: a human-readable CSV trial log plus a JSON-lines spool
// of telemetry records that could not be published yet.
namespace datalog {
bool begin();

// CSV trial log. The header is written once, when the file is created.
void logCsv(const String& row);
size_t logSize();
String logTail(size_t max_bytes);   // for the local web UI

// Store-and-forward spool.
bool   spoolPush(const String& json_line);
size_t spoolCount();
size_t spoolBytes();

// Hands each queued record to sender(). Stops at the first refusal and keeps
// that record and everything after it. Returns how many were accepted.
size_t spoolDrain(bool (*sender)(const String&), size_t max_records);

void   formatAll();                 // wipes both files (maintenance command)
size_t freeBytes();
}  // namespace datalog
