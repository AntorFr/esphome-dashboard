/**
 * @file homeassistant_climate.h
 * @brief Home Assistant Climate component for ESPHome
 * 
 * This component allows ESPHome to import and control climate entities
 * from Home Assistant (thermostats, HVAC, etc.)
 */
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace homeassistant_addon {

/**
 * @brief Climate component that mirrors a Home Assistant climate entity
 * 
 * Features:
 * - Subscribes to HA climate state changes
 * - Allows setting target temperature
 * - Allows changing HVAC mode
 * - Reports current temperature and action
 */
class HomeassistantClimate : public climate::Climate, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
  
  // Configuration
  void set_entity_id(const char *entity_id) { this->entity_id_ = entity_id; }
  void set_temperature_step(float step) { this->temperature_step_ = step; }
  void set_min_temperature(float min_temp) { this->min_temperature_ = min_temp; }
  void set_max_temperature(float max_temp) { this->max_temperature_ = max_temp; }
  
  // Climate traits
  climate::ClimateTraits traits() override;
  
 protected:
  // Called when user changes settings via ESPHome
  void control(const climate::ClimateCall &call) override;
  
  // Send commands to Home Assistant
  void send_set_temperature(float temperature);
  void send_set_hvac_mode(climate::ClimateMode mode);
  
  // Parse Home Assistant states
  void parse_current_temperature(const std::string &state);
  void parse_target_temperature(const std::string &state);
  void parse_hvac_mode(const std::string &state);
  void parse_hvac_action(const std::string &state);
  
  // Convert between ESPHome and HA modes
  static climate::ClimateMode ha_mode_to_esphome(const std::string &mode);
  static const char* esphome_mode_to_ha(climate::ClimateMode mode);
  static climate::ClimateAction ha_action_to_esphome(const std::string &action);
  
  const char *entity_id_{nullptr};
  float temperature_step_{0.5f};
  float min_temperature_{7.0f};
  float max_temperature_{35.0f};
  
  // Track if we've received initial state
  bool received_state_{false};
};

}  // namespace homeassistant_addon
}  // namespace esphome
