# UX & interaction — modèle commun

> Chapeau commun aux deux écrans. Détails fidèles aux prototypes :
> [interaction-dial.md](interaction-dial.md) et [interaction-d1001.md](interaction-d1001.md).
> Les **prototypes HTML** (`../dial-prototype.html`, `../d1001-prototype.html`) restent la
> source de vérité exécutable. **Les angles et ratios sont les invariants** ; les pixels
> cités viennent des maquettes et doivent être **dérivés en responsive** (cf. ADR-0002).

## Machine à états de navigation (commune)

```
Veille (horloge) ──tap/touch──▶ Menu (groupes) ──ouvrir groupe──▶ Groupe (cards) ──ouvrir card──▶ Réglage card
       ▲                              ▲                                 │                              │
       └────────── timeout ──────────┴───────────── back ──────────────┴────────────── back ─────────┘
```

- **Retour** = remonter d'un niveau.
- **Timeout d'inactivité** → Veille (tout event `activity` réarme le minuteur).
- **Groupe mono-appareil** → on saute le niveau Groupe (ouvre directement la card).
- Sur D1001, **Menu et Groupe fusionnent** en une seule vue (onglets + grille).

## Vocabulaire visuel commun

- **Arc de valeur** : arc central qui *affiche* l'état ET *se pilote*. **Ouvert en bas
  (~60°)** pour laisser place à la pagination / au chevron de retour. Géométrie de
  référence (repère « 0° = haut, sens horaire », `x=cx+r·sin θ`, `y=cy−r·cos θ`) :
  **START = 150°**, **SWEEP = 300°** → l'arc va de 150° (bas-droite) à −150°/210° (bas-gauche),
  laissant un trou de 60° centré en bas (180°).
- **Valeur normalisée** `f ∈ [0,1]` → longueur de l'arc rempli (`150° → 150−300·f`).
- **Couleur** = fonction de l'entité (voir [color-system.md](color-system.md)).
- **État inactif/indispo** : arc/track gris `#232330`, icône grise `#5F5E5A`, nom gris
  `#8A8A92`.

## Mapping état → affichage (commun, depuis les prototypes)

| type | `f` (longueur arc) | label | « actif » |
|------|--------------------|-------|-----------|
| `switch` | on ? 1 : 0 (anneau plein, pas de réglage) | Allumé / Éteint | `on` |
| `light`  | on ? brightness : 0 | `xx%` / Éteint | `on` |
| `cover`  | position | `xx%` | toujours actif |
| `media`  | volume | Lecture / Pause | `on` |

> v1 démarre avec **`light` + `switch`** ; `cover` / `media` / `climate` suivent (cf. roadmap).

## Palette de référence (maquettes → à mapper sur tokens HA)

Fonds : body `#0A0A0C`, écran `#0E0E12`, tuile `#17171E`. Track arc `#232330`.
Accents : light `#FFB020`, cover `#4F8CFF`, media `#A06CFF`, switch `#3DD68C`.
Neutres : jauge de retour `#EDEDF0`, texte secondaire `#8A8A92`, icône off `#5F5E5A`.
