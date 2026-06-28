#pragma once
// COUCHE 2 — Renderer LVGL (API C directe, cf. ADR-0003). Partagé Dial + D1001.
// Respecte ADR-0005 : objets construits une fois (build), update-in-place au render,
// événements LVGL natifs sur de vrais lv_obj.
#include <lvgl.h>
#include <string>
#include <vector>
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

 protected:
  lv_obj_t *make_screen_();
  lv_obj_t *make_flex_container_(lv_obj_t *parent);
  lv_obj_t *make_button_(lv_obj_t *parent, const char *text, InputEvent event, int index);
  void set_focus_(std::vector<lv_obj_t *> &buttons, int focused);
  const Card *current_card_(const ViewModel &vm) const;

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
};

}  // namespace ha_dashboard
}  // namespace esphome
