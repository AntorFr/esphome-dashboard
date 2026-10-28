#pragma once
// Diagnostic build only: a dose knob for the redraw load. N ghosts / pumpkins /
// bats bounce across the whole screen on the top layer, each on its own periods, so the redrawn
// areas keep moving and the load grows with N.
#include <vector>
#include "lvgl.h"

static std::vector<lv_obj_t *> g_stress_objs;

static void stress_x_cb(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *) o, v); }
static void stress_y_cb(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *) o, v); }

static void stress_anim_(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_reverse_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
}

inline void dsi_stress_set(int n, const lv_font_t *big, const lv_font_t *small) {
  while ((int) g_stress_objs.size() > n) {
    lv_obj_t *o = g_stress_objs.back();
    g_stress_objs.pop_back();
    lv_anim_delete(o, nullptr);
    lv_obj_delete(o);
  }
  // MDI ghost, pumpkin (160 px font), bat (96 px font).
  static const char *const glyphs[3] = {"\xF3\xB0\x8A\xA0", "\xF3\xB0\xAE\xA3", "\xF3\xB0\xAD\x9F"};
  static const uint32_t colors[3] = {0xE8E0F0, 0xFF8A1F, 0x9B6BD6};
  const int32_t w = lv_display_get_horizontal_resolution(nullptr) - 160;
  const int32_t h = lv_display_get_vertical_resolution(nullptr) - 160;
  while ((int) g_stress_objs.size() < n) {
    int i = (int) g_stress_objs.size();
    int kind = i % 3;
    lv_obj_t *o = lv_label_create(lv_layer_top());
    lv_label_set_text(o, glyphs[kind]);
    lv_obj_set_style_text_font(o, kind == 2 ? small : big, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(colors[kind]), 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    bool flip = (i / 3) % 2;
    stress_anim_(o, stress_x_cb, flip ? w : 0, flip ? 0 : w, 1700 + 230 * i);
    stress_anim_(o, stress_y_cb, flip ? 0 : h, flip ? h : 0, 2300 + 170 * i);
    g_stress_objs.push_back(o);
  }
}
