#pragma once
#include <Arduino.h>

// Serial link to the ESP32-CAM detection node. Protocol: docs/LINK_PROTOCOL.md
struct CamReport {
  uint16_t seq;
  uint32_t uptime_s;
  uint16_t caterpillars;
  uint16_t aphids;
  uint16_t nontarget;
  uint8_t  confidence_pct;
  uint32_t motion_px;
  float    fps;
  uint32_t received_ms;     // millis() when this report landed
};

struct CamHealth {
  bool     online;
  uint8_t  state;           // 0 boot, 1 learning, 2 armed, 3 error
  uint32_t free_heap;
  uint32_t frames;
  uint32_t errors;
  uint32_t sentences_ok;
  uint32_t sentences_bad;   // checksum failures - link quality indicator
  uint32_t sentences_lost;  // inferred from gaps in seq
  uint32_t last_rx_ms;
};

namespace cam_link {
void begin();

// Pump the UART. Returns true when a fresh DET report was parsed this call.
bool poll();

const CamReport& lastReport();
const CamHealth& health();

// Tell the camera whether the UV array is lit, so it can reset its background
// model instead of reporting the illumination change as a swarm.
void sendConfig(bool uv_on, uint8_t sensitivity_pct);
void sendPing();

// True when nothing has arrived for CAM_LINK_TIMEOUT_MS.
bool linkStale();
}  // namespace cam_link
