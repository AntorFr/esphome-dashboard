#include "homeassistant_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace homeassistant_addon {

static const char *const TAG = "homeassistant_addon.cover";

void HomeassistantCover::setup() {
  ESP_LOGD(TAG, "Setting up HomeAssistant Cover '%s' for entity '%s'", 
           this->get_name().c_str(), this->entity_id_);
  
  // Subscribe to state changes
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, nullopt,
      [this](StringRef state) {
        this->on_state_received(state);
      });
  
  // Subscribe to position attribute
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("current_position"),
      [this](StringRef position) {
        this->on_position_received(position);
      });
  
  // Subscribe to tilt attribute (optional)
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("current_tilt_position"),
      [this](StringRef tilt) {
        std::string tilt_str = tilt.str();
        if (!tilt_str.empty() && tilt_str != "unavailable" && tilt_str != "unknown") {
          this->supports_tilt_ = true;
          auto val = parse_number<float>(tilt_str);
          if (val.has_value()) {
            this->tilt = val.value() / 100.0f;
            this->publish_state(false);
          }
        }
      });
}

void HomeassistantCover::on_state_received(StringRef state) {
  std::string state_str = state.str();
  ESP_LOGD(TAG, "'%s' received state: %s", this->entity_id_, state_str.c_str());
  
  if (state_str == "open") {
    this->position = cover::COVER_OPEN;
    this->current_operation = cover::COVER_OPERATION_IDLE;
  } else if (state_str == "closed") {
    this->position = cover::COVER_CLOSED;
    this->current_operation = cover::COVER_OPERATION_IDLE;
  } else if (state_str == "opening") {
    this->current_operation = cover::COVER_OPERATION_OPENING;
  } else if (state_str == "closing") {
    this->current_operation = cover::COVER_OPERATION_CLOSING;
  } else if (state_str == "unavailable" || state_str == "unknown") {
    ESP_LOGW(TAG, "'%s' state is %s", this->entity_id_, state_str.c_str());
    return;
  }
  
  this->publish_state(false);
}

void HomeassistantCover::on_position_received(StringRef position_str) {
  std::string pos_str = position_str.str();
  if (pos_str.empty() || pos_str == "unavailable" || pos_str == "unknown") {
    return;
  }
  
  this->supports_position_ = true;
  auto val = parse_number<float>(pos_str);
  if (val.has_value()) {
    // HA position: 0 = closed, 100 = open
    // ESPHome position: 0.0 = closed, 1.0 = open
    this->position = val.value() / 100.0f;
    ESP_LOGD(TAG, "'%s' received position: %.0f%% -> %.2f", 
             this->entity_id_, val.value(), this->position);
    this->publish_state(false);
  }
}

cover::CoverTraits HomeassistantCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_stop(this->supports_stop_);
  traits.set_supports_position(this->supports_position_);
  traits.set_supports_tilt(this->supports_tilt_);
  traits.set_is_assumed_state(false);
  return traits;
}

void HomeassistantCover::control(const cover::CoverCall &call) {
  // Service and keys as string literals for StringRef::from_lit
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto POSITION_KEY = StringRef::from_lit("position");
  static constexpr auto TILT_KEY = StringRef::from_lit("tilt_position");
  
  api::HomeassistantActionRequest req;
  std::string entity_id_str = this->entity_id_;
  std::string service_str;
  std::string position_str;
  
  if (call.get_stop()) {
    service_str = "cover.stop_cover";
    req.service = StringRef(service_str);
    req.data.init(1);
    auto &entity_id_kv = req.data.emplace_back();
    entity_id_kv.key = ENTITY_ID_KEY;
    entity_id_kv.value = StringRef(entity_id_str);
  } else if (call.get_position().has_value()) {
    float pos = call.get_position().value();
    
    if (pos == cover::COVER_OPEN) {
      service_str = "cover.open_cover";
      req.service = StringRef(service_str);
      req.data.init(1);
      auto &entity_id_kv = req.data.emplace_back();
      entity_id_kv.key = ENTITY_ID_KEY;
      entity_id_kv.value = StringRef(entity_id_str);
    } else if (pos == cover::COVER_CLOSED) {
      service_str = "cover.close_cover";
      req.service = StringRef(service_str);
      req.data.init(1);
      auto &entity_id_kv = req.data.emplace_back();
      entity_id_kv.key = ENTITY_ID_KEY;
      entity_id_kv.value = StringRef(entity_id_str);
    } else {
      // Set specific position
      service_str = "cover.set_cover_position";
      req.service = StringRef(service_str);
      req.data.init(2);
      
      auto &entity_id_kv = req.data.emplace_back();
      entity_id_kv.key = ENTITY_ID_KEY;
      entity_id_kv.value = StringRef(entity_id_str);
      
      position_str = to_string(static_cast<int>(pos * 100));
      auto &pos_kv = req.data.emplace_back();
      pos_kv.key = POSITION_KEY;
      pos_kv.value = StringRef(position_str);
    }
  } else if (call.get_tilt().has_value()) {
    float tilt = call.get_tilt().value();
    service_str = "cover.set_cover_tilt_position";
    req.service = StringRef(service_str);
    req.data.init(2);
    
    auto &entity_id_kv = req.data.emplace_back();
    entity_id_kv.key = ENTITY_ID_KEY;
    entity_id_kv.value = StringRef(entity_id_str);
    
    position_str = to_string(static_cast<int>(tilt * 100));
    auto &tilt_kv = req.data.emplace_back();
    tilt_kv.key = TILT_KEY;
    tilt_kv.value = StringRef(position_str);
  } else {
    ESP_LOGW(TAG, "Unknown cover control command");
    return;
  }
  
  ESP_LOGD(TAG, "Calling service: %s", service_str.c_str());
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantCover::dump_config() {
  ESP_LOGCONFIG(TAG, "HomeAssistant Cover '%s':", this->get_name().c_str());
  ESP_LOGCONFIG(TAG, "  Entity ID: %s", this->entity_id_);
}

}  // namespace homeassistant_addon
}  // namespace esphome
