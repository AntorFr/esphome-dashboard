# ADR-0001 — Cœur = composant C++ externe ESPHome

**Statut** : Accepté

## Contexte
La logique visée (modèle de cards, machine à états de navigation, résolution de couleur
alignée HA, widgets custom comme l'arc ouvert et le menu radial) dépasse ce que permettent
confortablement les lambdas/packages YAML purs. L'existant `esphome-dial` était déjà un
composant C++ (`dial_menu`), et l'utilisateur maîtrise ce mode (cf. `esphome-tx-ultimate`).

## Décision
Implémenter le cœur comme **composant externe C++** ESPHome (`components/ha_dashboard/`),
distribué via `external_components` / packages GitHub. Conventions calquées sur
`esphome-tx-ultimate` : `components/<name>/{__init__.py, *.h, *.cpp}`, schéma Python qui
**valide et auto-injecte** des entités, `tests/` en validation-schéma, events kebab-case.

## Conséquences
- (+) Type-safe, puissant, testable, contrôle total du rendu.
- (+) Réutilisation des patterns éprouvés de l'utilisateur.
- (−) Barrière de contribution plus haute que du YAML pur.
- Distribution : `external_components: github://AntorFR/esphome-dashboard@main`.
