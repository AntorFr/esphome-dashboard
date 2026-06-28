# Leçons apprises — anti-patterns à éviter

> Capitalise sur l'expérience de `esphome-dial` et des libs voisines. Chaque entrée :
> *symptôme → cause → parade adoptée*. À relire avant d'attaquer le renderer/input.

## 1. ⚠️ CRITIQUE — Zones tactiles perdues au changement de vue

**Symptôme (vécu sur esphome-dial)** : énormément d'allers-retours de debug. En changeant
de tuile / de card, le **touch ne répondait plus** : la zone tactile n'était pas
rechargée/recalculée lors de la transition. Comportement intermittent, pénible à reproduire.

**Causes racines typiques :**
- **Hit-testing manuel par coordonnées cachées** : on mémorise des rectangles/positions au
  moment du rendu, puis on teste le toucher contre ces coords. Au changement de vue, le
  layout change mais les coords mises en cache **ne sont pas recalculées** → zones mortes ou
  décalées. (C'est exactement ce que font les prototypes JS — acceptable pour un mockup,
  **proscrit** en prod.)
- **Destroy/recreate d'objets LVGL** à chaque transition : laisse des **handlers
  pendouillants**, un `indev` qui pointe sur un objet détruit, ou des objets recréés sans
  ré-enregistrement des callbacks.
- **Layout modifié sans invalidation** : les hit-areas LVGL ne sont pas recalculées avant
  la prochaine lecture tactile.
- **Plusieurs handlers concurrents** selon l'état, qui se marchent dessus.

**Parade adoptée (imposée — cf. [ADR-0005](adr/0005-touch-view-lifecycle.md)) :**
1. **Événements LVGL natifs sur de vrais `lv_obj`** : chaque élément cliquable est un objet
   LVGL avec sa hit-area → LVGL fait le hit-test, **toujours cohérent avec le layout courant**.
2. **Update-in-place > destroy/recreate** : pool d'objets réutilisés, on met à jour plutôt
   que de détruire/recréer.
3. **Contrat de cycle de vie de vue** idempotent : `mount()` / `update(state)` / `unmount()`.
   Transition = `unmount` complet puis `mount`.
4. **`indev` rattaché une seule fois**, persistant aux changements d'écran (`lv_scr_load`).
5. **Invalidation explicite** (`lv_obj_update_layout` / invalidate) après tout changement de
   layout.
6. Si un hit-test manuel reste nécessaire (menu radial Dial), **recalculer les zones par la
   même fonction de layout que le rendu**, à chaque (re)mount, liées à l'instance de vue.

## 2. Coordonnées en dur / non responsive

**Symptôme** : `ha_deck` (et beaucoup de configs LVGL) utilisent du positionnement absolu →
ne passe pas d'un écran à l'autre, déprécié.

**Parade** : moteur de **layout responsive** dérivant tout de la géométrie de l'écran
(rond 240 vs portrait 800×1280). Aucune constante de pixel dans la logique des widgets.

## 3. Flood de l'API HA sur entrée continue

**Symptôme** : rotation rapide de l'encodeur → rafale de commandes → API HA saturée, latence,
valeurs qui « sautent » (m5-dial a dû ajouter `send_value_delay` / `send_value_lock`).

**Parade** : **throttling** des commandes sortantes (≈200 ms) + lock anti-rebond ;
affichage optimiste local, réconciliation à la réception de l'état HA.

## 4. i18n par chaînes en dur

**Symptôme** : esphome-dial a des chaînes en dur → ajout de langue = patch dans le code,
fonts d'accents gérées au cas par cas.

**Parade** : **tables de traduction clé→texte** (en/fr extensible), fonts chargées selon la
langue ([ADR-0004](adr/0004-i18n-tables.md)).

## 5. Config peu ergonomique pour l'utilisateur final

**Symptôme** : esphome-dial demandait pas mal de plomberie YAML (apps, ids, etc.).

**Parade** : config **ultra-minimale auto-injectée** depuis `display:` — l'utilisateur ne
décrit que `groups`/`cards` ([ADR-0006](adr/0006-config-auto-inject.md)).

## 6. ESP32-P4 / LVGL haute résolution (récent dans ESPHome)

**Symptôme** : support P4 + MIPI-DSI + LVGL grand écran encore en optimisation (ESPHome ≥ 2026.5).

**Parade** : valider d'abord sur le Dial (terrain stable), s'appuyer sur les configs P4
connues (`chrisdunnname/esphome-p4-86-panel`, `jtenniswood/esphome-lvgl` Guition P4) pour le
bring-up D1001.
