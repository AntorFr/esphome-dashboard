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

LauncherModule *Controller::first_launcher_() {
  if (this->groups_ == nullptr)
    return nullptr;
  for (auto &g : *this->groups_)
    if (g.is_launcher && g.launcher != nullptr)
      return g.launcher;
  return nullptr;
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
            if (c->type == CardType::COVER && c->cover != nullptr) {
              auto call = c->cover->make_call();  // Dial: tap centre on a cover = stop
              call.set_command_stop();
              call.perform();
            } else {
              this->primary_action_(*c);
            }
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
        case InputEvent::COVER_OPEN:
        case InputEvent::COVER_CLOSE:
          if (Card *c = this->current_card_())
            if (c->type == CardType::COVER && c->cover != nullptr) {
              auto call = c->cover->make_call();
              if (event == InputEvent::COVER_OPEN)
                call.set_command_open();
              else
                call.set_command_close();
              call.perform();
            }
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
        case InputEvent::OPEN_SHEET:
          this->sheet_ci_ = index;  // the renderer shows the sheet; remember the card for actions
          break;
        case InputEvent::SHEET_CLOSE:
          this->sheet_ci_ = -1;
          break;
        case InputEvent::SHEET_TEMP_UP:
        case InputEvent::SHEET_TEMP_DOWN:
        case InputEvent::SHEET_MODE:
        case InputEvent::SHEET_PLAY_PAUSE:
        case InputEvent::SHEET_MEDIA_NEXT:
        case InputEvent::SHEET_MEDIA_PREV:
        case InputEvent::SHEET_COVER_OPEN:
        case InputEvent::SHEET_COVER_STOP:
        case InputEvent::SHEET_COVER_CLOSE:
        case InputEvent::SHEET_SET_VALUE: {
          int gi = this->group_index_;
          if (this->groups_ && gi < (int) this->groups_->size() && this->sheet_ci_ >= 0 &&
              this->sheet_ci_ < (int) (*this->groups_)[gi].cards.size())
            this->sheet_action_((*this->groups_)[gi].cards[this->sheet_ci_], event, index);
          break;
        }
        case InputEvent::LAUNCHER_ACTIVATE:
        case InputEvent::LAUNCHER_OPEN_CHILDREN:
        case InputEvent::LAUNCHER_BACK:
        case InputEvent::LAUNCHER_LOAD_MORE:
        case InputEvent::LAUNCHER_REFRESH: {
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
              else if (event == InputEvent::LAUNCHER_LOAD_MORE)
                g.launcher->load_more_children();
              else if (g.launcher->level() == LauncherLevel::GRID)  // LAUNCHER_REFRESH
                g.launcher->load();  // pull-to-refresh only reloads the favourites grid
            }
          }
          break;
        }
        case InputEvent::OPEN_NOW_PLAYING:
          if (LauncherModule *L = this->first_launcher_()) {
            L->fetch_now_playing();
            this->state_ = NavState::NOW_PLAYING;
          }
          break;
        default:
          break;
      }
      break;
    }

    case NavState::NOW_PLAYING: {
      LauncherModule *L = this->first_launcher_();
      switch (event) {
        case InputEvent::NP_PLAY_PAUSE:
          if (L != nullptr)
            L->transport("play_pause");
          break;
        case InputEvent::NP_NEXT:
          if (L != nullptr)
            L->transport("next");
          break;
        case InputEvent::NP_PREV:
          if (L != nullptr)
            L->transport("previous");
          break;
        case InputEvent::NP_VOL_UP:
          if (L != nullptr)
            L->volume_step("up");
          break;
        case InputEvent::NP_VOL_DOWN:
          if (L != nullptr)
            L->volume_step("down");
          break;
        case InputEvent::NP_SET_VOLUME:
          if (L != nullptr)
            L->set_volume(index);  // index = slider value 0..100
          break;
        case InputEvent::NP_MUTE:
          if (L != nullptr)
            L->toggle_mute();
          break;
        case InputEvent::NP_SHUFFLE:
          if (L != nullptr)
            L->toggle_shuffle();
          break;
        case InputEvent::NP_REPEAT:
          if (L != nullptr)
            L->cycle_repeat();
          break;
        case InputEvent::BACK:
          this->state_ = NavState::DASHBOARD;
          break;
        case InputEvent::SLEEP:
          this->state_ = NavState::IDLE;
          break;
        default:
          break;
      }
      // Push the next auto-refresh out by the full interval so it doesn't read a not-yet-updated
      // MA state right after a control tap (the optimistic UI already shows the new state).
      this->np_poll_ms_ = millis();
      break;
    }
  }

  this->render_();
}

void Controller::tick(uint32_t now_ms) {
  // Flush a coalesced re-render (multiple HA state callbacks in one loop -> a single render).
  if (this->render_dirty_) {
    this->render_dirty_ = false;
    this->render_();
  }

  // Auto-retry a launcher whose load failed (e.g. network not ready right after boot): while
  // its tab is active and status is ERROR, retry every few seconds until it succeeds.
  if (this->state_ == NavState::DASHBOARD && this->groups_ != nullptr) {
    int gi = this->group_index_;
    if (gi >= 0 && gi < (int) this->groups_->size()) {
      Group &g = (*this->groups_)[gi];
      if (g.is_launcher && g.launcher != nullptr && g.launcher->status() == LauncherStatus::ERROR &&
          now_ms - this->launcher_retry_ms_ >= 2000) {
        this->launcher_retry_ms_ = now_ms;
        g.launcher->load();
      }
    }
  }

  // While the now-playing card is open, refresh its state (position/volume/…) every couple of
  // seconds so the progress bar advances. Async fetch -> no loop stall; doesn't count as user
  // activity (the inactivity timeout still applies).
  if (this->state_ == NavState::NOW_PLAYING && now_ms - this->np_poll_ms_ >= 2000) {
    this->np_poll_ms_ = now_ms;
    if (LauncherModule *L = this->first_launcher_())
      L->fetch_now_playing();
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
      // Dial: tap centre cycles the mode (off -> heat -> cool -> auto). (D1001 uses the sheet.)
      if (c.climate != nullptr) {
        climate::ClimateMode next = climate::CLIMATE_MODE_OFF;
        switch (c.climate->mode) {
          case climate::CLIMATE_MODE_OFF: next = climate::CLIMATE_MODE_HEAT; break;
          case climate::CLIMATE_MODE_HEAT: next = climate::CLIMATE_MODE_COOL; break;
          case climate::CLIMATE_MODE_COOL: next = climate::CLIMATE_MODE_HEAT_COOL; break;
          default: next = climate::CLIMATE_MODE_OFF; break;
        }
        auto call = c.climate->make_call();
        call.set_mode(next);
        call.perform();
      }
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

// Control-sheet button -> the matching HA entity call on the sheet's card.
void Controller::sheet_action_(Card &c, InputEvent ev, int index) {
  switch (ev) {
    case InputEvent::SHEET_TEMP_UP:
    case InputEvent::SHEET_TEMP_DOWN:
      if (c.climate != nullptr) {
        float step = c.climate->get_traits().get_visual_target_temperature_step();
        if (step <= 0.0f)
          step = 0.5f;
        auto call = c.climate->make_call();
        call.set_target_temperature(c.climate->target_temperature +
                                    (ev == InputEvent::SHEET_TEMP_UP ? step : -step));
        call.perform();
      }
      break;
    case InputEvent::SHEET_MODE:
      if (c.climate != nullptr) {
        climate::ClimateMode m = climate::CLIMATE_MODE_OFF;
        if (index == 1)
          m = climate::CLIMATE_MODE_HEAT;
        else if (index == 2)
          m = climate::CLIMATE_MODE_COOL;
        else if (index == 3)
          m = climate::CLIMATE_MODE_HEAT_COOL;
        auto call = c.climate->make_call();
        call.set_mode(m);
        call.perform();
      }
      break;
    case InputEvent::SHEET_PLAY_PAUSE:
      if (c.media != nullptr)
        c.media->play_pause();
      break;
    case InputEvent::SHEET_MEDIA_NEXT:
      if (c.media != nullptr)
        c.media->next_track();
      break;
    case InputEvent::SHEET_MEDIA_PREV:
      if (c.media != nullptr)
        c.media->previous_track();
      break;
    case InputEvent::SHEET_COVER_OPEN:
    case InputEvent::SHEET_COVER_STOP:
    case InputEvent::SHEET_COVER_CLOSE:
      if (c.cover != nullptr) {
        auto call = c.cover->make_call();
        if (ev == InputEvent::SHEET_COVER_OPEN)
          call.set_command_open();
        else if (ev == InputEvent::SHEET_COVER_CLOSE)
          call.set_command_close();
        else
          call.set_command_stop();
        call.perform();
      }
      break;
    case InputEvent::SHEET_SET_VALUE: {
      float v = index / 100.0f;
      if (c.type == CardType::MEDIA_PLAYER && c.media != nullptr) {
        c.media->set_volume(v);
      } else if (c.type == CardType::COVER && c.cover != nullptr) {
        auto call = c.cover->make_call();
        call.set_position(v);
        call.perform();
      } else {  // light stub (no binding yet): optimistic local value
        c.value_local = v;
        c.on = v > 0.0f;
      }
      break;
    }
    default:
      break;
  }
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
