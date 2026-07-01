# ADR-0006 — Config auto-injectée depuis `display:`

**Statut** : Accepté

## Contexte
Objectif d'ergonomie : l'utilisateur final ne doit décrire que ses `groups`/`cards`, pas la
plomberie (board, display driver, LVGL, fonts, souscriptions HA). `esphome-dial` demandait
trop de YAML manuel.

Contrainte ESPHome : un composant C++ **ne peut pas injecter un bloc `packages:`** — les
packages sont résolus très tôt, avant la validation du composant.

## Décision
Approche en deux temps :
1. **Substitution `display`** + un **package d'entrée** : `packages/entrypoint.yaml` utilise
   `${display}` pour `!include` le bon `boards/<display>.yaml` (esp32/psram/framework/bus/
   display/touch/encodeur) + `lvgl_base.yaml` + `fonts/*` (selon `language`).
2. **Codegen du composant `dashboard:`** : `__init__.py` valide le schéma et **auto-crée** les
   sensors/text_sensors HA (souscription état+attributs), les actions de commande, le renderer
   LVGL et l'input adapter du board — façon auto-injection d'entités de `esphome-tx-ultimate`.

Config utilisateur cible :
```yaml
substitutions:
  display: dial
packages:
  dashboard: github://AntorFR/esphome-dashboard/packages/entrypoint.yaml@main
dashboard:
  style: neon
  language: fr
  groups: [ ... ]
```

## Conséquences
- (+) Config minimale, peu d'erreurs possibles côté utilisateur.
- (−) Magie d'injection à documenter (debug moins évident) → `config-reference.md` doit
  expliquer ce qui est généré.
- (−) Le board reste sélectionné par substitution (limite technique assumée), pas « 100% depuis
  le composant ».
