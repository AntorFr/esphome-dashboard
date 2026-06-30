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
  // Button-hold return gauge (driven from the component while the encoder button is held).
  void set_return_progress(float p) { this->render_return_(p); }

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
  // Music Library launcher tab: list of favourites (rebuilt only when the module's
  // status/count/level changes), with covers via online_image slots.
  void render_launcher_(int gi, const Group &g);
  // Register on-download finished/error callbacks for every cover slot (once, at build time).
  void register_cover_slots_(const std::vector<Group> &groups);
  // Refresh the LVGL image bound to a cover slot once its download/decode completes.
  void on_cover_ready_(online_image::OnlineImage *slot);
  void on_cover_error_(online_image::OnlineImage *slot);
  // Covers download one at a time (serialized) to avoid exhausting TLS/socket memory.
  void advance_cover_(online_image::OnlineImage *finished_slot);

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
  lv_obj_t *card_prev_btn_{nullptr};  // media: previous track (shown only for media cards)
  lv_obj_t *card_next_btn_{nullptr};  // media: next track

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

  lv_obj_t *dashboard_scr_{nullptr};
  lv_obj_t *dash_header_{nullptr};
  lv_obj_t *time_lbl_{nullptr};
  lv_obj_t *date_lbl_{nullptr};
  lv_obj_t *weather_icon_lbl_{nullptr};
  lv_obj_t *weather_temp_lbl_{nullptr};
  lv_obj_t *weather_cond_lbl_{nullptr};
  std::vector<lv_obj_t *> tab_btns_;
  std::vector<lv_obj_t *> tab_lbls_;
  std::vector<lv_obj_t *> group_grids_;
  std::vector<std::vector<Tile>> group_tiles_;
  // Launcher tabs: one (scrollable) list container per group (nullptr for non-launcher
  // groups), with a render signature to skip rebuilds when nothing changed.
  std::vector<lv_obj_t *> launcher_grids_;
  std::vector<long> launcher_sig_;
  // Cover slots (flattened across launcher groups) -> currently bound LVGL image (or null).
  std::vector<online_image::OnlineImage *> cover_slot_list_;
  std::vector<lv_obj_t *> cover_widget_list_;
  // Serialized cover download queue (current grid), advanced as each finishes/errors.
  std::vector<online_image::OnlineImage *> cover_queue_;
  size_t cover_load_idx_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
