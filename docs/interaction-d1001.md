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

## Note d'implémentation (robustesse)

Tuiles, onglets, slider, toggle, bouton retour = **vrais `lv_obj` avec événements LVGL
natifs**. À chaque changement d'onglet ou ouverture/fermeture de détail : suivre le contrat
`mount/update/unmount` et **update-in-place** (cf. [ADR-0005](adr/0005-touch-view-lifecycle.md))
— c'est précisément la transition qui cassait le touch sur l'existant.
