#include "controller.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "launcher_module.h"

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
  // D1001 starts straight on the merged dashboard; Dial starts idle.
  this->state_ = this->dashboard_mode_ ? NavState::DASHBOARD : NavState::IDLE;
  this->last_event_ms_ = millis();
  if (this->dashboard_mode_)
    this->maybe_load_launcher_(this->group_index_);  // initial tab may be a launcher
  this->render_();
}

// (Re)load a launcher group's favourites when its tab is entered. Loads once (status IDLE)
// or after a previous failure (status ERROR); otherwise leaves the cached list in place.
void Controller::maybe_load_launcher_(int gi) {
  if (this->groups_ == nullptr || gi < 0 || gi >= (int) this->groups_->size())
    return;
  Group &g = (*this->groups_)[gi];
  if (!g.is_launcher || g.launcher == nullptr)
    return;
  LauncherStatus s = g.launcher->status();
  if (s == LauncherStatus::IDLE || s == LauncherStatus::ERROR)
    g.launcher->load();
}

Card *Controller::current_card_() {
  int gi = this->group_index_, ci = this->card_index_;
  if (this->groups_ == nullptr || gi < 0 || gi >= (int) this->groups_->size())
    return nullptr;
  auto &cards = (*this->groups_)[gi].cards;
  if (ci < 0 || ci >= (int) cards.size())
    return nullptr;
  return &cards[ci];
}

void Controller::enter_group_(int group_index) {
  this->group_index_ = group_index;
  this->card_index_ = 0;
  // Dial : le niveau Groupe EST le carrousel de cards (chaque position = une card avec son
  // arc + réglage + toggle). Un groupe mono-card = un carrousel à une seule card (sans dots).
  this->state_ = NavState::GROUP;
}

void Controller::handle(InputEvent event, int index) {
  this->last_event_ms_ = millis();

  switch (this->state_) {
    case NavState::IDLE:
      // N'importe quelle interaction réveille : dashboard (D1001) ou menu (Dial).
      this->state_ = this->dashboard_mode_ ? NavState::DASHBOARD : NavState::MENU;
      if (!this->dashboard_mode_)
        this->group_index_ = 0;
      break;

    case NavState::MENU: {
      int n = this->group_count_();
      if (n == 0)
        break;
      switch (event) {
        // Encoder rotates the radial launcher focus (touch drag is handled in the renderer,
        // which reports the snapped focus via SELECT_GROUP).
        case InputEvent::ENCODER_CW:
          this->group_index_ = (this->group_index_ + 1) % n;
          break;
        case InputEvent::ENCODER_CCW:
          this->group_index_ = (this->group_index_ - 1 + n) % n;
          break;
        case InputEvent::SELECT_GROUP:
          if (index >= 0 && index < n)
            this->group_index_ = index;  // focus only (radial snap)
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

    // Dial carousel: slide <-/-> changes card (FOCUS), encoder adjusts the value (debounced),
    // tap toggles, slide-up / hold goes back to the menu.
    case NavState::GROUP: {
      int n = this->card_count_(this->group_index_);
      if (n == 0) {
        this->state_ = NavState::MENU;
        break;
      }
      switch (event) {
        case InputEvent::FOCUS_NEXT:
          if (Card *c = this->current_card_())
            this->commit_pending_(*c);  // flush staged change before leaving the card
          this->card_index_ = (this->card_index_ + 1) % n;
          break;
        case InputEvent::FOCUS_PREV:
          if (Card *c = this->current_card_())
            this->commit_pending_(*c);
          this->card_index_ = (this->card_index_ - 1 + n) % n;
          break;
        case InputEvent::ENCODER_CW:
          if (Card *c = this->current_card_())
            this->adjust_(*c, +1);
          break;
        case InputEvent::ENCODER_CCW:
          if (Card *c = this->current_card_())
            this->adjust_(*c, -1);
          break;
        case InputEvent::TOGGLE:
        case InputEvent::SELECT:
          if (Card *c = this->current_card_()) {
            this->commit_pending_(*c);
            this->primary_action_(*c);
          }
          break;
        case InputEvent::MEDIA_PREV:
          if (Card *c = this->current_card_())
            if (c->type == CardType::MEDIA_PLAYER && c->media != nullptr)
              c->media->previous_track();
          break;
        case InputEvent::MEDIA_NEXT:
          if (Card *c = this->current_card_())
            if (c->type == CardType::MEDIA_PLAYER && c->media != nullptr)
              c->media->next_track();
          break;
        case InputEvent::BACK:
          if (Card *c = this->current_card_())
            this->commit_pending_(*c);
          this->state_ = NavState::MENU;
          break;
        case InputEvent::SLEEP:
          if (Card *c = this->current_card_())
            this->commit_pending_(*c);
          this->state_ = NavState::IDLE;
          break;
        default:
          break;
      }
      break;
    }

    case NavState::CARD: {
      int gi = this->group_index_, ci = this->card_index_;
      bool valid = this->groups_ && gi < (int) this->groups_->size() &&
                   ci < (int) (*this->groups_)[gi].cards.size();
      switch (event) {
        case InputEvent::TOGGLE:
        case InputEvent::SELECT:
          if (valid)
            this->primary_action_((*this->groups_)[gi].cards[ci]);
          break;
        // Encoder rotation adjusts the focused card's value (brightness/volume/temp/position).
        case InputEvent::FOCUS_NEXT:
          if (valid)
            this->adjust_((*this->groups_)[gi].cards[ci], +1);
          break;
        case InputEvent::FOCUS_PREV:
          if (valid)
            this->adjust_((*this->groups_)[gi].cards[ci], -1);
          break;
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

    case NavState::DASHBOARD: {
      int n = this->group_count_();
      switch (event) {
        case InputEvent::SELECT_GROUP:
          if (index >= 0 && index < n) {
            this->group_index_ = index;
            // Tapping a launcher tab always returns to its home grid (leaves any open detail
            // list), then (re)loads the favourites.
            Group &g = (*this->groups_)[index];
            if (g.is_launcher && g.launcher != nullptr)
              g.launcher->back();  // DETAIL -> GRID (no-op if already on the grid)
            this->maybe_load_launcher_(index);
          }
          break;
        case InputEvent::TOGGLE: {
          int gi = this->group_index_;
          if (this->groups_ && gi < (int) this->groups_->size() && index >= 0 &&
              index < (int) (*this->groups_)[gi].cards.size()) {
            Card &c = (*this->groups_)[gi].cards[index];
            this->primary_action_(c);
          }
          break;
        }
        case InputEvent::LAUNCHER_ACTIVATE:
        case InputEvent::LAUNCHER_OPEN_CHILDREN:
        case InputEvent::LAUNCHER_BACK:
        case InputEvent::LAUNCHER_LOAD_MORE: {
          int gi = this->group_index_;
          if (this->groups_ && gi < (int) this->groups_->size()) {
            Group &g = (*this->groups_)[gi];
            if (g.is_launcher && g.launcher != nullptr) {
              if (event == InputEvent::LAUNCHER_ACTIVATE)
                g.launcher->activate(index);
              else if (event == InputEvent::LAUNCHER_OPEN_CHILDREN)
                g.launcher->open_children(index);
              else if (event == InputEvent::LAUNCHER_BACK)
                g.launcher->back();
              else  // LAUNCHER_LOAD_MORE
                g.launcher->load_more_children();
            }
          }
          break;
        }
        default:
          break;
      }
      break;
    }
  }

  this->render_();
}

void Controller::tick(uint32_t now_ms) {
  // Auto-retry a launcher whose load failed (e.g. network not ready right after boot): while
  // its tab is active and status is ERROR, retry every few seconds until it succeeds.
  if (this->state_ == NavState::DASHBOARD && this->groups_ != nullptr) {
    int gi = this->group_index_;
    if (gi >= 0 && gi < (int) this->groups_->size()) {
      Group &g = (*this->groups_)[gi];
      if (g.is_launcher && g.launcher != nullptr && g.launcher->status() == LauncherStatus::ERROR &&
          now_ms - this->launcher_retry_ms_ >= 4000) {
        this->launcher_retry_ms_ = now_ms;
        g.launcher->load();
      }
    }
  }

  // Commit a staged (debounced) adjustment once the encoder has been quiet long enough.
  if (Card *c = this->current_card_()) {
    if (c->has_pending && now_ms - c->pending_ms >= this->debounce_ms_)
      this->commit_pending_(*c);
  }
  if (this->state_ == NavState::IDLE)
    return;  // already in standby
  if (now_ms - this->last_event_ms_ >= this->timeout_ms_) {
    ESP_LOGD(TAG, "inactivity timeout -> idle");
    if (Card *c = this->current_card_())
      this->commit_pending_(*c);
    this->state_ = NavState::IDLE;
    this->render_();
  }
}

void Controller::primary_action_(Card &c) {
  switch (c.type) {
    case CardType::COVER:
      if (c.cover != nullptr) {
        auto call = c.cover->make_call();
        if (c.cover->position > 0.5f)
          call.set_command_close();
        else
          call.set_command_open();
        call.perform();
      }
      break;
    case CardType::MEDIA_PLAYER:
      if (c.media != nullptr)
        c.media->play_pause();
      break;
    case CardType::CLIMATE:
      // No on/off toggle for climate; adjusted via detail/encoder (later).
      break;
    case CardType::SWITCH:
    case CardType::LIGHT:
    default:
      if (c.sw != nullptr)
        c.sw->toggle();
      else
        c.on = !c.on;
      break;
  }
  ESP_LOGD(TAG, "primary action on '%s'", c.name.c_str());
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Encoder rotate: stage an optimistic normalized value (shown immediately by the renderer
// via Card::display_value). The actual HA command is sent later by commit_pending_ once the
// encoder goes quiet (debounce) — avoids flooding HA with one call per detent.
void Controller::adjust_(Card &c, int dir) {
  float step = 0.05f;  // default step for cover / media / light
  if (c.type == CardType::CLIMATE) {
    if (c.climate == nullptr)
      return;
    auto traits = c.climate->get_traits();
    float lo = traits.get_visual_min_temperature(), hi = traits.get_visual_max_temperature();
    step = (hi > lo) ? 0.5f / (hi - lo) : 0.05f;  // ~0.5°C per detent, normalized
  } else if (c.type == CardType::SWITCH) {
    return;  // nothing to adjust
  }
  float base = c.has_pending ? c.pending_value : c.value();
  c.pending_value = clamp01(base + dir * step);
  c.has_pending = true;
  c.pending_ms = millis();
  ESP_LOGD(TAG, "adjust '%s' dir=%d -> %.2f (pending)", c.name.c_str(), dir, c.pending_value);
}

// Push the staged value to Home Assistant and clear the pending flag.
void Controller::commit_pending_(Card &c) {
  if (!c.has_pending)
    return;
  float v = c.pending_value;
  switch (c.type) {
    case CardType::COVER:
      if (c.cover != nullptr)
        c.cover->make_call().set_position(v).perform();
      break;
    case CardType::MEDIA_PLAYER:
      if (c.media != nullptr)
        c.media->set_volume(v);
      break;
    case CardType::CLIMATE:
      if (c.climate != nullptr) {
        auto traits = c.climate->get_traits();
        float lo = traits.get_visual_min_temperature(), hi = traits.get_visual_max_temperature();
        c.climate->make_call().set_target_temperature(lo + v * (hi - lo)).perform();
      }
      break;
    case CardType::LIGHT:
    default:
      c.value_local = v;  // stub binding for now
      c.on = v > 0.0f;
      break;
  }
  c.has_pending = false;
  ESP_LOGD(TAG, "commit '%s' -> %.2f", c.name.c_str(), v);
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
