#include "ha_dashboard.h"
#include <lvgl.h>
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard";
static constexpr uint32_t LONG_PRESS_MS = 700;  // maintien bouton -> BACK

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

void HaDashboard::setup() {
  this->renderer_.set_profile(this->profile_);
  this->controller_.set_inactivity_timeout(this->timeout_ms_);
  ESP_LOGCONFIG(TAG, "ha_dashboard setup (profile=%s, %d groupes)", this->profile_.c_str(),
                (int) this->groups_.size());
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

  // Re-render quand l'état HA d'une card switch change (binding live).
  for (auto &g : this->groups_) {
    for (auto &c : g.cards) {
      if (c.sw != nullptr)
        c.sw->add_on_state_callback([this](bool) { this->controller_.refresh(); });
    }
  }

  this->controller_.start();

  if (this->encoder_ != nullptr)
    this->last_encoder_ = this->encoder_->get_state();
  this->built_ = true;
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
  if (v > this->last_encoder_) {
    this->controller_.handle(InputEvent::FOCUS_NEXT, -1);
    this->last_encoder_ = v;
  } else if (v < this->last_encoder_) {
    this->controller_.handle(InputEvent::FOCUS_PREV, -1);
    this->last_encoder_ = v;
  }
}

void HaDashboard::poll_button_() {
  if (this->button_ == nullptr)
    return;
  bool down = this->button_->state;
  uint32_t now = millis();
  if (down && !this->button_down_) {
    this->button_down_ = true;
    this->button_down_ms_ = now;
  } else if (!down && this->button_down_) {
    this->button_down_ = false;
    uint32_t dur = now - this->button_down_ms_;
    // Maintien = retour (jauge de retour à venir) ; appui court = valider.
    this->controller_.handle(dur >= LONG_PRESS_MS ? InputEvent::BACK : InputEvent::SELECT, -1);
  }
}

void HaDashboard::loop() {
  this->build_if_ready_();
  if (!this->built_)
    return;
  this->poll_encoder_();
  this->poll_button_();
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
