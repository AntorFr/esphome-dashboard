#pragma once

#include "esphome/core/component.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/api/custom_api_device.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace homeassistant_addon {

// Device-side proxy for a Home Assistant `light.*` entity. Mirrors HA on/off + brightness
// state (subscribe_home_assistant_state) and commands back via light.* actions. Modeled on
// HomeassistantMediaPlayer: a plain Component the dashboard binds to directly (not a native
// light::LightState, which would echo on write_state).
class HomeassistantLight : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_entity_id(const char *entity_id) { this->entity_id_ = entity_id; }

  // Getters
  const char *get_entity_id() const { return this->entity_id_; }
  bool is_on() const { return this->on_; }
  bool supports_brightness() const { return this->supports_brightness_; }
  float get_brightness() const { return this->brightness_; }  // 0..1

  // Colour / effect capabilities (from HA's supported_color_modes / effect_list attributes).
  bool supports_color() const { return this->supports_color_; }        // rgb/rgbw/rgbww/hs/xy
  bool supports_color_temp() const { return this->supports_color_temp_; }
  int get_min_kelvin() const { return this->min_kelvin_; }
  int get_max_kelvin() const { return this->max_kelvin_; }
  int get_color_temp_kelvin() const { return this->color_temp_kelvin_; }  // 0 = unknown
  const std::string &get_effect() const { return this->effect_; }        // current effect ("" = none)
  const std::vector<std::string> &get_effect_list() const { return this->effect_list_; }

  // Control methods (issue HA actions)
  void toggle();
  void turn_on();
  void turn_off();
  void set_brightness(float brightness);  // 0..1 ; <=0 turns the light off
  void set_rgb(uint8_t r, uint8_t g, uint8_t b);   // light.turn_on rgb_color
  void set_color_temp_kelvin(int kelvin);          // light.turn_on color_temp_kelvin
  void set_effect(const std::string &effect);      // light.turn_on effect

  // Callback for state changes (dashboard re-render).
  void add_on_state_callback(std::function<void()> &&callback) {
    this->state_callback_.add(std::move(callback));
  }

 protected:
  void send_command_(const std::string &service);
  void send_brightness_(int brightness_pct);
  void parse_supported_color_modes_(const std::string &modes);
  void parse_effect_list_(const std::string &list);

  const char *entity_id_{nullptr};

  // State mirrored from HA.
  bool on_{false};
  bool supports_brightness_{false};
  float brightness_{0.0f};  // 0..1

  // Colour / effect capabilities + state mirrored from HA.
  bool supports_color_{false};
  bool supports_color_temp_{false};
  int min_kelvin_{2000};
  int max_kelvin_{6535};
  int color_temp_kelvin_{0};  // 0 = unknown
  std::string effect_;
  std::vector<std::string> effect_list_;

  CallbackManager<void()> state_callback_;
};

}  // namespace homeassistant_addon
}  // namespace esphome
