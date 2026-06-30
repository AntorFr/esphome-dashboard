#pragma once
// Composant ESPHome — glue : possède le Controller (couche 1), le LvglRenderer
// (couche 2) et lit les entrées physiques (couche 3 : encodeur/bouton du Dial).
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/font/font.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "controller.h"
#include "launcher_module.h"
#include "lvgl_renderer.h"
#include "model.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include "esphome/components/http_request/http_request.h"
#include "http_music_library.h"
#endif

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
  void set_weather_entity(const char *entity) { this->weather_entity_ = entity; }
  void set_font_small(font::Font *f) { this->renderer_.set_font_small(f); }
  void set_font_medium(font::Font *f) { this->renderer_.set_font_medium(f); }
  void set_font_large(font::Font *f) { this->renderer_.set_font_large(f); }
  void set_font_weather(font::Font *f) { this->renderer_.set_font_weather(f); }
  void set_font_icons(font::Font *f) { this->renderer_.set_font_icons(f); }

  // Appelés par le codegen (to_code) pour peupler le modèle.
  void add_group(const std::string &name, const std::string &icon);
  void add_card(int group_index, int type, const std::string &entity, const std::string &name, uint32_t color,
                bool has_color);
  // Card switch liée à un esphome switch (binding HA réel).
  void add_switch_card(int group_index, switch_::Switch *sw, const std::string &name, uint32_t color,
                       bool has_color);
  void add_cover_card(int group_index, cover::Cover *cover, const std::string &name, uint32_t color, bool has_color);
  void add_climate_card(int group_index, climate::Climate *climate, const std::string &name, uint32_t color,
                        bool has_color);
  void add_media_card(int group_index, homeassistant_addon::HomeassistantMediaPlayer *media, const std::string &name,
                      uint32_t color, bool has_color);
#ifdef USE_HA_DASHBOARD_LAUNCHER
  // Music Library launcher group (D1001 tab): owns an HTTP backend + a LauncherModule, wired
  // to the speaker (queue_id) and profile (owner). See ADR-0007.
  void add_launcher_group(const std::string &name, const std::string &icon,
                          http_request::HttpRequestComponent *http, const std::string &base_url,
                          const std::string &owner, const std::string &queue_id);
#endif

 protected:
  void build_if_ready_();
  void poll_encoder_();
  void poll_button_();
  void update_clock_();
  void subscribe_weather_();
  void push_weather_();

  std::vector<Group> groups_;
  Controller controller_;
  LvglRenderer renderer_;

  // Launcher modules (layer 1) for music_library groups; pointers stored in Group::launcher.
  std::vector<std::unique_ptr<LauncherModule>> launchers_;
#ifdef USE_HA_DASHBOARD_LAUNCHER
  std::vector<std::unique_ptr<HttpMusicLibrary>> ml_backends_;
#endif

  sensor::Sensor *encoder_{nullptr};
  binary_sensor::BinarySensor *button_{nullptr};
  time::RealTimeClock *time_{nullptr};
  uint32_t last_clock_ms_{0};
  const char *weather_entity_{nullptr};
  std::string weather_temp_;
  std::string weather_cond_;
  std::string weather_icon_;
  bool weather_subscribed_{false};

  std::string profile_{"dial"};
  std::string language_{"en"};
  uint32_t timeout_ms_{30000};
  bool built_{false};

  float last_encoder_{NAN};
  bool button_down_{false};
  bool button_hold_triggered_{false};  // hold already produced a BACK -> ignore the release
  uint32_t button_down_ms_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
