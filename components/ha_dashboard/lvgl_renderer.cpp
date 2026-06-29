#include "lvgl_renderer.h"
#include <cstdio>
#include <vector>

namespace esphome {
namespace ha_dashboard {

// Palette (cf. docs/color-system.md). Tokens HA à brancher plus tard.
static constexpr uint32_t COL_BG = 0x0E0E12;
static constexpr uint32_t COL_TILE = 0x17171E;
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

void LvglRenderer::build_dashboard_(const std::vector<Group> &groups) {
  this->dashboard_scr_ = this->make_screen_();
  lv_obj_t *root = lv_obj_create(this->dashboard_scr_);
  lv_obj_set_size(root, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(root, 0, 0);
  lv_obj_set_style_pad_all(root, 16, 0);
  lv_obj_set_style_pad_row(root, 14, 0);
  lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  // Header (clock/weather to come — placeholder title for now).
  this->dash_header_ = lv_label_create(root);
  lv_label_set_text(this->dash_header_, "Dashboard");
  lv_obj_set_style_text_color(this->dash_header_, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_font(this->dash_header_, &lv_font_montserrat_20, 0);

  // Tabs row (one button per group).
  lv_obj_t *tabs = lv_obj_create(root);
  lv_obj_set_width(tabs, lv_pct(100));
  lv_obj_set_height(tabs, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tabs, 0, 0);
  lv_obj_set_style_pad_all(tabs, 0, 0);
  lv_obj_set_style_pad_column(tabs, 8, 0);
  lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
  lv_obj_set_scroll_dir(tabs, LV_DIR_HOR);
  this->tab_btns_.clear();
  for (size_t gi = 0; gi < groups.size(); gi++) {
    lv_obj_t *btn = lv_button_create(tabs);
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, groups[gi].name.c_str());
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    auto *d = new CbData{this, InputEvent::SELECT_GROUP, (int) gi};
    g_cbdata.push_back(d);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, d);
    this->tab_btns_.push_back(btn);
  }

  // Content area : one grid container per group (only the active one is shown).
  lv_obj_t *content = lv_obj_create(root);
  lv_obj_set_width(content, lv_pct(100));
  lv_obj_set_flex_grow(content, 1);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 0, 0);

  this->group_grids_.clear();
  this->group_tiles_.clear();
  for (size_t gi = 0; gi < groups.size(); gi++) {
    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_HIDDEN);

    std::vector<Tile> tiles;
    for (size_t ci = 0; ci < groups[gi].cards.size(); ci++) {
      const Card &card = groups[gi].cards[ci];
      lv_obj_t *tile = lv_button_create(grid);
      lv_obj_set_size(tile, lv_pct(48), 130);
      lv_obj_set_style_bg_color(tile, lv_color_hex(COL_TILE), 0);
      lv_obj_set_style_radius(tile, 16, 0);
      lv_obj_set_style_pad_all(tile, 16, 0);
      lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

      Tile t;
      t.root = tile;

      // Top row: icon (left) + state label (right), like the mockup.
      lv_obj_t *toprow = lv_obj_create(tile);
      lv_obj_set_width(toprow, lv_pct(100));
      lv_obj_set_height(toprow, LV_SIZE_CONTENT);
      lv_obj_set_style_bg_opa(toprow, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(toprow, 0, 0);
      lv_obj_set_style_pad_all(toprow, 0, 0);
      lv_obj_set_flex_flow(toprow, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(toprow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      t.icon = lv_label_create(toprow);
      lv_label_set_text(t.icon, LV_SYMBOL_POWER);
      lv_obj_set_style_text_font(t.icon, &lv_font_montserrat_20, 0);

      t.state = lv_label_create(toprow);
      lv_label_set_text(t.state, state_label(card));

      lv_obj_t *name = lv_label_create(tile);
      lv_label_set_text(name, card.name.c_str());
      lv_obj_set_style_text_color(name, lv_color_hex(COL_TEXT), 0);
      lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);

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

  // Tabs: highlight the active group.
  for (size_t i = 0; i < this->tab_btns_.size(); i++) {
    bool on = (int) i == active;
    lv_obj_set_style_bg_color(this->tab_btns_[i], lv_color_hex(on ? 0x2A2A33 : COL_TILE), 0);
    lv_obj_set_style_border_width(this->tab_btns_[i], on ? 2 : 0, 0);
    lv_obj_set_style_border_color(this->tab_btns_[i], lv_color_hex(COL_ACCENT), 0);
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
