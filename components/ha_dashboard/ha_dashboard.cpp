#include "ha_dashboard.h"
#include <cstdio>
#include <cstdlib>
#include <lvgl.h>
#include "esphome/components/api/api_server.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_VOICE_ASSISTANT
#include "esphome/components/voice_assistant/voice_assistant.h"
#endif

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard";
static constexpr uint32_t HOLD_MS = 1100;    // button hold to fill the return gauge (cf. prototype)
static constexpr uint32_t TAP_MAX_MS = 350;  // shorter press = quick click (SELECT)

void HaDashboard::add_group(const std::string &name, const std::string &icon) {
  Group g;
  g.name = name;
  g.icon = icon;
  this->groups_.push_back(g);
}

void HaDashboard::add_card(int group_index, int type, const std::string &entity, const std::string &name,
                           uint32_t color, bool has_color) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card c;
  c.type = static_cast<CardType>(type);
  c.entity = entity;
  c.name = name.empty() ? entity : name;
  c.color = color;
  c.has_color = has_color;
  this->groups_[group_index].cards.push_back(c);
}

void HaDashboard::add_switch_card(int group_index, switch_::Switch *sw, const std::string &name, uint32_t color,
                                  bool has_color) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card c;
  c.type = CardType::SWITCH;
  c.sw = sw;
  if (!name.empty()) {
    c.name = name;
  } else if (sw != nullptr) {
    c.name = sw->get_name().c_str();
  } else {
    c.name = "Switch";
  }
  c.color = color;
  c.has_color = has_color;
  this->groups_[group_index].cards.push_back(c);
}

static Card &push_card_(std::vector<Group> &groups, int gi, CardType type, const std::string &name, uint32_t color,
                        bool has_color) {
  Card c;
  c.type = type;
  c.name = name;
  c.color = color;
  c.has_color = has_color;
  groups[gi].cards.push_back(c);
  return groups[gi].cards.back();
}

void HaDashboard::add_cover_card(int group_index, cover::Cover *cover, const std::string &name, uint32_t color,
                                 bool has_color, const std::string &cover_kind) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card &c = push_card_(this->groups_, group_index, CardType::COVER,
                       !name.empty() ? name : std::string("Cover"), color, has_color);
  c.cover = cover;
  c.cover_kind = cover_kind;
}

void HaDashboard::add_climate_card(int group_index, climate::Climate *climate, const std::string &name, uint32_t color,
                                   bool has_color) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card &c = push_card_(this->groups_, group_index, CardType::CLIMATE,
                       !name.empty() ? name : std::string("Climate"), color, has_color);
  c.climate = climate;
}

void HaDashboard::add_media_card(int group_index, homeassistant_addon::HomeassistantMediaPlayer *media,
                                 const std::string &name, uint32_t color, bool has_color) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card &c = push_card_(this->groups_, group_index, CardType::MEDIA_PLAYER,
                       !name.empty() ? name : std::string("Media"), color, has_color);
  c.media = media;
}

#ifdef USE_HA_DASHBOARD_LAUNCHER
void HaDashboard::add_launcher_group(const std::string &name, const std::string &icon,
                                     http_request::HttpRequestComponent *http, const std::string &base_url,
                                     const std::string &owner, const std::string &queue_id) {
  auto backend = std::make_unique<HttpMusicLibrary>();
  backend->set_http(http);
  backend->set_base_url(base_url);
  backend->set_queue_id(queue_id);

  auto launcher = std::make_unique<LauncherModule>();
  launcher->set_owner(owner);
  launcher->set_backend(backend.get());
  launcher->set_on_changed([this]() { this->controller_.refresh(); });

  Group g;
  g.name = name;
  g.icon = icon;
  g.is_launcher = true;
  g.launcher = launcher.get();
  this->groups_.push_back(g);

  this->ml_backends_.push_back(std::move(backend));
  this->launchers_.push_back(std::move(launcher));
}

void HaDashboard::add_launcher_player_name(const std::string &name) {
  if (!this->groups_.empty() && this->groups_.back().is_launcher)
    this->groups_.back().player_name = name;
}

void HaDashboard::add_launcher_cover_slot(online_image::OnlineImage *slot) {
  // Attach to the launcher group just added (codegen calls this right after add_launcher_group).
  if (!this->groups_.empty() && this->groups_.back().is_launcher)
    this->groups_.back().cover_slots.push_back(slot);
}

void HaDashboard::add_launcher_thumb_slot(online_image::OnlineImage *slot) {
  if (!this->groups_.empty() && this->groups_.back().is_launcher)
    this->groups_.back().thumb_slots.push_back(slot);
}

void HaDashboard::set_launcher_image_format(const std::string &fmt) {
  if (!this->ml_backends_.empty())
    this->ml_backends_.back()->set_image_format(fmt);
}
#endif

void HaDashboard::setup() {
  this->renderer_.set_profile(this->profile_);
  this->controller_.set_inactivity_timeout(this->timeout_ms_);
  ESP_LOGCONFIG(TAG, "ha_dashboard setup (profile=%s, %d groupes)", this->profile_.c_str(),
                (int) this->groups_.size());
  this->subscribe_weather_();
  // La construction LVGL est différée jusqu'à ce que LVGL soit initialisé (cf. loop).
}

void HaDashboard::build_if_ready_() {
  if (this->built_)
    return;
  if (lv_display_get_default() == nullptr)
    return;  // LVGL pas encore prêt

  this->renderer_.build(this->groups_);
  this->renderer_.set_event_handler([this](InputEvent e, int idx) { this->handle_event_(e, idx); });
  this->controller_.set_renderer(&this->renderer_);
  this->controller_.set_model(&this->groups_);
  this->controller_.set_dashboard_mode(this->profile_ == "reterminal_d1001");

  // Re-render quand l'état HA d'une card change (binding live).
  for (auto &g : this->groups_) {
    for (auto &c : g.cards) {
      if (c.sw != nullptr)
        c.sw->add_on_state_callback([this](bool) { this->controller_.refresh(); });
      if (c.cover != nullptr)
        c.cover->add_on_state_callback([this]() { this->controller_.refresh(); });
      if (c.climate != nullptr)
        c.climate->add_on_state_callback([this](climate::Climate & /*unused*/) { this->controller_.refresh(); });
      if (c.media != nullptr)
        c.media->add_on_state_callback([this]() { this->controller_.refresh(); });
    }
  }

  this->controller_.start();

  if (this->encoder_ != nullptr)
    this->last_encoder_ = this->encoder_->get_state();
  this->built_ = true;
  this->push_weather_();  // reflect any weather state received before the UI was built
  ESP_LOGI(TAG, "vues LVGL construites");
}

void HaDashboard::poll_encoder_() {
  if (this->encoder_ == nullptr)
    return;
  float v = this->encoder_->get_state();
  if (std::isnan(v))
    return;
  if (std::isnan(this->last_encoder_)) {
    this->last_encoder_ = v;
    return;
  }
  // The encoder is context-dependent: the controller maps CW/CCW to menu rotation (MENU)
  // or value adjustment (GROUP carousel). Touch handles card navigation (FOCUS_*).
  if (v > this->last_encoder_) {
    this->controller_.handle(InputEvent::ENCODER_CW, -1);
    this->last_encoder_ = v;
  } else if (v < this->last_encoder_) {
    this->controller_.handle(InputEvent::ENCODER_CCW, -1);
    this->last_encoder_ = v;
  }
}

void HaDashboard::poll_button_() {
  if (this->button_ == nullptr)
    return;
  bool down = this->button_->state;
  uint32_t now = millis();
  if (down && !this->button_down_) {  // press
    this->button_down_ = true;
    this->button_hold_triggered_ = false;
    this->button_down_ms_ = now;
  } else if (down && this->button_down_) {  // held: animate the return gauge like a slide-up
    if (this->button_hold_triggered_)
      return;
    uint32_t dur = now - this->button_down_ms_;
    NavState st = this->controller_.state();
    if (st == NavState::GROUP) {
      float p = (float) dur / HOLD_MS;
      if (p > 1.0f)
        p = 1.0f;
      this->renderer_.set_return_progress(p);
      if (p >= 1.0f) {  // gauge full -> back to the menu
        this->renderer_.set_return_progress(0);
        this->controller_.handle(InputEvent::BACK, -1);
        this->button_hold_triggered_ = true;
      }
    } else if (st == NavState::MENU && dur >= HOLD_MS) {  // no gauge in the menu, just go idle
      this->controller_.handle(InputEvent::BACK, -1);
      this->button_hold_triggered_ = true;
    }
  } else if (!down && this->button_down_) {  // release
    this->button_down_ = false;
    uint32_t dur = now - this->button_down_ms_;
    if (this->button_hold_triggered_)
      return;  // hold already acted
    if (dur < TAP_MAX_MS) {
      this->controller_.handle(InputEvent::SELECT, -1);  // quick click
    } else if (this->controller_.state() == NavState::GROUP) {
      this->renderer_.set_return_progress(0);  // partial hold -> cancel the gauge
    }
  }
}

// Map a Home Assistant weather condition to a short French label.
static const char *condition_fr(const std::string &c) {
  if (c == "sunny")
    return "Ensoleillé";
  if (c == "clear-night")
    return "Nuit claire";
  if (c == "partlycloudy")
    return "Éclaircies";
  if (c == "cloudy")
    return "Nuageux";
  if (c == "fog")
    return "Brouillard";
  if (c == "rainy")
    return "Pluie";
  if (c == "pouring")
    return "Averses";
  if (c == "lightning" || c == "lightning-rainy")
    return "Orage";
  if (c == "snowy" || c == "snowy-rainy")
    return "Neige";
  if (c == "hail")
    return "Grêle";
  if (c == "windy" || c == "windy-variant")
    return "Venteux";
  if (c == "exceptional")
    return "Exceptionnel";
  return c.c_str();  // fallback: raw condition
}

// Material Design Icons glyph (UTF-8) for a HA weather condition. Needs the MDI font.
static const char *condition_icon(const std::string &c) {
  if (c == "sunny")
    return "\U000F0599";  // weather-sunny
  if (c == "clear-night")
    return "\U000F0594";  // weather-night
  if (c == "partlycloudy")
    return "\U000F0595";  // weather-partly-cloudy
  if (c == "cloudy")
    return "\U000F0590";  // weather-cloudy
  if (c == "fog")
    return "\U000F0591";  // weather-fog
  if (c == "rainy")
    return "\U000F0597";  // weather-rainy
  if (c == "pouring")
    return "\U000F0596";  // weather-pouring
  if (c == "lightning")
    return "\U000F0593";  // weather-lightning
  if (c == "lightning-rainy")
    return "\U000F067E";  // weather-lightning-rainy
  if (c == "snowy")
    return "\U000F0598";  // weather-snowy
  if (c == "snowy-rainy")
    return "\U000F067F";  // weather-snowy-rainy
  if (c == "hail")
    return "\U000F0592";  // weather-hail
  if (c == "windy" || c == "windy-variant")
    return "\U000F059D";  // weather-windy
  return "\U000F0026";  // alert (exceptional / fallback)
}

void HaDashboard::subscribe_weather_() {
  if (this->weather_entity_ == nullptr || this->weather_subscribed_ || api::global_api_server == nullptr)
    return;
  this->weather_subscribed_ = true;
  // State = weather condition.
  api::global_api_server->subscribe_home_assistant_state(this->weather_entity_, nullptr, [this](StringRef s) {
    std::string raw(s.c_str());
    this->weather_cond_ = condition_fr(raw);
    this->weather_icon_ = condition_icon(raw);
    this->push_weather_();
  });
  // Temperature attribute.
  api::global_api_server->subscribe_home_assistant_state(this->weather_entity_, "temperature", [this](StringRef s) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%.0f°", atof(s.c_str()));
    this->weather_temp_ = buf;
    this->push_weather_();
  });
}

void HaDashboard::push_weather_() {
  if (this->built_)
    this->renderer_.set_weather(this->weather_icon_.c_str(), this->weather_temp_.c_str(),
                                this->weather_cond_.c_str());
}

void HaDashboard::update_clock_() {
  if (this->time_ == nullptr)
    return;
  uint32_t now = millis();
  if (now - this->last_clock_ms_ < 1000)
    return;
  this->last_clock_ms_ = now;
  ESPTime t = this->time_->now();
  if (!t.is_valid())
    return;

  // Day/month names (strftime uses the C locale = English on embedded, so format here).
  static const char *const DAYS_FR[] = {"",        "dimanche", "lundi", "mardi",
                                        "mercredi", "jeudi",    "vendredi", "samedi"};
  static const char *const MONTHS_FR[] = {"",       "janvier", "février",  "mars",     "avril",   "mai",     "juin",
                                          "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
  static const char *const DAYS_EN[] = {"",     "Sunday",   "Monday", "Tuesday",
                                        "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char *const MONTHS_EN[] = {"",       "January", "February", "March",     "April",    "May",      "June",
                                          "July",    "August",  "September", "October", "November", "December"};
  bool fr = this->language_ == "fr";
  const char *const *days = fr ? DAYS_FR : DAYS_EN;
  const char *const *months = fr ? MONTHS_FR : MONTHS_EN;

  char time_buf[8];
  char date_buf[40];
  t.strftime(time_buf, sizeof(time_buf), "%H:%M");
  uint8_t dow = (t.day_of_week >= 1 && t.day_of_week <= 7) ? t.day_of_week : 0;
  uint8_t mon = (t.month >= 1 && t.month <= 12) ? t.month : 0;
  if (fr)
    snprintf(date_buf, sizeof(date_buf), "%s %d %s", days[dow], t.day_of_month, months[mon]);
  else
    snprintf(date_buf, sizeof(date_buf), "%s %s %d", days[dow], months[mon], t.day_of_month);
  // Only push when the displayed minute changed — avoids re-invalidating the clock label every
  // second for the same "HH:MM".
  if (this->last_clock_str_ != time_buf) {
    this->last_clock_str_ = time_buf;
    this->renderer_.set_clock(time_buf, date_buf);
  }
  if (t.hour != this->last_idle_hour_) {
    this->last_idle_hour_ = t.hour;
    this->renderer_.set_idle_hour(t.hour);
  }
}

void HaDashboard::loop() {
  this->build_if_ready_();
  if (!this->built_)
    return;
#ifdef USE_HA_DASHBOARD_LAUNCHER
  // Deliver any completed launcher HTTP requests (ran on the worker task) on the main loop.
  for (auto &b : this->ml_backends_)
    b->process_pending();
#endif
  this->poll_encoder_();
  this->poll_button_();
  this->update_clock_();
  this->tick_timers_(millis());
  this->controller_.tick(millis());

  // Extended standby: once the clock screen has been idle for deep_standby_timeout, fire
  // on_deep_standby once (e.g. turn the backlight off). Woken in wake_from_deep_standby_().
  if (this->deep_standby_ms_ > 0 && !this->deep_standby_active_ &&
      this->controller_.state() == NavState::IDLE &&
      millis() - this->controller_.last_event_ms() >= this->deep_standby_ms_) {
    this->deep_standby_active_ = true;
    ESP_LOGD(TAG, "deep standby");
    this->deep_standby_trigger_.trigger();
  }
}

bool HaDashboard::wake_from_deep_standby_() {
  if (!this->deep_standby_active_)
    return false;
  this->deep_standby_active_ = false;
  ESP_LOGD(TAG, "wake from deep standby");
  this->wake_trigger_.trigger();
  return true;
}

void HaDashboard::toggle_deep_standby() {
  if (this->deep_standby_active_) {
    this->wake_from_deep_standby_();
    this->controller_.notify_activity();  // wake the screen back to home
  } else {
    this->deep_standby_active_ = true;
    ESP_LOGD(TAG, "deep standby (button)");
    this->deep_standby_trigger_.trigger();
  }
}

// Route overlay/header voice + timer actions to the component; navigation goes to the controller.
void HaDashboard::handle_event_(InputEvent e, int idx) {
  // Wake from extended standby on the first interaction: turn the backlight back on and swallow
  // this event (don't also fire the tapped action or a click), like waking a phone.
  if (this->wake_from_deep_standby_()) {
    this->controller_.notify_activity();  // bring the screen back to the home/dashboard
    return;
  }

  // Audible click feedback: fire on a discrete tap of a button/tile, but not on continuous
  // gestures (carousel swipes, slider drags) or system events, which would spam the click.
  switch (e) {
    case InputEvent::WAKE:
    case InputEvent::SLEEP:
    case InputEvent::FOCUS_NEXT:
    case InputEvent::FOCUS_PREV:
    case InputEvent::ENCODER_CW:
    case InputEvent::ENCODER_CCW:
    case InputEvent::NP_SET_VOLUME:
    case InputEvent::SHEET_SET_VALUE:
    case InputEvent::LAUNCHER_REFRESH:
    case InputEvent::LAUNCHER_LOAD_MORE:
      break;
    default:
      this->tap_trigger_.trigger();
      break;
  }

  switch (e) {
    case InputEvent::VOICE_START:
      // Tap-to-talk / retry: start the assistant pipeline directly (bypassing the wake word).
      ESP_LOGD(TAG, "voice: start requested (tap-to-talk)");
#ifdef USE_VOICE_ASSISTANT
      if (voice_assistant::global_voice_assistant != nullptr)
        voice_assistant::global_voice_assistant->request_start(false, true);
#endif
      break;
    case InputEvent::VOICE_CANCEL:
      this->renderer_.voice_hide();
      this->refresh_mic_chip_();
      break;
    case InputEvent::VOICE_MUTE_TOGGLE:
      this->voice_set_muted(!this->voice_muted_);
      break;
    case InputEvent::TIMER_STOP:
      if (!this->ringing_timer_id_.empty()) {
        this->timer_cancelled(this->ringing_timer_id_);
        this->ringing_timer_id_.clear();
      }
      this->renderer_.voice_hide();
      this->refresh_mic_chip_();
      break;
    case InputEvent::TIMER_ADD_MIN:
      for (auto &t : this->timers_)
        if (t.id == this->ringing_timer_id_) {
          t.remaining_s = 60;
          t.is_active = true;
          break;
        }
      this->ringing_timer_id_.clear();
      this->renderer_.voice_hide();
      this->refresh_mic_chip_();
      this->push_timers_();
      break;
    case InputEvent::OPEN_TIMERS:
      // The renderer opens the timers overlay directly (see btn_event_cb); nothing to do here.
      break;
    case InputEvent::OPEN_SETTINGS:
      this->renderer_.show_settings_();
      this->push_settings_();
      break;
    case InputEvent::SETTINGS_CLOSE:
      break;  // the renderer hides its shade itself
    case InputEvent::SET_VOLUME:
      if (this->volume_number_ != nullptr)
        this->volume_number_->make_call().set_value(idx).perform();
      this->push_settings_();
      break;
    case InputEvent::SET_BRIGHTNESS:
      if (this->brightness_number_ != nullptr)
        this->brightness_number_->make_call().set_value(idx).perform();
      this->push_settings_();
      break;
    case InputEvent::SET_STANDBY:
      if (this->standby_number_ != nullptr)
        this->standby_number_->make_call().set_value(idx).perform();
      this->push_settings_();
      break;
    case InputEvent::TOGGLE_CLICK:
      if (this->click_switch_ != nullptr)
        this->click_switch_->toggle();
      this->push_settings_();
      break;
    default:
      this->controller_.handle(e, idx);
      break;
  }
}

void HaDashboard::push_settings_() {
  auto num = [](number::Number *n) {
    return (n != nullptr && !std::isnan(n->state)) ? (int) lroundf(n->state) : 0;
  };
  bool click = this->click_switch_ != nullptr && this->click_switch_->state;
  int batt = (this->battery_sensor_ != nullptr && !std::isnan(this->battery_sensor_->state))
                 ? (int) lroundf(this->battery_sensor_->state)
                 : 0;
  bool charging = this->charging_sensor_ != nullptr && this->charging_sensor_->state;
  this->renderer_.update_settings_(num(this->volume_number_), num(this->brightness_number_),
                                   num(this->standby_number_), click, batt, charging);
}

void HaDashboard::voice_listening() {
  this->wake_from_deep_standby_();       // wake word turns the backlight back on
  this->controller_.notify_activity();  // a wake word / tap-to-talk wakes the screen (like a touch)
  this->renderer_.set_mic_state(MicState::LISTENING);
  this->renderer_.voice_show(VoiceState::LISTENING);
}
void HaDashboard::voice_thinking() { this->renderer_.voice_show(VoiceState::THINKING); }
void HaDashboard::voice_responding() { this->renderer_.voice_show(VoiceState::RESPONDING); }
void HaDashboard::voice_error() { this->renderer_.voice_show(VoiceState::ERROR); }
void HaDashboard::voice_end() {
  // A ringing timer keeps the overlay up; otherwise close it and restore the mic chip.
  if (this->renderer_.voice_state() == VoiceState::TIMER_RINGING)
    return;
  this->renderer_.voice_hide();
  this->refresh_mic_chip_();
}
void HaDashboard::voice_level(float level) { this->renderer_.set_voice_level(level); }

void HaDashboard::voice_set_muted(bool muted) {
  this->voice_muted_ = muted;
  if (muted)
    this->renderer_.voice_show(VoiceState::MUTED);
  else
    this->renderer_.voice_hide();
  this->refresh_mic_chip_();
}
void HaDashboard::voice_set_available(bool available) {
  this->voice_available_ = available;
  this->refresh_mic_chip_();
}

void HaDashboard::refresh_mic_chip_() {
  MicState st = MicState::ARMED;
  if (!this->voice_available_)
    st = MicState::UNAVAILABLE;
  else if (this->voice_muted_)
    st = MicState::MUTED;
  this->renderer_.set_mic_state(st);
}

void HaDashboard::timer_started(const std::string &id, const std::string &name, uint32_t seconds) {
  for (auto &t : this->timers_)
    if (t.id == id) {
      t.name = name;
      t.remaining_s = t.total_s = seconds;
      t.is_active = true;
      this->push_timers_();
      return;
    }
  this->timers_.push_back(TimerInfo{id, name, seconds, seconds, true});
  this->push_timers_();
}
void HaDashboard::timer_updated(const std::string &id, uint32_t seconds_left, bool is_active) {
  for (auto &t : this->timers_)
    if (t.id == id) {
      t.remaining_s = seconds_left;
      t.is_active = is_active;
      this->push_timers_();
      return;
    }
}
void HaDashboard::timer_cancelled(const std::string &id) {
  for (size_t i = 0; i < this->timers_.size(); i++)
    if (this->timers_[i].id == id) {
      this->timers_.erase(this->timers_.begin() + i);
      break;
    }
  this->push_timers_();
}
void HaDashboard::timer_finished(const std::string &id) {
  std::string name;
  uint32_t total = 0;
  for (auto &t : this->timers_)
    if (t.id == id) {
      t.remaining_s = 0;
      t.is_active = false;
      name = t.name;
      total = t.total_s;
      break;
    }
  this->ringing_timer_id_ = id;
  this->wake_from_deep_standby_();       // a finished timer turns the backlight back on
  this->controller_.notify_activity();  // a finished timer lights up the screen (like a touch)
  this->renderer_.voice_show(VoiceState::TIMER_RINGING);
  char sub[48];
  if (!name.empty())
    std::snprintf(sub, sizeof(sub), "%s - %u:%02u", name.c_str(), total / 60, total % 60);
  else
    std::snprintf(sub, sizeof(sub), "%u:%02u", total / 60, total % 60);
  this->renderer_.voice_set_sub(sub);
  this->push_timers_();
}

void HaDashboard::push_timers_() { this->renderer_.set_timers(this->timers_); }

void HaDashboard::tick_timers_(uint32_t now_ms) {
  if (this->timers_.empty())
    return;
  if (now_ms - this->last_timer_tick_ms_ < 1000)
    return;
  this->last_timer_tick_ms_ = now_ms;
  bool changed = false;
  for (auto &t : this->timers_)
    if (t.is_active && t.remaining_s > 0) {
      t.remaining_s--;
      changed = true;
    }
  if (changed)
    this->push_timers_();
}

void HaDashboard::dump_config() {
  ESP_LOGCONFIG(TAG, "ha_dashboard:");
  ESP_LOGCONFIG(TAG, "  profile: %s", this->profile_.c_str());
  ESP_LOGCONFIG(TAG, "  language: %s", this->language_.c_str());
  ESP_LOGCONFIG(TAG, "  inactivity_timeout: %u ms", (unsigned) this->timeout_ms_);
  ESP_LOGCONFIG(TAG, "  encoder: %s", YESNO(this->encoder_ != nullptr));
  ESP_LOGCONFIG(TAG, "  button: %s", YESNO(this->button_ != nullptr));
  for (const auto &g : this->groups_) {
    ESP_LOGCONFIG(TAG, "  - groupe '%s' (%d cards)", g.name.c_str(), (int) g.cards.size());
  }
}

}  // namespace ha_dashboard
}  // namespace esphome
