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

  void set_font_small(font::Font *f) { this->font_small_ = f; }
  void set_font_medium(font::Font *f) { this->font_medium_ = f; }
  void set_font_large(font::Font *f) { this->font_large_ = f; }

 protected:
  lv_obj_t *make_screen_();
  lv_obj_t *make_flex_container_(lv_obj_t *parent);
  lv_obj_t *make_button_(lv_obj_t *parent, const char *text, InputEvent event, int index);
  void set_focus_(std::vector<lv_obj_t *> &buttons, int focused);
  const Card *current_card_(const ViewModel &vm) const;

  // D1001 dashboard (tabs + tile grid).
  void build_dashboard_(const std::vector<Group> &groups);
  void render_dashboard_(const ViewModel &vm);

  // Apply an esphome font if provided, else the built-in LVGL fallback.
  void set_text_font_(lv_obj_t *obj, font::Font *f, const lv_font_t *fallback);

  // Per-tile widgets we update in place on state change.
  struct Tile {
    lv_obj_t *root{nullptr};
    lv_obj_t *icon{nullptr};
    lv_obj_t *state{nullptr};
  };

  EventHandler handler_;
  std::string profile_{"dial"};
  bool round_{true};

  lv_obj_t *idle_scr_{nullptr};
  lv_obj_t *menu_scr_{nullptr};
  lv_obj_t *card_scr_{nullptr};
  std::vector<lv_obj_t *> group_scrs_;

  std::vector<lv_obj_t *> group_btns_;               // boutons du menu
  std::vector<std::vector<lv_obj_t *>> card_btns_;   // boutons par groupe

  lv_obj_t *card_title_{nullptr};
  lv_obj_t *card_value_{nullptr};

  font::Font *font_small_{nullptr};
  font::Font *font_medium_{nullptr};
  font::Font *font_large_{nullptr};

  lv_obj_t *dashboard_scr_{nullptr};
  lv_obj_t *dash_header_{nullptr};
  lv_obj_t *time_lbl_{nullptr};
  lv_obj_t *date_lbl_{nullptr};
  std::vector<lv_obj_t *> tab_btns_;
  std::vector<lv_obj_t *> tab_lbls_;
  std::vector<lv_obj_t *> group_grids_;
  std::vector<std::vector<Tile>> group_tiles_;
};

}  // namespace ha_dashboard
}  // namespace esphome
