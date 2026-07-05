#include "homeassistant_light.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"

namespace esphome {
namespace homeassistant_addon {

static const char *const TAG = "homeassistant_addon.light";

void HomeassistantLight::setup() {
  // Main state: on / off (also unavailable / unknown).
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, nullopt,
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s' state: %s", this->entity_id_, state_str.c_str());
        if (state_str == "unavailable" || state_str == "unknown")
          return;
        bool new_on = (state_str == "on");
        if (new_on != this->on_) {
          this->on_ = new_on;
          this->state_callback_.call();
        }
      });

  // Brightness attribute: HA reports 0..255 (absent for on/off-only lights).
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("brightness"),
      [this](StringRef state) {
        std::string state_str = state.str();
        if (state_str.empty() || state_str == "None" || state_str == "unknown" ||
            state_str == "unavailable") {
          return;
        }
        auto val = parse_number<float>(state_str);
        if (val.has_value()) {
          this->supports_brightness_ = true;
          float new_b = val.value() / 255.0f;
          if (std::abs(new_b - this->brightness_) > 0.002f) {
            this->brightness_ = new_b;
            ESP_LOGD(TAG, "'%s' brightness: %.2f", this->entity_id_, new_b);
            this->state_callback_.call();
          }
        }
      });
}

void HomeassistantLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Home Assistant Light:");
  ESP_LOGCONFIG(TAG, "  Entity ID: %s", this->entity_id_);
}

void HomeassistantLight::send_command_(const std::string &service) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");

  api::HomeassistantActionRequest req;
  std::string full_service = "light." + service;
  std::string entity_id_str = this->entity_id_;

  req.service = StringRef(full_service);
  req.data.init(1);
  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(entity_id_str);

  ESP_LOGD(TAG, "Calling %s on %s", full_service.c_str(), this->entity_id_);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantLight::send_brightness_(int brightness_pct) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto BRIGHTNESS_KEY = StringRef::from_lit("brightness_pct");

  api::HomeassistantActionRequest req;
  std::string full_service = "light.turn_on";
  std::string entity_id_str = this->entity_id_;
  std::string pct_str = to_string(brightness_pct);

  req.service = StringRef(full_service);
  req.data.init(2);

  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(entity_id_str);

  auto &pct_kv = req.data.emplace_back();
  pct_kv.key = BRIGHTNESS_KEY;
  pct_kv.value = StringRef(pct_str);

  ESP_LOGD(TAG, "Calling light.turn_on on %s with brightness_pct=%d", this->entity_id_, brightness_pct);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantLight::toggle() { this->send_command_("toggle"); }

void HomeassistantLight::turn_on() { this->send_command_("turn_on"); }

void HomeassistantLight::turn_off() { this->send_command_("turn_off"); }

void HomeassistantLight::set_brightness(float brightness) {
  if (brightness <= 0.0f) {
    this->send_command_("turn_off");
    return;
  }
  if (brightness > 1.0f)
    brightness = 1.0f;
  this->send_brightness_((int) lroundf(brightness * 100.0f));
}

}  // namespace homeassistant_addon
}  // namespace esphome
