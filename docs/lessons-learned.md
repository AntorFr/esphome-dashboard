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

## 7. reTerminal D1001 — tactile GSL3670 : pas natif ESPHome, mais driver dispo

**Symptôme** : le D1001 utilise un contrôleur tactile **GSL3670 (SiLead)**, pour lequel
**ESPHome n'a aucune plateforme native** (`touchscreen:` dispo : axs15231, cst816, cst226,
ektf2232, ft5x06, gt911, tt21100, xpt2046… mais pas gsl/silead). Sans driver : écran + WiFi
OK mais **pas d'entrée tactile**.

**Parade (chemin clair)** : Seeed fournit un driver **`esp_lcd_touch_gsl3670`** (framework
Espressif `esp_lcd_touch`) dans le BSP `Seeed-Studio/reTerminal-D1001/components/esp_lcd_touch_gsl3670` :
- API : `esp_lcd_touch_new_i2c_gsl3670(io, config, &touch)` ; adresse **0x40**
  (`ESP_LCD_TOUCH_IO_I2C_GSL3670_ADDRESS`).
- ⚠️ `gsl_point_id.c` = **firmware/coefficients à uploader à l'init** (spécifique SiLead).
- Bus : I2C0 (SDA GPIO37 / SCL GPIO38), INT GPIO16, RST via XL9535 EXP_GPO12.

→ **SOLUTION RETENUE (validée matériel)** : utiliser le **composant officiel** de clydebarrow
(`github://clydebarrow/esphome@gsl3670`, + base `touchscreen` de la PR), `model:
seeed-reterminal-d1001` (qui télécharge le **firmware binaire dédié** `seeed-d1001-fw.bin` et
fournit calibration/transform/INT par défaut). Notre composant vendorisé a été abandonné : son
firmware générique (GSLX670_FW) ne s'initialisait pas de façon fiable sur ce panneau (0xb0=00).

⚠️ **PIÈGE CRITIQUE — le reset tactile est sur XL9535 pin 14, PAS 12.** Le schéma laissait
penser EXP_GPO12, mais le bon défaut (modèle officiel) est **`reset_pin: {xl9535: <hub>,
number: 14}`**. Avec le pin 12, le GSL n'était jamais vraiment reset → init bancale et
**interruption INT (GPIO16) qui ne se déclenche jamais → aucune touche lue** (le driver est
piloté par interruption, pas par polling). Corrigé en pin 14 → tactile pleinement fonctionnel.
Config minimale : `platform: gsl3670, model: seeed-reterminal-d1001, i2c_id: <panel>,
reset_pin: {xl9535: <hub>, number: 14}` (le reste vient des défauts du modèle).

## 8. Brochage D1001 — source de vérité = schéma officiel Seeed

Le BSP GitHub `Seeed-Studio/reTerminal-D1001` **ne contient pas** les pins en dur (tirés de
l'esp-bsp Espressif via dépendance). La source fiable est le **schéma PDF officiel**
(`reTerminal D1001_sch.pdf`). Pins extraits (rev v01) :
- **SDIO→C6** : CMD=GPIO6, CLK=GPIO11, D0..D3=GPIO7/8/9/10, C6_CHIP_PU(reset)=GPIO13.
- **I2C1 (système)** : SDA=GPIO20, SCL=GPIO21 → XL9535 **0x20**, RTC 0x51, IMU 0x6A, codec 0x18.
- **I2C0 (écran/cam)** : SDA=GPIO37, SCL=GPIO38 → touch 0x40, caméra 0x26.
- **Écran** : LCD_PWR_EN=EXP_GPO0, LCD_RST=EXP_GPO2, backlight/alim EXP_GPO7 (via XL9535).
- **MicroSD** : CMD=GPIO44, CLK=GPIO43, D0..D3=GPIO39/40/41/42.

## 9. Long blocking work in setup() trips the task watchdog

**Symptom**: the GSL3670 firmware upload (~4356 sequential I2C writes) ran inside `setup()`
and took ~5s, tripping the ESP-IDF task watchdog → `rst:0xc (SW_CPU_RESET)` boot loop on the
ESP32-P4.

**Fix**: feed the watchdog periodically during long synchronous loops —
`App.feed_wdt()` every 64 iterations (`#include "esphome/core/application.h"`). Confirmed on
hardware: device boots cleanly, no resets.

## 10. ESP32-P4 MIPI-DSI framebuffer underrun at 800x1280

**Symptom**: on-device logs spam `lcd.dsi: can't fetch data from external memory fast enough,
underrun happens` (~hundreds of occurrences) at 800×1280 — PSRAM bandwidth can't keep up with
the DSI scanout, causing visual glitches/tearing.

**Mitigations to try (TODO)**: lower `pclk_frequency` in the display model/override, tune
PSRAM speed/mode (octal/120MHz if supported), reduce LVGL draw-buffer pressure, or enable a
bounce buffer for the DSI. Not blocking for bring-up; affects display quality only.
