# Architecture

> Source de vérité de l'architecture logicielle. Voir [`../dashboard-design.md`](../dashboard-design.md)
> pour le brief produit, et [`adr/`](adr/) pour le journal des décisions.

## 1. Objectif

Lib graphique modulaire **ESPHome + LVGL** pilotant Home Assistant, configurable en
YAML minimal. Une **même card logique**, plusieurs rendus selon l'appareil. Cible v1 :
M5Stack Dial (rond 240×240, ESP32-S3) et Seeed reTerminal D1001 (8" 800×1280, ESP32-P4).

Principe directeur : **ports & adapters** (architecture hexagonale). Le cœur logique ne
connaît ni le hardware, ni LVGL, ni les gestes — il dialogue à travers des interfaces.

## 2. Les trois couches

```
        ┌─────────────────────────────────────────────┐
 YAML → │  Config (Python schema, __init__.py)         │  display / style / language / groups[].cards[]
        └───────────────────────┬─────────────────────┘
                                 ▼ (codegen)
        ┌─────────────────────────────────────────────┐
        │  COUCHE 1 — Modèle + Controller (HW-agnostique)
        │  Card, Group, état, résolution couleur, actions
        │  Machine à états : Veille → Menu → Groupe → Card
        └───────┬───────────────────────────┬─────────┘
   port Renderer│                            │port HA backend
        ┌───────▼─────────┐        ┌─────────▼──────────┐
        │ COUCHE 2        │        │ ESPHome HA API     │
        │ Renderer LVGL   │        │ + homeassistant_addon (domaines non-natifs)
        │ (Dial + D1001)  │        └────────────────────┘
        └───────▲─────────┘
   port Input   │  events sémantiques
        ┌───────┴─────────┐
        │ COUCHE 3 Input  │  encoder+touch (Dial) | touch (D1001)
        └─────────────────┘
```

### Couche 1 — Modèle de card + Controller (HW-agnostique)
- **`Card`** (et sous-types `CardLight`, `CardSwitch`, … ) : type, entité HA liée, état
  courant, drapeau `available`, override couleur optionnel, nom, icône. Sait calculer sa
  *valeur normalisée* (0..1 pour l'arc), son *label*, sa *couleur active* (via `ColorResolver`),
  et exécuter ses *actions* (toggle, set_value) → traduites en commandes par le backend HA.
- **`Group`** : nom, icône, liste ordonnée de cards.
- **`Controller`** : machine à états de navigation (`Veille / Menu / Groupe / Card`),
  index courant, gestion du timeout d'inactivité, règle « groupe mono-appareil saute le
  niveau Groupe ». Reçoit des **events sémantiques** de la couche Input, met à jour l'état,
  demande au Renderer de refléter le nouvel état.

Cette couche **ne référence jamais** LVGL, un pin GPIO, ou un geste précis.

### Couche 2 — Renderer (port + adapter LVGL)
- **`Renderer` (interface / port)** — voir contrat §3. Une implémentation par techno.
- **`LvglRenderer`** — pilote LVGL via l'**API C directe** (cf. [ADR-0003](adr/0003-raw-lvgl.md)),
  partagé Dial + D1001. S'appuie sur un **moteur de layout responsive** (`layout.*`) : jamais
  de coordonnées en dur — tout dérive de la géométrie de l'écran (rond vs portrait).
- Widgets : `widget_value_arc` (arc ouvert ~60°), `widget_menu_radial` (Dial),
  `widget_menu_tabsgrid` (D1001), `widget_return_gauge`, `widget_idle`.
- Slot futur : renderer Nextion (v2), même contrat.

### Couche 3 — Input (port + adapters par appareil)
- **`InputAdapter` (interface / port)** : convertit les entrées physiques en **events
  sémantiques** consommés par le Controller, jamais des coordonnées brutes vers la logique.
- **`InputEncoderTouch`** (Dial) : encodeur (régler), touch (slide ←/→ nav, slide ↑ retour,
  tap toggle), bouton (maintien = jauge de retour).
- **`InputTouch`** (D1001) : touch seul (swipe onglet, tap tuile, slider/toggle en détail).

## 3. Contrats d'interface (ports)

> Signatures indicatives — à figer en M1. L'important est la **séparation des
> responsabilités**, pas la syntaxe exacte.

### Port Renderer
```cpp
class Renderer {
 public:
  virtual void render_idle(const IdleState &) = 0;
  virtual void render_menu(const MenuState &) = 0;     // groupes (radial / onglets)
  virtual void render_group(const GroupState &) = 0;   // cards d'un groupe (focus / grille)
  virtual void render_card(const Card &, const CardViewState &) = 0;  // réglage / détail
  virtual void render_return_gauge(float progress) = 0; // 0..1 ; -1 = masquée
  // Cycle de vie des vues : voir ADR-0005 (mount/update/unmount, update-in-place).
};
```

### Port InputAdapter → events
Events sémantiques émis vers le Controller (kebab-case côté config/logs) :
`next-card`, `prev-card`, `adjust` (delta), `toggle`, `open` (focalisé), `back`,
`back-gauge` (progress 0..1, peut annuler), `select-group(i)`, `select-card(i)`, `activity`
(reset timeout veille).

### Port HA backend
- **In** : souscription état + attributs d'une entité → callback qui met à jour la `Card`.
- **Out** : commande (toggle / set brightness / set position / volume / play-pause) →
  service HA, **throttlé** (cf. anti-flood, [lessons-learned](lessons-learned.md)).
- **`homeassistant_addon`** : pour les domaines non exposés nativement par ESPHome
  (cover/climate/media_player) — porté/nettoyé depuis esphome-dial en M5.

## 4. Configuration & codegen (auto-inject)

- `display` est une **substitution** ESPHome : `packages/entrypoint.yaml` `!include` le bon
  `boards/<display>.yaml` + `lvgl_base` + `fonts/*`. (Un composant C++ ne peut pas injecter
  `packages:`, résolus trop tôt → on passe par la substitution. Voir [ADR-0006](adr/0006-config-auto-inject.md).)
- Le composant **`dashboard:`** (`components/ha_dashboard/__init__.py`) valide le schéma et
  **auto-crée par codegen** : les sensors/text_sensors HA (souscription), les actions de
  commande, le renderer LVGL et l'input adapter du board. L'utilisateur n'écrit que
  `display` + 1 ligne de package + le bloc `dashboard:`. Détail : [config-reference](config-reference.md).

## 5. Invariants à respecter

- La couche 1 ne dépend d'aucune techno de rendu/d'entrée.
- Aucune coordonnée en dur dans le renderer (layout responsive).
- Robustesse tactile = règles de [ADR-0005](adr/0005-touch-view-lifecycle.md) (événements
  LVGL natifs, update-in-place, cycle de vie de vue, indev rattaché une fois).
- Tout envoi de commande HA est throttlé.
- Conventions de structure/nommage : façon `esphome-tx-ultimate` (cf. [ADR-0001](adr/0001-cpp-external-component.md)).

## 6. Arborescence

Voir le plan d'implémentation et la section « Structure du repo ». Résumé :
`components/ha_dashboard/{model,renderer,renderer_lvgl,input}` + `components/homeassistant_addon`,
`packages/{boards,fonts,styles}`, `examples/`, `tests/`, `docs/`.
