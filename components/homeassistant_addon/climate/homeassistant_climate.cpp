/**
 * @file homeassistant_climate.cpp
 * @brief Implementation of Home Assistant Climate component
 */

#include "homeassistant_climate.h"
#include "esphome/components/api/api_server.h"
#include "esphome/core/log.h"

namespace esphome {
namespace homeassistant_addon {

static const char *const TAG = "homeassistant_addon.climate";

void HomeassistantClimate::setup() {
  ESP_LOGI(TAG, "Setting up Home Assistant Climate '%s'...", this->entity_id_);
  
  // Subscribe to the main state (hvac_mode)
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, optional<std::string>(), 
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s': Got state: %s", this->entity_id_, state_str.c_str());
        this->parse_hvac_mode(state_str);
        this->received_state_ = true;
        this->publish_state();
      });
  
  // Subscribe to current_temperature attribute
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("current_temperature"),
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s': Got current_temperature: %s", this->entity_id_, state_str.c_str());
        this->parse_current_temperature(state_str);
        this->publish_state();
      });
  
  // Subscribe to temperature (target) attribute
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("temperature"),
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s': Got target temperature: %s", this->entity_id_, state_str.c_str());
        this->parse_target_temperature(state_str);
        this->publish_state();
      });
  
  // Subscribe to hvac_action attribute (heating, cooling, idle, off)
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("hvac_action"),
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s': Got hvac_action: %s", this->entity_id_, state_str.c_str());
        this->parse_hvac_action(state_str);
        this->publish_state();
      });

  // Subscribe to the supported hvac_modes list so the dashboard only offers real modes.
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("hvac_modes"),
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s': Got hvac_modes: %s", this->entity_id_, state_str.c_str());
        this->parse_hvac_modes(state_str);
        this->publish_state();
      });
}

// HA reports hvac_modes as a list (e.g. "['off', 'heat', 'auto']"). Match exact tokens so
// "heat_cool" is not mistaken for heat + cool.
void HomeassistantClimate::parse_hvac_modes(const std::string &modes) {
  if (modes.empty() || modes == "unknown" || modes == "unavailable")
    return;
  std::set<climate::ClimateMode> found;
  size_t i = 0;
  while (i < modes.size()) {
    char c = modes[i];
    if ((c >= 'a' && c <= 'z') || c == '_') {
      size_t j = i;
      while (j < modes.size() && ((modes[j] >= 'a' && modes[j] <= 'z') || modes[j] == '_'))
        j++;
      std::string t = modes.substr(i, j - i);
      i = j;
      if (t == "off")
        found.insert(climate::CLIMATE_MODE_OFF);
      else if (t == "heat_cool")
        found.insert(climate::CLIMATE_MODE_HEAT_COOL);
      else if (t == "heat")
        found.insert(climate::CLIMATE_MODE_HEAT);
      else if (t == "cool")
        found.insert(climate::CLIMATE_MODE_COOL);
      else if (t == "auto")
        found.insert(climate::CLIMATE_MODE_AUTO);
      else if (t == "dry")
        found.insert(climate::CLIMATE_MODE_DRY);
      else if (t == "fan_only")
        found.insert(climate::CLIMATE_MODE_FAN_ONLY);
    } else {
      i++;
    }
  }
  if (!found.empty())
    this->supported_modes_ = found;
}

void HomeassistantClimate::dump_config() {
  ESP_LOGCONFIG(TAG, "Home Assistant Climate:");
  ESP_LOGCONFIG(TAG, "  Entity ID: '%s'", this->entity_id_);
  ESP_LOGCONFIG(TAG, "  Temperature Step: %.1f", this->temperature_step_);
  ESP_LOGCONFIG(TAG, "  Min Temperature: %.1f", this->min_temperature_);
  ESP_LOGCONFIG(TAG, "  Max Temperature: %.1f", this->max_temperature_);
}

float HomeassistantClimate::get_setup_priority() const {
  return setup_priority::AFTER_CONNECTION;
}

climate::ClimateTraits HomeassistantClimate::traits() {
  auto traits = climate::ClimateTraits();

  // Supported modes: the entity's real hvac_modes once HA has reported them, else a permissive
  // fallback. OFF is always included (ESPHome requires it).
  if (!this->supported_modes_.empty()) {
    traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
    for (auto mode : this->supported_modes_)
      traits.add_supported_mode(mode);
  } else {
    traits.set_supported_modes({
        climate::CLIMATE_MODE_OFF,
        climate::CLIMATE_MODE_HEAT,
        climate::CLIMATE_MODE_COOL,
        climate::CLIMATE_MODE_HEAT_COOL,
        climate::CLIMATE_MODE_AUTO,
    });
  }
  
  // Temperature settings
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(this->min_temperature_);
  traits.set_visual_max_temperature(this->max_temperature_);
  traits.set_visual_temperature_step(this->temperature_step_);
  
  return traits;
}

void HomeassistantClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    climate::ClimateMode mode = *call.get_mode();
    ESP_LOGI(TAG, "Setting HVAC mode to: %s", climate::climate_mode_to_string(mode));
    this->send_set_hvac_mode(mode);
    this->mode = mode;
  }
  
  if (call.get_target_temperature().has_value()) {
    float temp = *call.get_target_temperature();
    ESP_LOGI(TAG, "Setting target temperature to: %.1f", temp);
    this->send_set_temperature(temp);
    this->target_temperature = temp;
  }
  
  this->publish_state();
}

void HomeassistantClimate::send_set_temperature(float temperature) {
  if (!api::global_api_server->is_connected()) {
    ESP_LOGE(TAG, "No clients connected to API server");
    return;
  }
  
  static constexpr auto SERVICE_NAME = StringRef::from_lit("climate.set_temperature");
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto TEMP_KEY = StringRef::from_lit("temperature");
  
  api::HomeassistantActionRequest req;
  req.service = SERVICE_NAME;
  
  // Stack buffer for temperature - stays valid until send_homeassistant_action returns
  char temp_buf[16];
  snprintf(temp_buf, sizeof(temp_buf), "%.1f", temperature);
  
  req.data.init(2);
  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(this->entity_id_);  // entity_id_ is a class member, stays valid
  
  auto &temp_kv = req.data.emplace_back();
  temp_kv.key = TEMP_KEY;
  temp_kv.value = StringRef(temp_buf);
  
  ESP_LOGD(TAG, "Sending set_temperature: %s", temp_buf);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantClimate::send_set_hvac_mode(climate::ClimateMode mode) {
  if (!api::global_api_server->is_connected()) {
    ESP_LOGE(TAG, "No clients connected to API server");
    return;
  }
  
  static constexpr auto SERVICE_NAME = StringRef::from_lit("climate.set_hvac_mode");
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto MODE_KEY = StringRef::from_lit("hvac_mode");
  
  // Get mode string - this returns a const char* that's static
  const char *mode_cstr = this->esphome_mode_to_ha(mode);
  
  api::HomeassistantActionRequest req;
  req.service = SERVICE_NAME;
  
  req.data.init(2);
  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(this->entity_id_);  // entity_id_ is a class member
  
  auto &mode_kv = req.data.emplace_back();
  mode_kv.key = MODE_KEY;
  mode_kv.value = StringRef(mode_cstr);
  
  ESP_LOGD(TAG, "Sending set_hvac_mode: %s", mode_cstr);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantClimate::parse_current_temperature(const std::string &state) {
  if (state.empty() || state == "unknown" || state == "unavailable") {
    return;
  }
  
  char *end;
  float value = strtof(state.c_str(), &end);
  if (end != state.c_str()) {
    this->current_temperature = value;
  }
}

void HomeassistantClimate::parse_target_temperature(const std::string &state) {
  if (state.empty() || state == "unknown" || state == "unavailable") {
    return;
  }
  
  char *end;
  float value = strtof(state.c_str(), &end);
  if (end != state.c_str()) {
    this->target_temperature = value;
  }
}

void HomeassistantClimate::parse_hvac_mode(const std::string &state) {
  if (state.empty() || state == "unknown" || state == "unavailable") {
    return;
  }
  
  this->mode = ha_mode_to_esphome(state);
}

void HomeassistantClimate::parse_hvac_action(const std::string &state) {
  if (state.empty() || state == "unknown" || state == "unavailable") {
    return;
  }
  
  this->action = ha_action_to_esphome(state);
}

climate::ClimateMode HomeassistantClimate::ha_mode_to_esphome(const std::string &mode) {
  if (mode == "off") {
    return climate::CLIMATE_MODE_OFF;
  } else if (mode == "heat") {
    return climate::CLIMATE_MODE_HEAT;
  } else if (mode == "cool") {
    return climate::CLIMATE_MODE_COOL;
  } else if (mode == "heat_cool" || mode == "auto") {
    return climate::CLIMATE_MODE_HEAT_COOL;
  } else if (mode == "dry") {
    return climate::CLIMATE_MODE_DRY;
  } else if (mode == "fan_only") {
    return climate::CLIMATE_MODE_FAN_ONLY;
  }
  
  ESP_LOGW(TAG, "Unknown HVAC mode: %s", mode.c_str());
  return climate::CLIMATE_MODE_OFF;
}

const char* HomeassistantClimate::esphome_mode_to_ha(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_OFF:
      return "off";
    case climate::CLIMATE_MODE_HEAT:
      return "heat";
    case climate::CLIMATE_MODE_COOL:
      return "cool";
    case climate::CLIMATE_MODE_HEAT_COOL:
      return "heat_cool";
    case climate::CLIMATE_MODE_AUTO:
      return "auto";
    case climate::CLIMATE_MODE_DRY:
      return "dry";
    case climate::CLIMATE_MODE_FAN_ONLY:
      return "fan_only";
    default:
      return "off";
  }
}

climate::ClimateAction HomeassistantClimate::ha_action_to_esphome(const std::string &action) {
  if (action == "off") {
    return climate::CLIMATE_ACTION_OFF;
  } else if (action == "heating") {
    return climate::CLIMATE_ACTION_HEATING;
  } else if (action == "cooling") {
    return climate::CLIMATE_ACTION_COOLING;
  } else if (action == "idle") {
    return climate::CLIMATE_ACTION_IDLE;
  } else if (action == "drying") {
    return climate::CLIMATE_ACTION_DRYING;
  } else if (action == "fan") {
    return climate::CLIMATE_ACTION_FAN;
  }
  
  ESP_LOGW(TAG, "Unknown HVAC action: %s", action.c_str());
  return climate::CLIMATE_ACTION_OFF;
}

}  // namespace homeassistant_addon
}  // namespace esphome
