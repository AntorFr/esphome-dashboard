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
- Contrainte forte associée : [ADR-0005](0005-touch-view-lifecycle.md) (robustesse tactile).

## Approche réalisée (prototype M0.5)
On **héberge le runtime LVGL sur le composant `lvgl:` d'ESPHome** (qui gère init, buffers,
flush vers le `display`, tick `lv_timer_handler`, et l'`indev` tactile/encodeur) ET on
**construit tous nos widgets en C++ via l'API C LVGL** (`lv_obj_create`, `lv_button_create`,
`lv_screen_load`, events natifs…). Aucun widget déclaré en YAML — cohérent avec « API C
directe ». Avantage : pas de réinvention de l'intégration display/PSRAM (le point dur), tout
en gardant le contrôle total du rendu. Le composant `ha_dashboard` déclare `DEPENDENCIES =
["lvgl"]` et construit ses écrans en différé (quand `lv_display_get_default()` est prêt).
LVGL ciblé : **9.5.0** (version embarquée par ESPHome 2026.6).
