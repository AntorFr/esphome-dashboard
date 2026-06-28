# Interaction — M5Stack Dial (écran rond 240×240)

> Extrait fidèle de [`../dial-prototype.html`](../dial-prototype.html) — **source de vérité**.
> Repère angulaire : 0° = haut, sens horaire. Pixels = maquette 240×240 (centre 120,120) ;
> **à dériver en responsive**. Le bouton encodeur est dur → minimiser son usage, **pas de
> double-appui**, tactile privilégié.

## Gestes

| Geste | Action |
|-------|--------|
| Slide ← / → (tactile) | card précédente / suivante dans le groupe |
| Rotation molette | régler la card affichée (la nav est sur le tactile → pas de mode) |
| Tap centre | toggle on/off (sauf `cover`) |
| Slide ↑ (tactile) **ou** maintien bouton | retour au menu — **même jauge de retour** |
| Appui molette (clic) | ouvrir la card focalisée (depuis le menu) |

**Détection de direction** : au-delà de **8 px** de déplacement, on fige le mode `h`
(horizontal) ou `v` (vertical) selon le plus grand delta.

## Carrousel de cards (vue Groupe)

- Pendant un drag horizontal : translation suivie, **bornée à ±60 px**.
- Au relâcher : **dx ≤ −40 px → suivante**, **dx ≥ +40 px → précédente**, sinon retour à 0
  (animation `transform .2s`). Sans drag (tap) → toggle.
- **Pagination** : points en bas (`bottom ≈ 26 px`), point actif **large 16 px** vs 6 px,
  hauteur 6 px. Indice « chevron-up menu » en `bottom ≈ 9 px`.

### Arc de valeur (vue card)
- Rayon **90**, épaisseur **14**, lin. arrondi. Track `#232330`. Arc rempli = accent (ou
  `#232330` si inactif). START=150°, SWEEP=300° (cf. ux-interaction).
- Contenu centré : icône **26 px**, valeur **34 px**, nom **13 px** (accent si actif).

## Jauge de retour (slide ↑ ou maintien bouton)

Deux arcs **neutres `#EDEDF0`**, rayon **106**, épaisseur **5**, distincts de l'arc de valeur :

- Arc droit : de **150°** vers `150 − 150·p`.
- Arc gauche : de **210°** vers `210 + 150·p`.
- À **p = 1**, les deux atteignent le haut (0°/360°) = **se rejoignent → validation** (→ Menu).
- **Relâcher avant la jonction = annule** (les arcs redescendent, anim ~220 ms).
- **Slide ↑** : `p = clamp(−dy / 130, 0, 1)` → seuil ≈ **130 px**.
- **Maintien bouton** : `p = (t − t0) / 1100` → durée ≈ **1,1 s**.
- Pendant la montée : contenu card s'estompe (`opacity = 1 − 0.45·p`), pagination s'estompe
  (`1 − p`), indice masqué.

## Menu (lanceur circulaire)

- Groupes = **vrais cercles** sur un anneau de rayon **82**.
- Cercle de base **r = 25** (Ø 50) ; **focalisé r ≈ 29** (Ø ~58) → **grossit** en arrivant
  en haut. Facteur de focus `ff = clamp(1 − angleAuHaut / (360/N), 0, 1)`.
- Icône : taille `19 → 25 px` selon `ff` ; couleur `#8A8A92 → #FFB020`. Remplissage cercle
  `#1E1E26 → #3A2E12`, contour `#FFB020` d'opacité `ff`.
- Centre : **nom du groupe focalisé** (22 px) + « N appareils » (13 px).
- **Rotation** : drag → `rot = base − dx/55` ; au relâcher, **snap** à l'entier le plus
  proche (animation d'amortissement).
- **Ouvrir** : tap sur une icône (distance < **30 px** du centre du cercle) **ou** clic
  molette sur le groupe focalisé.

## Note d'implémentation (robustesse)

Le menu radial est le **seul** cas où un hit-test manuel peut être toléré. S'il est utilisé,
recalculer les zones avec la **même fonction de layout** que le rendu, à chaque `mount`, liées
à l'instance de vue (cf. [ADR-0005](adr/0005-touch-view-lifecycle.md)). Partout ailleurs :
événements LVGL natifs sur de vrais `lv_obj`.
