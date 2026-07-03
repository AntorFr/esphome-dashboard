#pragma once
// COUCHE 2 — Renderer LVGL (API C directe, cf. ADR-0003). Partagé Dial + D1001.
// Respecte ADR-0005 : objets construits une fois (build), update-in-place au render,
// événements LVGL natifs sur de vrais lv_obj.
#include <lvgl.h>
#include <string>
#include <vector>
#include "esphome/components/font/font.h"
#include "renderer.h"

namespace esphome {
namespace ha_dashboard {

class LvglRenderer : public Renderer {
 public:
  void set_profile(const std::string &profile);
  void set_event_handler(EventHandler handler) override { this->handler_ = std::move(handler); }
  void build(const std::vector<Group> &groups) override;
  void render(const ViewModel &vm) override;

  // Appelé par les callbacks LVGL natifs.
  void emit(InputEvent event, int index) {
    if (this->handler_)
      this->handler_(event, index);
  }
  // Pull-to-refresh hook (called by the launcher list's scroll callback): arm on top
  // over-scroll, fire LAUNCHER_REFRESH when the scroll settles.
  void on_launcher_scroll(lv_obj_t *grid, bool ended);
  // Show a transient launch-confirmation toast (auto-hides after a couple of seconds).
  void show_toast(const std::string &text);
  void hide_toast_();  // called by the toast's auto-hide timer

  // Update the dashboard header clock (called by the component from a time source).
  void set_clock(const char *time_str, const char *date_str);
  // Update the header weather slot (icon glyph + temperature + condition text).
  void set_weather(const char *icon_glyph, const char *temp_str, const char *cond_str);
  // Tint the idle (standby) background according to the time of day.
  void set_idle_hour(int hour);

  void set_font_small(font::Font *f) { this->font_small_ = f; }
  void set_font_medium(font::Font *f) { this->font_medium_ = f; }
  void set_font_large(font::Font *f) { this->font_large_ = f; }
  void set_font_weather(font::Font *f) { this->font_weather_ = f; }
  void set_font_icons(font::Font *f) { this->font_icons_ = f; }
  void set_font_icons_lg(font::Font *f) { this->font_icons_lg_ = f; }
  void set_font_voice(font::Font *f) { this->font_voice_ = f; }
  // Button-hold return gauge (driven from the component while the encoder button is held).
  void set_return_progress(float p) { this->render_return_(p); }

  // --- Voice assistant (top-layer overlay, over any screen) ---
  void voice_show(VoiceState st);            // build-once, then restyle to the given state
  void voice_hide();                         // hide the overlay
  void set_voice_level(float level);         // 0..1 mic level (listening waveform amplitude)
  void voice_set_sub(const std::string &s);  // set the overlay's secondary line (e.g. ringing timer)
  VoiceState voice_state() const { return this->voice_state_; }
  // Header surfaces (D1001 dashboard): mic chip state + active-timer pill.
  void set_mic_state(MicState st);
  void set_timers(const std::vector<TimerInfo> &timers);
  // Timers screen (active list + ring), a top-layer overlay opened from the header pill.
  void show_timers_();
  void hide_timers_();

 protected:
  lv_obj_t *make_screen_();
  const Card *current_card_(const ViewModel &vm) const;

  // Dial radial launcher (group picker): icons sit at fixed positions on the ring, the
  // highlight (grow + accent) moves to the focused one (cf. esp-dial behaviour).
  void build_menu_(const std::vector<Group> &groups);
  void layout_menu_(int focus);                    // restyle for the focused index (no motion)
  void menu_circle_pos_(int i, float &x, float &y) const;
  static void menu_gesture_cb(lv_event_t *e);      // tap an icon to enter its group

  // Dial card carousel (group = one card at a time, slide to navigate).
  void build_card_view_();
  void render_card_view_(const ViewModel &vm);
  void set_carousel_translate_(float dx);          // follow horizontal drag
  void render_return_(float p);                    // return gauge progress 0..1
  static void carousel_gesture_cb(lv_event_t *e);  // slide / tap on the card screen

  // D1001 dashboard (tabs + tile grid).
  void build_dashboard_(const std::vector<Group> &groups);
  void render_dashboard_(const ViewModel &vm);

 public:
  // Classic-card control sheet ("more-info" modal, opened from a tile). Called by the tile/icon
  // callbacks (open) and the sheet's own buttons (close). Public so the static LVGL cbs reach it.
  void show_sheet_(int card_index);
  void hide_sheet_();

 protected:
  void build_sheet_content_(const Card &c);  // (re)build the sheet body for a card
  void refresh_sheet_();                      // update the sheet's live values (called on render)
  // Set an MDI card/type icon on a label (font_icons_lg_ if available, else LVGL symbol fallback).
  void set_card_icon_(lv_obj_t *label, const Card &c, uint32_t color);
  // Music Library launcher tab: list of favourites (rebuilt only when the module's
  // status/count/level changes), with covers via online_image slots.
  void render_launcher_(int gi, const Group &g);
  // "Now playing" card (built once, populated from the launcher's NowPlaying).
  void build_now_playing_();
  void render_now_playing_(const ViewModel &vm);
  const Group *first_launcher_(const ViewModel &vm) const;
  // Register on-download finished/error callbacks for every cover slot (once, at build time).
  void register_cover_slots_(const std::vector<Group> &groups);
  // Refresh the LVGL image bound to a cover slot once its download/decode completes.
  void on_cover_ready_(online_image::OnlineImage *slot);
  void on_cover_error_(online_image::OnlineImage *slot);
  // Covers download one at a time (serialized) to avoid exhausting TLS/socket memory.
  void advance_cover_(online_image::OnlineImage *finished_slot);
  // Bind an LVGL image to a slot for `url`, (re)downloading (serially) only if the slot isn't
  // already holding that exact URL. While a download is pending the image is hidden so a
  // stale, wrong-size frame is never shown. Returns the slot's index in cover_slot_list_.
  int bind_cover_(lv_obj_t *img, online_image::OnlineImage *slot, const std::string &url);

  // Apply an esphome font if provided, else the built-in LVGL fallback.
  void set_text_font_(lv_obj_t *obj, font::Font *f, const lv_font_t *fallback);

  // Per-tile widgets we update in place on state change.
  struct Tile {
    lv_obj_t *root{nullptr};
    lv_obj_t *icon{nullptr};
    lv_obj_t *state{nullptr};
    lv_obj_t *bar{nullptr};  // value bar for non-switch cards (null for switch)
  };

  EventHandler handler_;
  std::string profile_{"dial"};
  bool round_{true};

  lv_obj_t *idle_scr_{nullptr};
  lv_obj_t *idle_time_lbl_{nullptr};
  lv_obj_t *idle_date_lbl_{nullptr};
  lv_obj_t *idle_wx_icon_{nullptr};
  lv_obj_t *idle_wx_temp_{nullptr};
  lv_obj_t *idle_wx_cond_{nullptr};
  lv_obj_t *menu_scr_{nullptr};
  lv_obj_t *card_scr_{nullptr};

  // Static model + display geometry (for the gesture math; cf. ADR-0005).
  const std::vector<Group> *model_{nullptr};
  int w_{240};
  int h_{240};

  // Radial launcher widgets (icons fixed; only the highlight moves).
  std::vector<lv_obj_t *> menu_circles_;
  std::vector<lv_obj_t *> menu_icons_;
  lv_obj_t *menu_name_lbl_{nullptr};
  bool m_down_{false};
  int m_sx_{0}, m_sy_{0};

  // Card carousel widgets.
  lv_obj_t *card_title_{nullptr};
  lv_obj_t *card_value_{nullptr};
  lv_obj_t *card_arc_{nullptr};
  lv_obj_t *card_icon_{nullptr};
  lv_obj_t *card_center_{nullptr};  // icon/value/name column (translated + faded)
  lv_obj_t *card_dots_{nullptr};    // pagination dots container
  lv_obj_t *card_hint_{nullptr};    // "^ menu" chevron hint
  lv_obj_t *ret_l_{nullptr};        // return gauge: left arc
  lv_obj_t *ret_r_{nullptr};        // return gauge: right arc
  lv_obj_t *card_prev_btn_{nullptr};  // media prev / cover open (left side button)
  lv_obj_t *card_next_btn_{nullptr};  // media next / cover close (right side button)
  lv_obj_t *card_prev_lbl_{nullptr};  // glyph of the left button (swapped per card type)
  lv_obj_t *card_next_lbl_{nullptr};  // glyph of the right button
  struct CbData *card_prev_cb_{nullptr};  // left button's callback data (event swapped per type)
  struct CbData *card_next_cb_{nullptr};  // right button's callback data

  // Carousel gesture state.
  bool gest_drag_{false};
  int gest_sx_{0}, gest_sy_{0};
  int gest_dx_{0}, gest_dy_{0};            // last delta seen while pressing
  int gest_peak_dx_{0}, gest_peak_dy_{0};  // peak signed displacement (robust to edge lift-off)
  int gest_mode_{0};                       // 0 none, 1 horizontal (carousel), 2 vertical (gauge)
  bool gest_committed_{false};             // horizontal card change already fired this gesture
  float gest_ret_p_{0.0f};

  font::Font *font_small_{nullptr};
  font::Font *font_medium_{nullptr};
  font::Font *font_large_{nullptr};
  font::Font *font_weather_{nullptr};
  font::Font *font_icons_{nullptr};
  font::Font *font_icons_lg_{nullptr};  // larger MDI set for classic-card tile/sheet icons
  font::Font *font_voice_{nullptr};     // large MDI glyphs for the voice overlay orb

  lv_obj_t *dashboard_scr_{nullptr};
  lv_obj_t *dash_header_{nullptr};
  lv_obj_t *time_lbl_{nullptr};
  lv_obj_t *date_lbl_{nullptr};
  lv_obj_t *weather_icon_lbl_{nullptr};
  lv_obj_t *weather_temp_lbl_{nullptr};
  lv_obj_t *weather_cond_lbl_{nullptr};
  lv_obj_t *np_btn_{nullptr};  // header "now playing" button

  // "Now playing" card (opened from the header button).
  lv_obj_t *now_playing_scr_{nullptr};
  lv_obj_t *np_title_lbl_{nullptr};
  lv_obj_t *np_sub_lbl_{nullptr};
  lv_obj_t *np_pp_icon_{nullptr};   // play/pause glyph (toggles)
  lv_obj_t *np_cover_img_{nullptr};
  lv_obj_t *np_shuffle_lbl_{nullptr};
  lv_obj_t *np_repeat_lbl_{nullptr};
  lv_obj_t *np_vol_lbl_{nullptr};
  lv_obj_t *np_vol_slider_{nullptr};  // absolute volume slider (0..100)
  lv_obj_t *np_progress_{nullptr};    // playback position bar
  lv_obj_t *np_time_lbl_{nullptr};    // elapsed time
  lv_obj_t *np_dur_lbl_{nullptr};     // total duration
  online_image::OnlineImage *np_cover_slot_{nullptr};  // dedicated slot for the now-playing art
  std::vector<lv_obj_t *> tab_btns_;
  std::vector<lv_obj_t *> tab_lbls_;
  std::vector<lv_obj_t *> group_grids_;
  std::vector<std::vector<Tile>> group_tiles_;
  // Launcher tabs: one (scrollable) list container per group (nullptr for non-launcher
  // groups), with a render signature to skip rebuilds when nothing changed.
  std::vector<lv_obj_t *> launcher_grids_;
  std::vector<long> launcher_sig_;
  int active_group_{0};  // group currently shown on the dashboard (for the control sheet)

  // Control sheet (more-info modal on the top layer). Built once; content rebuilt per open.
  lv_obj_t *sheet_scrim_{nullptr};
  lv_obj_t *sheet_root_{nullptr};
  lv_obj_t *sheet_body_{nullptr};       // content container (cleaned/rebuilt per card)
  lv_obj_t *sheet_title_{nullptr};
  lv_obj_t *sheet_icon_{nullptr};
  const Card *sheet_card_{nullptr};     // card the sheet is showing (null = closed)
  // Live widgets refreshed from the card state:
  lv_obj_t *sheet_value_lbl_{nullptr};  // climate target temp
  lv_obj_t *sheet_sub_lbl_{nullptr};    // climate current / media artist
  lv_obj_t *sheet_pp_icon_{nullptr};    // media play/pause glyph
  lv_obj_t *sheet_slider_{nullptr};     // volume / position / brightness
  lv_obj_t *sheet_modes_[4]{nullptr, nullptr, nullptr, nullptr};  // climate mode buttons

  // --- Voice assistant overlay (top layer, built once; restyled per state) ---
  void build_voice_();               // create the overlay tree on lv_layer_top()
  void voice_apply_(VoiceState st);  // restyle in place for a state (no create/destroy)
  lv_obj_t *voice_root_{nullptr};    // full-screen container
  lv_obj_t *voice_orb_{nullptr};     // central orb
  lv_obj_t *voice_orb_icon_{nullptr};
  lv_obj_t *voice_wave_{nullptr};    // listening level bars (container)
  std::vector<lv_obj_t *> voice_bars_;
  lv_obj_t *voice_status_{nullptr};  // status line ("Je vous écoute…")
  lv_obj_t *voice_sub_{nullptr};     // secondary line (error detail / timer name)
  lv_obj_t *voice_ttsbar_{nullptr};  // responding TTS progress bar
  lv_obj_t *voice_actions_{nullptr}; // buttons row (error / muted / ringing)
  lv_obj_t *voice_hint_{nullptr};    // bottom hint
  lv_obj_t *voice_close_{nullptr};   // top-right close
  VoiceState voice_state_{VoiceState::HIDDEN};

  // Header mic chip + active-timer pill (D1001 dashboard header).
  lv_obj_t *mic_chip_{nullptr};
  lv_obj_t *mic_chip_icon_{nullptr};
  lv_obj_t *timer_pill_{nullptr};
  lv_obj_t *timer_pill_lbl_{nullptr};
  MicState mic_state_{MicState::ARMED};

  // Timers screen (top-layer overlay): progress ring for the soonest + list of all actives.
  void build_timers_();
  void refresh_timers_();  // populate ring + list from timers_data_
  std::vector<TimerInfo> timers_data_;
  lv_obj_t *timers_scr_{nullptr};
  lv_obj_t *timers_ring_{nullptr};
  lv_obj_t *timers_ring_time_{nullptr};
  lv_obj_t *timers_ring_name_{nullptr};
  lv_obj_t *timers_list_{nullptr};

  bool pull_armed_{false};  // pull-to-refresh: armed once the list is over-scrolled at the top

  // Episode-thumbnail recycling (current detail list): every row has an image, but only the
  // rows on screen get one of the limited thumb slots — reassigned as the list scrolls.
  std::vector<lv_obj_t *> ep_row_;                          // row container per episode
  std::vector<lv_obj_t *> ep_img_;                          // thumbnail image per episode
  std::vector<std::string> ep_url_;                         // signed thumb URL per episode ("" = none)
  std::vector<int> ep_slot_;                                // thumb-slot index per episode (-1 = none)
  std::vector<online_image::OnlineImage *> ep_thumb_slots_; // episode slots (thumb_slots[1..])
  std::vector<int> thumb_owner_;                            // episode idx per episode slot (-1 = free)
  lv_obj_t *ep_list_{nullptr};                              // the scrollable list (viewport)
  uint32_t ep_assign_ms_{0};                                // throttle scroll-driven reassign
  void assign_episode_thumbs_();

  // Launch-confirmation toast (floats on the top layer, auto-hides via a timer). Two lines:
  // title + "lecture sur <speaker>" (the two halves are passed newline-separated).
  lv_obj_t *toast_{nullptr};
  lv_obj_t *toast_lbl_{nullptr};
  lv_obj_t *toast_sub_{nullptr};
  lv_timer_t *toast_timer_{nullptr};
  // Cover/thumbnail slots (flattened across launcher groups, both pools) -> currently bound
  // LVGL image (or null). cover_url_list_ = the URL a slot has actually FINISHED loading
  // (committed in on_cover_ready_); cover_pending_url_ = the URL last requested. A reload is
  // skipped only when the *loaded* URL matches — so a rebuild during an in-flight download
  // re-queues it instead of wrongly assuming it's already there.
  std::vector<online_image::OnlineImage *> cover_slot_list_;
  std::vector<lv_obj_t *> cover_widget_list_;
  std::vector<std::string> cover_url_list_;
  std::vector<std::string> cover_pending_url_;
  // Serialized cover download queue (current grid), advanced as each finishes/errors.
  std::vector<online_image::OnlineImage *> cover_queue_;
  size_t cover_load_idx_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
