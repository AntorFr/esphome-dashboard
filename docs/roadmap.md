# Roadmap & état d'avancement

> Point d'entrée pour reprendre le contexte entre sessions. Cocher au fil de l'eau.
> Plan complet archivé dans le plan d'implémentation approuvé.

## Décisions clés (rappel)

- Composant C++ externe ESPHome ([ADR-0001](adr/0001-cpp-external-component.md)).
- Core partagé + 2 renderers (Dial+D1001) en parallèle ([ADR-0002](adr/0002-parallel-renderers.md)).
- LVGL via API C directe ([ADR-0003](adr/0003-raw-lvgl.md)).
- i18n tables clé→texte ([ADR-0004](adr/0004-i18n-tables.md)).
- Robustesse tactile / cycle de vie des vues ([ADR-0005](adr/0005-touch-view-lifecycle.md)) — **non négociable**.
- Config auto-injectée depuis `display:` ([ADR-0006](adr/0006-config-auto-inject.md)).
- Scope v1 démarre **light + switch**, luminosité seule, indispo = grisé.

## Jalons

### M0 — Base de connaissance (docs/) ✅ FAIT
- [x] `.gitignore` + arborescence repo
- [x] `architecture.md`
- [x] `lessons-learned.md` (bug tactile en tête)
- [x] `adr/` 0001→0006 (+ index)
- [x] `ux-interaction.md` + `interaction-dial.md` + `interaction-d1001.md`
- [x] `color-system.md` + `config-reference.md`
- [x] `roadmap.md`
- [x] Mémoires Claude

### M0.5 — Prototype de navigation (validation archi) ✅ FAIT
Objectif : valider que l'architecture logicielle compile sur les **deux** cibles avec une
navigation minimale (veille / menu / groupe / card), dashboard quasi vide.
- [x] Composant C++ `ha_dashboard` : `model` + `Controller` (machine à états) + interface
      `Renderer` + `LvglRenderer` (LVGL 9.5 C API) + glue Component (encodeur/bouton Dial)
- [x] Schéma Python `__init__.py` (profile / groups / cards) + codegen
- [x] Packages board : `m5stack_dial.yaml` (GC9A01A) et `reterminal_d1001.yaml` (P4/MIPI-DSI)
- [x] `esphome config` **valide sur les deux** (test_dial.yaml, test_d1001.yaml)
- [x] `esphome compile` Dial (ESP32-S3) — OK (RAM 8.2% / Flash 28.8%)
- [x] `esphome compile` D1001 (ESP32-P4) — OK (RAM 5.3% / Flash 31.2%)
- [ ] Test sur matériel réel (gestes, robustesse tactile ADR-0005)

### M1a — Carte `switch` (binary) avec binding HA réel ✅ FAIT
- [x] Card liée à un `switch::Switch*` (`switch_id`) — état live `->state`, action `->toggle()`
- [x] Re-render auto sur changement d'état HA (`add_on_state_callback`)
- [x] Schéma : `type: switch` requiert `switch_id` ; `language: fr|en`
- [x] Configs de test réalistes `examples/dial_office.yaml` (Dial) et `examples/d1001_office.yaml`
      (D1001, WiFi via esp32_hosted/C6) — mêmes menus Focus + Outils du device réel
- [x] `esphome config` OK (tous) ; `compile` Dial OK ; `compile` D1001 réaliste en validation
- [ ] Menus Musique/Chauffage/Portail = cartes media_player/climate/cover (M5)

> Écart assumé vs cible : config actuelle = `profile:` + include board explicite ;
> l'auto-inject via substitution `display` + `entrypoint.yaml` (ADR-0006) reste à faire.
> LVGL est hébergé par le composant `lvgl:` d'ESPHome (runtime), nos widgets sont construits
> en C++ via l'API C LVGL (cf. note ADR-0003).

### M1 — Walking skeleton : card `light` bout-en-bout (Dial + D1001)
- [ ] Modèle `Card`/`Group` (+ drapeau `available`)
- [ ] `Controller` minimal (état Card + réglage)
- [ ] Interfaces `Renderer` + `InputAdapter`
- [ ] `LvglRenderer` + `layout` responsive + `widget_value_arc`
- [ ] `InputEncoderTouch` (Dial) + `InputTouch` (D1001)
- [ ] Binding HA : état in + commande luminosité out (throttlé)
- [ ] `color_resolver` (live RGB + off-gray + grisé indispo)
- [ ] i18n de base ; `entrypoint.yaml` + `boards/*` + `lvgl_base` + fonts
- [ ] Cadrer init LVGL / buffers / PSRAM (compléter ADR-0003)
- **DoD** : sur Dial, l'arc reflète la luminosité HA live, molette règle (throttlé), tap
  toggle ; idem vue détail D1001. Valide les 3 couches et les 2 renderers.

### M2 — Card `switch` (complète le scope v1 minimal)
- [ ] Type `switch` (anneau plein, tap=toggle), état grisé/indispo

### M3 — Navigation complète
- [ ] Menu radial (Dial) / onglets+grille (D1001)
- [ ] Jauge de retour, veille horloge, timeout, saut groupe mono-appareil, pagination

### M4 — Finitions v1
- [ ] Animations fidèles (focus grossit, dots qui s'estompent)
- [ ] i18n complète fr/en, header/veille D1001
- [ ] `examples/` + `tests/` + README/docs config + CI (`esphome config` / `--only-generate`)

### M5 — Cards additionnelles (post-v1)
- [ ] `cover`, puis `media_player` (volume + play/pause), puis `climate`
- [ ] `homeassistant_addon` porté/nettoyé (domaines non-natifs)

### M6 — Module « Histoires & Musique » (D1001, music-library) — cadré ([ADR-0007](adr/0007-music-launcher-module.md))
> Lanceur enfants : grille de pochettes → un tap lance playlist/podcast/épisode.
> Échange = REST direct ESP→music-library ; enceinte + profil figés en YAML par appareil.
> Maquettes : `music-launcher-mockups.html` (5 écrans D1001).
- [x] **music-library** : endpoint compact `GET /api/v1/quick/{owner}` (PR #1, release v0.12.0-beta)
- [ ] Couche 1 : `LauncherModule` (config base_url/owner/queue_id + état favoris/sélection)
- [ ] Port `MusicLibraryBackend` (out) : `fetch_favorites` / `play` / `fetch_children` + adapter HTTP
- [ ] Renderer D1001 : `render_launcher()` (grille pochettes via `online_image`) +
      `render_launcher_detail()` (épisodes/chapitres) ; `NavState` LAUNCHER / LAUNCHER_DETAIL
- [ ] Entrée de menu dédiée (onglet) + schéma `music_library:` dans `__init__.py` (codegen)
- [ ] Hand-off lecture → card `media_player` existante (now playing) OU toast de confirmation
- [ ] **À trancher** : transport du drill-down épisodes/chapitres — endpoint JSON compact
      à ajouter côté music-library (préféré) vs HTML existant `/media/{id}/episodes` (rejeté)

### Tactile GSL3670 ✅ FAIT — VALIDÉ MATÉRIEL (composant officiel)
- [x] Composant officiel `github://clydebarrow/esphome@gsl3670` (modèle `seeed-reterminal-d1001`,
      firmware binaire dédié + calibration/INT par défaut). Notre vendorisation abandonnée
      (firmware générique non fiable, 0xb0=00).
- [x] **Clé** : reset tactile = **XL9535 pin 14** (pas 12 !) — sinon init bancale + INT muet.
- [x] **Validé sur D1001 réel** : init OK, taps onglets/tuiles fonctionnels.

### Bring-up D1001 ✅ VALIDÉ MATÉRIEL
- [x] Écran MIPI-DSI (modèle SEEED-RETERMINAL-D1001) — **PSRAM 200MHz** (sinon underrun→noir)
- [x] **Rétroéclairage** : `light: monochromatic` sur `LCD_PWM` (GPIO14), ALWAYS_ON
- [x] WiFi via C6 (`esp32_hosted` SDIO, brochage schéma) — connecté, IP obtenue
- [x] `ha_dashboard` + 4 switches HA — boot stable, 0 reset

## Backlog / plus tard
- **Fidélité visuelle aux mockups** (PRIORITÉ) : remplacer le rendu placeholder par le design
  néon — D1001 : header (heure/date/météo) + onglets de groupes + grille de tuiles + vue détail
  (arc ouvert + slider + toggle) ; Dial : carrousel de cards (arc ouvert), menu radial, jauge
  de retour. Cf. `docs/interaction-*.md` et les prototypes HTML.
- **Mode calibration tactile** : assistant à l'écran (tap des coins) qui mesure et stocke les
  bornes en `preferences`, au lieu de hardcoder `calibration:` par board.
- Underrun DSI résiduel : vérifier qu'il reste à 0 en charge (sinon tuner pclk/bounce).
- v2 : renderer **Nextion** (Sonoff NSPanel) réutilisant modèle de cards + langage visuel.
- Auto-rotation IMU (D1001).
- Contrôle couleur/température des lumières.
- Plusieurs entités par card.
