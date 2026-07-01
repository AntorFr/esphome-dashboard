#pragma once

#include "esphome/core/component.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/api/api_server.h"

namespace esphome {
namespace homeassistant_addon {

class HomeassistantCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }
  
  void set_entity_id(const char *entity_id) { entity_id_ = entity_id; }
  const char *get_entity_id() const { return entity_id_; }
  
  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  
  void on_state_received(StringRef state);
  void on_position_received(StringRef position_str);
  
  const char *entity_id_{nullptr};
  
  // Traits detected from HA
  bool supports_position_{false};
  bool supports_tilt_{false};
  bool supports_stop_{true};
};

}  // namespace homeassistant_addon
}  // namespace esphome
