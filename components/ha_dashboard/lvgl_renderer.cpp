#include "lvgl_renderer.h"
#include <cstdio>
#include <vector>
#include "esphome/core/log.h"

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

lv_obj_t *LvglRenderer::make_flex_container_(lv_obj_t *parent) {
  lv_obj_t *cont = lv_obj_create(parent);
  lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 10, 0);
  lv_obj_set_style_pad_row(cont, 8, 0);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return cont;
}

lv_obj_t *LvglRenderer::make_button_(lv_obj_t *parent, const char *text, InputEvent event, int index) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_width(btn, lv_pct(90));
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_TILE), 0);
  lv_obj_set_style_radius(btn, 12, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_border_width(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl);

  auto *d = new CbData{this, event, index};  // NOLINT(cppcoreguidelines-owning-memory)
  g_cbdata.push_back(d);
  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, d);
  return btn;
}

void LvglRenderer::build(const std::vector<Group> &groups) {
  // --- Idle screen ---
  this->idle_scr_ = this->make_screen_();
  lv_obj_add_flag(this->idle_scr_, LV_OBJ_FLAG_CLICKABLE);
  {
    lv_obj_t *lbl = lv_label_create(this->idle_scr_);
    lv_label_set_text(lbl, "tap to wake");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(lbl);
    auto *d = new CbData{this, InputEvent::WAKE, -1};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(this->idle_scr_, btn_event_cb, LV_EVENT_CLICKED, d);
  }

  // --- Menu (liste des groupes) ---
  this->menu_scr_ = this->make_screen_();
  {
    lv_obj_t *cont = this->make_flex_container_(this->menu_scr_);
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "Menu");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    this->group_btns_.clear();
    for (size_t gi = 0; gi < groups.size(); gi++) {
      lv_obj_t *btn = this->make_button_(cont, groups[gi].name.c_str(), InputEvent::SELECT, (int) gi);
      this->group_btns_.push_back(btn);
    }
  }

  // --- Un écran par groupe (cards) ---
  this->group_scrs_.clear();
  this->card_btns_.clear();
  for (size_t gi = 0; gi < groups.size(); gi++) {
    lv_obj_t *scr = this->make_screen_();
    lv_obj_t *cont = this->make_flex_container_(scr);
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, groups[gi].name.c_str());
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);

    std::vector<lv_obj_t *> btns;
    for (size_t ci = 0; ci < groups[gi].cards.size(); ci++) {
      const char *type = groups[gi].cards[ci].type == CardType::SWITCH ? "switch" : "light";
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s  (%s)", groups[gi].cards[ci].name.c_str(), type);
      lv_obj_t *btn = this->make_button_(cont, buf, InputEvent::SELECT, (int) ci);
      btns.push_back(btn);
    }
    this->make_button_(cont, "< Retour", InputEvent::BACK, -1);
    this->group_scrs_.push_back(scr);
    this->card_btns_.push_back(btns);
  }

  // --- Détail d'une card (réutilisé, update-in-place) ---
  this->card_scr_ = this->make_screen_();
  {
    lv_obj_t *cont = this->make_flex_container_(this->card_scr_);
    this->card_title_ = lv_label_create(cont);
    lv_label_set_text(this->card_title_, "");
    lv_obj_set_style_text_color(this->card_title_, lv_color_hex(COL_ACCENT), 0);

    this->card_value_ = lv_label_create(cont);
    lv_label_set_text(this->card_value_, "");
    lv_obj_set_style_text_color(this->card_value_, lv_color_hex(COL_TEXT), 0);

    this->make_button_(cont, "Toggle", InputEvent::TOGGLE, -1);
    this->make_button_(cont, "< Retour", InputEvent::BACK, -1);
  }

  // D1001 merged dashboard (tabs + tile grid).
  if (!this->round_)
    this->build_dashboard_(groups);
}

// Accent color for a card: explicit override, else per-type default (cf. color-system.md).
static uint32_t accent_for(const Card &c) {
  if (c.has_color)
    return c.color;
  return c.type == CardType::SWITCH ? 0x3DD68C : 0xFFB020;
}

static const char *state_label(const Card &c) {
  if (!c.available())
    return "indisponible";
  return c.is_on() ? "Allumé" : "Éteint";
}

// LVGL built-in symbol per card type (real domain icons need an icon font — TODO).
static const char *icon_for(const Card &c) {
  switch (c.type) {
    case CardType::SWITCH:
      return LV_SYMBOL_POWER;
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

  lv_obj_t *weather = lv_label_create(this->dash_header_);
  lv_label_set_text(weather, "");  // TODO: bind a HA weather entity
  lv_obj_set_style_text_color(weather, lv_color_hex(COL_MUTED), 0);
  this->set_text_font_(weather, this->font_small_, &lv_font_montserrat_20);

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
      lv_label_set_text(t.state, state_label(card));
      this->set_text_font_(t.state, this->font_small_, &lv_font_montserrat_20);

      lv_obj_t *name = lv_label_create(tile);
      lv_label_set_text(name, card.name.c_str());
      lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
      this->set_text_font_(name, this->font_medium_, &lv_font_montserrat_28);

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
        lv_label_set_text(t.state, state_label(c));
        lv_obj_set_style_text_color(t.state, lv_color_hex(col), 0);
      }
    } else {
      lv_obj_add_flag(this->group_grids_[gi], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (this->dashboard_scr_ != nullptr)
    lv_screen_load(this->dashboard_scr_);
}

void LvglRenderer::set_focus_(std::vector<lv_obj_t *> &buttons, int focused) {
  for (size_t i = 0; i < buttons.size(); i++) {
    lv_obj_set_style_border_width(buttons[i], (int) i == focused ? 3 : 0, 0);
  }
}

const Card *LvglRenderer::current_card_(const ViewModel &vm) const {
  if (vm.groups == nullptr || vm.group_index < 0 || vm.group_index >= (int) vm.groups->size())
    return nullptr;
  const Group &g = (*vm.groups)[vm.group_index];
  if (vm.card_index < 0 || vm.card_index >= (int) g.cards.size())
    return nullptr;
  return &g.cards[vm.card_index];
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
      this->set_focus_(this->group_btns_, vm.group_index);
      if (this->menu_scr_ != nullptr)
        lv_screen_load(this->menu_scr_);
      break;

    case NavState::GROUP:
      if (vm.group_index >= 0 && vm.group_index < (int) this->card_btns_.size())
        this->set_focus_(this->card_btns_[vm.group_index], vm.card_index);
      if (vm.group_index >= 0 && vm.group_index < (int) this->group_scrs_.size())
        lv_screen_load(this->group_scrs_[vm.group_index]);
      break;

    case NavState::CARD: {
      const Card *c = this->current_card_(vm);
      if (c != nullptr && this->card_title_ != nullptr && this->card_value_ != nullptr) {
        lv_label_set_text(this->card_title_, c->name.c_str());
        char buf[32];
        if (!c->available()) {
          std::snprintf(buf, sizeof(buf), "indisponible");
        } else if (c->type == CardType::SWITCH) {
          std::snprintf(buf, sizeof(buf), "%s", c->is_on() ? "Allumé" : "Éteint");
        } else {  // light
          if (c->is_on())
            std::snprintf(buf, sizeof(buf), "%d%%", (int) (c->value * 100));
          else
            std::snprintf(buf, sizeof(buf), "Éteint");
        }
        lv_label_set_text(this->card_value_, buf);
      }
      if (this->card_scr_ != nullptr)
        lv_screen_load(this->card_scr_);
      break;
    }
  }
}

}  // namespace ha_dashboard
}  // namespace esphome
