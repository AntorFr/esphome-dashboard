#include "gsl3670_touchscreen.h"
#include "gsl3670_firmware.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

namespace esphome {
namespace gsl3670 {

static const char *const TAG = "gsl3670";

static const uint8_t GSL3670_READ_XY_REG = 0x80;  // touch points
static const uint8_t GSL3670_STATUS_REG = 0xb0;   // must read 0x5a x4 after init

void GSL3670Touchscreen::write_byte_reg_(uint8_t reg, uint8_t value) {
  this->write_register(reg, &value, 1);
}

void GSL3670Touchscreen::hw_reset_() {
  if (this->reset_pin_ == nullptr)
    return;
  this->reset_pin_->setup();
  this->reset_pin_->digital_write(false);
  delay(20);
  this->reset_pin_->digital_write(true);
  delay(20);
}

void GSL3670Touchscreen::clear_reg_() {
  this->hw_reset_();
  this->write_byte_reg_(0x88, 0x01);
  delay(5);
  this->write_byte_reg_(0xe4, 0x04);
  delay(5);
  this->write_byte_reg_(0xe0, 0x00);
  delay(20);
}

void GSL3670Touchscreen::load_firmware_() {
  // ~4.3k I2C writes: feed the task watchdog periodically to avoid a reset.
  for (uint32_t i = 0; i < GSL3670_FW_LEN; i++) {
    const GslFwEntry &e = GSL3670_FW[i];
    uint8_t buf[4] = {
        static_cast<uint8_t>(e.val & 0xff),
        static_cast<uint8_t>((e.val >> 8) & 0xff),
        static_cast<uint8_t>((e.val >> 16) & 0xff),
        static_cast<uint8_t>((e.val >> 24) & 0xff),
    };
    // Register 0xf0 (page select) takes a single byte; all others take 4 bytes.
    this->write_register(e.offset, buf, e.offset == 0xf0 ? 1 : 4);
    if ((i & 0x3f) == 0)
      App.feed_wdt();
  }
}

void GSL3670Touchscreen::startup_chip_() {
  this->write_byte_reg_(0xe0, 0x00);  // start (SiLead gsl_DataInit not ported)
  delay(10);
}

bool GSL3670Touchscreen::chip_init_() {
  // Same sequence as the Seeed driver: clear -> reset -> fw -> startup -> reset -> startup.
  this->clear_reg_();
  // reset + preparation
  this->hw_reset_();
  this->write_byte_reg_(0xe4, 0x04);
  delay(10);
  {
    uint8_t zero[4] = {0, 0, 0, 0};
    this->write_register(0xbc, zero, 4);
    delay(10);
  }
  this->load_firmware_();
  this->startup_chip_();
  this->hw_reset_();
  this->startup_chip_();

  // Verification: register 0xb0 must return 0x5a 0x5a 0x5a 0x5a.
  uint8_t status[4] = {0};
  if (this->read_register(GSL3670_STATUS_REG, status, 4) != i2c::ERROR_OK)
    return false;
  ESP_LOGD(TAG, "status 0xb0 = %02x %02x %02x %02x", status[0], status[1], status[2], status[3]);
  return status[0] == 0x5a && status[1] == 0x5a && status[2] == 0x5a && status[3] == 0x5a;
}

void GSL3670Touchscreen::setup() {
  ESP_LOGCONFIG(TAG, "Setting up GSL3670 touchscreen (%u firmware entries)...", (unsigned) GSL3670_FW_LEN);
  // Read via polling (update_interval); the INT pin is kept for future use.
  if (this->interrupt_pin_ != nullptr)
    this->interrupt_pin_->setup();
  this->ready_ = this->chip_init_();
  if (!this->ready_) {
    ESP_LOGW(TAG, "GSL3670 init not confirmed (0xb0 != 0x5a). Reading anyway.");
  } else {
    ESP_LOGCONFIG(TAG, "GSL3670 ready.");
  }
}

void GSL3670Touchscreen::update_touches() {
  uint8_t data[44];
  if (this->read_register(GSL3670_READ_XY_REG, data, sizeof(data)) != i2c::ERROR_OK) {
    this->skip_update_ = true;  // do not report "no touch" on an I2C error
    return;
  }
  uint8_t fingers = data[0];
  if (fingers > 10)  // bogus value -> drop frame
    return;
  for (uint8_t i = 0; i < fingers && i < 5; i++) {
    const uint8_t *p = &data[(i + 1) * 4];
    // X and Y are 12-bit; the upper nibble of the high bytes carries flags -> mask it.
    uint16_t x = ((p[3] & 0x0f) << 8) | p[2];
    uint16_t y = ((p[1] & 0x0f) << 8) | p[0];
    uint8_t id = (p[3] & 0xf0) >> 4;
    ESP_LOGV(TAG, "touch[%u] raw=(%u,%u) id=%u", i, x, y, id);
    this->add_raw_touch_position_(id, x, y);
  }
}

void GSL3670Touchscreen::dump_config() {
  ESP_LOGCONFIG(TAG, "GSL3670 Touchscreen:");
  LOG_I2C_DEVICE(this);
  LOG_PIN("  Interrupt Pin: ", this->interrupt_pin_);
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  ESP_LOGCONFIG(TAG, "  Firmware entries: %u", (unsigned) GSL3670_FW_LEN);
  ESP_LOGCONFIG(TAG, "  Init confirmed: %s", YESNO(this->ready_));
}

}  // namespace gsl3670
}  // namespace esphome
