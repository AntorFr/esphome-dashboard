#pragma once
// COUCHE 1 — Controller : machine à états de navigation. HW-agnostique :
// ne connaît ni LVGL ni le hardware, dialogue via l'interface Renderer + InputEvent.
#include <cstdint>
#include <vector>
#include "model.h"
#include "renderer.h"

namespace esphome {
namespace ha_dashboard {

class Controller {
 public:
  void set_renderer(Renderer *renderer) { this->renderer_ = renderer; }
  void set_model(std::vector<Group> *groups) { this->groups_ = groups; }
  void set_inactivity_timeout(uint32_t ms) { this->timeout_ms_ = ms; }
  void set_debounce(uint32_t ms) { this->debounce_ms_ = ms; }
  // Dashboard mode (D1001): merged tabs + tile grid, no idle/menu/group levels.
  void set_dashboard_mode(bool enabled) { this->dashboard_mode_ = enabled; }

  // Premier rendu (état IDLE) une fois le renderer prêt.
  void start();

  // Reçoit un event sémantique. `index` >= 0 = sélection directe d'un item.
  void handle(InputEvent event, int index);

  // À appeler à chaque loop : gère le timeout d'inactivité -> veille.
  void tick(uint32_t now_ms);

  // Re-rendu de l'état courant (ex. quand l'état HA d'une card change).
  void refresh() { this->render_(); }

  NavState state() const { return this->state_; }

 protected:
  void render_();
  void primary_action_(Card &card);      // tap/toggle action depending on the card type
  void adjust_(Card &card, int dir);     // encoder rotate: stage an optimistic value change (+/-)
  void commit_pending_(Card &card);      // push the staged value to HA (after debounce / on leave)
  Card *current_card_();                  // focused card in the current group, or null
  void maybe_load_launcher_(int gi);      // (re)load a launcher group's favourites on entry
  int group_count_() const;
  int card_count_(int group_index) const;
  void enter_group_(int group_index);

  std::vector<Group> *groups_{nullptr};
  Renderer *renderer_{nullptr};

  NavState state_{NavState::IDLE};
  int group_index_{0};
  int card_index_{0};
  bool group_skipped_{false};  // single-card group skipped -> BACK from CARD goes to MENU
  bool dashboard_mode_{false};

  uint32_t timeout_ms_{30000};
  uint32_t last_event_ms_{0};
  uint32_t debounce_ms_{350};  // encoder -> HA commit delay (optimistic preview meanwhile)
};

}  // namespace ha_dashboard
}  // namespace esphome
