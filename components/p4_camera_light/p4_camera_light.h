#pragma once
// Experimental: ambient-light proxy from the D1001 MIPI-CSI SC2356 camera via esp_video (V4L2).
// See ADR-0008. ESP32-P4 only. Stage 1: bring the stack up + grab one RAW8 frame per update and
// report its mean luma (0..100%). Power sequencing (CAM_EN/PWDN/RST via the XL9535) is TODO.
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome {
namespace p4_camera_light {

static constexpr int P4_CAM_BUF_NUM = 2;

class P4CameraLight : public PollingComponent, public sensor::Sensor {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // Camera power/reset lines (on the D1001 these are XL9535 expander pins). Optional.
  void set_enable_pin(GPIOPin *pin) { this->enable_pin_ = pin; }
  void set_power_down_pin(GPIOPin *pin) { this->power_down_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }

 protected:
  void power_up_();             // XL9535 CAM_EN/PWDN/RST sequence (Seeed BSP timing)
  bool init_camera_();          // esp_video_init + open + format + buffers + stream on
  bool capture_mean_(float &out_pct);  // dequeue one frame, average, requeue

  GPIOPin *enable_pin_{nullptr};
  GPIOPin *power_down_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  int fd_{-1};
  bool ready_{false};
  uint32_t width_{0};
  uint32_t height_{0};
  void *buffers_[P4_CAM_BUF_NUM]{};
  size_t buffer_len_[P4_CAM_BUF_NUM]{};
};

}  // namespace p4_camera_light
}  // namespace esphome
