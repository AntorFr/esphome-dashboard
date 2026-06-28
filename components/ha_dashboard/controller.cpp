#include "controller.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard.controller";

int Controller::group_count_() const { return this->groups_ ? (int) this->groups_->size() : 0; }

int Controller::card_count_(int group_index) const {
  if (this->groups_ == nullptr || group_index < 0 || group_index >= (int) this->groups_->size())
    return 0;
  return (int) (*this->groups_)[group_index].cards.size();
}

void Controller::start() {
  this->state_ = NavState::IDLE;
  this->last_event_ms_ = millis();
  this->render_();
}

void Controller::enter_group_(int group_index) {
  this->group_index_ = group_index;
  this->card_index_ = 0;
  // Règle : un groupe à une seule card saute le niveau Groupe (cf. docs/ux-interaction.md).
  if (this->card_count_(group_index) <= 1) {
    this->group_skipped_ = true;
    this->state_ = NavState::CARD;
  } else {
    this->group_skipped_ = false;
    this->state_ = NavState::GROUP;
  }
}

void Controller::handle(InputEvent event, int index) {
  this->last_event_ms_ = millis();

  switch (this->state_) {
    case NavState::IDLE:
      // N'importe quelle interaction réveille.
      this->state_ = NavState::MENU;
      this->group_index_ = 0;
      break;

    case NavState::MENU: {
      int n = this->group_count_();
      if (n == 0)
        break;
      switch (event) {
        case InputEvent::FOCUS_NEXT:
          this->group_index_ = (this->group_index_ + 1) % n;
          break;
        case InputEvent::FOCUS_PREV:
          this->group_index_ = (this->group_index_ - 1 + n) % n;
          break;
        case InputEvent::SELECT:
          this->enter_group_(index >= 0 ? index : this->group_index_);
          break;
        case InputEvent::BACK:
        case InputEvent::SLEEP:
          this->state_ = NavState::IDLE;
          break;
        default:
          break;
      }
      break;
    }

    case NavState::GROUP: {
      int n = this->card_count_(this->group_index_);
      if (n == 0) {
        this->state_ = NavState::MENU;
        break;
      }
      switch (event) {
        case InputEvent::FOCUS_NEXT:
          this->card_index_ = (this->card_index_ + 1) % n;
          break;
        case InputEvent::FOCUS_PREV:
          this->card_index_ = (this->card_index_ - 1 + n) % n;
          break;
        case InputEvent::SELECT:
          this->card_index_ = index >= 0 ? index : this->card_index_;
          this->state_ = NavState::CARD;
          break;
        case InputEvent::BACK:
          this->state_ = NavState::MENU;
          break;
        case InputEvent::SLEEP:
          this->state_ = NavState::IDLE;
          break;
        default:
          break;
      }
      break;
    }

    case NavState::CARD:
      switch (event) {
        case InputEvent::TOGGLE:
        case InputEvent::SELECT: {
          // Prototype : bascule l'état local (binding HA = jalon suivant).
          int gi = this->group_index_, ci = this->card_index_;
          if (this->groups_ && gi < (int) this->groups_->size() &&
              ci < (int) (*this->groups_)[gi].cards.size()) {
            Card &c = (*this->groups_)[gi].cards[ci];
            c.on = !c.on;
            ESP_LOGI(TAG, "toggle card '%s' -> %s", c.name.c_str(), c.on ? "on" : "off");
          }
          break;
        }
        case InputEvent::BACK:
          this->state_ = this->group_skipped_ ? NavState::MENU : NavState::GROUP;
          break;
        case InputEvent::SLEEP:
          this->state_ = NavState::IDLE;
          break;
        default:
          break;
      }
      break;
  }

  this->render_();
}

void Controller::tick(uint32_t now_ms) {
  if (this->state_ == NavState::IDLE)
    return;
  if (now_ms - this->last_event_ms_ >= this->timeout_ms_) {
    ESP_LOGD(TAG, "inactivity timeout -> idle");
    this->state_ = NavState::IDLE;
    this->render_();
  }
}

void Controller::render_() {
  if (this->renderer_ == nullptr)
    return;
  ViewModel vm;
  vm.state = this->state_;
  vm.groups = this->groups_;
  vm.group_index = this->group_index_;
  vm.card_index = this->card_index_;
  this->renderer_->render(vm);
}

}  // namespace ha_dashboard
}  // namespace esphome
