# ADR-0003 — Piloter LVGL via l'API C directe

**Statut** : Accepté (détails d'intégration à figer en M1)

## Contexte
Les widgets visés sont custom et non triviaux : arc de valeur **ouvert en bas ~60°**, menu
radial dont l'item focalisé **grossit**, **jauge de retour** à deux arcs neutres. Le composant
`lvgl:` natif d'ESPHome (déclaratif YAML) ne permet pas facilement ces objets sur mesure.

## Décision
Le `LvglRenderer` **pilote LVGL via son API C directe** depuis le C++, en s'appuyant sur le
`display` ESPHome (mipi_spi pour le Dial, mipi_dsi pour le D1001) et le `touchscreen`. C'est
l'approche de l'`esphome-dial` existant (`dial_menu_lvgl`).

## Conséquences
- (+) Contrôle total du rendu et des animations, fidélité aux prototypes.
- (−) Doit gérer init LVGL, buffers/PSRAM, et le hook flush vers le display ESPHome.
- À cadrer en M1 : comment LVGL est initialisé et comment le buffer/flush se branche sur le
  `display` ESPHome (PSRAM obligatoire sur D1001). S'inspirer de `reference/esphome-dial`.
- Contrainte forte associée : [ADR-0005](0005-touch-view-lifecycle.md) (robustesse tactile).
