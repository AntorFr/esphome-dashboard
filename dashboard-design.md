# ESPHome Dashboard — Brief de conception (v1)

> Document de handoff vers Claude Code. Capture les décisions prises en phase
> design. Objectif : faire évoluer `esphome-dial` en une lib graphique modulaire
> multi-écrans pour piloter Home Assistant.

---

## 1. Objectif & périmètre

Lib graphique modulaire ESPHome + LVGL pour construire des dashboards de contrôle
HA, configurable en YAML, avec tous les imports gérés par la lib.

**Périmètre v1** (deux écrans LVGL) :
- **M5Stack Dial** — rond 240×240, ESP32-S3, encodeur rotatif + tactile.
- **Seeed reTerminal D1001** — 8" 800×1280 portrait, ESP32-P4 + ESP32-C6, tactile.

**v2 (plus tard)** : **Sonoff NSPanel** — écran Nextion (pas LVGL). Réutilise le
*langage visuel* et le *modèle de cards*, mais le rendu est réécrit (`.tft` + série).

---

## 2. Hardware cible

| | M5Dial | reTerminal D1001 |
|---|---|---|
| Puce | ESP32-S3 | ESP32-P4 (+ ESP32-C6 WiFi/BLE) |
| Écran | rond 240×240 GC9A01 (SPI) | 800×1280 MIPI-DSI portrait |
| Driver ESPHome | `mipi_spi` / display existant | `mipi_dsi` + `es32_hosted` (pour le C6) |
| Tactile | FT3267 | GSL3670 |
| Entrée | encodeur (GPIO40/41) + bouton (GPIO42) + touch | tactile seul + IMU (auto-rotation) |

**Risque connu** : LVGL haute résolution sur ESP32-P4 dans ESPHome est récent et en
cours d'optimisation. Démarrer/valider sur le Dial (terrain stable), le D1001 ensuite.

---

## 3. Architecture (à respecter)

Séparation stricte en trois couches, façon ports & adapters :

1. **Modèle de card (commun)** — définition logique d'une card : type, binding HA,
   état, résolution de couleur, actions. Indépendant du hardware. Partagé par tous
   les écrans (et réutilisable par le futur renderer Nextion).
2. **Renderer (par techno)** — v1 : un **renderer LVGL** partagé par Dial + D1001,
   avec une couche de **layout responsive** (jamais de coordonnées en dur).
   Slot prévu pour un renderer Nextion en v2.
3. **Input adapter (par appareil)** — encodeur+touch (Dial) vs tactile (D1001).

But : une **même card logique, deux rendus** (focus plein écran sur Dial / tuile sur
D1001). Pas deux UI parallèles.

Base existante à généraliser : composant C++ `dial_menu` + packages YAML
(`m5stack_dial.yaml`, `dial_menu_lvgl.yaml`, fonts), apps switch/cover/climate/
media_player, idle screen, i18n, `homeassistant_addon` pour les entités HA non
natives (cover/climate/media_player).

---

## 4. Modèle de navigation

Niveaux logiques (mêmes sur les deux écrans, gestes différents) :

```
Veille (horloge) → Menu (groupes) → Groupe (cartes) → réglage d'une carte
```

- Les **items du menu = des groupes d'appareils**, définis à la configuration.
- Retour = remonter d'un niveau. Timeout d'inactivité → Veille.
- Si un groupe ne contient qu'un seul appareil, sauter le niveau Groupe.

---

## 5. Interaction Dial (écran rond)

Le bouton de l'encodeur est dur/peu agréable → minimiser son usage, **pas de
double-appui**. Tactile privilégié, avec retour d'état visuel.

| Geste | Action |
|---|---|
| Slide ← / → (tactile) | carte précédente / suivante dans le groupe |
| Rotation molette | régler la carte affichée (la nav est sur le tactile, pas la molette → pas de mode) |
| Tap centre | toggle on/off |
| Slide ↑ (tactile) **ou** maintien du bouton | retour au menu — **même jauge** |
| Appui molette (clic) | ouvrir la carte focalisée (menu) |

**Jauge de retour** : deux arcs **blancs neutres** (distincts de l'arc de valeur
coloré) qui partent des bords de l'ouverture du bas (autour des points de
pagination) et montent des deux côtés jusqu'à **se rejoindre en haut** = validation.
Relâcher avant la jonction **annule** (les arcs redescendent). Slide ↑ et maintien
du bouton remplissent la même jauge. Durée maintien ≈ 1,1 s ; seuil slide ≈ 130 px.

**Menu (lanceur circulaire)** : groupes en **vrais cercles** sur un anneau
(`radius`), celui en focus **grossit** (`button_size` 50 → `button_size_focused` 58)
en arrivant en haut quand on tourne. Centre = nom du groupe focalisé. Tap d'une
icône = ouvrir ce groupe ; clic molette = ouvrir le groupe focalisé.

**Pagination** : points en bas de l'arc (carte n/total), qui s'estompent pendant
que la jauge de retour monte.

---

## 6. Déclinaison D1001 (grand écran portrait)

- **Portrait** comme cible par défaut (auto-rotation IMU possible plus tard).
- Les deux niveaux Menu/Groupe **fusionnent sur une vue** : groupes en **onglets**
  en haut + appareils du groupe actif en **grille de tuiles** en dessous.
- Header : heure, date, météo, pièce.
- Swipe = changer d'onglet. **Tap sur une tuile = ouvrir la carte en détail**
  (version focus plein cadre pour réglage fin).

---

## 7. Style « Néon » + système de couleurs

Base visuelle : fonds très sombres, **arcs épais**, gros chiffres blancs, typo
lisible (lisible à 240 px comme à 2 m sur le mur).

**Couleur = fonction de l'entité**, alignée sur Home Assistant. Ordre de résolution
de la teinte « active » d'une card :

1. **Couleur live** de l'entité (lumières uniquement : `rgb_color` quand allumée/colorée).
2. **Override manuel** YAML (`color:`).
3. **Défaut aligné HA** par domaine/état — reprendre les noms de variables HA
   (`state-<domaine>-<device_class>-<état>-color`, repli `state-active-color`), et
   **fixer les hex sur les valeurs par défaut de HA**.
4. **Inactif/off** : gris atténué (média inactif : brun `#795548` façon HA).

Décision par défaut : pour une lumière, **le live l'emporte** sur l'override
(l'override sert de repli quand pas de couleur).

Couleurs illustratives utilisées dans le prototype (à mapper sur tokens HA) :
lumière `#FFB020`, volet `#4F8CFF`, media `#A06CFF`, interrupteur `#3DD68C`,
jauge de retour neutre `#EDEDF0`.

---

## 8. Les 4 cards (v1)

Vocabulaire commun : un **arc de valeur** central qui *affiche* l'état ET *se pilote*
(molette sur Dial). L'arc **n'est pas un cercle complet** : il s'ouvre en bas
(~60°) pour laisser la place aux points de pagination / chevron de retour
(démarre en bas-droite, fait le tour, finit en bas-gauche).

| Card | Actif | Réglage | Off/inactif |
|---|---|---|---|
| Interrupteur (switch) | anneau plein, couleur domaine | tap = toggle (pas d'arc à régler) | gris |
| Variateur (light) | arc = % (live RGB si dispo) | molette = luminosité | gris, « Éteint » |
| Volet (cover) | arc = position | molette = position | — |
| Media player | arc = volume + état lecture | molette = volume, tap = play/pause | « Pause » |

---

## 9. Référence d'interaction

`dial-prototype.html` (fourni) : prototype interactif fidèle des gestes Dial
(carrousel de cartes, jauge de retour, lanceur circulaire avec focus qui grossit).
C'est la **source de vérité des interactions Dial**.

`d1001-prototype.html` (fourni) : mockup interactif du D1001 en portrait — header
(heure/date/météo), onglets de groupes, grille de tuiles, et **tap sur une tuile →
carte en détail** (arc ouvert en bas + slider + toggle). **Source de vérité du
rendu D1001.** Les deux fichiers partagent le même vocabulaire de cards et le même
système de couleurs.

---

## 10. Cible de config YAML (esprit)

Étendre le schéma `dial_menu` existant avec la notion de **groupes** et rendre le
hardware/écran sélectionnable. Exemple indicatif :

```yaml
dashboard:
  display: dial          # dial | reterminal_d1001
  style: neon
  language: fr
  groups:
    - name: "Salon"
      icon: sofa
      cards:
        - type: light
          entity: light.salon_plafond
        - type: cover
          entity: cover.salon_volet
        - type: media_player
          entity: media_player.salon_tv
    - name: "Cuisine"
      icon: kitchen
      cards:
        - type: switch
          entity: switch.cuisine_prise
```

Principes : imports/fonts/LVGL gérés par la lib ; l'utilisateur ne décrit que
`display` + `groups` + `cards`. Couleurs auto (HA) avec `color:` optionnel par card.
