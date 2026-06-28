#pragma once
// COUCHE 1 — Modèle de card (HW-agnostique). Voir docs/architecture.md.
#include <cstdint>
#include <string>
#include <vector>
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace ha_dashboard {

// v1 démarre avec light + switch (cf. docs/roadmap.md). Ordre = valeurs passées par le codegen.
enum class CardType : uint8_t {
  LIGHT = 0,
  SWITCH = 1,
};

// Définition logique d'une card. L'état runtime est stubbé pour ce premier prototype
// (binding HA réel = jalon suivant).
struct Card {
  CardType type{CardType::LIGHT};
  std::string entity;
  std::string name;
  uint32_t color{0};       // override couleur (0xRRGGBB) ; sinon défaut par domaine
  bool has_color{false};

  // Binding HA : pour une card switch, pointe sur un esphome switch (souvent une
  // plateforme `homeassistant`) -> état live via ->state, action via ->toggle().
  switch_::Switch *sw{nullptr};

  // État runtime local (utilisé tant qu'il n'y a pas de binding, ex. light stub).
  bool on{false};
  float value{0.0f};       // 0..1 (luminosité / position / volume)

  // État on/off effectif : binding si présent, sinon état local.
  bool is_on() const { return this->sw != nullptr ? this->sw->state : this->on; }
  bool available() const { return true; }  // disponibilité HA fine = jalon suivant
};

struct Group {
  std::string name;
  std::string icon;
  std::vector<Card> cards;
};

}  // namespace ha_dashboard
}  // namespace esphome
