#pragma once
// GSL3670 (SiLead) touchscreen — native ESPHome driver (direct I2C).
// Protocol ported from Seeed-Studio/reTerminal-D1001 (esp_lcd_touch_gsl3670.c):
// reset -> upload firmware -> startup -> read 44 bytes @0x80.
// The SiLead filtering algorithm (gsl_alg_id_main) is NOT ported: we report raw points,
// which is enough for navigation (tap/slide). See docs/lessons-learned.md §7.
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/touchscreen/touchscreen.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gsl3670 {

class GSL3670Touchscreen : public touchscreen::Touchscreen, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_interrupt_pin(GPIOPin *pin) { this->interrupt_pin_ = pin; }
  void set_reset_pin(GPIOPin *pin) { this->reset_pin_ = pin; }

 protected:
  void update_touches() override;

  void hw_reset_();
  void write_byte_reg_(uint8_t reg, uint8_t value);
  void clear_reg_();
  void load_firmware_();
  void startup_chip_();
  bool chip_init_();

  GPIOPin *interrupt_pin_{nullptr};
  GPIOPin *reset_pin_{nullptr};
  bool ready_{false};
};

}  // namespace gsl3670
}  // namespace esphome
