#include "ha_dashboard.h"
#include <cstdio>
#include <cstdlib>
#include <lvgl.h>
#include "esphome/components/api/api_server.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

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
                                 bool has_color) {
  if (group_index < 0 || group_index >= (int) this->groups_.size())
    return;
  Card &c = push_card_(this->groups_, group_index, CardType::COVER,
                       !name.empty() ? name : std::string("Cover"), color, has_color);
  c.cover = cover;
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

void HaDashboard::add_launcher_cover_slot(online_image::OnlineImage *slot) {
  // Attach to the launcher group just added (codegen calls this right after add_launcher_group).
  if (!this->groups_.empty() && this->groups_.back().is_launcher)
    this->groups_.back().cover_slots.push_back(slot);
}

void HaDashboard::add_launcher_thumb_slot(online_image::OnlineImage *slot) {
  if (!this->groups_.empty() && this->groups_.back().is_launcher)
    this->groups_.back().thumb_slots.push_back(slot);
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
  this->renderer_.set_event_handler([this](InputEvent e, int idx) { this->controller_.handle(e, idx); });
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
  this->renderer_.set_clock(time_buf, date_buf);
  this->renderer_.set_idle_hour(t.hour);
}

void HaDashboard::loop() {
  this->build_if_ready_();
  if (!this->built_)
    return;
  this->poll_encoder_();
  this->poll_button_();
  this->update_clock_();
  this->controller_.tick(millis());
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
