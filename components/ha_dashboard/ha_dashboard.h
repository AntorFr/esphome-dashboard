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
#include "esphome/core/automation.h"
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

  // Fires on a discrete tap of a button/tile (not on swipes, slider drags or system events),
  // so YAML can add audible click feedback. See handle_event_.
  Trigger<> *get_tap_trigger() { return &this->tap_trigger_; }

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
  void set_font_icons_lg(font::Font *f) { this->renderer_.set_font_icons_lg(f); }
  void set_font_voice(font::Font *f) { this->renderer_.set_font_voice(f); }

  // --- Voice assistant (called from voice_assistant / micro_wake_word YAML automations) ---
  void voice_listening();
  void voice_thinking();
  void voice_responding();
  void voice_error();
  void voice_end();               // pipeline ended -> hide the overlay (unless a timer is ringing)
  void voice_level(float level);  // 0..1 mic level during listening (waveform amplitude)
  void voice_set_muted(bool muted);
  void voice_set_available(bool available);
  // HA Assist voice timers.
  void timer_started(const std::string &id, const std::string &name, uint32_t seconds);
  void timer_updated(const std::string &id, uint32_t seconds_left, bool is_active);
  void timer_cancelled(const std::string &id);
  void timer_finished(const std::string &id);

  // Appelés par le codegen (to_code) pour peupler le modèle.
  void add_group(const std::string &name, const std::string &icon);
  void add_card(int group_index, int type, const std::string &entity, const std::string &name, uint32_t color,
                bool has_color);
  // Card switch liée à un esphome switch (binding HA réel).
  void add_switch_card(int group_index, switch_::Switch *sw, const std::string &name, uint32_t color,
                       bool has_color);
  void add_cover_card(int group_index, cover::Cover *cover, const std::string &name, uint32_t color, bool has_color,
                      const std::string &cover_kind);
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
  // Friendly speaker name (launch toast) for the most recently added launcher group.
  void add_launcher_player_name(const std::string &name);
  // Register a grid cover slot (online_image) for the most recently added launcher group.
  void add_launcher_cover_slot(online_image::OnlineImage *slot);
  // Register a detail thumbnail slot (header / episode thumbnails) for that launcher group.
  void add_launcher_thumb_slot(online_image::OnlineImage *slot);
  // Cover/thumbnail encoding requested from music-library ("jpg" default | "bmp").
  void set_launcher_image_format(const std::string &fmt);
#endif

 protected:
  void build_if_ready_();
  void handle_event_(InputEvent e, int idx);  // route voice/timer actions here, nav to controller
  Trigger<> tap_trigger_;                      // fired for click feedback on discrete taps
  void refresh_mic_chip_();                    // reconcile the header mic chip with muted/available
  void push_timers_();                         // recompute + push timers to the renderer
  void tick_timers_(uint32_t now_ms);          // 1 s countdown for the header pill
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
  std::string last_clock_str_;   // last "HH:MM" pushed (skip redundant clock-label updates)
  int last_idle_hour_{-1};       // last hour pushed to the idle tint
  const char *weather_entity_{nullptr};
  std::string weather_temp_;
  std::string weather_cond_;
  std::string weather_icon_;
  bool weather_subscribed_{false};

  std::string profile_{"dial"};
  std::string language_{"en"};
  uint32_t timeout_ms_{30000};
  bool built_{false};

  // Voice assistant state.
  bool voice_muted_{false};
  bool voice_available_{true};
  std::vector<TimerInfo> timers_;
  std::string ringing_timer_id_;
  uint32_t last_timer_tick_ms_{0};

  float last_encoder_{NAN};
  bool button_down_{false};
  bool button_hold_triggered_{false};  // hold already produced a BACK -> ignore the release
  uint32_t button_down_ms_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
