#pragma once
// Composant ESPHome — glue : possède le Controller (couche 1), le LvglRenderer
// (couche 2) et lit les entrées physiques (couche 3 : encodeur/bouton du Dial).
#include <cmath>
#include <string>
#include <vector>
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "controller.h"
#include "lvgl_renderer.h"
#include "model.h"

namespace esphome {
namespace ha_dashboard {

class HaDashboard : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_profile(const std::string &profile) { this->profile_ = profile; }
  void set_language(const std::string &language) { this->language_ = language; }
  void set_inactivity_timeout(uint32_t ms) { this->timeout_ms_ = ms; }
  void set_encoder(sensor::Sensor *s) { this->encoder_ = s; }
  void set_button(binary_sensor::BinarySensor *b) { this->button_ = b; }
  void set_time(time::RealTimeClock *t) { this->time_ = t; }

  // Appelés par le codegen (to_code) pour peupler le modèle.
  void add_group(const std::string &name, const std::string &icon);
  void add_card(int group_index, int type, const std::string &entity, const std::string &name, uint32_t color,
                bool has_color);
  // Card switch liée à un esphome switch (binding HA réel).
  void add_switch_card(int group_index, switch_::Switch *sw, const std::string &name, uint32_t color,
                       bool has_color);

 protected:
  void build_if_ready_();
  void poll_encoder_();
  void poll_button_();
  void update_clock_();

  std::vector<Group> groups_;
  Controller controller_;
  LvglRenderer renderer_;

  sensor::Sensor *encoder_{nullptr};
  binary_sensor::BinarySensor *button_{nullptr};
  time::RealTimeClock *time_{nullptr};
  uint32_t last_clock_ms_{0};

  std::string profile_{"dial"};
  std::string language_{"en"};
  uint32_t timeout_ms_{30000};
  bool built_{false};

  float last_encoder_{NAN};
  bool button_down_{false};
  uint32_t button_down_ms_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
