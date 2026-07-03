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
  COVER_OPEN,    // cover: open (Dial side button)
  COVER_CLOSE,   // cover: close (Dial side button)
  MEDIA_NEXT,    // media player: next track
  LAUNCHER_ACTIVATE,       // music launcher: tap a favourite/row -> play (index = item)
  LAUNCHER_OPEN_CHILDREN,  // music launcher: tap the list button -> drill into episodes (index = tile)
  LAUNCHER_BACK,           // music launcher: leave the episode/chapter list -> back to grid
  LAUNCHER_LOAD_MORE,      // music launcher: load the next page of episodes/chapters
  LAUNCHER_REFRESH,        // music launcher: pull-to-refresh at the top of the favourites grid
  OPEN_NOW_PLAYING,        // header media widget -> open the "now playing" card
  NP_PLAY_PAUSE,           // now playing: toggle play/pause
  NP_NEXT,                 // now playing: next track
  NP_PREV,                 // now playing: previous track
  NP_VOL_UP,               // now playing: volume up
  NP_VOL_DOWN,             // now playing: volume down
  NP_SET_VOLUME,           // now playing: set absolute volume (index = 0..100 from the slider)
  NP_MUTE,                 // now playing: toggle mute (tap the volume icon)
  NP_SHUFFLE,              // now playing: toggle shuffle
  NP_REPEAT,               // now playing: cycle repeat mode

  // Classic-card control sheet (D1001 "more-info" modal). Actions apply to the card the sheet
  // was opened on (tracked by the controller).
  OPEN_SHEET,              // open the control sheet for a card (index = card in the active group)
  SHEET_CLOSE,             // close the control sheet
  SHEET_TEMP_UP,           // climate: target temperature +
  SHEET_TEMP_DOWN,         // climate: target temperature -
  SHEET_MODE,              // climate: set mode (index = 0 off / 1 heat / 2 cool / 3 auto)
  SHEET_PLAY_PAUSE,        // media: play/pause
  SHEET_MEDIA_NEXT,        // media: next track
  SHEET_MEDIA_PREV,        // media: previous track
  SHEET_COVER_OPEN,        // cover: open
  SHEET_COVER_STOP,        // cover: stop
  SHEET_COVER_CLOSE,       // cover: close
  SHEET_SET_VALUE,         // slider (index = 0..100): volume (media) / position (cover) / brightness (light)

  // Voice assistant overlay actions (buttons on the top-layer overlay / header mic chip).
  VOICE_START,             // start listening (tap mic chip / "retry")
  VOICE_CANCEL,            // cancel / close the overlay
  VOICE_MUTE_TOGGLE,       // toggle microphone mute (long-press chip / "reactivate")
  TIMER_STOP,             // stop the ringing (or a) timer (index = timer, -1 = the ringing one)
  TIMER_ADD_MIN,          // add one minute to the ringing timer
  OPEN_TIMERS,             // open the timers screen (tap the header timer pill)
};

// Voice-assistant overlay states (independent of NavState — the overlay sits on the top
// layer, over any screen). Driven by the ESPHome voice_assistant/micro_wake_word events.
enum class VoiceState : uint8_t {
  HIDDEN = 0,     // overlay not shown
  LISTENING,      // wake detected / recording (audio level animated)
  THINKING,       // STT done -> intent/LLM
  RESPONDING,     // TTS playing (reply)
  ERROR,          // not understood / pipeline error
  MUTED,          // microphone muted (wake word disabled)
  UNAVAILABLE,    // HA offline / no Assist pipeline
  TIMER_RINGING,  // a voice timer finished -> full-screen alarm
};

// Header microphone chip states (persistent surface on the D1001 dashboard header).
enum class MicState : uint8_t { ARMED = 0, LISTENING, MUTED, UNAVAILABLE };

// One HA Assist voice timer (for the header pill + the timers screen).
struct TimerInfo {
  std::string id;
  std::string name;
  uint32_t remaining_s{0};
  uint32_t total_s{0};
  bool is_active{false};
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
