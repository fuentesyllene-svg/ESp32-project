#include "esp_camera.h"
#include <cstring>
#include <vector>

namespace {
std::vector<uint8_t> frame_;
camera_fb_t fb_{};
bool fail_next_ = false;
sensor_t sensor_{};

int noop_i(sensor_t*, int) { return 0; }
int noop_g(sensor_t*, gainceiling_t) { return 0; }
}  // namespace

esp_err_t esp_camera_init(const camera_config_t*) {
  sensor_.set_gain_ctrl = noop_i;
  sensor_.set_gainceiling = noop_g;
  sensor_.set_exposure_ctrl = noop_i;
  sensor_.set_aec2 = noop_i;
  sensor_.set_brightness = noop_i;
  sensor_.set_contrast = noop_i;
  sensor_.set_whitebal = noop_i;
  sensor_.set_hmirror = noop_i;
  sensor_.set_vflip = noop_i;
  return ESP_OK;
}

camera_fb_t* esp_camera_fb_get() {
  if (fail_next_ || frame_.empty()) return nullptr;
  return &fb_;
}

void esp_camera_fb_return(camera_fb_t*) {}
sensor_t* esp_camera_sensor_get() { return &sensor_; }
bool psramFound() { return true; }

void shim_camera_set_frame(const uint8_t* gray, int w, int h) {
  frame_.assign(gray, gray + static_cast<size_t>(w) * h);
  fb_.buf = frame_.data();
  fb_.len = frame_.size();
  fb_.width = w;
  fb_.height = h;
  fb_.format = PIXFORMAT_GRAYSCALE;
}

void shim_camera_fail_next(bool fail) { fail_next_ = fail; }
