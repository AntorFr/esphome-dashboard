# Interaction — Seeed reTerminal D1001 (portrait 800×1280)

> Extrait fidèle de [`../d1001-prototype.html`](../d1001-prototype.html) — **source de vérité**.
> Pixels = maquette (écran mock 360×576) → **à dériver en responsive** pour 800×1280.
> Portrait par défaut (auto-rotation IMU possible plus tard). Tactile seul.

## Structure de l'écran

```
┌─────────────────────────────┐
│ 14:32            ☁ 19°       │  header : heure (40px) / date (14px #8A8A92)
│ vendredi 27 juin             │          + météo (icône 24px + temp 14px), pièce
│ [Salon][Cuisine][Chambre]…   │  onglets de groupes (scroll horizontal)
├─────────────────────────────┤
│ ┌─────────┐ ┌─────────┐      │  grille de tuiles 2 colonnes
│ │ tuile   │ │ tuile   │      │
│ └─────────┘ └─────────┘      │
└─────────────────────────────┘
```

- **Menu + Groupe fusionnés** : onglets en haut + appareils du groupe actif en grille.
- **Header** : heure, date, météo, (pièce). 
- **Swipe** horizontal = changer d'onglet. **Tap tuile = ouvrir la vue détail.**

## Onglets (groupes)

- Rangée `flex`, scroll horizontal, séparateur bas `1px #232330`.
- Onglet actif : texte `#fff`, soulignement `2px #FFB020`, semi-gras. Inactif : `#8A8A92`.

## Grille de tuiles

- **2 colonnes**, gap 12, padding 16×20. Tuile : fond `#17171E`, radius 16, padding 16.
- Contenu : ligne haute = icône **26 px** (accent ou `#5F5E5A` si off) + label d'état
  **13 px** (accent si actif, sinon `#8A8A92`) ; puis nom **15 px** ; puis **barre de
  progression** (height 5, radius 3, fond `#26262F`, remplissage = accent à `value%`).
- **`switch` : pas de barre** (anneau plein logique).
- Labels d'état : voir mapping commun (Allumé/Éteint, xx%, Lecture/Pause).

## Vue détail (tap sur une tuile)

- Header détail : bouton **retour** (40×40, radius 12, `#17171E`, chevron-left) + titre
  **18 px** = « Groupe · Appareil ».
- **Arc de valeur** : rayon **84**, épaisseur **16**, centre (110,110) en maquette.
  START=150°, SWEEP=300° (identique au Dial). Icône **30 px**, valeur **42 px**.
  `f` = switch ? (on?1:0) : (on || cover ? value : 0). Couleur accent si actif.
- **Slider** (`range 0..100`, `accent-color` = accent) — **masqué pour `switch`**.
- **Bouton toggle** — **masqué pour `cover`** ; libellé :
  - light/switch : « Éteindre » / « Allumer »
  - media : « Pause » / « Lecture »
- Interactions : slider `input` → met à jour valeur + arc (et `on = value>0` hors cover) ;
  toggle → bascule `on` ; retour → ferme le détail, rafraîchit la grille.

## Light control sheet (colour + effects)

The `light` more-info sheet is **capability-driven**: it renders only the controls the entity
actually exposes (read from HA via the `homeassistant_addon` light proxy — `supported_color_modes`,
`min/max_color_temp_kelvin`, `effect_list`). No per-lamp YAML is needed.

- **Brightness slider** — shown when the light is dimmable (`brightness` attribute present).
- **Colour swatches** — shown when `supported_color_modes` includes an RGB-family mode
  (`rgb`/`rgbw`/`rgbww`/`hs`/`xy`). A fixed 12-hue palette (`LIGHT_PALETTE` in `model.h`);
  tapping one sends `light.turn_on` with `rgb_color`. LVGL 9 dropped the colourwheel widget, so a
  discrete swatch grid is used instead (also the most robust surface on the touch screen).
- **Warm→cool swatches** — shown when `color_temp` is supported. Fixed Kelvin presets
  (`LIGHT_CT_KELVIN`) filtered to the light's reported range; the nearest preset to the live value
  is outlined. Tapping sends `color_temp_kelvin`.
- **Effect chips** — one per entry in `effect_list`; the active effect is outlined. Tapping sends
  `light.turn_on` with `effect`.

`rgb_color` is a list, which HA's service schema rejects as a stringified `data` value — it is sent
through the action's `data_template` (HA renders `[r, g, b]` back into a real list). Swatch taps are
momentary (fire-and-forget); brightness/temp/effect state is reflected on the next HA state push.

## Note d'implémentation (robustesse)

Tuiles, onglets, slider, toggle, bouton retour = **vrais `lv_obj` avec événements LVGL
natifs**. À chaque changement d'onglet ou ouverture/fermeture de détail : suivre le contrat
`mount/update/unmount` et **update-in-place** (cf. [ADR-0005](adr/0005-touch-view-lifecycle.md))
— c'est précisément la transition qui cassait le touch sur l'existant.
