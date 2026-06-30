#pragma once
// Port Renderer (interface abstraite) + types partagés de navigation.
// Le Controller (couche 1) ne dépend que de cette interface, jamais de LVGL.
#include <functional>
#include <vector>
#include "model.h"

namespace esphome {
namespace ha_dashboard {

// Niveaux de navigation communs aux deux écrans (cf. docs/ux-interaction.md).
enum class NavState : uint8_t {
  IDLE = 0,   // idle / standby
  MENU,       // group picker (Dial radial launcher)
  GROUP,      // card picker within a group (Dial)
  CARD,       // card detail / adjust
  DASHBOARD,  // merged tabs + tile grid (D1001)
  NOW_PLAYING,  // music launcher: full "now playing" card (opened from the header widget)
};

// Events sémantiques émis par la couche Input (et par les widgets natifs LVGL)
// vers le Controller. `index` = item ciblé pour une sélection directe (sinon -1).
enum class InputEvent : uint8_t {
  WAKE = 0,      // leave idle
  FOCUS_NEXT,    // next focus (touch swipe ->) — Dial carousel = next card
  FOCUS_PREV,    // previous focus (touch swipe <-) — Dial carousel = previous card
  SELECT,        // confirm focus (or index if provided)
  BACK,          // go up one level (slide up / button hold)
  TOGGLE,        // card primary action (on/off), index = card
  SLEEP,         // back to idle (timeout)
  SELECT_GROUP,  // focus a group directly (D1001 tabs / Dial radial snap), index = group
  ENCODER_CW,    // encoder clockwise — context-dependent (menu rotate / card adjust)
  ENCODER_CCW,   // encoder counter-clockwise
  MEDIA_PREV,    // media player: previous track
  MEDIA_NEXT,    // media player: next track
  LAUNCHER_ACTIVATE,       // music launcher: tap a favourite/row -> play (index = item)
  LAUNCHER_OPEN_CHILDREN,  // music launcher: tap the list button -> drill into episodes (index = tile)
  LAUNCHER_BACK,           // music launcher: leave the episode/chapter list -> back to grid
  LAUNCHER_LOAD_MORE,      // music launcher: load the next page of episodes/chapters
  OPEN_NOW_PLAYING,        // header media widget -> open the "now playing" card
  NP_PLAY_PAUSE,           // now playing: toggle play/pause
  NP_NEXT,                 // now playing: next track
  NP_PREV,                 // now playing: previous track
};

// Instantané de l'état de navigation passé au renderer.
struct ViewModel {
  NavState state{NavState::IDLE};
  const std::vector<Group> *groups{nullptr};
  int group_index{0};  // groupe courant / focalisé
  int card_index{0};   // card courante / focalisée
};

class Renderer {
 public:
  virtual ~Renderer() = default;

  using EventHandler = std::function<void(InputEvent, int)>;

  // Branche le callback d'events sémantiques (widgets natifs -> Controller).
  virtual void set_event_handler(EventHandler handler) = 0;

  // Construit l'arbre d'objets LVGL une seule fois à partir du modèle statique.
  // (Pas de destroy/recreate aux transitions — cf. ADR-0005.)
  virtual void build(const std::vector<Group> &groups) = 0;

  // Reflète l'état de navigation courant (update-in-place + chargement d'écran).
  virtual void render(const ViewModel &vm) = 0;
};

}  // namespace ha_dashboard
}  // namespace esphome
