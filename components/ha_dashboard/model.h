#pragma once
// COUCHE 1 — Modèle de card (HW-agnostique). Voir docs/architecture.md.
#include <cstdint>
#include <string>
#include <vector>
#include "esphome/components/climate/climate.h"
#include "esphome/components/cover/cover.h"
#include "esphome/components/homeassistant_addon/homeassistant_media_player.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace online_image {
class OnlineImage;  // cover image slots (pointer only here; see lvgl_renderer)
}
namespace ha_dashboard {

class LauncherModule;  // layer-1 music launcher (see launcher_module.h); pointer only here

enum class CardType : uint8_t {
  LIGHT = 0,
  SWITCH = 1,
  COVER = 2,
  MEDIA_PLAYER = 3,
  CLIMATE = 4,
};

// Définition logique d'une card. Le binding HA se fait via des objets ESPHome :
// switch (plateforme homeassistant) / cover+climate (homeassistant_addon) /
// media_player (homeassistant_addon, classe custom).
struct Card {
  CardType type{CardType::LIGHT};
  std::string entity;
  std::string name;
  uint32_t color{0};       // override couleur (0xRRGGBB) ; sinon défaut par domaine
  bool has_color{false};

  switch_::Switch *sw{nullptr};
  cover::Cover *cover{nullptr};
  climate::Climate *climate{nullptr};
  homeassistant_addon::HomeassistantMediaPlayer *media{nullptr};

  // État runtime local (utilisé pour les types sans binding, ex. light stub).
  bool on{false};
  float value_local{0.0f};

  // Debounce de réglage (encodeur) : aperçu optimiste affiché tout de suite, commande
  // HA envoyée seulement après un court silence (cf. Controller::tick).
  bool has_pending{false};
  float pending_value{0.0f};  // valeur cible normalisée 0..1 en attente d'envoi
  uint32_t pending_ms{0};

  bool available() const { return true; }  // disponibilité HA fine = jalon suivant

  // État on/off "actif" effectif selon le type.
  bool is_on() const {
    switch (this->type) {
      case CardType::SWITCH:
        return this->sw != nullptr ? this->sw->state : this->on;
      case CardType::COVER:
        return this->cover != nullptr ? this->cover->position > 0.0f : this->on;
      case CardType::MEDIA_PLAYER:
        return this->media != nullptr
                   ? this->media->get_state() == homeassistant_addon::MediaPlayerState::PLAYING
                   : this->on;
      case CardType::CLIMATE:
        return this->climate != nullptr ? this->climate->mode != climate::CLIMATE_MODE_OFF : this->on;
      case CardType::LIGHT:
      default:
        return this->on;
    }
  }

  // Valeur normalisée 0..1 pour l'arc / la barre de progression.
  float value() const {
    switch (this->type) {
      case CardType::COVER:
        return this->cover != nullptr ? this->cover->position : 0.0f;
      case CardType::MEDIA_PLAYER:
        return this->media != nullptr ? this->media->get_volume() : 0.0f;
      case CardType::CLIMATE: {
        if (this->climate == nullptr)
          return 0.0f;
        auto traits = this->climate->get_traits();
        float lo = traits.get_visual_min_temperature();
        float hi = traits.get_visual_max_temperature();
        if (hi <= lo)
          return 0.0f;
        float t = (this->climate->target_temperature - lo) / (hi - lo);
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      }
      case CardType::LIGHT:
        return this->is_on() ? this->value_local : 0.0f;
      case CardType::SWITCH:
      default:
        return this->is_on() ? 1.0f : 0.0f;
    }
  }

  // Valeur à afficher : aperçu en attente (optimiste) sinon valeur live.
  float display_value() const { return this->has_pending ? this->pending_value : this->value(); }

  // La card affiche-t-elle une valeur réglable (barre / arc) ? (switch = non)
  bool has_value() const { return this->type != CardType::SWITCH; }
};

struct Group {
  std::string name;
  std::string icon;
  std::vector<Card> cards;

  // Music Library launcher group (D1001 tab): no entity cards — renders a cover/title grid
  // driven by `launcher`. See ADR-0007. `launcher` is owned by HaDashboard.
  bool is_launcher{false};
  LauncherModule *launcher{nullptr};
  // Pool of online_image slots (one per favourite index) for covers; declared in YAML.
  std::vector<online_image::OnlineImage *> cover_slots;
};

}  // namespace ha_dashboard
}  // namespace esphome
