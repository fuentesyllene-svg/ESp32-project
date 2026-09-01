#include "link.h"
#include "config.h"

namespace {
HardwareSerial port(1);
link::ConfigHandler on_config_ = nullptr;
link::PingHandler   on_ping_ = nullptr;

char   buf_[96];
size_t len_ = 0;
uint16_t seq_ = 0;

uint8_t checksum(const char* s, size_t n) {
  uint8_t cs = 0;
  for (size_t i = 0; i < n; ++i) cs ^= static_cast<uint8_t>(s[i]);
  return cs;
}

void send(const char* body) {
  port.printf("$%s*%02X\r\n", body, checksum(body, strlen(body)));
}

void handle(char* body) {
  char* f[6];
  int n = 0;
  char* p = body;
  f[n++] = p;
  while (*p && n < 6) {
    if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
    ++p;
  }
  if (n < 2 || strcmp(f[0], "PEST") != 0) return;

  if (strcmp(f[1], "CFG") == 0 && n >= 4) {
    const bool uv = strtoul(f[2], nullptr, 10) != 0;
    const uint8_t sens = static_cast<uint8_t>(strtoul(f[3], nullptr, 10));
    if (on_config_) on_config_(uv, sens);
  } else if (strcmp(f[1], "PNG") == 0) {
    if (on_ping_) on_ping_();
  }
}
}  // namespace

namespace link {

void begin(ConfigHandler on_config, PingHandler on_ping) {
  on_config_ = on_config;
  on_ping_ = on_ping;
  port.begin(LINK_BAUD, SERIAL_8N1, PIN_UPLINK_RX, PIN_UPLINK_TX);
}

void poll() {
  while (port.available()) {
    const char c = static_cast<char>(port.read());
    if (c == '\n' || c == '\r') {
      if (len_ > 0) {
        buf_[len_] = '\0';
        if (buf_[0] == '$') {
          char* star = strrchr(buf_, '*');
          if (star && strlen(star) >= 3) {
            *star = '\0';
            const uint8_t want =
                static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));
            if (checksum(buf_ + 1, strlen(buf_ + 1)) == want) handle(buf_ + 1);
          }
        }
        len_ = 0;
      }
    } else if (len_ < sizeof(buf_) - 1) {
      buf_[len_++] = c;
    } else {
      len_ = 0;
    }
  }
}

void sendDetection(uint16_t caterpillars, uint16_t aphids, uint16_t nontarget,
                   uint8_t confidence_pct, uint32_t motion_px, float fps) {
  char body[96];
  snprintf(body, sizeof(body), "PEST,DET,%u,%lu,%u,%u,%u,%u,%lu,%u",
           seq_++, static_cast<unsigned long>(millis() / 1000), caterpillars,
           aphids, nontarget, confidence_pct,
           static_cast<unsigned long>(motion_px),
           static_cast<unsigned>(fps * 10.0f + 0.5f));
  send(body);
}

void sendStatus(uint8_t state, uint32_t free_heap, uint32_t frames,
                uint32_t errors) {
  char body[96];
  snprintf(body, sizeof(body), "PEST,STA,%u,%lu,%u,%lu,%lu,%lu", seq_++,
           static_cast<unsigned long>(millis() / 1000), state,
           static_cast<unsigned long>(free_heap),
           static_cast<unsigned long>(frames),
           static_cast<unsigned long>(errors));
  send(body);
}

}  // namespace link
