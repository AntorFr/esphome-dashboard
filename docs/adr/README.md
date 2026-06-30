# Journal des décisions d'architecture (ADR)

Format léger : *Contexte → Décision → Conséquences*. Statut : `Accepté` sauf mention.

| # | Décision | Statut |
|---|----------|--------|
| [0001](0001-cpp-external-component.md) | Cœur = composant C++ externe ESPHome | Accepté |
| [0002](0002-parallel-renderers.md) | Core partagé + 2 renderers (Dial+D1001) en parallèle | Accepté |
| [0003](0003-raw-lvgl.md) | Piloter LVGL via l'API C directe | Accepté |
| [0004](0004-i18n-tables.md) | i18n par tables de traduction clé→texte | Accepté |
| [0005](0005-touch-view-lifecycle.md) | Robustesse tactile & cycle de vie des vues | Accepté |
| [0006](0006-config-auto-inject.md) | Config auto-injectée depuis `display:` | Accepté |
| [0007](0007-music-launcher-module.md) | Module music launcher (D1001) en REST direct vers music-library | Accepté |
