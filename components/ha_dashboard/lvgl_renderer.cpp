#include "lvgl_renderer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "esphome/core/log.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard.lvgl";

// Palette (cf. docs/color-system.md). Tokens HA à brancher plus tard.
static constexpr uint32_t COL_BG = 0x0E0E12;
static constexpr uint32_t COL_TILE = 0x2C2C38;  // lighter than the mockup so tiles stand out from the bg
static constexpr uint32_t COL_TEXT = 0xFFFFFF;
static constexpr uint32_t COL_MUTED = 0x8A8A92;
static constexpr uint32_t COL_ACCENT = 0xFFB020;

// Donnée transportée par chaque widget natif vers son event sémantique.
struct CbData {
  LvglRenderer *renderer;
  InputEvent event;
  int index;
};
// Conserve les CbData vivantes pour toute la durée de vie (pas de free).
static std::vector<CbData *> g_cbdata;

static void btn_event_cb(lv_event_t *e) {
  auto *d = static_cast<CbData *>(lv_event_get_user_data(e));
  if (d != nullptr && d->renderer != nullptr)
    d->renderer->emit(d->event, d->index);
}

void LvglRenderer::set_profile(const std::string &profile) {
  this->profile_ = profile;
  this->round_ = (profile != "reterminal_d1001");
}

void LvglRenderer::set_clock(const char *time_str, const char *date_str) {
  if (this->time_lbl_ != nullptr)
    lv_label_set_text(this->time_lbl_, time_str);
  if (this->date_lbl_ != nullptr)
    lv_label_set_text(this->date_lbl_, date_str);
  if (this->idle_time_lbl_ != nullptr)
    lv_label_set_text(this->idle_time_lbl_, time_str);
  if (this->idle_date_lbl_ != nullptr)
    lv_label_set_text(this->idle_date_lbl_, date_str);
}

void LvglRenderer::set_weather(const char *icon_glyph, const char *temp_str, const char *cond_str) {
  if (this->weather_icon_lbl_ != nullptr && icon_glyph != nullptr)
    lv_label_set_text(this->weather_icon_lbl_, icon_glyph);
  if (this->weather_temp_lbl_ != nullptr)
    lv_label_set_text(this->weather_temp_lbl_, temp_str);
  if (this->weather_cond_lbl_ != nullptr)
    lv_label_set_text(this->weather_cond_lbl_, cond_str);
}

void LvglRenderer::set_text_font_(lv_obj_t *obj, font::Font *f, const lv_font_t *fallback) {
  if (f != nullptr)
    lv_obj_set_style_text_font(obj, f->get_lv_font(), 0);
  else if (fallback != nullptr)
    lv_obj_set_style_text_font(obj, fallback, 0);
}

lv_obj_t *LvglRenderer::make_screen_() {
  lv_obj_t *scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  return scr;
}

// ---- Geometry helpers (prototype repère: 0° = top, clockwise) ----
// Convert a prototype angle (0° = top, CW) to an LVGL angle (0° = 3 o'clock, CW).
static int to_lv_angle(float proto_deg) {
  int a = (int) lroundf(proto_deg) - 90;
  a %= 360;
  return a < 0 ? a + 360 : a;
}
// Distance of an angle to the top (0°), folded to [0,180].
static float ang_to_top(float deg) {
  float a = fmodf(fmodf(deg, 360.0f) + 360.0f, 360.0f);
  return a > 180.0f ? 360.0f - a : a;
}
static uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  int r = (int) lroundf(ar + (br - ar) * t);
  int g = (int) lroundf(ag + (bg - ag) * t);
  int bl = (int) lroundf(ab + (bb - ab) * t);
  return ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) bl;
}

// Representative color for a group's menu icon: first card with an explicit color, else a
// palette by index (so the radial menu stays colorful even without per-card overrides).
static uint32_t group_repr_color(const std::vector<Group> *model, int i) {
  static const uint32_t PALETTE[] = {0xFFB020, 0x4F8CFF, 0xA06CFF, 0x3DD68C, 0xFF6B6B, 0x03A964, 0x577EFF};
  if (model != nullptr && i >= 0 && i < (int) model->size()) {
    for (const auto &c : (*model)[i].cards)
      if (c.has_color)
        return c.color;
  }
  return PALETTE[((i % 7) + 7) % 7];
}

void LvglRenderer::build(const std::vector<Group> &groups) {
  this->model_ = &groups;
  if (lv_display_t *d = lv_display_get_default()) {
    this->w_ = lv_display_get_horizontal_resolution(d);
    this->h_ = lv_display_get_vertical_resolution(d);
  }
  // --- Idle / standby screen: centered clock (time + date). Tap to wake. ---
  this->idle_scr_ = this->make_screen_();
  lv_obj_add_flag(this->idle_scr_, LV_OBJ_FLAG_CLICKABLE);
  {
    lv_obj_t *col = lv_obj_create(this->idle_scr_);
    lv_obj_set_size(col, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);  // let the tap reach the screen
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    this->idle_time_lbl_ = lv_label_create(col);
    lv_label_set_text(this->idle_time_lbl_, "--:--");
    lv_obj_set_style_text_color(this->idle_time_lbl_, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(this->idle_time_lbl_, this->font_large_, &lv_font_montserrat_48);

    this->idle_date_lbl_ = lv_label_create(col);
    lv_label_set_text(this->idle_date_lbl_, "");
    lv_obj_set_style_text_color(this->idle_date_lbl_, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(this->idle_date_lbl_, this->font_medium_, &lv_font_montserrat_28);

    auto *d = new CbData{this, InputEvent::WAKE, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->idle_scr_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // Dial radial launcher + card carousel (round profile). D1001 uses the merged dashboard.
  if (this->round_) {
    this->build_menu_(groups);
    this->build_card_view_();
  } else {
    this->build_dashboard_(groups);
  }
}

// ===================== Dial radial launcher (group picker) =====================

void LvglRenderer::menu_circle_pos_(int i, float &x, float &y) const {
  int ng = (this->model_ != nullptr) ? (int) this->model_->size() : 1;
  if (ng < 1)
    ng = 1;
  float ang = i * (360.0f / ng);  // fixed: index 0 at top, clockwise
  float a = ang * (float) M_PI / 180.0f;
  float ring = 0.34f * this->w_;  // maquette: r=82 on a 240 screen
  x = this->w_ / 2.0f + ring * sinf(a);
  y = this->h_ / 2.0f - ring * cosf(a);
}

void LvglRenderer::layout_menu_(int focus) {
  int ng = (int) this->menu_circles_.size();
  if (ng < 1)
    return;
  float base_r = 0.105f * this->w_;  // maquette: r=25 on a 240 screen
  for (int i = 0; i < ng; i++) {
    float x, y;
    this->menu_circle_pos_(i, x, y);
    float ff = (i == focus) ? 1.0f : 0.0f;  // highlight only the focused icon (no motion)
    float r = base_r * (1.0f + 0.16f * ff);
    uint32_t gc = group_repr_color(this->model_, i);  // per-group color
    lv_obj_t *c = this->menu_circles_[i];
    lv_obj_set_size(c, (int) lroundf(2 * r), (int) lroundf(2 * r));
    lv_obj_set_pos(c, (int) lroundf(x - r), (int) lroundf(y - r));
    // Focused: tinted fill + full colored ring + bigger. Unfocused: dark fill, faint ring.
    lv_obj_set_style_bg_color(c, lv_color_hex(ff > 0 ? lerp_color(0x1E1E26, gc, 0.30f) : 0x1E1E26), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(gc), 0);
    lv_obj_set_style_border_opa(c, ff > 0 ? LV_OPA_COVER : (lv_opa_t) 90, 0);
    // Icon colored everywhere (dimmed when unfocused, full color when focused).
    lv_obj_set_style_text_color(this->menu_icons_[i],
                                lv_color_hex(ff > 0 ? gc : lerp_color(0x6A6A72, gc, 0.45f)), 0);
  }
  if (this->menu_name_lbl_ != nullptr && this->model_ != nullptr && focus >= 0 &&
      focus < (int) this->model_->size())
    lv_label_set_text(this->menu_name_lbl_, (*this->model_)[focus].name.c_str());
}

void LvglRenderer::menu_gesture_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev == nullptr)
    return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  float scale = self->w_ / 240.0f;

  if (code == LV_EVENT_PRESSED) {
    self->m_down_ = true;
    self->m_sx_ = p.x;
    self->m_sy_ = p.y;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (!self->m_down_)
      return;
    self->m_down_ = false;
    // Tap (or short drag) enters the nearest icon. Same layout math as the rendering (ADR-0005).
    int ng = (int) self->menu_circles_.size();
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < ng; i++) {
      float x, y;
      self->menu_circle_pos_(i, x, y);
      float d = std::hypot(p.x - x, p.y - y);
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    if (best >= 0 && bd < 38 * scale)
      self->emit(InputEvent::SELECT, best);
  }
}

// Map a config icon name to a Material Design Icons glyph (UTF-8). These codepoints must be
// present in the ha_font_icons glyph subset (cf. packages/fonts/montserrat_fr.yaml).
static const char *group_icon_glyph(const std::string &name) {
  if (name == "settings" || name == "cog")
    return "\U000F0493";
  if (name == "power")
    return "\U000F0425";
  if (name == "speaker" || name == "music")
    return "\U000F04C3";
  if (name == "thermostat" || name == "climate")
    return "\U000F0393";
  if (name == "gate" || name == "cover")
    return "\U000F0299";
  if (name == "sofa")
    return "\U000F04B9";
  if (name == "kitchen")
    return "\U000F0A70";
  if (name == "light" || name == "lightbulb")
    return "\U000F0335";
  return "\U000F02D7";  // help-circle (fallback)
}

void LvglRenderer::build_menu_(const std::vector<Group> &groups) {
  this->menu_scr_ = this->make_screen_();
  lv_obj_add_flag(this->menu_scr_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->menu_scr_, LV_OBJ_FLAG_SCROLLABLE);

  this->menu_circles_.clear();
  this->menu_icons_.clear();
  for (size_t i = 0; i < groups.size(); i++) {
    lv_obj_t *c = lv_obj_create(this->menu_scr_);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);  // taps handled by the screen gesture cb
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ic = lv_label_create(c);
    if (this->font_icons_ != nullptr) {
      // MDI glyph (same icon set as Home Assistant), subset into ha_font_icons.
      lv_label_set_text(ic, group_icon_glyph(groups[i].icon));
      lv_obj_set_style_text_font(ic, this->font_icons_->get_lv_font(), 0);
    } else {
      // Fallback when no icon font is configured: the group's initial.
      char init[2] = {(char) (groups[i].name.empty() ? '?' : toupper(groups[i].name[0])), 0};
      lv_label_set_text(ic, init);
      this->set_text_font_(ic, this->font_medium_, &lv_font_montserrat_20);
    }
    lv_obj_center(ic);

    this->menu_circles_.push_back(c);
    this->menu_icons_.push_back(ic);
  }

  // Center: focused group name only (device count dropped to leave more room).
  this->menu_name_lbl_ = lv_label_create(this->menu_scr_);
  lv_label_set_text(this->menu_name_lbl_, "");
  lv_obj_set_style_text_color(this->menu_name_lbl_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->menu_name_lbl_, this->font_medium_, &lv_font_montserrat_28);
  lv_obj_center(this->menu_name_lbl_);

  lv_obj_add_event_cb(this->menu_scr_, menu_gesture_cb, LV_EVENT_ALL, this);
  this->layout_menu_(0);
}

// ===================== Dial card carousel =====================

void LvglRenderer::set_carousel_translate_(float dx) {
  int v = (int) lroundf(dx);
  if (this->card_arc_ != nullptr)
    lv_obj_set_style_translate_x(this->card_arc_, v, 0);
  if (this->card_center_ != nullptr)
    lv_obj_set_style_translate_x(this->card_center_, v, 0);
}

void LvglRenderer::render_return_(float p) {
  this->gest_ret_p_ = p;
  bool show = p > 0.002f;
  if (this->ret_l_ != nullptr && this->ret_r_ != nullptr) {
    if (show) {
      lv_obj_remove_flag(this->ret_r_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(this->ret_l_, LV_OBJ_FLAG_HIDDEN);
      lv_arc_set_bg_angles(this->ret_r_, to_lv_angle(150 - 150 * p), to_lv_angle(150));
      lv_arc_set_bg_angles(this->ret_l_, to_lv_angle(210), to_lv_angle(210 + 150 * p));
    } else {
      lv_obj_add_flag(this->ret_r_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(this->ret_l_, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (this->card_center_ != nullptr)
    lv_obj_set_style_opa(this->card_center_, (lv_opa_t) lroundf((1.0f - 0.45f * p) * 255), 0);
  if (this->card_dots_ != nullptr)
    lv_obj_set_style_opa(this->card_dots_, (lv_opa_t) lroundf((1.0f - p) * 255), 0);
  if (this->card_hint_ != nullptr)
    lv_obj_set_style_opa(this->card_hint_, show ? LV_OPA_TRANSP : LV_OPA_70, 0);
}

void LvglRenderer::carousel_gesture_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_active();
  if (indev == nullptr)
    return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  float scale = self->w_ / 240.0f;

  if (code == LV_EVENT_PRESSED) {
    self->gest_drag_ = true;
    self->gest_mode_ = 0;
    self->gest_ret_p_ = 0;
    self->gest_sx_ = p.x;
    self->gest_sy_ = p.y;
    self->gest_dx_ = self->gest_dy_ = 0;
    self->gest_peak_dx_ = self->gest_peak_dy_ = 0;
    self->gest_committed_ = false;
  } else if (code == LV_EVENT_PRESSING) {
    if (!self->gest_drag_)
      return;
    int dx = p.x - self->gest_sx_, dy = p.y - self->gest_sy_;
    self->gest_dx_ = dx;
    self->gest_dy_ = dy;
    // Track the peak signed displacement: on a round screen the finger lifts at the edge
    // (PRESS_LOST) and the last sample can be small — the peak reflects the real intent.
    if (std::abs(dx) > std::abs(self->gest_peak_dx_))
      self->gest_peak_dx_ = dx;
    if (std::abs(dy) > std::abs(self->gest_peak_dy_))
      self->gest_peak_dy_ = dy;
    if (self->gest_mode_ == 0 && std::hypot((float) dx, (float) dy) > 8 * scale)
      self->gest_mode_ = (std::abs(dx) > std::abs(dy)) ? 1 : 2;
    if (self->gest_mode_ == 1) {
      // Fire as soon as the swipe is clear enough (no need to release) — feels much snappier.
      if (!self->gest_committed_ && std::abs(dx) >= 28 * scale) {
        self->emit(dx < 0 ? InputEvent::FOCUS_NEXT : InputEvent::FOCUS_PREV, -1);
        self->gest_committed_ = true;
        self->set_carousel_translate_(0);
      } else if (!self->gest_committed_) {
        float t = dx < -60 * scale ? -60 * scale : (dx > 60 * scale ? 60 * scale : dx);
        self->set_carousel_translate_(t);
      }
    } else if (self->gest_mode_ == 2) {
      float pr = dy < 0 ? std::min(1.0f, -dy / (110.0f * scale)) : 0.0f;
      self->render_return_(pr);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (!self->gest_drag_)
      return;
    self->gest_drag_ = false;
    int dx = self->gest_peak_dx_, dy = self->gest_peak_dy_;  // decide on the peak, not the last sample
    if (self->gest_mode_ == 1) {                             // horizontal -> change card
      self->set_carousel_translate_(0);
      if (!self->gest_committed_) {  // slow/short drag that didn't trip the mid-gesture commit
        if (dx <= -16 * scale)
          self->emit(InputEvent::FOCUS_NEXT, -1);
        else if (dx >= 16 * scale)
          self->emit(InputEvent::FOCUS_PREV, -1);
      }
    } else if (self->gest_mode_ == 2) {  // vertical -> back to menu if far enough up
      self->render_return_(0);
      if (dy <= -55 * scale || self->gest_ret_p_ >= 0.9f)
        self->emit(InputEvent::BACK, -1);
    } else {  // no lock -> tap
      self->emit(InputEvent::TOGGLE, -1);
    }
  }
}

void LvglRenderer::build_card_view_() {
  this->card_scr_ = this->make_screen_();
  lv_obj_add_flag(this->card_scr_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->card_scr_, LV_OBJ_FLAG_SCROLLABLE);

  // Open-bottom value arc (gap ~60° centered at the bottom).
  this->card_arc_ = lv_arc_create(this->card_scr_);
  lv_obj_set_size(this->card_arc_, (int) (0.875f * this->w_), (int) (0.875f * this->h_));  // ~210/240
  lv_obj_center(this->card_arc_);
  lv_arc_set_rotation(this->card_arc_, 0);
  lv_arc_set_bg_angles(this->card_arc_, 120, 60);  // clockwise 120->60 => 60° gap at bottom
  lv_arc_set_range(this->card_arc_, 0, 1000);
  lv_arc_set_value(this->card_arc_, 0);
  lv_obj_set_style_arc_width(this->card_arc_, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(this->card_arc_, lv_color_hex(0x232330), LV_PART_MAIN);
  lv_obj_set_style_arc_width(this->card_arc_, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(this->card_arc_, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  lv_obj_remove_style(this->card_arc_, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(this->card_arc_, LV_OBJ_FLAG_CLICKABLE);

  // Return gauge: two neutral arcs (hidden until a slide-up gesture).
  for (lv_obj_t **slot : {&this->ret_r_, &this->ret_l_}) {
    lv_obj_t *a = lv_arc_create(this->card_scr_);
    lv_obj_set_size(a, (int) (0.883f * this->w_), (int) (0.883f * this->h_));  // ~212 (r=106)
    lv_obj_center(a);
    lv_arc_set_rotation(a, 0);
    lv_arc_set_bg_angles(a, 0, 0);
    lv_obj_set_style_arc_width(a, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(0xEDEDF0), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    *slot = a;
  }

  // Centered icon + value + name.
  this->card_center_ = lv_obj_create(this->card_scr_);
  lv_obj_set_size(this->card_center_, (int) (0.625f * this->w_), LV_SIZE_CONTENT);
  lv_obj_center(this->card_center_);
  lv_obj_set_style_bg_opa(this->card_center_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(this->card_center_, 0, 0);
  lv_obj_set_style_pad_all(this->card_center_, 0, 0);
  lv_obj_set_style_pad_row(this->card_center_, 2, 0);
  lv_obj_set_flex_flow(this->card_center_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(this->card_center_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(this->card_center_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->card_center_, LV_OBJ_FLAG_SCROLLABLE);

  this->card_icon_ = lv_label_create(this->card_center_);
  lv_label_set_text(this->card_icon_, LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(this->card_icon_, &lv_font_montserrat_28, 0);

  this->card_value_ = lv_label_create(this->card_center_);
  lv_label_set_text(this->card_value_, "");
  lv_obj_set_style_text_color(this->card_value_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->card_value_, this->font_large_, &lv_font_montserrat_40);

  this->card_title_ = lv_label_create(this->card_center_);
  lv_label_set_text(this->card_title_, "");
  lv_obj_set_style_text_color(this->card_title_, lv_color_hex(COL_ACCENT), 0);
  this->set_text_font_(this->card_title_, this->font_small_, &lv_font_montserrat_20);

  // Pagination dots (rebuilt per group in render_card_view_).
  this->card_dots_ = lv_obj_create(this->card_scr_);
  lv_obj_set_size(this->card_dots_, lv_pct(80), LV_SIZE_CONTENT);
  lv_obj_align(this->card_dots_, LV_ALIGN_BOTTOM_MID, 0, (int) (-0.108f * this->h_));  // bottom ~26
  lv_obj_set_style_bg_opa(this->card_dots_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(this->card_dots_, 0, 0);
  lv_obj_set_style_pad_all(this->card_dots_, 0, 0);
  lv_obj_set_style_pad_column(this->card_dots_, 7, 0);
  lv_obj_set_flex_flow(this->card_dots_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->card_dots_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(this->card_dots_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->card_dots_, LV_OBJ_FLAG_SCROLLABLE);

  // "^ menu" hint at the very bottom.
  this->card_hint_ = lv_label_create(this->card_scr_);
  lv_label_set_text(this->card_hint_, LV_SYMBOL_UP " menu");
  lv_obj_set_style_text_color(this->card_hint_, lv_color_hex(0x6E6E78), 0);
  lv_obj_set_style_opa(this->card_hint_, LV_OPA_70, 0);
  lv_obj_align(this->card_hint_, LV_ALIGN_BOTTOM_MID, 0, (int) (-0.0375f * this->h_));  // bottom ~9
  lv_obj_remove_flag(this->card_hint_, LV_OBJ_FLAG_CLICKABLE);

  // Media transport buttons (prev/next): real clickable objects, so they consume the tap
  // and never fight the screen gesture engine. Shown only for media cards (render_card_view_).
  struct {
    lv_obj_t **slot;
    const char *sym;
    InputEvent ev;
    lv_align_t align;
    int dx;
  } mbtns[] = {
      {&this->card_prev_btn_, LV_SYMBOL_PREV, InputEvent::MEDIA_PREV, LV_ALIGN_LEFT_MID, (int) (0.085f * this->w_)},
      {&this->card_next_btn_, LV_SYMBOL_NEXT, InputEvent::MEDIA_NEXT, LV_ALIGN_RIGHT_MID, (int) (-0.085f * this->w_)},
  };
  int btn_sz = (int) (0.18f * this->w_);
  for (auto &m : mbtns) {
    lv_obj_t *b = lv_button_create(this->card_scr_);
    lv_obj_set_size(b, btn_sz, btn_sz);
    lv_obj_align(b, m.align, m.dx, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *lbl = lv_label_create(b);
    lv_label_set_text(lbl, m.sym);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
    auto *d = new CbData{this, m.ev, -1};  // NOLINT(cppcoreguidelines-owning-memory)
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
    *m.slot = b;
  }

  lv_obj_add_event_cb(this->card_scr_, carousel_gesture_cb, LV_EVENT_ALL, this);
}

// Accent color for a card: explicit override, else per-type default (cf. color-system.md).
static uint32_t accent_for(const Card &c) {
  if (c.has_color)
    return c.color;
  return c.type == CardType::SWITCH ? 0x3DD68C : 0xFFB020;
}

static void format_state(const Card &c, char *buf, size_t n) {
  if (!c.available()) {
    std::snprintf(buf, n, "indispo");
    return;
  }
  switch (c.type) {
    case CardType::COVER:
      std::snprintf(buf, n, "%d%%", (int) lroundf(c.value() * 100));
      break;
    case CardType::MEDIA_PLAYER:
      std::snprintf(buf, n, "%s", c.is_on() ? "Lecture" : "Pause");
      break;
    case CardType::CLIMATE:
      std::snprintf(buf, n, "%.1f°", c.climate != nullptr ? c.climate->target_temperature : 0.0f);
      break;
    case CardType::LIGHT:
      if (c.is_on())
        std::snprintf(buf, n, "%d%%", (int) lroundf(c.value() * 100));
      else
        std::snprintf(buf, n, "Éteint");
      break;
    case CardType::SWITCH:
    default:
      std::snprintf(buf, n, "%s", c.is_on() ? "Allumé" : "Éteint");
      break;
  }
}

// LVGL built-in symbol per card type (real domain icons need an icon font — TODO).
static const char *icon_for(const Card &c) {
  switch (c.type) {
    case CardType::SWITCH:
      return LV_SYMBOL_POWER;
    case CardType::COVER:
      return LV_SYMBOL_UP;
    case CardType::MEDIA_PLAYER:
      return LV_SYMBOL_AUDIO;
    case CardType::CLIMATE:
      return LV_SYMBOL_GPS;
    case CardType::LIGHT:
    default:
      return LV_SYMBOL_CHARGE;
  }
}

void LvglRenderer::build_dashboard_(const std::vector<Group> &groups) {
  this->dashboard_scr_ = this->make_screen_();
  lv_obj_t *root = lv_obj_create(this->dashboard_scr_);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_hor(root, 20, 0);
  lv_obj_set_style_pad_top(root, 22, 0);
  lv_obj_set_style_pad_bottom(root, 0, 0);
  lv_obj_set_style_pad_row(root, 16, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // --- Header: [time / date]  ........  [weather] ---
  this->dash_header_ = lv_obj_create(root);
  lv_obj_set_width(this->dash_header_, lv_pct(100));
  lv_obj_set_height(this->dash_header_, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(this->dash_header_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(this->dash_header_, 0, 0);
  lv_obj_set_style_pad_all(this->dash_header_, 0, 0);
  lv_obj_set_flex_flow(this->dash_header_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->dash_header_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_clear_flag(this->dash_header_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *left = lv_obj_create(this->dash_header_);
  lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(left, 0, 0);
  lv_obj_set_style_pad_all(left, 0, 0);
  lv_obj_set_style_pad_row(left, 4, 0);
  lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
  this->time_lbl_ = lv_label_create(left);
  lv_label_set_text(this->time_lbl_, "--:--");
  lv_obj_set_style_text_color(this->time_lbl_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->time_lbl_, this->font_large_, &lv_font_montserrat_48);
  this->date_lbl_ = lv_label_create(left);
  lv_label_set_text(this->date_lbl_, "");
  lv_obj_set_style_text_color(this->date_lbl_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->date_lbl_, this->font_small_, &lv_font_montserrat_20);

  // Right: weather (temperature + condition), bound to a HA weather entity.
  lv_obj_t *weather = lv_obj_create(this->dash_header_);
  lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(weather, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(weather, 0, 0);
  lv_obj_set_style_pad_all(weather, 0, 0);
  lv_obj_set_style_pad_row(weather, 2, 0);
  lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(weather, LV_OBJ_FLAG_SCROLLABLE);
  this->weather_icon_lbl_ = lv_label_create(weather);
  lv_label_set_text(this->weather_icon_lbl_, "");
  lv_obj_set_style_text_color(this->weather_icon_lbl_, lv_color_hex(COL_TEXT), 0);
  if (this->font_weather_ != nullptr)
    lv_obj_set_style_text_font(this->weather_icon_lbl_, this->font_weather_->get_lv_font(), 0);
  this->weather_temp_lbl_ = lv_label_create(weather);
  lv_label_set_text(this->weather_temp_lbl_, "");
  lv_obj_set_style_text_color(this->weather_temp_lbl_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->weather_temp_lbl_, this->font_medium_, &lv_font_montserrat_28);
  this->weather_cond_lbl_ = lv_label_create(weather);
  lv_label_set_text(this->weather_cond_lbl_, "");
  lv_obj_set_style_text_color(this->weather_cond_lbl_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->weather_cond_lbl_, this->font_small_, &lv_font_montserrat_20);

  // --- Tabs (underline style, bottom rule like the mockup) ---
  lv_obj_t *tabs = lv_obj_create(root);
  lv_obj_set_width(tabs, lv_pct(100));
  lv_obj_set_height(tabs, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(tabs, 0, 0);
  lv_obj_set_style_pad_column(tabs, 20, 0);
  lv_obj_set_style_border_width(tabs, 1, 0);
  lv_obj_set_style_border_side(tabs, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(tabs, lv_color_hex(0x232330), 0);
  lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);  // avoid taps being eaten as scroll
  this->tab_btns_.clear();
  this->tab_lbls_.clear();
  for (size_t gi = 0; gi < groups.size(); gi++) {
    lv_obj_t *tab = lv_obj_create(tabs);
    lv_obj_set_size(tab, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(tab, 0, 0);
    lv_obj_set_style_pad_hor(tab, 2, 0);
    lv_obj_set_style_pad_ver(tab, 8, 0);
    lv_obj_set_style_border_color(tab, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_border_side(tab, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *lbl = lv_label_create(tab);
    lv_label_set_text(lbl, groups[gi].name.c_str());
    this->set_text_font_(lbl, this->font_medium_, &lv_font_montserrat_28);
    auto *d = new CbData{this, InputEvent::SELECT_GROUP, (int) gi};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(tab, btn_event_cb, LV_EVENT_CLICKED, d);
    this->tab_btns_.push_back(tab);
    this->tab_lbls_.push_back(lbl);
  }

  // --- Content: one tile grid per group (only the active one shown) ---
  lv_obj_t *content = lv_obj_create(root);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  this->group_grids_.clear();
  this->group_tiles_.clear();
  for (size_t gi = 0; gi < groups.size(); gi++) {
    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_top(grid, 16, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_HIDDEN);

    std::vector<Tile> tiles;
    for (size_t ci = 0; ci < groups[gi].cards.size(); ci++) {
      const Card &card = groups[gi].cards[ci];
      lv_obj_t *tile = lv_button_create(grid);
      lv_obj_set_size(tile, lv_pct(48), 170);
      lv_obj_set_style_bg_color(tile, lv_color_hex(COL_TILE), 0);
      lv_obj_set_style_shadow_width(tile, 0, 0);
      lv_obj_set_style_radius(tile, 16, 0);
      lv_obj_set_style_pad_all(tile, 16, 0);
      lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

      Tile t;
      t.root = tile;

      lv_obj_t *toprow = lv_obj_create(tile);
      lv_obj_set_width(toprow, lv_pct(100));
      lv_obj_set_height(toprow, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(toprow, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(toprow, 0, 0);
      lv_obj_set_style_pad_all(toprow, 0, 0);
      lv_obj_set_flex_flow(toprow, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(toprow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_clear_flag(toprow, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(toprow, LV_OBJ_FLAG_EVENT_BUBBLE);  // let taps reach the tile button

      t.icon = lv_label_create(toprow);
      lv_label_set_text(t.icon, icon_for(card));
      lv_obj_set_style_text_font(t.icon, &lv_font_montserrat_48, 0);  // LVGL symbol font

      t.state = lv_label_create(toprow);
      char sbuf[24];
      format_state(card, sbuf, sizeof(sbuf));
      lv_label_set_text(t.state, sbuf);
      this->set_text_font_(t.state, this->font_small_, &lv_font_montserrat_20);

      lv_obj_t *name = lv_label_create(tile);
      lv_label_set_text(name, card.name.c_str());
      lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
      this->set_text_font_(name, this->font_medium_, &lv_font_montserrat_28);

      // Value bar for cards that expose a value (cover/media/climate/light).
      if (card.has_value()) {
        t.bar = lv_bar_create(tile);
        lv_obj_set_width(t.bar, lv_pct(100));
        lv_obj_set_height(t.bar, 6);
        lv_obj_set_style_radius(t.bar, 3, 0);
        lv_obj_set_style_bg_color(t.bar, lv_color_hex(0x26262F), LV_PART_MAIN);
        lv_obj_set_style_bg_color(t.bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
        lv_bar_set_range(t.bar, 0, 100);
        lv_bar_set_value(t.bar, 0, LV_ANIM_OFF);
      }

      auto *d = new CbData{this, InputEvent::TOGGLE, (int) ci};
      g_cbdata.push_back(d);
      lv_obj_add_event_cb(tile, btn_event_cb, LV_EVENT_CLICKED, d);
      tiles.push_back(t);
    }
    this->group_grids_.push_back(grid);
    this->group_tiles_.push_back(tiles);
  }
}

void LvglRenderer::render_dashboard_(const ViewModel &vm) {
  if (vm.groups == nullptr)
    return;
  int active = vm.group_index;

  // Tabs: active = white label + accent underline; others muted, no underline.
  for (size_t i = 0; i < this->tab_btns_.size(); i++) {
    bool on = (int) i == active;
    lv_obj_set_style_border_width(this->tab_btns_[i], on ? 2 : 0, 0);
    if (i < this->tab_lbls_.size())
      lv_obj_set_style_text_color(this->tab_lbls_[i], lv_color_hex(on ? COL_TEXT : COL_MUTED), 0);
  }

  // Grids: show only the active group's tiles, refresh their state in place.
  for (size_t gi = 0; gi < this->group_grids_.size(); gi++) {
    if ((int) gi == active) {
      lv_obj_clear_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);
      const Group &g = (*vm.groups)[gi];
      for (size_t ci = 0; ci < this->group_tiles_[gi].size() && ci < g.cards.size(); ci++) {
        const Card &c = g.cards[ci];
        const Tile &t = this->group_tiles_[gi][ci];
        uint32_t col = c.is_on() ? accent_for(c) : COL_MUTED;
        lv_obj_set_style_text_color(t.icon, lv_color_hex(col), 0);
        char sbuf[24];
        format_state(c, sbuf, sizeof(sbuf));
        lv_label_set_text(t.state, sbuf);
        lv_obj_set_style_text_color(t.state, lv_color_hex(col), 0);
        if (t.bar != nullptr) {
          lv_bar_set_value(t.bar, (int) lroundf(c.value() * 100), LV_ANIM_OFF);
          lv_obj_set_style_bg_color(t.bar, lv_color_hex(c.is_on() ? accent_for(c) : COL_MUTED), LV_PART_INDICATOR);
        }
      }
    } else {
      lv_obj_add_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (this->dashboard_scr_ != nullptr)
    lv_screen_load(this->dashboard_scr_);
}

const Card *LvglRenderer::current_card_(const ViewModel &vm) const {
  if (vm.groups == nullptr || vm.group_index < 0 || vm.group_index >= (int) vm.groups->size())
    return nullptr;
  const Group &g = (*vm.groups)[vm.group_index];
  if (vm.card_index < 0 || vm.card_index >= (int) g.cards.size())
    return nullptr;
  return &g.cards[vm.card_index];
}

void LvglRenderer::set_idle_hour(int hour) {
  (void) hour;  // time-of-day tint dropped: the mockup wants a plain black idle screen
  if (this->idle_scr_ == nullptr)
    return;
  lv_obj_set_style_bg_color(this->idle_scr_, lv_color_hex(0x0A0A0C), 0);
  lv_obj_set_style_bg_opa(this->idle_scr_, LV_OPA_COVER, 0);
}

void LvglRenderer::render_card_view_(const ViewModel &vm) {
  const Card *c = this->current_card_(vm);
  // Reset gesture-driven transforms (in case we arrive mid-animation).
  this->set_carousel_translate_(0);
  this->render_return_(0);
  if (c != nullptr && this->card_title_ != nullptr && this->card_value_ != nullptr) {
    uint32_t col = c->is_on() ? accent_for(*c) : COL_MUTED;
    lv_label_set_text(this->card_title_, c->name.c_str());
    char buf[24];
    format_state(*c, buf, sizeof(buf));
    lv_label_set_text(this->card_value_, buf);
    if (this->card_icon_ != nullptr) {
      lv_label_set_text(this->card_icon_, icon_for(*c));
      lv_obj_set_style_text_color(this->card_icon_, lv_color_hex(col), 0);
    }
    if (this->card_arc_ != nullptr) {
      lv_arc_set_value(this->card_arc_, (int) lroundf(c->display_value() * 1000));  // optimistic preview
      lv_obj_set_style_arc_color(this->card_arc_, lv_color_hex(c->is_on() ? accent_for(*c) : 0x232330),
                                 LV_PART_INDICATOR);
    }
  }
  // Media transport buttons only on media cards.
  bool is_media = (c != nullptr && c->type == CardType::MEDIA_PLAYER);
  for (lv_obj_t *b : {this->card_prev_btn_, this->card_next_btn_}) {
    if (b == nullptr)
      continue;
    if (is_media)
      lv_obj_remove_flag(b, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
  }
  // Pagination dots for the current group.
  if (this->card_dots_ != nullptr && vm.groups != nullptr && vm.group_index >= 0 &&
      vm.group_index < (int) vm.groups->size()) {
    int n = (int) (*vm.groups)[vm.group_index].cards.size();
    lv_obj_clean(this->card_dots_);
    float scale = this->w_ / 240.0f;
    for (int i = 0; i < n; i++) {
      bool active = (i == vm.card_index);
      lv_obj_t *dot = lv_obj_create(this->card_dots_);
      lv_obj_set_size(dot, (int) lroundf((active ? 16 : 6) * scale), (int) lroundf(6 * scale));
      lv_obj_set_style_radius(dot, (int) lroundf(3 * scale), 0);
      lv_obj_set_style_border_width(dot, 0, 0);
      lv_obj_set_style_bg_color(dot, lv_color_hex(active ? 0xFFFFFF : 0x3A3A44), 0);
      lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
  if (this->card_scr_ != nullptr)
    lv_screen_load(this->card_scr_);
}

void LvglRenderer::render(const ViewModel &vm) {
  switch (vm.state) {
    case NavState::IDLE:
      if (this->idle_scr_ != nullptr)
        lv_screen_load(this->idle_scr_);
      break;

    case NavState::DASHBOARD:
      this->render_dashboard_(vm);
      break;

    case NavState::MENU:
      this->layout_menu_(vm.group_index);
      if (this->menu_scr_ != nullptr)
        lv_screen_load(this->menu_scr_);
      break;

    case NavState::GROUP:
    case NavState::CARD:
      this->render_card_view_(vm);
      break;
  }
}

}  // namespace ha_dashboard
}  // namespace esphome
