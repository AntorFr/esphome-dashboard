#include "p4_camera_light.h"
#ifdef USE_ESP32
#include "esphome/core/log.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <cstring>
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"

namespace esphome {
namespace p4_camera_light {

static const char *const TAG = "p4_camera_light";

// D1001 SCCB (sensor config) = I2C_0, SCL GPIO38 / SDA GPIO37 (ADR-0008).
// TODO(stage 2): reuse ESPHome's i2c_master_bus_handle for this bus instead of re-init,
// and sequence CAM_EN/PWDN/RST via the XL9535 expander before esp_video_init.
static constexpr int SCCB_I2C_PORT = 0;
static constexpr int SCCB_SCL_PIN = 38;
static constexpr int SCCB_SDA_PIN = 37;
static constexpr int SCCB_FREQ = 100000;

void P4CameraLight::setup() {
  this->power_up_();
  if (this->init_camera_()) {
    this->ready_ = true;
    ESP_LOGCONFIG(TAG, "camera up (%" PRIu32 "x%" PRIu32 ")", this->width_, this->height_);
  } else {
    ESP_LOGE(TAG, "camera init failed — check the XL9535 power sequence / SCCB wiring");
    this->mark_failed();
  }
}

// Bring the sensor out of power-down/reset. On the D1001 CAM_EN/PWDN/RST are XL9535 expander
// pins; the order + delays match the Seeed BSP (esp32_p4_re_terminal_d1001.c).
void P4CameraLight::power_up_() {
  if (this->enable_pin_ != nullptr) {
    this->enable_pin_->setup();
    this->enable_pin_->digital_write(true);  // CAM_EN = 1 (camera power on)
    delay(50);
  }
  if (this->power_down_pin_ != nullptr) {
    this->power_down_pin_->setup();
    this->power_down_pin_->digital_write(true);  // CAM_PWDN = 1 (per BSP)
  }
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);   // release, then pulse low
    delay(10);
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(50);
  }
}

bool P4CameraLight::init_camera_() {
  esp_video_init_csi_config_t csi_config[1] = {};
  csi_config[0].sccb_config.init_sccb = true;
  csi_config[0].sccb_config.i2c_config.port = SCCB_I2C_PORT;
  csi_config[0].sccb_config.i2c_config.scl_pin = (gpio_num_t) SCCB_SCL_PIN;
  csi_config[0].sccb_config.i2c_config.sda_pin = (gpio_num_t) SCCB_SDA_PIN;
  csi_config[0].sccb_config.freq = SCCB_FREQ;
  csi_config[0].reset_pin = GPIO_NUM_NC;  // reset/pwdn are on the XL9535 expander (TODO stage 2)
  csi_config[0].pwdn_pin = GPIO_NUM_NC;

  esp_video_init_config_t cam_config = {};
  cam_config.csi = csi_config;

  esp_err_t err = esp_video_init(&cam_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init failed: %d", (int) err);
    return false;
  }

  this->fd_ = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY | O_NONBLOCK, 0);
  if (this->fd_ < 0) {
    ESP_LOGE(TAG, "open %s failed", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    return false;
  }

  struct v4l2_capability cap = {};
  if (ioctl(this->fd_, VIDIOC_QUERYCAP, &cap) != 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
    return false;
  }
  ESP_LOGI(TAG, "driver=%s card=%s", cap.driver, cap.card);

  struct v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->fd_, VIDIOC_G_FMT, &fmt) != 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    return false;
  }
  this->width_ = fmt.fmt.pix.width;
  this->height_ = fmt.fmt.pix.height;

  // RAW8 Bayer — cheapest, and the byte values track scene luminance well enough for a light
  // proxy (we only average). Keep the sensor's native resolution.
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR8;
  if (ioctl(this->fd_, VIDIOC_S_FMT, &fmt) != 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT RAW8 failed");
    return false;
  }

  struct v4l2_requestbuffers req = {};
  req.count = P4_CAM_BUF_NUM;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (ioctl(this->fd_, VIDIOC_REQBUFS, &req) != 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
    return false;
  }

  for (int i = 0; i < P4_CAM_BUF_NUM; i++) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (ioctl(this->fd_, VIDIOC_QUERYBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF %d failed", i);
      return false;
    }
    this->buffer_len_[i] = buf.length;
    this->buffers_[i] = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, this->fd_,
                             buf.m.offset);
    if (this->buffers_[i] == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap %d failed", i);
      return false;
    }
    if (ioctl(this->fd_, VIDIOC_QBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF %d failed", i);
      return false;
    }
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(this->fd_, VIDIOC_STREAMON, &type) != 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
    return false;
  }
  return true;
}

bool P4CameraLight::capture_mean_(float &out_pct) {
  struct v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  if (ioctl(this->fd_, VIDIOC_DQBUF, &buf) != 0)
    return false;  // no frame ready yet (non-blocking)

  const uint8_t *data = static_cast<const uint8_t *>(this->buffers_[buf.index]);
  size_t len = buf.bytesused ? buf.bytesused : this->buffer_len_[buf.index];
  // Subsample every 64th byte — a light meter needs an average, not every pixel.
  uint64_t sum = 0;
  uint32_t n = 0;
  for (size_t i = 0; i < len; i += 64) {
    sum += data[i];
    n++;
  }
  ioctl(this->fd_, VIDIOC_QBUF, &buf);  // return the buffer to the queue

  if (n == 0)
    return false;
  out_pct = (float) sum / (float) n / 255.0f * 100.0f;
  return true;
}

void P4CameraLight::update() {
  if (!this->ready_)
    return;
  float pct;
  if (this->capture_mean_(pct)) {
    this->publish_state(pct);
  } else {
    ESP_LOGD(TAG, "no frame this cycle");
  }
}

void P4CameraLight::dump_config() {
  LOG_SENSOR("", "P4 Camera Light", this);
  ESP_LOGCONFIG(TAG, "  device: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
}

}  // namespace p4_camera_light
}  // namespace esphome
#endif  // USE_ESP32
