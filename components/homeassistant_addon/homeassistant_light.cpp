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

  // Supported colour modes: tells the dashboard whether to offer swatches / a warm-cool picker.
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("supported_color_modes"),
      [this](StringRef state) { this->parse_supported_color_modes_(state.str()); });

  // Colour-temperature bounds + current value (Kelvin). Absent for RGB-only lights.
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("min_color_temp_kelvin"),
      [this](StringRef state) {
        auto v = parse_number<int>(state.str());
        if (v.has_value())
          this->min_kelvin_ = v.value();
      });
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("max_color_temp_kelvin"),
      [this](StringRef state) {
        auto v = parse_number<int>(state.str());
        if (v.has_value())
          this->max_kelvin_ = v.value();
      });
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("color_temp_kelvin"),
      [this](StringRef state) {
        auto v = parse_number<int>(state.str());
        int nk = v.has_value() ? v.value() : 0;
        if (nk != this->color_temp_kelvin_) {
          this->color_temp_kelvin_ = nk;
          this->state_callback_.call();
        }
      });

  // Effects: the supported list + the active effect (for highlight).
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("effect_list"),
      [this](StringRef state) { this->parse_effect_list_(state.str()); });
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("effect"),
      [this](StringRef state) {
        std::string s = state.str();
        if (s == "None" || s == "unknown" || s == "unavailable")
          s.clear();
        if (s != this->effect_) {
          this->effect_ = s;
          this->state_callback_.call();
        }
      });
}

// HA reports supported_color_modes as a list, e.g. "['color_temp', 'xy']" or "['onoff']".
// rgb/rgbw/rgbww/hs/xy => full colour ; color_temp => warm-cool picker.
void HomeassistantLight::parse_supported_color_modes_(const std::string &modes) {
  bool color = modes.find("rgb") != std::string::npos || modes.find("'hs'") != std::string::npos ||
               modes.find("'xy'") != std::string::npos;
  bool ct = modes.find("color_temp") != std::string::npos;
  if (color != this->supports_color_ || ct != this->supports_color_temp_) {
    this->supports_color_ = color;
    this->supports_color_temp_ = ct;
    ESP_LOGD(TAG, "'%s' color=%d color_temp=%d", this->entity_id_, color, ct);
    this->state_callback_.call();
  }
}

// Parse a Python-repr list of single-quoted strings, e.g. "['Rainbow', 'Color Loop']".
void HomeassistantLight::parse_effect_list_(const std::string &list) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < list.size()) {
    if (list[i] == '\'') {
      size_t j = i + 1;
      while (j < list.size() && list[j] != '\'')
        j++;
      if (j >= list.size())
        break;
      std::string item = list.substr(i + 1, j - i - 1);
      if (!item.empty() && item != "None")
        out.push_back(item);
      i = j + 1;
    } else {
      i++;
    }
  }
  if (out != this->effect_list_) {
    this->effect_list_ = std::move(out);
    this->state_callback_.call();
  }
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

void HomeassistantLight::set_rgb(uint8_t r, uint8_t g, uint8_t b) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto RGB_KEY = StringRef::from_lit("rgb_color");

  // rgb_color is a list; HA's service schema rejects a stringified list passed in `data`, so it
  // goes through `data_template` — HA renders "[r, g, b]" and parse_result yields a real list.
  api::HomeassistantActionRequest req;
  std::string full_service = "light.turn_on";
  std::string entity_id_str = this->entity_id_;
  char rgb[24];
  std::snprintf(rgb, sizeof(rgb), "[%d, %d, %d]", (int) r, (int) g, (int) b);
  std::string rgb_str = rgb;

  req.service = StringRef(full_service);
  req.data.init(1);
  auto &entity_kv = req.data.emplace_back();
  entity_kv.key = ENTITY_ID_KEY;
  entity_kv.value = StringRef(entity_id_str);
  req.data_template.init(1);
  auto &rgb_kv = req.data_template.emplace_back();
  rgb_kv.key = RGB_KEY;
  rgb_kv.value = StringRef(rgb_str);

  ESP_LOGD(TAG, "Calling light.turn_on on %s with rgb_color=%s", this->entity_id_, rgb_str.c_str());
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantLight::set_color_temp_kelvin(int kelvin) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto CT_KEY = StringRef::from_lit("color_temp_kelvin");

  api::HomeassistantActionRequest req;
  std::string full_service = "light.turn_on";
  std::string entity_id_str = this->entity_id_;
  std::string kelvin_str = to_string(kelvin);

  req.service = StringRef(full_service);
  req.data.init(2);
  auto &entity_kv = req.data.emplace_back();
  entity_kv.key = ENTITY_ID_KEY;
  entity_kv.value = StringRef(entity_id_str);
  auto &ct_kv = req.data.emplace_back();
  ct_kv.key = CT_KEY;
  ct_kv.value = StringRef(kelvin_str);

  ESP_LOGD(TAG, "Calling light.turn_on on %s with color_temp_kelvin=%d", this->entity_id_, kelvin);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantLight::set_effect(const std::string &effect) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  static constexpr auto EFFECT_KEY = StringRef::from_lit("effect");

  api::HomeassistantActionRequest req;
  std::string full_service = "light.turn_on";
  std::string entity_id_str = this->entity_id_;
  std::string effect_str = effect;

  req.service = StringRef(full_service);
  req.data.init(2);
  auto &entity_kv = req.data.emplace_back();
  entity_kv.key = ENTITY_ID_KEY;
  entity_kv.value = StringRef(entity_id_str);
  auto &effect_kv = req.data.emplace_back();
  effect_kv.key = EFFECT_KEY;
  effect_kv.value = StringRef(effect_str);

  ESP_LOGD(TAG, "Calling light.turn_on on %s with effect=%s", this->entity_id_, effect_str.c_str());
  api::global_api_server->send_homeassistant_action(req);
}

}  // namespace homeassistant_addon
}  // namespace esphome
