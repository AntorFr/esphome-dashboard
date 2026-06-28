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
  // --- Écran de veille ---
  this->idle_scr_ = this->make_screen_();
  lv_obj_add_flag(this->idle_scr_, LV_OBJ_FLAG_CLICKABLE);
  {
    lv_obj_t *lbl = lv_label_create(this->idle_scr_);
    lv_label_set_text(lbl, "Veille\ntouchez pour réveiller");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_MUTED), 0);
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
        if (!c->available) {
          std::snprintf(buf, sizeof(buf), "indisponible");
        } else if (c->type == CardType::SWITCH) {
          std::snprintf(buf, sizeof(buf), "%s", c->on ? "Allumé" : "Éteint");
        } else {  // light
          if (c->on)
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
