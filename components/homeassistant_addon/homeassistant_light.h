#pragma once

#include "esphome/core/component.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/api/custom_api_device.h"
#include <functional>
#include <string>

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

  // Control methods (issue HA actions)
  void toggle();
  void turn_on();
  void turn_off();
  void set_brightness(float brightness);  // 0..1 ; <=0 turns the light off

  // Callback for state changes (dashboard re-render).
  void add_on_state_callback(std::function<void()> &&callback) {
    this->state_callback_.add(std::move(callback));
  }

 protected:
  void send_command_(const std::string &service);
  void send_brightness_(int brightness_pct);

  const char *entity_id_{nullptr};

  // State mirrored from HA.
  bool on_{false};
  bool supports_brightness_{false};
  float brightness_{0.0f};  // 0..1

  CallbackManager<void()> state_callback_;
};

}  // namespace homeassistant_addon
}  // namespace esphome
