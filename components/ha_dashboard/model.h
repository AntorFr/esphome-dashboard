#pragma once
// COUCHE 1 — Modèle de card (HW-agnostique). Voir docs/architecture.md.
#include <cstdint>
#include <string>
#include <vector>

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

  // État runtime (placeholder en attendant le binding HA)
  bool on{false};
  float value{0.0f};       // 0..1 (luminosité / position / volume)
  bool available{true};
};

struct Group {
  std::string name;
  std::string icon;
  std::vector<Card> cards;
};

}  // namespace ha_dashboard
}  // namespace esphome
