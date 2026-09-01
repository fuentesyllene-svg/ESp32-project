// Minimal esp_camera shim: lets the detection pipeline be driven with
// synthetic frames on a workstation. Used only by test/host.
#pragma once
#include <cstdint>
#include <cstddef>

#define ESP_OK 0
typedef int esp_err_t;

typedef enum { PIXFORMAT_GRAYSCALE, PIXFORMAT_JPEG, PIXFORMAT_RGB565 } pixformat_t;
typedef enum { FRAMESIZE_QQVGA, FRAMESIZE_QVGA, FRAMESIZE_VGA } framesize_t;
typedef enum { CAMERA_FB_IN_PSRAM, CAMERA_FB_IN_DRAM } camera_fb_location_t;
typedef enum { CAMERA_GRAB_WHEN_EMPTY, CAMERA_GRAB_LATEST } camera_grab_mode_t;
typedef enum { GAINCEILING_2X, GAINCEILING_16X } gainceiling_t;
#define LEDC_CHANNEL_0 0
#define LEDC_TIMER_0 0

typedef struct {
  int pin_pwdn, pin_reset, pin_xclk, pin_sccb_sda, pin_sccb_scl;
  int pin_d0, pin_d1, pin_d2, pin_d3, pin_d4, pin_d5, pin_d6, pin_d7;
  int pin_vsync, pin_href, pin_pclk;
  int xclk_freq_hz;
  int ledc_timer, ledc_channel;
  pixformat_t pixel_format;
  framesize_t frame_size;
  int jpeg_quality, fb_count;
  camera_fb_location_t fb_location;
  camera_grab_mode_t grab_mode;
} camera_config_t;

typedef struct {
  uint8_t* buf;
  size_t   len;
  size_t   width, height;
  pixformat_t format;
} camera_fb_t;

typedef struct sensor_s {
  int (*set_gain_ctrl)(struct sensor_s*, int);
  int (*set_gainceiling)(struct sensor_s*, gainceiling_t);
  int (*set_exposure_ctrl)(struct sensor_s*, int);
  int (*set_aec2)(struct sensor_s*, int);
  int (*set_brightness)(struct sensor_s*, int);
  int (*set_contrast)(struct sensor_s*, int);
  int (*set_whitebal)(struct sensor_s*, int);
  int (*set_hmirror)(struct sensor_s*, int);
  int (*set_vflip)(struct sensor_s*, int);
} sensor_t;

esp_err_t esp_camera_init(const camera_config_t* cfg);
camera_fb_t* esp_camera_fb_get();
void esp_camera_fb_return(camera_fb_t* fb);
sensor_t* esp_camera_sensor_get();
bool psramFound();

// ---- test hooks
void shim_camera_set_frame(const uint8_t* gray, int w, int h);
void shim_camera_fail_next(bool fail);
