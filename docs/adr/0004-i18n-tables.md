# ADR-0004 — i18n par tables de traduction clé→texte

**Statut** : Accepté

## Contexte
`esphome-dial` avait des chaînes en dur (« Allumé », « Éteint », « Pause »…) → ajout de langue
coûteux, gestion des fonts d'accents au cas par cas.

## Décision
Centraliser les libellés dans des **tables de traduction clé→texte** (`model/i18n.*`),
sélectionnées par `language:` (en/fr en v1, extensible). Les fonts adaptées à la langue sont
chargées via `packages/fonts/fonts_<lang>.yaml` (auto-injectées selon `language`).

Exemples de clés : `state.on`, `state.off`, `state.unavailable`, `media.playing`,
`media.paused`.

## Conséquences
- (+) Ajout de langue = ajout d'une table + d'un package de fonts, sans toucher la logique.
- (+) Pas de chaîne UI en dur dans le renderer.
- (−) Une indirection de plus ; nécessite que tout texte affiché passe par une clé.
