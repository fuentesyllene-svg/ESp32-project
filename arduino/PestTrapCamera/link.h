#pragma once
#include <Arduino.h>

// Camera-side end of the sentence protocol in docs/LINK_PROTOCOL.md.
namespace link {
using ConfigHandler = void (*)(bool uv_on, uint8_t sensitivity_pct);
using PingHandler   = void (*)();

void begin(ConfigHandler on_config, PingHandler on_ping);
void poll();

void sendDetection(uint16_t caterpillars, uint16_t aphids, uint16_t nontarget,
                   uint8_t confidence_pct, uint32_t motion_px, float fps);
void sendStatus(uint8_t state, uint32_t free_heap, uint32_t frames,
                uint32_t errors);
}  // namespace link
