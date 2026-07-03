#include "lvgl_renderer.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "esphome/core/log.h"
#include "launcher_module.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include "esphome/components/online_image/online_image.h"
#endif

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

// MDI glyphs (embedded in ha_font_icons) for the now-playing volume button. The built-in
// LVGL symbol set has no slashed speaker, so we use Material Design Icons here.
static const char *const ICON_VOL_ON = "\U000F057E";   // mdi volume-high
static const char *const ICON_VOL_OFF = "\U000F0E08";  // mdi volume-variant-off (slashed)

// Donnée transportée par chaque widget natif vers son event sémantique.
struct CbData {
  LvglRenderer *renderer;
  InputEvent event;
  int index;
  std::string toast;  // optional launch-confirmation toast text (empty = no toast)
};
// Conserve les CbData vivantes pour toute la durée de vie (pas de free).
static std::vector<CbData *> g_cbdata;

static uint32_t accent_for(const Card &c);    // defined below
static const char *icon_for(const Card &c);   // defined below (LVGL symbol fallback)

// MDI glyphs (embedded in ha_font_icons_lg) for classic-card icons + climate modes.
static const char *const MDI_POWER = "\U000F0425";
static const char *const MDI_LIGHT = "\U000F0335";
static const char *const MDI_SHUTTER = "\U000F111C";       // cover shutter — closed
static const char *const MDI_SHUTTER_OPEN = "\U000F111E";  // cover shutter — open
static const char *const MDI_GARAGE = "\U000F06D9";        // cover garage — closed
static const char *const MDI_GARAGE_OPEN = "\U000F06DA";   // cover garage — open
static const char *const MDI_GATE = "\U000F0299";          // cover gate — closed
static const char *const MDI_GATE_OPEN = "\U000F116A";     // cover gate — open
static const char *const MDI_SPEAKER = "\U000F04C3";
static const char *const MDI_THERMOSTAT = "\U000F0393";
static const char *const MDI_FIRE = "\U000F0238";
static const char *const MDI_SNOW = "\U000F0717";
// Voice-assistant glyphs (ha_font_voice for the orb; ha_font_icons_lg for the header chip/pill).
static const char *const MDI_MIC = "\U000F036C";
static const char *const MDI_MIC_OFF = "\U000F036D";
static const char *const MDI_VOL = "\U000F057E";
static const char *const MDI_ALERT = "\U000F0028";
static const char *const MDI_CLOUD_OFF = "\U000F0164";
static const char *const MDI_TIMER = "\U000F051B";
static const char *const MDI_TIMER_ALERT = "\U000F1ACD";
// Voice palette.
static constexpr uint32_t COL_VOICE = 0x35C6FF;
static constexpr uint32_t COL_VOICE2 = 0x7A5CFF;
static constexpr uint32_t COL_ERR = 0xFF5A5A;
static constexpr uint32_t COL_TIMER = 0xFFB020;

// Effective cover presentation: YAML `cover_kind` override, else the HA device_class.
static int cover_kind_of(const Card &c) {
  const std::string &k = c.cover_kind;
  if (k == "garage")
    return (int) CoverKind::GARAGE;
  if (k == "gate")
    return (int) CoverKind::GATE;
  if (k == "shutter")
    return (int) CoverKind::SHUTTER;
  if (c.cover != nullptr) {  // auto from device_class
    char buf[esphome::MAX_DEVICE_CLASS_LENGTH] = {0};
    c.cover->get_device_class_to(buf);
    std::string dc(buf);
    if (dc == "garage")
      return (int) CoverKind::GARAGE;
    if (dc == "gate")
      return (int) CoverKind::GATE;
  }
  return (int) CoverKind::SHUTTER;
}

// The MDI glyph for a card's type icon (cover depends on its kind).
static const char *card_mdi_glyph(const Card &c) {
  switch (c.type) {
    case CardType::SWITCH:
      return MDI_POWER;
    case CardType::LIGHT:
      return MDI_LIGHT;
    case CardType::COVER: {
      // Icon reflects the position: fully closed vs (partially) open, per cover kind.
      const bool open = c.is_on();  // cover: position > 0
      switch (cover_kind_of(c)) {
        case (int) CoverKind::GATE:
          return open ? MDI_GATE_OPEN : MDI_GATE;
        case (int) CoverKind::GARAGE:
          return open ? MDI_GARAGE_OPEN : MDI_GARAGE;
        default:
          return open ? MDI_SHUTTER_OPEN : MDI_SHUTTER;
      }
    }
    case CardType::MEDIA_PLAYER:
      return MDI_SPEAKER;
    case CardType::CLIMATE:
    default:
      return MDI_THERMOSTAT;
  }
}

// The quick-button glyph for a cover: the TARGET action (arrow), not the current state.
// Matches primary_action_ (closes when mostly open, else opens); direction follows the kind.
static const char *cover_action_glyph(const Card &c) {
  const bool will_close = c.value() > 0.5f;
  const bool gate = cover_kind_of(c) == (int) CoverKind::GATE;
  if (gate)
    return will_close ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT;
  return will_close ? LV_SYMBOL_DOWN : LV_SYMBOL_UP;
}

static void btn_event_cb(lv_event_t *e) {
  auto *d = static_cast<CbData *>(lv_event_get_user_data(e));
  if (d == nullptr || d->renderer == nullptr)
    return;
  d->renderer->emit(d->event, d->index);
  if (!d->toast.empty())
    d->renderer->show_toast(d->toast);
  // The control sheet is a renderer overlay: show/hide it here (the controller only tracks
  // which card the sheet acts on).
  if (d->event == InputEvent::OPEN_SHEET)
    d->renderer->show_sheet_(d->index);
  else if (d->event == InputEvent::SHEET_CLOSE)
    d->renderer->hide_sheet_();
  else if (d->event == InputEvent::OPEN_TIMERS)
    d->renderer->show_timers_();
}

// Control-sheet slider (volume / position / brightness): emit the value once released.
static void sheet_slider_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
  if (self != nullptr && slider != nullptr)
    self->emit(InputEvent::SHEET_SET_VALUE, (int) lv_slider_get_value(slider));
}

// Format seconds as "m:ss" (or "h:mm:ss" past an hour) for the now-playing progress times.
static std::string fmt_time(int total_s) {
  if (total_s < 0)
    total_s = 0;
  int h = total_s / 3600, m = (total_s % 3600) / 60, s = total_s % 60;
  char buf[16];
  if (h > 0)
    snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
  else
    snprintf(buf, sizeof(buf), "%d:%02d", m, s);
  return buf;
}

// Volume slider: emit the absolute value once the finger lifts (RELEASED only fires on user
// interaction, not on programmatic lv_slider_set_value -> no feedback loop with render).
static void np_vol_slider_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
  if (self != nullptr && slider != nullptr)
    self->emit(InputEvent::NP_SET_VOLUME, (int) lv_slider_get_value(slider));
}

// Pull-to-refresh on the launcher list: arm while the user over-scrolls past the top, fire
// once the scroll settles. scroll_y is negative when the content is pulled down beyond the top.
static constexpr int PULL_REFRESH_PX = 90;
static void launcher_scroll_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  auto *grid = static_cast<lv_obj_t *>(lv_event_get_target(e));
  if (self == nullptr || grid == nullptr)
    return;
  self->on_launcher_scroll(grid, lv_event_get_code(e) == LV_EVENT_SCROLL_END);
}
// Auto-hide the toast when its timer fires (one-shot per show: pause after hiding).
static void toast_hide_cb(lv_timer_t *t) {
  auto *self = static_cast<LvglRenderer *>(lv_timer_get_user_data(t));
  if (self != nullptr)
    self->hide_toast_();
}

void LvglRenderer::hide_toast_() {
  if (this->toast_ != nullptr)
    lv_obj_add_flag(this->toast_, LV_OBJ_FLAG_HIDDEN);
  if (this->toast_timer_ != nullptr)
    lv_timer_pause(this->toast_timer_);
}

void LvglRenderer::show_toast(const std::string &text) {
  // Build the toast once, on the top layer so it floats above whatever screen is loaded.
  if (this->toast_ == nullptr) {
    this->toast_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(this->toast_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(this->toast_, 760, 0);
    lv_obj_align(this->toast_, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_set_style_bg_color(this->toast_, lv_color_hex(0x23232C), 0);
    lv_obj_set_style_bg_opa(this->toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(this->toast_, 0, 0);
    lv_obj_set_style_radius(this->toast_, 22, 0);
    lv_obj_set_style_pad_hor(this->toast_, 32, 0);
    lv_obj_set_style_pad_ver(this->toast_, 26, 0);
    lv_obj_set_style_pad_column(this->toast_, 22, 0);
    lv_obj_set_style_shadow_width(this->toast_, 30, 0);
    lv_obj_set_style_shadow_opa(this->toast_, LV_OPA_50, 0);
    lv_obj_set_flex_flow(this->toast_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(this->toast_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(this->toast_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(this->toast_, LV_OBJ_FLAG_IGNORE_LAYOUT);  // keep our manual align

    // Big green music-note icon (built-in symbol font has one: LV_SYMBOL_AUDIO).
    lv_obj_t *ic = lv_label_create(this->toast_);
    lv_label_set_text(ic, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(0x3DD68C), 0);

    // Text column: title (large) + "lecture sur <speaker>" (muted).
    lv_obj_t *col = lv_obj_create(this->toast_);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    this->toast_lbl_ = lv_label_create(col);
    lv_obj_set_style_text_color(this->toast_lbl_, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(this->toast_lbl_, this->font_medium_, &lv_font_montserrat_28);
    this->toast_sub_ = lv_label_create(col);
    lv_obj_set_style_text_color(this->toast_sub_, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(this->toast_sub_, this->font_small_, &lv_font_montserrat_20);
  }

  // Split "title\nsubtitle"; hide the subtitle line when there's no speaker.
  const size_t nl = text.find('\n');
  lv_label_set_text(this->toast_lbl_, text.substr(0, nl).c_str());
  if (nl == std::string::npos) {
    lv_obj_add_flag(this->toast_sub_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(this->toast_sub_, text.substr(nl + 1).c_str());
    lv_obj_clear_flag(this->toast_sub_, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(this->toast_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->toast_);
  if (this->toast_timer_ == nullptr) {
    this->toast_timer_ = lv_timer_create(toast_hide_cb, 3800, this);
  } else {
    lv_timer_reset(this->toast_timer_);
    lv_timer_resume(this->toast_timer_);
  }
}

// A transparent, gap-14 horizontal row inside the sheet body.
static lv_obj_t *sheet_row_(lv_obj_t *parent) {
  lv_obj_t *r = lv_obj_create(parent);
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_style_pad_column(r, 18, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}

void LvglRenderer::show_sheet_(int card_index) {
  if (this->model_ == nullptr || this->active_group_ < 0 || this->active_group_ >= (int) this->model_->size())
    return;
  const Group &g = (*this->model_)[this->active_group_];
  if (card_index < 0 || card_index >= (int) g.cards.size())
    return;
  const Card &c = g.cards[card_index];

  if (this->sheet_root_ == nullptr) {
    this->sheet_scrim_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(this->sheet_scrim_);
    lv_obj_set_size(this->sheet_scrim_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(this->sheet_scrim_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(this->sheet_scrim_, LV_OPA_50, 0);
    lv_obj_add_flag(this->sheet_scrim_, LV_OBJ_FLAG_CLICKABLE);
    auto *sc = new CbData{this, InputEvent::SHEET_CLOSE, -1};
    g_cbdata.push_back(sc);
    lv_obj_add_event_cb(this->sheet_scrim_, btn_event_cb, LV_EVENT_CLICKED, sc);

    this->sheet_root_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(this->sheet_root_, lv_pct(100), lv_pct(84));
    lv_obj_align(this->sheet_root_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(this->sheet_root_, lv_color_hex(0x16161C), 0);
    lv_obj_set_style_border_width(this->sheet_root_, 0, 0);
    lv_obj_set_style_radius(this->sheet_root_, 28, 0);
    lv_obj_set_style_pad_all(this->sheet_root_, 28, 0);
    lv_obj_set_style_pad_row(this->sheet_root_, 12, 0);
    lv_obj_set_flex_flow(this->sheet_root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(this->sheet_root_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(this->sheet_root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_obj_create(this->sheet_root_);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_pad_column(hdr, 14, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    this->sheet_icon_ = lv_label_create(hdr);
    this->sheet_title_ = lv_label_create(hdr);
    lv_obj_set_flex_grow(this->sheet_title_, 1);
    lv_obj_set_style_text_color(this->sheet_title_, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(this->sheet_title_, this->font_large_, &lv_font_montserrat_48);
    lv_obj_t *close = lv_button_create(hdr);
    lv_obj_set_size(close, 64, 64);
    lv_obj_set_style_radius(close, 32, 0);
    lv_obj_set_style_bg_color(close, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_center(cl);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(COL_MUTED), 0);
    auto *dc = new CbData{this, InputEvent::SHEET_CLOSE, -1};
    g_cbdata.push_back(dc);
    lv_obj_add_event_cb(close, btn_event_cb, LV_EVENT_CLICKED, dc);

    this->sheet_body_ = lv_obj_create(this->sheet_root_);
    lv_obj_set_width(this->sheet_body_, lv_pct(100));
    lv_obj_set_flex_grow(this->sheet_body_, 1);
    lv_obj_set_style_bg_opa(this->sheet_body_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(this->sheet_body_, 0, 0);
    lv_obj_set_style_pad_all(this->sheet_body_, 0, 0);
    lv_obj_set_style_pad_row(this->sheet_body_, 24, 0);
    lv_obj_set_flex_flow(this->sheet_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(this->sheet_body_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(this->sheet_body_, LV_OBJ_FLAG_SCROLLABLE);
  }

  this->sheet_card_ = &c;
  this->set_card_icon_(this->sheet_icon_, c, accent_for(c));
  lv_obj_set_style_text_font(this->sheet_icon_, this->font_icons_lg_ != nullptr
                                                   ? this->font_icons_lg_->get_lv_font()
                                                   : &lv_font_montserrat_28, 0);
  lv_label_set_text(this->sheet_title_, c.name.c_str());
  this->build_sheet_content_(c);
  this->refresh_sheet_();
  lv_obj_clear_flag(this->sheet_scrim_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(this->sheet_root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->sheet_scrim_);
  lv_obj_move_foreground(this->sheet_root_);
}

void LvglRenderer::hide_sheet_() {
  this->sheet_card_ = nullptr;
  if (this->sheet_scrim_ != nullptr)
    lv_obj_add_flag(this->sheet_scrim_, LV_OBJ_FLAG_HIDDEN);
  if (this->sheet_root_ != nullptr)
    lv_obj_add_flag(this->sheet_root_, LV_OBJ_FLAG_HIDDEN);
}

void LvglRenderer::build_sheet_content_(const Card &c) {
  lv_obj_clean(this->sheet_body_);
  this->sheet_value_lbl_ = this->sheet_sub_lbl_ = this->sheet_pp_icon_ = this->sheet_slider_ = nullptr;
  for (int i = 0; i < 4; i++)
    this->sheet_modes_[i] = nullptr;
  const uint32_t accent = accent_for(c);

  // A round icon/text button in the sheet. Returns the glyph label (for later refresh).
  auto sbtn = [this](lv_obj_t *parent, const char *glyph, const lv_font_t *font, bool primary,
                     InputEvent ev) -> lv_obj_t * {
    lv_obj_t *b = lv_button_create(parent);
    int sz = primary ? 118 : 92;
    lv_obj_set_size(b, sz, sz);
    lv_obj_set_style_radius(b, sz / 2, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? 0x3DD68C : COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, glyph);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0x06281A : COL_TEXT), 0);
    auto *d = new CbData{this, ev, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
    return l;
  };
  auto make_slider = [this, accent](uint32_t icon_col, const char *icon, const lv_font_t *ifont) {
    lv_obj_t *row = sheet_row_(this->sheet_body_);
    lv_obj_set_width(row, lv_pct(90));
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, icon);
    lv_obj_set_style_text_font(ic, ifont, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(icon_col), 0);
    this->sheet_slider_ = lv_slider_create(row);
    lv_obj_set_flex_grow(this->sheet_slider_, 1);
    lv_obj_set_height(this->sheet_slider_, 14);
    lv_slider_set_range(this->sheet_slider_, 0, 100);
    lv_obj_set_style_bg_color(this->sheet_slider_, lv_color_hex(0x3A3A44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(this->sheet_slider_, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(this->sheet_slider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(this->sheet_slider_, 10, LV_PART_KNOB);
    lv_obj_add_event_cb(this->sheet_slider_, sheet_slider_cb, LV_EVENT_RELEASED, this);
  };

  if (c.type == CardType::CLIMATE) {
    lv_obj_t *row = sheet_row_(this->sheet_body_);
    lv_obj_set_style_pad_column(row, 34, 0);
    sbtn(row, LV_SYMBOL_MINUS, &lv_font_montserrat_48, false, InputEvent::SHEET_TEMP_DOWN);
    this->sheet_value_lbl_ = lv_label_create(row);
    lv_obj_set_style_text_color(this->sheet_value_lbl_, lv_color_hex(accent), 0);
    this->set_text_font_(this->sheet_value_lbl_, this->font_large_, &lv_font_montserrat_48);
    sbtn(row, LV_SYMBOL_PLUS, &lv_font_montserrat_48, false, InputEvent::SHEET_TEMP_UP);
    this->sheet_sub_lbl_ = lv_label_create(this->sheet_body_);
    lv_obj_set_style_text_color(this->sheet_sub_lbl_, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(this->sheet_sub_lbl_, this->font_medium_, &lv_font_montserrat_28);
    // modes: off / heat / cool / auto
    lv_obj_t *mr = sheet_row_(this->sheet_body_);
    const lv_font_t *mdi = this->font_icons_lg_ != nullptr ? this->font_icons_lg_->get_lv_font()
                                                           : &lv_font_montserrat_48;
    struct M { const char *g; const lv_font_t *f; };
    M ms[4] = {{LV_SYMBOL_POWER, &lv_font_montserrat_48}, {MDI_FIRE, mdi}, {MDI_SNOW, mdi},
               {LV_SYMBOL_REFRESH, &lv_font_montserrat_48}};
    for (int i = 0; i < 4; i++) {
      lv_obj_t *b = lv_button_create(mr);
      lv_obj_set_size(b, 96, 96);
      lv_obj_set_style_radius(b, 18, 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(COL_TILE), 0);
      lv_obj_set_style_shadow_width(b, 0, 0);
      lv_obj_set_style_border_width(b, 3, 0);
      lv_obj_set_style_border_color(b, lv_color_hex(COL_TILE), 0);
      lv_obj_t *l = lv_label_create(b);
      lv_label_set_text(l, ms[i].g);
      lv_obj_center(l);
      lv_obj_set_style_text_font(l, ms[i].f, 0);
      lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
      auto *d = new CbData{this, InputEvent::SHEET_MODE, i};
      g_cbdata.push_back(d);
      lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
      this->sheet_modes_[i] = b;
    }
  } else if (c.type == CardType::MEDIA_PLAYER) {
    this->sheet_sub_lbl_ = lv_label_create(this->sheet_body_);
    lv_obj_set_style_text_color(this->sheet_sub_lbl_, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(this->sheet_sub_lbl_, this->font_medium_, &lv_font_montserrat_28);
    lv_obj_t *tr = sheet_row_(this->sheet_body_);
    lv_obj_set_style_pad_column(tr, 30, 0);
    sbtn(tr, LV_SYMBOL_PREV, &lv_font_montserrat_48, false, InputEvent::SHEET_MEDIA_PREV);
    this->sheet_pp_icon_ = sbtn(tr, LV_SYMBOL_PAUSE, &lv_font_montserrat_48, true, InputEvent::SHEET_PLAY_PAUSE);
    sbtn(tr, LV_SYMBOL_NEXT, &lv_font_montserrat_48, false, InputEvent::SHEET_MEDIA_NEXT);
    make_slider(COL_MUTED, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_28);
  } else if (c.type == CardType::COVER) {
    const bool gate = cover_kind_of(c) == (int) CoverKind::GATE;
    lv_obj_t *tr = sheet_row_(this->sheet_body_);
    lv_obj_set_style_pad_column(tr, 26, 0);
    sbtn(tr, gate ? LV_SYMBOL_LEFT : LV_SYMBOL_UP, &lv_font_montserrat_48, false, InputEvent::SHEET_COVER_OPEN);
    sbtn(tr, LV_SYMBOL_STOP, &lv_font_montserrat_28, false, InputEvent::SHEET_COVER_STOP);
    sbtn(tr, gate ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN, &lv_font_montserrat_48, false, InputEvent::SHEET_COVER_CLOSE);
    make_slider(accent, LV_SYMBOL_SETTINGS, &lv_font_montserrat_28);
  } else {  // LIGHT (brightness)
    make_slider(accent, LV_SYMBOL_CHARGE, &lv_font_montserrat_28);
  }
}

void LvglRenderer::refresh_sheet_() {
  if (this->sheet_card_ == nullptr)
    return;
  const Card &c = *this->sheet_card_;
  char buf[24];
  if (c.type == CardType::CLIMATE && this->sheet_value_lbl_ != nullptr) {
    float target = c.climate != nullptr ? c.climate->target_temperature : 0.0f;
    float cur = c.climate != nullptr ? c.climate->current_temperature : 0.0f;
    std::snprintf(buf, sizeof(buf), "%.1f°", target);
    lv_label_set_text(this->sheet_value_lbl_, buf);
    std::snprintf(buf, sizeof(buf), "actuel %.1f°", cur);
    lv_label_set_text(this->sheet_sub_lbl_, buf);
    int cur_mode = 0;  // 0 off / 1 heat / 2 cool / 3 auto
    if (c.climate != nullptr) {
      switch (c.climate->mode) {
        case climate::CLIMATE_MODE_HEAT: cur_mode = 1; break;
        case climate::CLIMATE_MODE_COOL: cur_mode = 2; break;
        case climate::CLIMATE_MODE_HEAT_COOL:
        case climate::CLIMATE_MODE_AUTO: cur_mode = 3; break;
        default: cur_mode = 0; break;
      }
    }
    for (int i = 0; i < 4; i++)
      if (this->sheet_modes_[i] != nullptr)
        lv_obj_set_style_border_color(this->sheet_modes_[i],
                                      lv_color_hex(i == cur_mode ? accent_for(c) : COL_TILE), 0);
  } else if (c.type == CardType::MEDIA_PLAYER) {
    if (this->sheet_pp_icon_ != nullptr)
      lv_label_set_text(this->sheet_pp_icon_, c.is_on() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (this->sheet_sub_lbl_ != nullptr && c.media != nullptr) {
      const std::string &t = c.media->get_media_title();
      const std::string &a = c.media->get_media_artist();
      lv_label_set_text(this->sheet_sub_lbl_, (a.empty() ? t : (t.empty() ? a : t + " — " + a)).c_str());
    }
    if (this->sheet_slider_ != nullptr)
      lv_slider_set_value(this->sheet_slider_, (int) lroundf(c.value() * 100), LV_ANIM_OFF);
  } else if (this->sheet_slider_ != nullptr) {  // cover / light
    lv_slider_set_value(this->sheet_slider_, (int) lroundf(c.value() * 100), LV_ANIM_OFF);
  }
}

// ===================== Voice assistant overlay (top layer) =====================
// Built once on lv_layer_top(); voice_apply_ restyles it in place per state (ADR-0005 — no
// create/destroy on transitions). Independent of NavState: it floats over any screen.

// A round action button for the overlay (icon + optional label under it). Emits `ev`.
static lv_obj_t *voice_btn_(LvglRenderer *self, lv_obj_t *parent, const char *sym, const char *label,
                            bool primary, uint32_t accent, InputEvent ev) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_style_radius(b, 22, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(primary ? accent : COL_TILE), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_hor(b, 34, 0);
  lv_obj_set_style_pad_ver(b, 20, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(b, 12, 0);
  lv_obj_t *ic = lv_label_create(b);
  lv_label_set_text(ic, sym);
  lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(ic, lv_color_hex(primary ? 0x04222E : COL_TEXT), 0);
  if (label != nullptr && label[0] != '\0') {
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0x04222E : COL_TEXT), 0);
  }
  auto *d = new CbData{self, ev, -1};
  g_cbdata.push_back(d);
  lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
  return b;
}

void LvglRenderer::build_voice_() {
  if (this->voice_root_ != nullptr)
    return;
  this->voice_root_ = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(this->voice_root_);
  lv_obj_set_size(this->voice_root_, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(this->voice_root_, lv_color_hex(0x0B0B10), 0);
  lv_obj_set_style_bg_opa(this->voice_root_, LV_OPA_COVER, 0);  // opaque overlay
  lv_obj_set_flex_flow(this->voice_root_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(this->voice_root_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(this->voice_root_, 40, 0);
  lv_obj_set_style_pad_row(this->voice_root_, 34, 0);
  lv_obj_clear_flag(this->voice_root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(this->voice_root_, LV_OBJ_FLAG_HIDDEN);

  // Top-right close (cancel).
  this->voice_close_ = lv_button_create(this->voice_root_);
  lv_obj_set_size(this->voice_close_, 74, 74);
  lv_obj_set_style_radius(this->voice_close_, 37, 0);
  lv_obj_set_style_bg_color(this->voice_close_, lv_color_hex(0x20202A), 0);
  lv_obj_set_style_shadow_width(this->voice_close_, 0, 0);
  lv_obj_align(this->voice_close_, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_flag(this->voice_close_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_t *cx = lv_label_create(this->voice_close_);
  lv_label_set_text(cx, LV_SYMBOL_CLOSE);
  lv_obj_center(cx);
  lv_obj_set_style_text_color(cx, lv_color_hex(COL_MUTED), 0);
  {
    auto *d = new CbData{this, InputEvent::VOICE_CANCEL, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->voice_close_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // Central orb (circle + large glyph).
  this->voice_orb_ = lv_obj_create(this->voice_root_);
  lv_obj_remove_style_all(this->voice_orb_);
  lv_obj_set_size(this->voice_orb_, 300, 300);
  lv_obj_set_style_radius(this->voice_orb_, 150, 0);
  lv_obj_set_style_bg_color(this->voice_orb_, lv_color_hex(COL_VOICE), 0);
  lv_obj_set_style_bg_grad_color(this->voice_orb_, lv_color_hex(COL_VOICE2), 0);
  lv_obj_set_style_bg_grad_dir(this->voice_orb_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(this->voice_orb_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(this->voice_orb_, LV_OBJ_FLAG_SCROLLABLE);
  this->voice_orb_icon_ = lv_label_create(this->voice_orb_);
  lv_label_set_text(this->voice_orb_icon_, MDI_MIC);
  lv_obj_center(this->voice_orb_icon_);
  lv_obj_set_style_text_color(this->voice_orb_icon_, lv_color_hex(0xFFFFFF), 0);
  if (this->font_voice_ != nullptr)
    lv_obj_set_style_text_font(this->voice_orb_icon_, this->font_voice_->get_lv_font(), 0);

  // Listening level bars.
  this->voice_wave_ = lv_obj_create(this->voice_root_);
  lv_obj_remove_style_all(this->voice_wave_);
  lv_obj_set_size(this->voice_wave_, LV_SIZE_CONTENT, 70);
  lv_obj_set_flex_flow(this->voice_wave_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->voice_wave_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(this->voice_wave_, 9, 0);
  lv_obj_clear_flag(this->voice_wave_, LV_OBJ_FLAG_SCROLLABLE);
  const int heights[] = {26, 52, 70, 40, 60, 30, 54, 22};
  for (int i = 0; i < 8; i++) {
    lv_obj_t *bar = lv_obj_create(this->voice_wave_);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 10, heights[i]);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(i % 2 ? COL_VOICE2 : COL_VOICE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    this->voice_bars_.push_back(bar);
  }

  // Status + secondary line.
  this->voice_status_ = lv_label_create(this->voice_root_);
  lv_label_set_text(this->voice_status_, "");
  lv_obj_set_style_text_color(this->voice_status_, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_align(this->voice_status_, LV_TEXT_ALIGN_CENTER, 0);
  this->set_text_font_(this->voice_status_, this->font_large_, &lv_font_montserrat_48);

  this->voice_sub_ = lv_label_create(this->voice_root_);
  lv_label_set_text(this->voice_sub_, "");
  lv_obj_set_width(this->voice_sub_, lv_pct(80));
  lv_obj_set_style_text_color(this->voice_sub_, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_align(this->voice_sub_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(this->voice_sub_, LV_LABEL_LONG_WRAP);
  this->set_text_font_(this->voice_sub_, this->font_medium_, &lv_font_montserrat_28);

  // Responding TTS progress bar.
  this->voice_ttsbar_ = lv_bar_create(this->voice_root_);
  lv_obj_set_size(this->voice_ttsbar_, 420, 12);
  lv_obj_set_style_radius(this->voice_ttsbar_, 6, 0);
  lv_obj_set_style_bg_color(this->voice_ttsbar_, lv_color_hex(0x20202A), LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->voice_ttsbar_, lv_color_hex(COL_VOICE), LV_PART_INDICATOR);
  lv_bar_set_range(this->voice_ttsbar_, 0, 100);
  lv_bar_set_value(this->voice_ttsbar_, 40, LV_ANIM_OFF);

  // Actions row (error / muted / ringing).
  this->voice_actions_ = lv_obj_create(this->voice_root_);
  lv_obj_remove_style_all(this->voice_actions_);
  lv_obj_set_size(this->voice_actions_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(this->voice_actions_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->voice_actions_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(this->voice_actions_, 20, 0);
  lv_obj_clear_flag(this->voice_actions_, LV_OBJ_FLAG_SCROLLABLE);
  // Prebuilt buttons; shown/hidden per state.
  voice_btn_(this, this->voice_actions_, LV_SYMBOL_STOP, "Arrêter", true, COL_TIMER, InputEvent::TIMER_STOP);
  voice_btn_(this, this->voice_actions_, LV_SYMBOL_PLUS, "1 min", false, COL_TIMER, InputEvent::TIMER_ADD_MIN);
  voice_btn_(this, this->voice_actions_, MDI_MIC, "Réessayer", true, COL_VOICE, InputEvent::VOICE_START);
  voice_btn_(this, this->voice_actions_, LV_SYMBOL_CLOSE, "Fermer", false, COL_VOICE, InputEvent::VOICE_CANCEL);
  voice_btn_(this, this->voice_actions_, MDI_MIC, "Réactiver", true, COL_VOICE, InputEvent::VOICE_MUTE_TOGGLE);

  // Bottom hint.
  this->voice_hint_ = lv_label_create(this->voice_root_);
  lv_label_set_text(this->voice_hint_, "");
  lv_obj_set_style_text_color(this->voice_hint_, lv_color_hex(0x6E6E78), 0);
  this->set_text_font_(this->voice_hint_, this->font_small_, &lv_font_montserrat_20);
}

// Show/hide the N-th action button (creation order in voice_actions_).
static void voice_action_show(lv_obj_t *actions, std::initializer_list<int> shown) {
  uint32_t n = lv_obj_get_child_count(actions);
  for (uint32_t i = 0; i < n; i++)
    lv_obj_add_flag(lv_obj_get_child(actions, i), LV_OBJ_FLAG_HIDDEN);
  for (int idx : shown)
    if (idx >= 0 && idx < (int) n)
      lv_obj_remove_flag(lv_obj_get_child(actions, idx), LV_OBJ_FLAG_HIDDEN);
}

void LvglRenderer::voice_apply_(VoiceState st) {
  if (this->voice_root_ == nullptr)
    return;
  // Defaults: hide the optional widgets; each state re-enables what it needs.
  lv_obj_add_flag(this->voice_wave_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->voice_ttsbar_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->voice_actions_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(this->voice_close_, LV_OBJ_FLAG_HIDDEN);

  uint32_t orb = COL_VOICE, orb2 = COL_VOICE2;
  const char *glyph = MDI_MIC, *status = "", *sub = "", *hint = "";

  switch (st) {
    case VoiceState::LISTENING:
      glyph = MDI_MIC; status = "Je vous écoute..."; hint = "Touchez pour annuler";
      lv_obj_clear_flag(this->voice_wave_, LV_OBJ_FLAG_HIDDEN);
      break;
    case VoiceState::THINKING:
      glyph = MDI_MIC; status = "Un instant..."; hint = "Reconnaissance + intention";
      orb = COL_VOICE2; orb2 = 0x4B39B0;
      break;
    case VoiceState::RESPONDING:
      glyph = MDI_VOL; status = "Réponse..."; hint = "Lecture vocale";
      lv_obj_clear_flag(this->voice_ttsbar_, LV_OBJ_FLAG_HIDDEN);
      break;
    case VoiceState::ERROR:
      glyph = MDI_ALERT; status = "Je n'ai pas compris"; sub = "Reformulez, ou touchez le micro pour réessayer.";
      orb = COL_ERR; orb2 = 0xB21F1F;
      lv_obj_clear_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(this->voice_actions_, LV_OBJ_FLAG_HIDDEN);
      voice_action_show(this->voice_actions_, {2, 3});  // Réessayer / Fermer
      break;
    case VoiceState::MUTED:
      glyph = MDI_MIC_OFF; status = "Micro coupé"; hint = "Le wake-word est désactivé";
      orb = 0x3A3A44; orb2 = 0x1A1A20;
      lv_obj_clear_flag(this->voice_actions_, LV_OBJ_FLAG_HIDDEN);
      voice_action_show(this->voice_actions_, {4});  // Réactiver
      break;
    case VoiceState::UNAVAILABLE:
      glyph = MDI_CLOUD_OFF; status = "Assistant indisponible";
      sub = "Home Assistant est hors ligne, ou aucun pipeline Assist n'est configuré.";
      orb = 0x3A3A44; orb2 = 0x1A1A20;
      lv_obj_clear_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
      break;
    case VoiceState::TIMER_RINGING:
      glyph = MDI_TIMER_ALERT; status = "Minuterie terminée";
      orb = COL_TIMER; orb2 = 0xC97E00;
      lv_obj_clear_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(this->voice_actions_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(this->voice_close_, LV_OBJ_FLAG_HIDDEN);
      voice_action_show(this->voice_actions_, {0, 1});  // Arrêter / +1 min
      break;
    default:
      break;
  }

  lv_obj_set_style_bg_color(this->voice_orb_, lv_color_hex(orb), 0);
  lv_obj_set_style_bg_grad_color(this->voice_orb_, lv_color_hex(orb2), 0);
  lv_label_set_text(this->voice_orb_icon_, glyph);
  lv_label_set_text(this->voice_status_, status);
  if (sub[0] != '\0')
    lv_label_set_text(this->voice_sub_, sub);
  lv_label_set_text(this->voice_hint_, hint);
}

void LvglRenderer::voice_show(VoiceState st) {
  this->build_voice_();
  if (this->voice_root_ == nullptr)
    return;
  this->voice_state_ = st;
  this->voice_apply_(st);
  lv_obj_clear_flag(this->voice_root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->voice_root_);
}

void LvglRenderer::voice_hide() {
  this->voice_state_ = VoiceState::HIDDEN;
  if (this->voice_root_ != nullptr)
    lv_obj_add_flag(this->voice_root_, LV_OBJ_FLAG_HIDDEN);
}

void LvglRenderer::set_voice_level(float level) {
  if (this->voice_state_ != VoiceState::LISTENING || this->voice_bars_.empty())
    return;
  if (level < 0.05f)
    level = 0.05f;
  if (level > 1.0f)
    level = 1.0f;
  // Scale each bar around its base height for a lively waveform.
  const int base[] = {26, 52, 70, 40, 60, 30, 54, 22};
  for (size_t i = 0; i < this->voice_bars_.size(); i++) {
    int h = (int) (base[i] * (0.35f + 0.65f * level));
    lv_obj_set_height(this->voice_bars_[i], h < 6 ? 6 : h);
  }
}

void LvglRenderer::voice_set_sub(const std::string &s) {
  if (this->voice_sub_ == nullptr)
    return;
  lv_label_set_text(this->voice_sub_, s.c_str());
  if (s.empty())
    lv_obj_add_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_clear_flag(this->voice_sub_, LV_OBJ_FLAG_HIDDEN);
}

void LvglRenderer::set_mic_state(MicState st) {
  this->mic_state_ = st;
  if (this->mic_chip_ == nullptr)
    return;
  uint32_t bg = COL_TILE, fg = 0xCFD0D6;
  const char *glyph = MDI_MIC;
  switch (st) {
    case MicState::ARMED: fg = COL_VOICE; break;
    case MicState::LISTENING: bg = COL_VOICE; fg = 0x04222E; break;
    case MicState::MUTED: glyph = MDI_MIC_OFF; fg = COL_ERR; break;
    case MicState::UNAVAILABLE: fg = COL_MUTED; break;
  }
  lv_obj_set_style_bg_color(this->mic_chip_, lv_color_hex(bg), 0);
  if (this->mic_chip_icon_ != nullptr) {
    lv_label_set_text(this->mic_chip_icon_, glyph);
    lv_obj_set_style_text_color(this->mic_chip_icon_, lv_color_hex(fg), 0);
  }
}

// Format seconds as m:ss (or h:mm:ss past an hour) into buf.
static void fmt_timer(uint32_t s, char *buf, size_t n) {
  if (s >= 3600)
    std::snprintf(buf, n, "%u:%02u:%02u", s / 3600, (s % 3600) / 60, s % 60);
  else
    std::snprintf(buf, n, "%u:%02u", s / 60, s % 60);
}

void LvglRenderer::set_timers(const std::vector<TimerInfo> &timers) {
  this->timers_data_ = timers;
  if (this->timers_scr_ != nullptr && !lv_obj_has_flag(this->timers_scr_, LV_OBJ_FLAG_HIDDEN))
    this->refresh_timers_();
  if (this->timer_pill_ == nullptr)
    return;
  // Header pill: show the soonest active timer's remaining time (single badge, no count).
  const TimerInfo *soonest = nullptr;
  for (const auto &t : timers) {
    if (!t.is_active)
      continue;
    if (soonest == nullptr || t.remaining_s < soonest->remaining_s)
      soonest = &t;
  }
  if (soonest == nullptr) {
    lv_obj_add_flag(this->timer_pill_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  char buf[16];
  fmt_timer(soonest->remaining_s, buf, sizeof(buf));
  if (this->timer_pill_lbl_ != nullptr)
    lv_label_set_text(this->timer_pill_lbl_, buf);
  lv_obj_clear_flag(this->timer_pill_, LV_OBJ_FLAG_HIDDEN);
}

// ===================== Timers screen (top-layer overlay) =====================
// Opened from the header pill; shows a progress ring for the soonest active timer + a list
// of all actives. Built once; refresh_timers_ repopulates from timers_data_.

static void timers_close_cb(lv_event_t *e) {
  auto *self = static_cast<LvglRenderer *>(lv_event_get_user_data(e));
  if (self != nullptr)
    self->hide_timers_();
}

void LvglRenderer::build_timers_() {
  if (this->timers_scr_ != nullptr)
    return;
  this->timers_scr_ = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(this->timers_scr_);
  lv_obj_set_size(this->timers_scr_, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(this->timers_scr_, lv_color_hex(0x0B0B10), 0);
  lv_obj_set_style_bg_opa(this->timers_scr_, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(this->timers_scr_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(this->timers_scr_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(this->timers_scr_, 40, 0);
  lv_obj_set_style_pad_row(this->timers_scr_, 28, 0);
  lv_obj_clear_flag(this->timers_scr_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(this->timers_scr_, LV_OBJ_FLAG_HIDDEN);

  // Top bar: title + close.
  lv_obj_t *top = lv_obj_create(this->timers_scr_);
  lv_obj_remove_style_all(top);
  lv_obj_set_width(top, lv_pct(100));
  lv_obj_set_height(top, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *title = lv_label_create(top);
  lv_label_set_text(title, "Minuteries");
  lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(title, this->font_large_, &lv_font_montserrat_48);
  lv_obj_t *close = lv_button_create(top);
  lv_obj_set_size(close, 74, 74);
  lv_obj_set_style_radius(close, 37, 0);
  lv_obj_set_style_bg_color(close, lv_color_hex(0x20202A), 0);
  lv_obj_set_style_shadow_width(close, 0, 0);
  lv_obj_t *cx = lv_label_create(close);
  lv_label_set_text(cx, LV_SYMBOL_CLOSE);
  lv_obj_center(cx);
  lv_obj_set_style_text_color(cx, lv_color_hex(COL_MUTED), 0);
  lv_obj_add_event_cb(close, timers_close_cb, LV_EVENT_CLICKED, this);

  // Progress ring (soonest timer) with the remaining time + name in the centre.
  this->timers_ring_ = lv_arc_create(this->timers_scr_);
  lv_obj_set_size(this->timers_ring_, 380, 380);
  lv_arc_set_rotation(this->timers_ring_, 270);
  lv_arc_set_bg_angles(this->timers_ring_, 0, 360);
  lv_arc_set_range(this->timers_ring_, 0, 100);
  lv_obj_remove_flag(this->timers_ring_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(this->timers_ring_, 18, LV_PART_MAIN);
  lv_obj_set_style_arc_color(this->timers_ring_, lv_color_hex(0x23232C), LV_PART_MAIN);
  lv_obj_set_style_arc_width(this->timers_ring_, 18, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(this->timers_ring_, lv_color_hex(COL_TIMER), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(this->timers_ring_, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(this->timers_ring_, 0, LV_PART_KNOB);
  lv_obj_t *ringcol = lv_obj_create(this->timers_ring_);
  lv_obj_remove_style_all(ringcol);
  lv_obj_set_size(ringcol, 300, 300);
  lv_obj_center(ringcol);
  lv_obj_set_flex_flow(ringcol, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(ringcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(ringcol, LV_OBJ_FLAG_SCROLLABLE);
  this->timers_ring_time_ = lv_label_create(ringcol);
  lv_label_set_text(this->timers_ring_time_, "0:00");
  lv_obj_set_style_text_color(this->timers_ring_time_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->timers_ring_time_, this->font_large_, &lv_font_montserrat_48);
  this->timers_ring_name_ = lv_label_create(ringcol);
  lv_label_set_text(this->timers_ring_name_, "");
  lv_obj_set_style_text_color(this->timers_ring_name_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->timers_ring_name_, this->font_medium_, &lv_font_montserrat_28);

  // List of active timers.
  this->timers_list_ = lv_obj_create(this->timers_scr_);
  lv_obj_remove_style_all(this->timers_list_);
  lv_obj_set_width(this->timers_list_, lv_pct(100));
  lv_obj_set_flex_grow(this->timers_list_, 1);
  lv_obj_set_flex_flow(this->timers_list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(this->timers_list_, 14, 0);
  lv_obj_set_scroll_dir(this->timers_list_, LV_DIR_VER);
}

void LvglRenderer::refresh_timers_() {
  if (this->timers_scr_ == nullptr)
    return;
  // Ring = soonest active timer's elapsed fraction; centre shows its remaining + name.
  const TimerInfo *soonest = nullptr;
  for (const auto &t : this->timers_data_) {
    if (!t.is_active)
      continue;
    if (soonest == nullptr || t.remaining_s < soonest->remaining_s)
      soonest = &t;
  }
  char buf[16];
  if (soonest != nullptr) {
    fmt_timer(soonest->remaining_s, buf, sizeof(buf));
    lv_label_set_text(this->timers_ring_time_, buf);
    lv_label_set_text(this->timers_ring_name_, soonest->name.c_str());
    int pct = soonest->total_s > 0
                  ? (int) (100 - lroundf(100.0f * soonest->remaining_s / soonest->total_s))
                  : 0;
    lv_arc_set_value(this->timers_ring_, pct);
  } else {
    lv_label_set_text(this->timers_ring_time_, "—");
    lv_label_set_text(this->timers_ring_name_, "aucune minuterie");
    lv_arc_set_value(this->timers_ring_, 0);
  }

  // Rebuild the list of active timers.
  lv_obj_clean(this->timers_list_);
  for (const auto &t : this->timers_data_) {
    if (!t.is_active)
      continue;
    lv_obj_t *row = lv_obj_create(this->timers_list_);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 18, 0);
    lv_obj_set_style_pad_hor(row, 26, 0);
    lv_obj_set_style_pad_ver(row, 20, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 18, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = lv_label_create(row);
    lv_label_set_text(ic, MDI_TIMER);
    lv_obj_set_style_text_color(ic, lv_color_hex(COL_TIMER), 0);
    if (this->font_icons_lg_ != nullptr)
      lv_obj_set_style_text_font(ic, this->font_icons_lg_->get_lv_font(), 0);
    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, t.name.empty() ? "minuterie" : t.name.c_str());
    lv_obj_set_flex_grow(name, 1);
    lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(name, this->font_medium_, &lv_font_montserrat_28);
    lv_obj_t *time = lv_label_create(row);
    fmt_timer(t.remaining_s, buf, sizeof(buf));
    lv_label_set_text(time, buf);
    lv_obj_set_style_text_color(time, lv_color_hex(COL_TIMER), 0);
    this->set_text_font_(time, this->font_large_, &lv_font_montserrat_48);
  }
}

void LvglRenderer::show_timers_() {
  this->build_timers_();
  this->refresh_timers_();
  lv_obj_clear_flag(this->timers_scr_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->timers_scr_);
}

void LvglRenderer::hide_timers_() {
  if (this->timers_scr_ != nullptr)
    lv_obj_add_flag(this->timers_scr_, LV_OBJ_FLAG_HIDDEN);
}

void LvglRenderer::on_launcher_scroll(lv_obj_t *grid, bool ended) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  // Detail list: recycle episode thumbnails to whatever rows are now on screen. Throttle during
  // the drag; always reassign once it settles.
  if (!this->ep_img_.empty()) {
    uint32_t now = lv_tick_get();
    if (ended || now - this->ep_assign_ms_ >= 120) {
      this->ep_assign_ms_ = now;
      this->assign_episode_thumbs_();
    }
    return;
  }
#endif
  // Grid: pull-to-refresh.
  if (ended) {
    if (this->pull_armed_) {
      this->pull_armed_ = false;
      // Force the covers to reload so the refresh is *visible*: the favourites list is usually
      // unchanged and the cached covers would otherwise skip re-download (no feedback). Clearing
      // the loaded-URL tracking makes bind_cover_ re-fetch them on the rebuild.
      for (auto &u : this->cover_url_list_)
        u.clear();
      for (auto &u : this->cover_pending_url_)
        u.clear();
      this->emit(InputEvent::LAUNCHER_REFRESH, -1);
    }
  } else if (lv_obj_get_scroll_y(grid) <= -PULL_REFRESH_PX) {
    this->pull_armed_ = true;
  }
}

// Give the on-screen episode rows a thumbnail from the limited pool, recycling slots from rows
// that scrolled off. Called on (re)build and while scrolling the detail list.
void LvglRenderer::assign_episode_thumbs_() {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  if (this->ep_img_.empty() || this->ep_thumb_slots_.empty() || this->ep_list_ == nullptr)
    return;
  lv_obj_update_layout(this->ep_list_);  // ensure row coordinates are current
  lv_area_t va;
  lv_obj_get_coords(this->ep_list_, &va);
  const int32_t buffer = 160;  // px above/below the viewport to preload

  auto slot_index = [this](online_image::OnlineImage *slot) -> int {
    for (size_t k = 0; k < this->cover_slot_list_.size(); k++)
      if (this->cover_slot_list_[k] == slot)
        return (int) k;
    return -1;
  };

  // 1. Which episodes (with a thumbnail) are on screen (+buffer), capped to the pool size.
  std::vector<char> want(this->ep_row_.size(), 0);
  int n_wanted = 0;
  for (size_t i = 0; i < this->ep_row_.size(); i++) {
    if (this->ep_row_[i] == nullptr || this->ep_url_[i].empty())
      continue;
    lv_area_t ra;
    lv_obj_get_coords(this->ep_row_[i], &ra);
    if (ra.y2 >= va.y1 - buffer && ra.y1 <= va.y2 + buffer &&
        n_wanted < (int) this->ep_thumb_slots_.size()) {
      want[i] = 1;
      n_wanted++;
    }
  }

  // 2. Release slots whose episode scrolled out of the window (back to the placeholder).
  for (size_t s = 0; s < this->thumb_owner_.size(); s++) {
    int owner = this->thumb_owner_[s];
    if (owner >= 0 && (owner >= (int) want.size() || !want[owner])) {
      if (owner < (int) this->ep_img_.size() && this->ep_img_[owner] != nullptr)
        lv_image_set_src(this->ep_img_[owner], nullptr);
      int k = slot_index(this->ep_thumb_slots_[s]);
      if (k >= 0)
        this->cover_widget_list_[k] = nullptr;
      this->ep_slot_[owner] = -1;
      this->thumb_owner_[s] = -1;
    }
  }

  // 3. Compact the download queue if idle (bounds its growth over a long scroll).
  const bool was_idle = this->cover_load_idx_ >= this->cover_queue_.size();
  if (was_idle) {
    this->cover_queue_.clear();
    this->cover_load_idx_ = 0;
  }

  // 4. Assign a free slot to each wanted episode without one, and (re)load its thumbnail.
  for (size_t i = 0; i < want.size(); i++) {
    if (!want[i] || this->ep_slot_[i] != -1)
      continue;
    int free_s = -1;
    for (size_t s = 0; s < this->thumb_owner_.size(); s++)
      if (this->thumb_owner_[s] < 0) {
        free_s = (int) s;
        break;
      }
    if (free_s < 0)
      break;  // pool exhausted (|wanted| is capped to the pool, so this shouldn't happen)
    online_image::OnlineImage *slot = this->ep_thumb_slots_[free_s];
    lv_obj_t *img = this->ep_img_[i];
    this->thumb_owner_[free_s] = (int) i;
    this->ep_slot_[i] = free_s;
    int k = slot_index(slot);
    if (k >= 0)
      this->cover_widget_list_[k] = img;
    if (k >= 0 && this->cover_url_list_[k] == this->ep_url_[i]) {
      lv_image_set_src(img, slot->get_lv_image_dsc());  // already loaded -> show immediately
    } else {
      lv_image_set_src(img, nullptr);  // placeholder until decoded
      if (k >= 0)
        this->cover_pending_url_[k] = this->ep_url_[i];
      slot->set_url(this->ep_url_[i]);
      this->cover_queue_.push_back(slot);
    }
  }

  // 5. Kick the serial download queue if it was idle and we queued new work.
  if (was_idle && this->cover_load_idx_ < this->cover_queue_.size())
    this->cover_queue_[this->cover_load_idx_]->update();
#endif
}

// Drop leading symbol/emoji codepoints (>= U+2000) the accented text font can't render
// (e.g. "🌊 Episode" -> "Episode"); Latin letters + accents (< U+2000) are kept.
static std::string clean_title(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = s[i];
    if (c < 0x80)
      break;  // ASCII
    uint32_t cp;
    size_t len;
    if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1F;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0F;
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07;
      len = 4;
    } else {
      break;
    }
    if (i + len > s.size())
      break;
    for (size_t k = 1; k < len; k++)
      cp = (cp << 6) | (s[i + k] & 0x3F);
    if (cp < 0x2000)
      break;  // Latin / accents -> keep
    i += len;
  }
  while (i < s.size() && s[i] == ' ')
    i++;
  return i != 0 ? s.substr(i) : s;
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
  // Same on the standby screen.
  if (this->idle_wx_icon_ != nullptr && icon_glyph != nullptr)
    lv_label_set_text(this->idle_wx_icon_, icon_glyph);
  if (this->idle_wx_temp_ != nullptr)
    lv_label_set_text(this->idle_wx_temp_, temp_str);
  if (this->idle_wx_cond_ != nullptr)
    lv_label_set_text(this->idle_wx_cond_, cond_str);
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

    // Weather on the standby screen: [icon  temp] on one line, condition below.
    lv_obj_t *iwrow = lv_obj_create(col);
    lv_obj_set_size(iwrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(iwrow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(iwrow, 0, 0);
    lv_obj_set_style_pad_all(iwrow, 0, 0);
    lv_obj_set_style_pad_column(iwrow, 8, 0);
    lv_obj_set_style_margin_top(iwrow, 40, 0);
    lv_obj_set_flex_flow(iwrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(iwrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(iwrow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(iwrow, LV_OBJ_FLAG_SCROLLABLE);
    this->idle_wx_icon_ = lv_label_create(iwrow);
    lv_label_set_text(this->idle_wx_icon_, "");
    lv_obj_set_style_text_color(this->idle_wx_icon_, lv_color_hex(COL_TEXT), 0);
    if (this->font_weather_ != nullptr)
      lv_obj_set_style_text_font(this->idle_wx_icon_, this->font_weather_->get_lv_font(), 0);
    this->idle_wx_temp_ = lv_label_create(iwrow);
    lv_label_set_text(this->idle_wx_temp_, "");
    lv_obj_set_style_text_color(this->idle_wx_temp_, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(this->idle_wx_temp_, this->font_medium_, &lv_font_montserrat_28);
    this->idle_wx_cond_ = lv_label_create(col);
    lv_label_set_text(this->idle_wx_cond_, "");
    lv_obj_set_style_text_color(this->idle_wx_cond_, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(this->idle_wx_cond_, this->font_small_, &lv_font_montserrat_20);

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
    this->build_now_playing_();
    this->build_voice_();  // voice-assistant overlay (top layer), hidden until an event fires
    // Now-playing artwork reuses the detail pool's slot 0 (detail and now-playing screens are
    // never shown at the same time); keeps the grid covers untouched.
    for (const auto &g : groups) {
      if (g.is_launcher && !g.thumb_slots.empty()) {
        this->np_cover_slot_ = g.thumb_slots[0];
        break;
      }
    }
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
  // Both buttons sit low and centred as a pair (in the arc's bottom opening, above the dots)
  // so they don't overlap the value/volume ring — matches the mockup.
  int btn_dx = (int) (0.145f * this->w_);
  int btn_dy = (int) (-0.205f * this->h_);
  struct {
    lv_obj_t **slot;
    lv_obj_t **lbl_slot;
    CbData **cb_slot;
    const char *sym;
    InputEvent ev;
    lv_align_t align;
    int dx;
  } mbtns[] = {
      {&this->card_prev_btn_, &this->card_prev_lbl_, &this->card_prev_cb_, LV_SYMBOL_PREV,
       InputEvent::MEDIA_PREV, LV_ALIGN_BOTTOM_MID, -btn_dx},
      {&this->card_next_btn_, &this->card_next_lbl_, &this->card_next_cb_, LV_SYMBOL_NEXT,
       InputEvent::MEDIA_NEXT, LV_ALIGN_BOTTOM_MID, btn_dx},
  };
  int btn_sz = (int) (0.18f * this->w_);
  for (auto &m : mbtns) {
    lv_obj_t *b = lv_button_create(this->card_scr_);
    lv_obj_set_size(b, btn_sz, btn_sz);
    lv_obj_align(b, m.align, m.dx, btn_dy);
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
    *m.lbl_slot = lbl;
    *m.cb_slot = d;
  }

  lv_obj_add_event_cb(this->card_scr_, carousel_gesture_cb, LV_EVENT_ALL, this);
}

// Accent color for a card: explicit override, else per-type default (cf. color-system.md).
static uint32_t accent_for(const Card &c) {
  if (c.has_color)
    return c.color;
  return c.type == CardType::SWITCH ? 0x3DD68C : 0xFFB020;
}

// Set a card's type icon on `label`: MDI (font_icons_lg_) when available, else the built-in
// LVGL symbol at montserrat_48 (keeps the Dial — which has no large MDI font — working).
void LvglRenderer::set_card_icon_(lv_obj_t *label, const Card &c, uint32_t color) {
  if (this->font_icons_lg_ != nullptr) {
    lv_label_set_text(label, card_mdi_glyph(c));
    lv_obj_set_style_text_font(label, this->font_icons_lg_->get_lv_font(), 0);
  } else {
    lv_label_set_text(label, icon_for(c));
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
  }
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
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

  // Right group: a now-playing button + the weather block.
  lv_obj_t *right = lv_obj_create(this->dash_header_);
  lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(right, 0, 0);
  lv_obj_set_style_pad_all(right, 0, 0);
  lv_obj_set_style_pad_column(right, 14, 0);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

  // Active-timer pill (hidden until ≥1 timer runs) -> opens the timers screen.
  this->timer_pill_ = lv_button_create(right);
  lv_obj_set_height(this->timer_pill_, 56);
  lv_obj_set_style_radius(this->timer_pill_, 28, 0);
  lv_obj_set_style_bg_color(this->timer_pill_, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_shadow_width(this->timer_pill_, 0, 0);
  lv_obj_set_style_pad_hor(this->timer_pill_, 18, 0);
  lv_obj_set_flex_flow(this->timer_pill_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(this->timer_pill_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(this->timer_pill_, 8, 0);
  lv_obj_add_flag(this->timer_pill_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *tpi = lv_label_create(this->timer_pill_);
  lv_label_set_text(tpi, MDI_TIMER);
  lv_obj_set_style_text_color(tpi, lv_color_hex(COL_TIMER), 0);
  if (this->font_icons_lg_ != nullptr)
    lv_obj_set_style_text_font(tpi, this->font_icons_lg_->get_lv_font(), 0);
  this->timer_pill_lbl_ = lv_label_create(this->timer_pill_);
  lv_label_set_text(this->timer_pill_lbl_, "0:00");
  lv_obj_set_style_text_color(this->timer_pill_lbl_, lv_color_hex(COL_TIMER), 0);
  this->set_text_font_(this->timer_pill_lbl_, this->font_medium_, &lv_font_montserrat_28);
  {
    auto *d = new CbData{this, InputEvent::OPEN_TIMERS, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->timer_pill_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // Microphone chip: tap = tap-to-talk (start listening). State reflects armed/listening/muted.
  this->mic_chip_ = lv_button_create(right);
  lv_obj_set_size(this->mic_chip_, 56, 56);
  lv_obj_set_style_radius(this->mic_chip_, 28, 0);
  lv_obj_set_style_bg_color(this->mic_chip_, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_shadow_width(this->mic_chip_, 0, 0);
  this->mic_chip_icon_ = lv_label_create(this->mic_chip_);
  lv_label_set_text(this->mic_chip_icon_, MDI_MIC);
  lv_obj_center(this->mic_chip_icon_);
  lv_obj_set_style_text_color(this->mic_chip_icon_, lv_color_hex(COL_VOICE), 0);
  if (this->font_icons_lg_ != nullptr)
    lv_obj_set_style_text_font(this->mic_chip_icon_, this->font_icons_lg_->get_lv_font(), 0);
  {
    auto *d = new CbData{this, InputEvent::VOICE_START, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->mic_chip_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // Now-playing button -> opens the "now playing" card.
  this->np_btn_ = lv_button_create(right);
  lv_obj_set_size(this->np_btn_, 56, 56);
  lv_obj_set_style_bg_color(this->np_btn_, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_radius(this->np_btn_, 28, 0);
  lv_obj_set_style_shadow_width(this->np_btn_, 0, 0);
  lv_obj_t *npi = lv_label_create(this->np_btn_);
  lv_label_set_text(npi, LV_SYMBOL_AUDIO);
  lv_obj_center(npi);
  lv_obj_set_style_text_font(npi, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(npi, lv_color_hex(COL_TEXT), 0);
  {
    auto *d = new CbData{this, InputEvent::OPEN_NOW_PLAYING, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->np_btn_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // Right: weather (icon + temperature on one line, condition below), bound to a HA entity.
  lv_obj_t *weather = lv_obj_create(right);
  lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(weather, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(weather, 0, 0);
  lv_obj_set_style_pad_all(weather, 0, 0);
  lv_obj_set_style_pad_row(weather, 2, 0);
  lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_clear_flag(weather, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *wrow = lv_obj_create(weather);  // icon + temperature, same line
  lv_obj_set_size(wrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(wrow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wrow, 0, 0);
  lv_obj_set_style_pad_all(wrow, 0, 0);
  lv_obj_set_style_pad_column(wrow, 8, 0);
  lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(wrow, LV_OBJ_FLAG_SCROLLABLE);
  this->weather_icon_lbl_ = lv_label_create(wrow);
  lv_label_set_text(this->weather_icon_lbl_, "");
  lv_obj_set_style_text_color(this->weather_icon_lbl_, lv_color_hex(COL_TEXT), 0);
  if (this->font_weather_ != nullptr)
    lv_obj_set_style_text_font(this->weather_icon_lbl_, this->font_weather_->get_lv_font(), 0);
  this->weather_temp_lbl_ = lv_label_create(wrow);
  lv_label_set_text(this->weather_temp_lbl_, "");
  lv_obj_set_style_text_color(this->weather_temp_lbl_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->weather_temp_lbl_, this->font_medium_, &lv_font_montserrat_28);

  this->weather_cond_lbl_ = lv_label_create(weather);  // condition, below
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
  this->launcher_grids_.clear();
  this->launcher_sig_.clear();
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

      // All types except switch have a control sheet (more-info). The icon opens it.
      const bool has_sheet = card.type != CardType::SWITCH;
      const bool is_media = card.type == CardType::MEDIA_PLAYER;
      const bool is_cover = card.type == CardType::COVER;
      uint32_t icon_col = card.is_on() ? accent_for(card) : COL_MUTED;

      t.icon = lv_label_create(toprow);
      this->set_card_icon_(t.icon, card, icon_col);
      if (has_sheet) {  // icon = tap target that opens the control sheet (consumes the click)
        lv_obj_add_flag(t.icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(t.icon, 8, 0);
        auto *di = new CbData{this, InputEvent::OPEN_SHEET, (int) ci};
        g_cbdata.push_back(di);
        lv_obj_add_event_cb(t.icon, btn_event_cb, LV_EVENT_CLICKED, di);
      }

      if (is_media || is_cover) {
        // Quick action button (right) — direct primary action, does NOT open the sheet.
        // Its glyph shows the TARGET action (media play/pause; cover open/close direction),
        // while the tile icon on the left shows the CURRENT state.
        lv_obj_t *pb = lv_button_create(toprow);
        lv_obj_set_size(pb, 60, 60);
        lv_obj_set_style_radius(pb, 30, 0);
        lv_obj_set_style_bg_color(pb, lv_color_hex(is_media ? 0x3DD68C : accent_for(card)), 0);
        lv_obj_set_style_shadow_width(pb, 0, 0);
        t.state = lv_label_create(pb);  // reused to refresh the action glyph on render
        lv_label_set_text(t.state, is_media ? (card.is_on() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY)
                                            : cover_action_glyph(card));
        lv_obj_center(t.state);
        lv_obj_set_style_text_color(t.state, lv_color_hex(is_media ? 0x06281A : 0xFFFFFF), 0);
        lv_obj_set_style_text_font(t.state, &lv_font_montserrat_28, 0);
        auto *dp = new CbData{this, InputEvent::TOGGLE, (int) ci};  // primary action (play_pause / open-close)
        g_cbdata.push_back(dp);
        lv_obj_add_event_cb(pb, btn_event_cb, LV_EVENT_CLICKED, dp);
      } else {
        t.state = lv_label_create(toprow);
        char sbuf[24];
        format_state(card, sbuf, sizeof(sbuf));
        lv_label_set_text(t.state, sbuf);
        this->set_text_font_(t.state, this->font_small_, &lv_font_montserrat_20);
      }

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

      // Tile body tap: types with a quick button (media/cover) + climate open the sheet;
      // switch/light do the primary action directly.
      InputEvent tile_ev = (is_media || is_cover || card.type == CardType::CLIMATE)
                               ? InputEvent::OPEN_SHEET
                               : InputEvent::TOGGLE;
      auto *d = new CbData{this, tile_ev, (int) ci};
      g_cbdata.push_back(d);
      lv_obj_add_event_cb(tile, btn_event_cb, LV_EVENT_CLICKED, d);
      tiles.push_back(t);
    }
    this->group_grids_.push_back(grid);
    this->group_tiles_.push_back(tiles);

    // Launcher groups also get a (scrollable) list container, populated at render time from
    // the LauncherModule. Non-launcher groups keep a nullptr slot to stay index-aligned.
    lv_obj_t *lgrid = nullptr;
    if (groups[gi].is_launcher) {
      lgrid = lv_obj_create(content);
      lv_obj_set_size(lgrid, lv_pct(100), lv_pct(100));
      lv_obj_set_style_bg_opa(lgrid, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(lgrid, 0, 0);
      lv_obj_set_style_pad_all(lgrid, 0, 0);
      lv_obj_set_style_pad_top(lgrid, 16, 0);
      lv_obj_set_style_pad_row(lgrid, 10, 0);
      lv_obj_set_flex_flow(lgrid, LV_FLEX_FLOW_COLUMN);
      lv_obj_add_flag(lgrid, LV_OBJ_FLAG_HIDDEN);
      // Pull-to-refresh: watch over-scroll at the top.
      lv_obj_add_event_cb(lgrid, launcher_scroll_cb, LV_EVENT_SCROLL, this);
      lv_obj_add_event_cb(lgrid, launcher_scroll_cb, LV_EVENT_SCROLL_END, this);
    }
    this->launcher_grids_.push_back(lgrid);
    this->launcher_sig_.push_back(-1);
  }

  this->register_cover_slots_(groups);
}

void LvglRenderer::register_cover_slots_(const std::vector<Group> &groups) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  this->cover_slot_list_.clear();
  this->cover_widget_list_.clear();
  this->cover_url_list_.clear();
  this->cover_pending_url_.clear();
  auto register_pool = [this](const std::vector<online_image::OnlineImage *> &pool) {
    for (auto *slot : pool) {
      if (slot == nullptr)
        continue;
      this->cover_slot_list_.push_back(slot);
      this->cover_widget_list_.push_back(nullptr);
      this->cover_url_list_.push_back("");
      this->cover_pending_url_.push_back("");
      slot->add_on_finished_callback([this, slot](bool /*cached*/) { this->on_cover_ready_(slot); });
      slot->add_on_error_callback([this, slot]() { this->on_cover_error_(slot); });
    }
  };
  for (const auto &g : groups) {
    register_pool(g.cover_slots);  // grid covers
    register_pool(g.thumb_slots);  // detail header + episode thumbnails
  }
#else
  (void) groups;
#endif
}

// Bind `img` to `slot` for `url`. If the slot already holds that exact URL we just rebind and
// show it (no re-download — this is what makes returning to the grid instant). Otherwise we
// hide the image, issue the download, and queue it; on_cover_ready_ reveals it once decoded.
int LvglRenderer::bind_cover_(lv_obj_t *img, online_image::OnlineImage *slot, const std::string &url) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  int idx = -1;
  for (size_t s = 0; s < this->cover_slot_list_.size(); s++)
    if (this->cover_slot_list_[s] == slot) {
      idx = (int) s;
      break;
    }
  lv_image_set_src(img, slot->get_lv_image_dsc());
  if (idx >= 0)
    this->cover_widget_list_[idx] = img;
  if (idx >= 0 && this->cover_url_list_[idx] == url)
    return idx;  // already FINISHED loading this exact URL -> show immediately
  lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);  // hide until the right-size image is decoded
  if (idx >= 0)
    this->cover_pending_url_[idx] = url;  // committed to cover_url_list_ only on completion
  slot->set_url(url);
  this->cover_queue_.push_back(slot);
  return idx;
#else
  (void) img;
  (void) slot;
  (void) url;
  return -1;
#endif
}

void LvglRenderer::on_cover_ready_(online_image::OnlineImage *slot) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  for (size_t i = 0; i < this->cover_slot_list_.size(); i++) {
    if (this->cover_slot_list_[i] == slot) {
      this->cover_url_list_[i] = this->cover_pending_url_[i];  // commit: this URL is now loaded
      if (this->cover_widget_list_[i] != nullptr) {
        lv_image_set_src(this->cover_widget_list_[i], slot->get_lv_image_dsc());
        lv_obj_clear_flag(this->cover_widget_list_[i], LV_OBJ_FLAG_HIDDEN);  // reveal: it's ready
        lv_obj_invalidate(this->cover_widget_list_[i]);
      }
      break;
    }
  }
  this->advance_cover_(slot);
#else
  (void) slot;
#endif
}

void LvglRenderer::on_cover_error_(online_image::OnlineImage *slot) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  ESP_LOGW(TAG, "cover download failed");
  // Forget the URL so a later rebuild retries this slot instead of assuming it's loaded.
  for (size_t i = 0; i < this->cover_slot_list_.size(); i++)
    if (this->cover_slot_list_[i] == slot) {
      this->cover_url_list_[i].clear();
      this->cover_pending_url_[i].clear();
      break;
    }
  this->advance_cover_(slot);
#else
  (void) slot;
#endif
}

// Kick the next cover once the current one finished/errored (one TLS download at a time).
void LvglRenderer::advance_cover_(online_image::OnlineImage *finished_slot) {
#ifdef USE_HA_DASHBOARD_LAUNCHER
  if (this->cover_load_idx_ >= this->cover_queue_.size())
    return;
  if (this->cover_queue_[this->cover_load_idx_] != finished_slot)
    return;  // not the slot we're waiting on (stale/duplicate callback)
  this->cover_load_idx_++;
  if (this->cover_load_idx_ < this->cover_queue_.size())
    this->cover_queue_[this->cover_load_idx_]->update();
#else
  (void) finished_slot;
#endif
}

void LvglRenderer::render_dashboard_(const ViewModel &vm) {
  if (vm.groups == nullptr)
    return;
  int active = vm.group_index;
  this->active_group_ = active;  // for the control sheet (resolve which group's card)

  // Tabs: active = white label + accent underline; others muted, no underline.
  for (size_t i = 0; i < this->tab_btns_.size(); i++) {
    bool on = (int) i == active;
    lv_obj_set_style_border_width(this->tab_btns_[i], on ? 2 : 0, 0);
    if (i < this->tab_lbls_.size())
      lv_obj_set_style_text_color(this->tab_lbls_[i], lv_color_hex(on ? COL_TEXT : COL_MUTED), 0);
  }

  // Grids: show only the active group, refreshing it in place. Launcher groups use their
  // own list container instead of the entity tile grid.
  for (size_t gi = 0; gi < this->group_grids_.size(); gi++) {
    bool is_active = (int) gi == active;
    const Group &g = (*vm.groups)[gi];
    lv_obj_t *lgrid = (gi < this->launcher_grids_.size()) ? this->launcher_grids_[gi] : nullptr;

    if (g.is_launcher) {
      lv_obj_add_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);  // entity grid unused here
      if (lgrid != nullptr) {
        if (is_active) {
          lv_obj_clear_flag(lgrid, LV_OBJ_FLAG_HIDDEN);
          this->render_launcher_((int) gi, g);
        } else {
          lv_obj_add_flag(lgrid, LV_OBJ_FLAG_HIDDEN);
        }
      }
      continue;
    }

    if (lgrid != nullptr)
      lv_obj_add_flag(lgrid, LV_OBJ_FLAG_HIDDEN);
    if (is_active) {
      lv_obj_clear_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);
      for (size_t ci = 0; ci < this->group_tiles_[gi].size() && ci < g.cards.size(); ci++) {
        const Card &c = g.cards[ci];
        const Tile &t = this->group_tiles_[gi][ci];
        uint32_t col = c.is_on() ? accent_for(c) : COL_MUTED;
        lv_obj_set_style_text_color(t.icon, lv_color_hex(col), 0);
        if (c.type == CardType::COVER && this->font_icons_lg_ != nullptr)
          lv_label_set_text(t.icon, card_mdi_glyph(c));  // open/closed glyph follows position
        if (c.type == CardType::MEDIA_PLAYER) {
          // t.state is the play/pause button glyph (dark on green) — no text/colour override.
          lv_label_set_text(t.state, c.is_on() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        } else if (c.type == CardType::COVER) {
          // t.state is the quick-button glyph (white on accent): show the target action.
          lv_label_set_text(t.state, cover_action_glyph(c));
        } else {
          char sbuf[24];
          format_state(c, sbuf, sizeof(sbuf));
          lv_label_set_text(t.state, sbuf);
          lv_obj_set_style_text_color(t.state, lv_color_hex(col), 0);
        }
        if (t.bar != nullptr) {
          lv_bar_set_value(t.bar, (int) lroundf(c.value() * 100), LV_ANIM_OFF);
          lv_obj_set_style_bg_color(t.bar, lv_color_hex(c.is_on() ? accent_for(c) : COL_MUTED), LV_PART_INDICATOR);
        }
      }
    } else {
      lv_obj_add_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);
    }
  }

  this->refresh_sheet_();  // keep the control sheet (if open) in sync with the card state

  if (this->dashboard_scr_ != nullptr)
    lv_screen_load(this->dashboard_scr_);
}

void LvglRenderer::render_launcher_(int gi, const Group &g) {
  if (gi < 0 || gi >= (int) this->launcher_grids_.size())
    return;
  lv_obj_t *grid = this->launcher_grids_[gi];
  if (grid == nullptr || g.launcher == nullptr)
    return;
  LauncherModule *L = g.launcher;

  const bool detail = (L->level() == LauncherLevel::DETAIL);

  // Rebuild only when something changed (level / status / count / paging).
  long sig = (((long) detail * 8 + (long) L->status()) * 100000L) + (long) L->items().size() * 4L +
             (L->has_more() ? 2 : 0) + (L->loading_more() ? 1 : 0);
  if (this->launcher_sig_[gi] == sig)
    return;
  this->launcher_sig_[gi] = sig;

  lv_obj_clean(grid);  // destroys old children (and their event cbs)

#ifdef USE_HA_DASHBOARD_LAUNCHER
  // The cover widgets we are about to (re)create were just destroyed: drop stale pointers so
  // a late download callback can't touch a freed object. We keep cover_url_list_ intact so a
  // slot already holding the right image is not re-downloaded. Reset the serial queue too.
  auto clear_widgets = [this](const std::vector<online_image::OnlineImage *> &pool) {
    for (auto *slot : pool)
      for (size_t s = 0; s < this->cover_slot_list_.size(); s++)
        if (this->cover_slot_list_[s] == slot)
          this->cover_widget_list_[s] = nullptr;
  };
  clear_widgets(g.cover_slots);
  clear_widgets(g.thumb_slots);
  this->cover_queue_.clear();
  this->cover_load_idx_ = 0;
  // Reset episode-thumbnail recycling state (rebuilt below for the detail view).
  this->ep_row_.clear();
  this->ep_img_.clear();
  this->ep_url_.clear();
  this->ep_slot_.clear();
  this->ep_thumb_slots_.clear();
  this->thumb_owner_.clear();
  this->ep_list_ = nullptr;
#endif

  // Grid level = wrapping cover grid (2 columns); detail level = vertical list.
  if (detail) {
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  } else {
    // Grid level = a vertical list of full-width horizontal cards (cover + title + actions).
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 14, 0);
  }

  auto add_button = [this, grid](const char *text, const lv_font_t *fb, uint32_t color, InputEvent ev,
                                 int idx) -> lv_obj_t * {
    lv_obj_t *b = lv_button_create(grid);
    lv_obj_set_width(b, lv_pct(100));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_pad_all(b, 16, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    this->set_text_font_(l, this->font_medium_, fb);
    auto *d = new CbData{this, ev, idx};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
    return b;
  };

  // A favourite card (DA "variante A"): a full-width grey card laid out horizontally —
  // [ cover | title + type chip + actions ]. The cover is small (fast JPEG decode) but the
  // card fills the column. Only the "▶ Lecture" button starts playback (so a press-drag on
  // the card/cover scrolls the list); podcasts/audiobooks also get an "Épisodes/Chapitres"
  // button. Cover loads async via bind_cover_ (skip-if-cached + hide-until-ready).
  const int COVER_PX = 200;
  auto make_cover_tile = [&](const QuickItem &item, int idx) {
    lv_obj_t *card = lv_obj_create(grid);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_pad_column(card, 22, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Cover holder (not clickable -> press-drag scrolls the list).
    lv_obj_t *cover = lv_obj_create(card);
    lv_obj_set_size(cover, COVER_PX, COVER_PX);
    lv_obj_set_style_bg_color(cover, lv_color_hex(0x15151C), 0);
    lv_obj_set_style_border_width(cover, 0, 0);
    lv_obj_set_style_shadow_width(cover, 0, 0);
    lv_obj_set_style_radius(cover, 14, 0);
    lv_obj_set_style_pad_all(cover, 0, 0);
    lv_obj_set_style_clip_corner(cover, true, 0);
    lv_obj_clear_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
#ifdef USE_HA_DASHBOARD_LAUNCHER
    if (idx >= 0 && idx < (int) g.cover_slots.size() && g.cover_slots[idx] != nullptr &&
        !item.cover_url.empty()) {
      lv_obj_t *img = lv_image_create(cover);
      lv_obj_center(img);
      this->bind_cover_(img, g.cover_slots[idx], item.cover_url + "?size=" + std::to_string(COVER_PX));
    }
#endif

    // Body column: title, type chip, actions.
    lv_obj_t *body = lv_obj_create(card);
    lv_obj_set_height(body, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_row(body, 14, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(body);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, clean_title(item.title).c_str());
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(l, this->font_medium_, &lv_font_montserrat_28);

    // Type chip (rounded muted pill).
    const char *tylbl = "Titre";
    if (item.media_type == "playlist")
      tylbl = "Playlist";
    else if (item.media_type == "album")
      tylbl = "Album";
    else if (item.media_type == "radio")
      tylbl = "Radio";
    else if (item.media_type == "podcast")
      tylbl = "Podcast";
    else if (item.media_type == "audiobook")
      tylbl = "Livre audio";
    lv_obj_t *chip = lv_obj_create(body);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x23232C), 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 20, 0);
    lv_obj_set_style_pad_hor(chip, 16, 0);
    lv_obj_set_style_pad_ver(chip, 6, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cl = lv_label_create(chip);
    lv_label_set_text(cl, tylbl);
    lv_obj_set_style_text_color(cl, lv_color_hex(COL_MUTED), 0);
    this->set_text_font_(cl, this->font_small_, &lv_font_montserrat_20);

    // Actions row: ▶ Lecture (the only play trigger) + Épisodes/Chapitres for drillables.
    lv_obj_t *actions = lv_obj_create(body);
    lv_obj_set_size(actions, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(actions, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 14, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *playbtn = lv_button_create(actions);
    lv_obj_set_height(playbtn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(playbtn, lv_color_hex(0x3DD68C), 0);
    lv_obj_set_style_shadow_width(playbtn, 0, 0);
    lv_obj_set_style_radius(playbtn, 30, 0);
    lv_obj_set_style_pad_hor(playbtn, 22, 0);
    lv_obj_set_style_pad_ver(playbtn, 12, 0);
    lv_obj_t *pl = lv_label_create(playbtn);
    // "Lecture" is ASCII, so the whole label (play glyph + text) fits the built-in Montserrat.
    lv_label_set_text(pl, LV_SYMBOL_PLAY " Lecture");
    lv_obj_set_style_text_color(pl, lv_color_hex(0x06281A), 0);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_28, 0);
    std::string toast = clean_title(item.title);
    if (!g.player_name.empty())
      toast += "\nlecture sur " + g.player_name;  // second line in the toast
    auto *d = new CbData{this, InputEvent::LAUNCHER_ACTIVATE, idx, toast};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(playbtn, btn_event_cb, LV_EVENT_CLICKED, d);

    if (item.has_children) {
      lv_obj_t *eps = lv_button_create(actions);
      lv_obj_set_height(eps, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(eps, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(eps, 2, 0);
      lv_obj_set_style_border_color(eps, lv_color_hex(COL_ACCENT), 0);
      lv_obj_set_style_shadow_width(eps, 0, 0);
      lv_obj_set_style_radius(eps, 30, 0);
      lv_obj_set_style_pad_hor(eps, 18, 0);
      lv_obj_set_style_pad_ver(eps, 10, 0);
      lv_obj_set_style_pad_column(eps, 9, 0);
      lv_obj_set_flex_flow(eps, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(eps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      // Icon (built-in font, has the symbol) + text (accented font for "Épisodes").
      lv_obj_t *ei = lv_label_create(eps);
      lv_label_set_text(ei, LV_SYMBOL_LIST);
      lv_obj_set_style_text_color(ei, lv_color_hex(COL_ACCENT), 0);
      lv_obj_set_style_text_font(ei, &lv_font_montserrat_20, 0);
      lv_obj_t *el = lv_label_create(eps);
      lv_label_set_text(el, item.media_type == "audiobook" ? "Chapitres" : "Épisodes");
      lv_obj_set_style_text_color(el, lv_color_hex(COL_ACCENT), 0);
      this->set_text_font_(el, this->font_small_, &lv_font_montserrat_20);
      auto *dc = new CbData{this, InputEvent::LAUNCHER_OPEN_CHILDREN, idx};
      g_cbdata.push_back(dc);
      lv_obj_add_event_cb(eps, btn_event_cb, LV_EVENT_CLICKED, dc);
    }
  };

  // An episode/chapter row: [ thumbnail | title | ▶ ]. The row itself is NOT clickable so a
  // press-drag scrolls the list; only the round play button starts playback (avoids launching
  // a chapter while sliding). Episode thumbnails come from our signed, cached /thumb proxy and
  // are used VERBATIM (the HMAC signature covers src+size; appending would break it). They use
  // the dedicated detail pool (thumb_slots 1..; slot 0 is the header), never the grid covers.
  auto make_episode_row = [&](const QuickItem &item, int idx) {
    lv_obj_t *row = lv_obj_create(grid);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 12, 0);
    lv_obj_set_style_pad_right(row, 22, 0);  // keep the ▶ clear of the list scrollbar
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
#ifdef USE_HA_DASHBOARD_LAUNCHER
    // Every episode gets a fixed 56px thumbnail box (dark placeholder). A thumb slot is only
    // assigned while the row is on screen — assign_episode_thumbs_ recycles the limited pool as
    // the list scrolls, so all episodes get an image, not just the first few.
    if (!item.cover_url.empty()) {
      lv_obj_t *img = lv_image_create(row);
      lv_obj_set_size(img, 56, 56);
      lv_obj_set_style_radius(img, 8, 0);
      lv_obj_set_style_clip_corner(img, true, 0);
      lv_obj_set_style_bg_color(img, lv_color_hex(0x15151C), 0);
      lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);  // placeholder until the thumb loads
      if (idx >= 0 && idx < (int) this->ep_img_.size()) {
        this->ep_row_[idx] = row;
        this->ep_img_[idx] = img;
        this->ep_url_[idx] = item.cover_url;
      }
    }
#endif
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, clean_title(item.title).c_str());
    lv_obj_set_style_text_color(l, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(l, this->font_medium_, &lv_font_montserrat_28);

    // Round green play button (the only trigger).
    lv_obj_t *playbtn = lv_button_create(row);
    lv_obj_set_size(playbtn, 64, 64);
    lv_obj_set_style_radius(playbtn, 32, 0);
    lv_obj_set_style_bg_color(playbtn, lv_color_hex(0x3DD68C), 0);
    lv_obj_set_style_shadow_width(playbtn, 0, 0);
    lv_obj_set_style_pad_all(playbtn, 0, 0);
    lv_obj_t *pl = lv_label_create(playbtn);
    lv_label_set_text(pl, LV_SYMBOL_PLAY);
    lv_obj_center(pl);
    lv_obj_set_style_text_color(pl, lv_color_hex(0x06281A), 0);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_28, 0);
    std::string toast = clean_title(item.title);
    if (!g.player_name.empty())
      toast += "\nlecture sur " + g.player_name;  // second line in the toast
    auto *d = new CbData{this, InputEvent::LAUNCHER_ACTIVATE, idx, toast};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(playbtn, btn_event_cb, LV_EVENT_CLICKED, d);
  };

  // Detail level: a header row [ back chevron | parent cover | big title ].
  if (detail) {
    lv_obj_t *head = lv_obj_create(grid);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(head, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_set_style_pad_bottom(head, 8, 0);
    lv_obj_set_style_pad_column(head, 12, 0);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    // Back chevron (icon uses the built-in Montserrat, which has the symbol glyphs).
    lv_obj_t *back = lv_button_create(head);
    lv_obj_set_size(back, 56, 56);
    lv_obj_set_style_bg_color(back, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_set_style_radius(back, 12, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_obj_t *bi = lv_label_create(back);
    lv_label_set_text(bi, LV_SYMBOL_LEFT);
    lv_obj_center(bi);
    lv_obj_set_style_text_font(bi, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bi, lv_color_hex(COL_TEXT), 0);
    auto *db = new CbData{this, InputEvent::LAUNCHER_BACK, -1};
    g_cbdata.push_back(db);
    lv_obj_add_event_cb(back, btn_event_cb, LV_EVENT_CLICKED, db);

#ifdef USE_HA_DASHBOARD_LAUNCHER
    // Parent cover at native 64px on the detail pool's slot 0 (a dedicated slot, not a grid
    // slot — so it never disturbs the grid). bind_cover_ hides it until the 64px variant
    // arrives, so there's no big-image flash.
    if (!g.thumb_slots.empty() && g.thumb_slots[0] != nullptr && !L->detail_cover_url().empty()) {
      lv_obj_t *hc = lv_image_create(head);
      lv_obj_set_size(hc, 64, 64);
      lv_obj_set_style_clip_corner(hc, true, 0);
      lv_obj_set_style_radius(hc, 8, 0);
      this->bind_cover_(hc, g.thumb_slots[0], L->detail_cover_url() + "?size=64");
    }
#endif

    lv_obj_t *ht = lv_label_create(head);
    lv_obj_set_flex_grow(ht, 1);
    lv_label_set_long_mode(ht, LV_LABEL_LONG_DOT);
    lv_label_set_text(ht, clean_title(L->detail_title()).c_str());
    lv_obj_set_style_text_color(ht, lv_color_hex(COL_TEXT), 0);
    this->set_text_font_(ht, this->font_medium_, &lv_font_montserrat_28);
  }

  if (L->status() != LauncherStatus::READY) {
    if (L->status() == LauncherStatus::EMPTY) {
      lv_obj_t *lbl = lv_label_create(grid);
      lv_label_set_text(lbl, detail ? "Aucun épisode" : "Rien ici pour le moment");
      lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
      this->set_text_font_(lbl, this->font_medium_, &lv_font_montserrat_28);
    } else {
      // LOADING or ERROR (we auto-retry) -> loading indicator, never "indisponible".
      lv_obj_t *box = lv_obj_create(grid);
      lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(box, 0, 0);
      lv_obj_set_style_pad_all(box, 30, 0);
      lv_obj_set_style_pad_row(box, 12, 0);
      lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t *ic = lv_label_create(box);
      lv_label_set_text(ic, LV_SYMBOL_REFRESH);
      lv_obj_set_style_text_font(ic, &lv_font_montserrat_48, 0);
      lv_obj_set_style_text_color(ic, lv_color_hex(COL_ACCENT), 0);
      lv_obj_t *lbl = lv_label_create(box);
      lv_label_set_text(lbl, "Chargement...");
      lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
      this->set_text_font_(lbl, this->font_medium_, &lv_font_montserrat_28);
    }
    return;
  }

  const std::vector<QuickItem> &items = L->items();
#ifdef USE_HA_DASHBOARD_LAUNCHER
  if (detail) {
    this->ep_row_.assign(items.size(), nullptr);
    this->ep_img_.assign(items.size(), nullptr);
    this->ep_url_.assign(items.size(), std::string());
    this->ep_slot_.assign(items.size(), -1);
  }
#endif
  for (size_t i = 0; i < items.size(); i++) {
    if (!detail) {
      make_cover_tile(items[i], (int) i);  // cover grid tile (play + optional drill button)
    } else {
      make_episode_row(items[i], (int) i);  // episode/chapter row (thumbnail + title)
    }
  }

  // Detail: "load more" affordance when more pages are available.
  if (detail && L->has_more()) {
    if (L->loading_more()) {
      lv_obj_t *lbl = lv_label_create(grid);
      lv_label_set_text(lbl, "Chargement...");
      lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
      this->set_text_font_(lbl, this->font_small_, &lv_font_montserrat_20);
    } else {
      add_button("Charger plus", &lv_font_montserrat_28, COL_ACCENT, InputEvent::LAUNCHER_LOAD_MORE, -1);
    }
  }

#ifdef USE_HA_DASHBOARD_LAUNCHER
  // Detail: episode thumbnails use the recyclable pool (thumb_slots[1..]; slot 0 = header).
  // Assign the on-screen rows their slots now; the rest are (re)assigned as the list scrolls.
  if (detail) {
    this->ep_list_ = grid;
    for (size_t s = 1; s < g.thumb_slots.size(); s++)
      if (g.thumb_slots[s] != nullptr)
        this->ep_thumb_slots_.push_back(g.thumb_slots[s]);
    this->thumb_owner_.assign(this->ep_thumb_slots_.size(), -1);
    this->assign_episode_thumbs_();
  }
  // Start the serial cover downloads (one at a time; the rest follow via advance_cover_).
  if (!this->cover_queue_.empty())
    this->cover_queue_[0]->update();
#endif
}

const Group *LvglRenderer::first_launcher_(const ViewModel &vm) const {
  if (vm.groups == nullptr)
    return nullptr;
  for (const auto &g : *vm.groups)
    if (g.is_launcher && g.launcher != nullptr)
      return &g;
  return nullptr;
}

void LvglRenderer::build_now_playing_() {
  this->now_playing_scr_ = this->make_screen_();

  lv_obj_t *root = lv_obj_create(this->now_playing_scr_);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 24, 0);
  lv_obj_set_style_pad_row(root, 18, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Back chevron (top-left, outside the centered column).
  lv_obj_t *back = lv_button_create(this->now_playing_scr_);
  lv_obj_add_flag(back, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(back, 56, 56);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 16, 16);
  lv_obj_set_style_bg_color(back, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_radius(back, 12, 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_t *bi = lv_label_create(back);
  lv_label_set_text(bi, LV_SYMBOL_LEFT);
  lv_obj_center(bi);
  lv_obj_set_style_text_font(bi, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(bi, lv_color_hex(COL_TEXT), 0);
  {
    auto *d = new CbData{this, InputEvent::BACK, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(back, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  this->np_cover_img_ = lv_image_create(root);
  lv_obj_set_size(this->np_cover_img_, 280, 280);
  lv_obj_set_style_radius(this->np_cover_img_, 16, 0);
  lv_obj_set_style_clip_corner(this->np_cover_img_, true, 0);

  this->np_title_lbl_ = lv_label_create(root);
  lv_obj_set_width(this->np_title_lbl_, lv_pct(90));
  lv_label_set_long_mode(this->np_title_lbl_, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(this->np_title_lbl_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(this->np_title_lbl_, "—");
  lv_obj_set_style_text_color(this->np_title_lbl_, lv_color_hex(COL_TEXT), 0);
  this->set_text_font_(this->np_title_lbl_, this->font_large_, &lv_font_montserrat_48);

  this->np_sub_lbl_ = lv_label_create(root);
  lv_label_set_text(this->np_sub_lbl_, "");
  lv_obj_set_style_text_color(this->np_sub_lbl_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->np_sub_lbl_, this->font_medium_, &lv_font_montserrat_28);

  // Playback progress: [elapsed] [====bar====] [total].
  lv_obj_t *pr = lv_obj_create(root);
  lv_obj_set_width(pr, lv_pct(86));
  lv_obj_set_height(pr, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(pr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pr, 0, 0);
  lv_obj_set_style_pad_all(pr, 0, 0);
  lv_obj_set_style_pad_column(pr, 14, 0);
  lv_obj_set_style_margin_top(pr, 20, 0);
  lv_obj_set_flex_flow(pr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(pr, LV_OBJ_FLAG_SCROLLABLE);

  this->np_time_lbl_ = lv_label_create(pr);
  lv_label_set_text(this->np_time_lbl_, "0:00");
  lv_obj_set_style_text_color(this->np_time_lbl_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->np_time_lbl_, this->font_small_, &lv_font_montserrat_20);

  this->np_progress_ = lv_bar_create(pr);
  lv_obj_set_flex_grow(this->np_progress_, 1);
  lv_obj_set_height(this->np_progress_, 8);
  lv_bar_set_range(this->np_progress_, 0, 1000);
  lv_obj_set_style_bg_color(this->np_progress_, lv_color_hex(0x3A3A44), LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->np_progress_, lv_color_hex(0x3DD68C), LV_PART_INDICATOR);

  this->np_dur_lbl_ = lv_label_create(pr);
  lv_label_set_text(this->np_dur_lbl_, "0:00");
  lv_obj_set_style_text_color(this->np_dur_lbl_, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(this->np_dur_lbl_, this->font_small_, &lv_font_montserrat_20);

  lv_obj_t *tr = lv_obj_create(root);
  lv_obj_set_size(tr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(tr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tr, 0, 0);
  lv_obj_set_style_pad_all(tr, 0, 0);
  lv_obj_set_style_pad_column(tr, 24, 0);
  lv_obj_set_style_margin_top(tr, 12, 0);
  lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(tr, LV_OBJ_FLAG_SCROLLABLE);

  // Transport buttons. prev/next are grey circles; play/pause is bigger and green (the
  // primary action), with a dark glyph.
  auto tbtn = [this, tr](const char *sym, int size, InputEvent ev, bool primary) -> lv_obj_t * {
    lv_obj_t *b = lv_button_create(tr);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, size / 2, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(primary ? 0x3DD68C : COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, primary ? &lv_font_montserrat_48 : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(primary ? 0x06281A : COL_TEXT), 0);
    auto *d = new CbData{this, ev, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
    return l;
  };
  tbtn(LV_SYMBOL_PREV, 72, InputEvent::NP_PREV, false);
  this->np_pp_icon_ = tbtn(LV_SYMBOL_PLAY, 120, InputEvent::NP_PLAY_PAUSE, true);
  tbtn(LV_SYMBOL_NEXT, 72, InputEvent::NP_NEXT, false);

  // Volume row: speaker icon + horizontal slider (absolute 0..100).
  lv_obj_t *vr = lv_obj_create(root);
  lv_obj_set_width(vr, lv_pct(86));
  lv_obj_set_height(vr, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(vr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(vr, 0, 0);
  lv_obj_set_style_pad_all(vr, 0, 0);
  lv_obj_set_style_pad_column(vr, 18, 0);
  lv_obj_set_style_margin_top(vr, 22, 0);
  lv_obj_set_flex_flow(vr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(vr, LV_OBJ_FLAG_SCROLLABLE);

  // Volume icon doubles as a mute toggle.
  lv_obj_t *vbtn = lv_button_create(vr);
  lv_obj_set_size(vbtn, 56, 56);
  lv_obj_set_style_radius(vbtn, 28, 0);
  lv_obj_set_style_bg_opa(vbtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(vbtn, 0, 0);
  lv_obj_set_style_pad_all(vbtn, 0, 0);
  this->np_vol_lbl_ = lv_label_create(vbtn);
  lv_label_set_text(this->np_vol_lbl_, ICON_VOL_ON);
  lv_obj_center(this->np_vol_lbl_);
  // MDI font: a true slashed speaker for mute (the built-in symbol set has none).
  this->set_text_font_(this->np_vol_lbl_, this->font_icons_, &lv_font_montserrat_28);
  lv_obj_set_style_text_color(this->np_vol_lbl_, lv_color_hex(COL_MUTED), 0);
  auto *vmd = new CbData{this, InputEvent::NP_MUTE, -1};
  g_cbdata.push_back(vmd);
  lv_obj_add_event_cb(vbtn, btn_event_cb, LV_EVENT_CLICKED, vmd);

  this->np_vol_slider_ = lv_slider_create(vr);
  lv_obj_set_flex_grow(this->np_vol_slider_, 1);
  lv_obj_set_height(this->np_vol_slider_, 12);
  lv_slider_set_range(this->np_vol_slider_, 0, 100);
  lv_obj_set_style_bg_color(this->np_vol_slider_, lv_color_hex(0x3A3A44), LV_PART_MAIN);
  lv_obj_set_style_bg_color(this->np_vol_slider_, lv_color_hex(0x3DD68C), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(this->np_vol_slider_, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_set_style_pad_all(this->np_vol_slider_, 10, LV_PART_KNOB);  // bigger touch target
  lv_obj_add_event_cb(this->np_vol_slider_, np_vol_slider_cb, LV_EVENT_RELEASED, this);

  // Secondary controls: shuffle, repeat.
  lv_obj_t *cr = lv_obj_create(root);
  lv_obj_set_size(cr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(cr, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cr, 0, 0);
  lv_obj_set_style_pad_all(cr, 0, 0);
  lv_obj_set_style_pad_column(cr, 28, 0);
  lv_obj_set_style_margin_top(cr, 20, 0);
  lv_obj_set_flex_flow(cr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(cr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(cr, LV_OBJ_FLAG_SCROLLABLE);

  auto cbtn = [this, cr](const char *sym, InputEvent ev) -> lv_obj_t * {
    lv_obj_t *b = lv_button_create(cr);
    lv_obj_set_size(b, 56, 56);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_TILE), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(COL_MUTED), 0);
    auto *d = new CbData{this, ev, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(b, btn_event_cb, LV_EVENT_CLICKED, d);
    return l;
  };
  this->np_shuffle_lbl_ = cbtn(LV_SYMBOL_SHUFFLE, InputEvent::NP_SHUFFLE);
  this->np_repeat_lbl_ = cbtn(LV_SYMBOL_LOOP, InputEvent::NP_REPEAT);
}

void LvglRenderer::render_now_playing_(const ViewModel &vm) {
  const Group *g = this->first_launcher_(vm);
  if (g != nullptr && g->launcher != nullptr && this->np_title_lbl_ != nullptr) {
    const NowPlaying &np = g->launcher->now_playing();
    lv_label_set_text(this->np_title_lbl_, np.title.empty() ? "—" : clean_title(np.title).c_str());
    lv_label_set_text(this->np_sub_lbl_, clean_title(np.artist).c_str());
    if (this->np_pp_icon_ != nullptr)
      lv_label_set_text(this->np_pp_icon_, np.playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (this->np_shuffle_lbl_ != nullptr)
      lv_obj_set_style_text_color(this->np_shuffle_lbl_, lv_color_hex(np.shuffle ? COL_ACCENT : COL_MUTED), 0);
    if (this->np_repeat_lbl_ != nullptr)
      lv_obj_set_style_text_color(this->np_repeat_lbl_,
                                  lv_color_hex(np.repeat != "off" && !np.repeat.empty() ? COL_ACCENT : COL_MUTED),
                                  0);
    // Playback progress bar + elapsed/total times.
    if (this->np_progress_ != nullptr) {
      int v = np.duration_s > 0 ? (int) ((long) np.position_s * 1000 / np.duration_s) : 0;
      lv_bar_set_value(this->np_progress_, v < 0 ? 0 : (v > 1000 ? 1000 : v), LV_ANIM_OFF);
      lv_label_set_text(this->np_time_lbl_, fmt_time(np.position_s).c_str());
      lv_label_set_text(this->np_dur_lbl_, fmt_time(np.duration_s).c_str());
    }
    // Reflect the current volume on the slider (RELEASED-only callback -> no feedback loop).
    if (this->np_vol_slider_ != nullptr && np.volume >= 0)
      lv_slider_set_value(this->np_vol_slider_, np.volume, LV_ANIM_OFF);
    // Mute state on the volume icon (muted -> slashed speaker + accent).
    if (this->np_vol_lbl_ != nullptr) {
      lv_label_set_text(this->np_vol_lbl_, np.muted ? ICON_VOL_OFF : ICON_VOL_ON);
      lv_obj_set_style_text_color(this->np_vol_lbl_, lv_color_hex(np.muted ? COL_ACCENT : COL_MUTED), 0);
    }
#ifdef USE_HA_DASHBOARD_LAUNCHER
    if (this->np_cover_slot_ != nullptr && this->np_cover_img_ != nullptr) {
      int idx = -1;
      for (size_t s = 0; s < this->cover_slot_list_.size(); s++)
        if (this->cover_slot_list_[s] == this->np_cover_slot_) {
          idx = (int) s;
          break;
        }
      lv_image_set_src(this->np_cover_img_, this->np_cover_slot_->get_lv_image_dsc());
      if (idx >= 0)
        this->cover_widget_list_[idx] = this->np_cover_img_;
      // Same skip/hide policy as the launcher covers (the now-playing slot is shared with the
      // detail header, so it may currently hold a different image). Downloaded directly here,
      // not via the launcher's serial queue (this isn't the launcher screen). The loaded URL is
      // committed in on_cover_ready_, so a re-render mid-download won't wrongly skip it.
      if (np.cover_url.empty()) {
        lv_obj_add_flag(this->np_cover_img_, LV_OBJ_FLAG_HIDDEN);
      } else if (idx >= 0 && this->cover_url_list_[idx] == np.cover_url) {
        lv_obj_clear_flag(this->np_cover_img_, LV_OBJ_FLAG_HIDDEN);  // already loaded -> show
      } else {
        lv_obj_add_flag(this->np_cover_img_, LV_OBJ_FLAG_HIDDEN);  // hide until decoded
        if (idx >= 0)
          this->cover_pending_url_[idx] = np.cover_url;
        this->np_cover_slot_->set_url(np.cover_url);
        this->np_cover_slot_->update();
      }
    }
#endif
  }
  if (this->now_playing_scr_ != nullptr)
    lv_screen_load(this->now_playing_scr_);
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
      // Media: the centre icon is the play/pause control (tap centre = play/pause); the title
      // is dropped on the round screen. Other types show their type icon.
      lv_label_set_text(this->card_icon_, c->type == CardType::MEDIA_PLAYER
                                              ? (c->is_on() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY)
                                              : icon_for(*c));
      lv_obj_set_style_text_color(this->card_icon_, lv_color_hex(col), 0);
    }
    if (this->card_arc_ != nullptr) {
      lv_arc_set_value(this->card_arc_, (int) lroundf(c->display_value() * 1000));  // optimistic preview
      lv_obj_set_style_arc_color(this->card_arc_, lv_color_hex(c->is_on() ? accent_for(*c) : 0x232330),
                                 LV_PART_INDICATOR);
    }
  }
  // Side buttons: media = prev/next; cover = open/close (direction-aware, tap centre = stop).
  bool is_media = (c != nullptr && c->type == CardType::MEDIA_PLAYER);
  bool is_cover = (c != nullptr && c->type == CardType::COVER);
  if (this->card_prev_btn_ != nullptr && this->card_next_btn_ != nullptr) {
    if (is_media || is_cover) {
      if (is_cover) {
        bool gate = cover_kind_of(*c) == (int) CoverKind::GATE;
        lv_label_set_text(this->card_prev_lbl_, gate ? LV_SYMBOL_LEFT : LV_SYMBOL_UP);
        lv_label_set_text(this->card_next_lbl_, gate ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN);
        if (this->card_prev_cb_ != nullptr)
          this->card_prev_cb_->event = InputEvent::COVER_OPEN;
        if (this->card_next_cb_ != nullptr)
          this->card_next_cb_->event = InputEvent::COVER_CLOSE;
      } else {
        lv_label_set_text(this->card_prev_lbl_, LV_SYMBOL_PREV);
        lv_label_set_text(this->card_next_lbl_, LV_SYMBOL_NEXT);
        if (this->card_prev_cb_ != nullptr)
          this->card_prev_cb_->event = InputEvent::MEDIA_PREV;
        if (this->card_next_cb_ != nullptr)
          this->card_next_cb_->event = InputEvent::MEDIA_NEXT;
      }
      lv_obj_remove_flag(this->card_prev_btn_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(this->card_next_btn_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(this->card_prev_btn_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(this->card_next_btn_, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // When bottom buttons are shown, lift the centre column so the name clears them; and for
  // media drop the redundant state text (the play/pause icon already conveys it).
  if (this->card_center_ != nullptr)
    lv_obj_set_style_translate_y(this->card_center_, (is_media || is_cover) ? (int) (-0.11f * this->h_) : 0, 0);
  if (this->card_value_ != nullptr) {
    if (is_media)
      lv_obj_add_flag(this->card_value_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(this->card_value_, LV_OBJ_FLAG_HIDDEN);
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

    case NavState::NOW_PLAYING:
      this->render_now_playing_(vm);
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
