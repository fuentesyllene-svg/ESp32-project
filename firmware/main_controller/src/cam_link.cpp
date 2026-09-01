#include "cam_link.h"
#include "config.h"

namespace {
HardwareSerial link(2);                    // UART2 on PIN_LINK_RX/PIN_LINK_TX
char     buf_[128];
size_t   buf_len_ = 0;
CamReport report_{};
CamHealth health_{};
bool     have_seq_ = false;
uint16_t expect_seq_ = 0;

uint8_t checksum(const char* s, size_t len) {
  uint8_t cs = 0;
  for (size_t i = 0; i < len; ++i) cs ^= static_cast<uint8_t>(s[i]);
  return cs;
}

void sendSentence(const char* body) {
  const uint8_t cs = checksum(body, strlen(body));
  link.printf("$%s*%02X\r\n", body, cs);
}

// Splits a comma-separated body in place. Returns the field count.
int split(char* body, char* out[], int max_fields) {
  int n = 0;
  char* p = body;
  out[n++] = p;
  while (*p && n < max_fields) {
    if (*p == ',') { *p = '\0'; out[n++] = p + 1; }
    ++p;
  }
  return n;
}

void handleSentence(char* body) {
  char* f[12];
  const int n = split(body, f, 12);
  if (n < 2 || strcmp(f[0], "PEST") != 0) return;

  if (strcmp(f[1], "DET") == 0 && n >= 10) {
    CamReport r;
    r.seq            = static_cast<uint16_t>(strtoul(f[2], nullptr, 10));
    r.uptime_s       = strtoul(f[3], nullptr, 10);
    r.caterpillars   = static_cast<uint16_t>(strtoul(f[4], nullptr, 10));
    r.aphids         = static_cast<uint16_t>(strtoul(f[5], nullptr, 10));
    r.nontarget      = static_cast<uint16_t>(strtoul(f[6], nullptr, 10));
    r.confidence_pct = static_cast<uint8_t>(strtoul(f[7], nullptr, 10));
    r.motion_px      = strtoul(f[8], nullptr, 10);
    r.fps            = strtoul(f[9], nullptr, 10) / 10.0f;
    r.received_ms    = millis();

    // Gaps in the sequence number are dropped sentences: a direct measure of
    // link quality, reported in the telemetry payload.
    if (have_seq_ && r.seq != expect_seq_) {
      health_.sentences_lost += static_cast<uint16_t>(r.seq - expect_seq_);
    }
    expect_seq_ = r.seq + 1;
    have_seq_ = true;

    report_ = r;
    health_.online = true;
    health_.last_rx_ms = r.received_ms;
  } else if (strcmp(f[1], "STA") == 0 && n >= 8) {
    health_.state      = static_cast<uint8_t>(strtoul(f[4], nullptr, 10));
    health_.free_heap  = strtoul(f[5], nullptr, 10);
    health_.frames     = strtoul(f[6], nullptr, 10);
    health_.errors     = strtoul(f[7], nullptr, 10);
    health_.online     = true;
    health_.last_rx_ms = millis();
  }
}
}  // namespace

namespace cam_link {

void begin() {
  link.begin(115200, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  link.setRxBufferSize(512);
  health_ = CamHealth{};
  health_.last_rx_ms = millis();
}

bool poll() {
  bool got_det = false;
  const uint16_t before = report_.seq;
  const uint32_t before_rx = report_.received_ms;

  while (link.available()) {
    const char c = static_cast<char>(link.read());
    if (c == '\n' || c == '\r') {
      if (buf_len_ > 0) {
        buf_[buf_len_] = '\0';
        // Expect $<body>*CS
        if (buf_[0] == '$') {
          char* star = strrchr(buf_, '*');
          if (star && (star - buf_) > 1 && strlen(star) >= 3) {
            *star = '\0';
            const uint8_t want = static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));
            if (checksum(buf_ + 1, strlen(buf_ + 1)) == want) {
              ++health_.sentences_ok;
              handleSentence(buf_ + 1);
            } else {
              ++health_.sentences_bad;
            }
          }
        }
        buf_len_ = 0;
      }
    } else if (buf_len_ < sizeof(buf_) - 1) {
      buf_[buf_len_++] = c;
    } else {
      buf_len_ = 0;                        // overlong line: resynchronise
      ++health_.sentences_bad;
    }
  }

  if (report_.received_ms != before_rx || report_.seq != before) got_det = true;
  return got_det;
}

const CamReport& lastReport() { return report_; }
const CamHealth& health() { return health_; }

void sendConfig(bool uv_on, uint8_t sensitivity_pct) {
  char body[48];
  snprintf(body, sizeof(body), "PEST,CFG,%d,%u", uv_on ? 1 : 0,
           static_cast<unsigned>(sensitivity_pct));
  sendSentence(body);
}

void sendPing() { sendSentence("PEST,PNG"); }

bool linkStale() {
  return (millis() - health_.last_rx_ms) > CAM_LINK_TIMEOUT_MS;
}

}  // namespace cam_link
