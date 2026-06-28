# Système de couleurs

> **Couleur = fonction de l'entité**, alignée sur Home Assistant. Implémenté par
> `model/color_resolver.*`. Tokens définis dans `packages/styles/neon.yaml`.

## Ordre de résolution de la teinte « active »

Pour une card, la couleur active est résolue dans cet ordre (premier qui s'applique) :

1. **Couleur live** de l'entité — **lumières uniquement** : `rgb_color` quand allumée/colorée.
2. **Override manuel** YAML : `color:` sur la card.
3. **Défaut aligné HA** par domaine/état : token
   `state-<domaine>-<device_class>-<état>-color`, repli `state-active-color`. **Hex figés sur
   les valeurs par défaut de Home Assistant.**
4. **Inactif / off / indisponible** : gris atténué. Cas spécial média inactif : brun
   `#795548` (façon HA).

**Règle clé** : pour une lumière, **le live l'emporte sur l'override** (l'override sert de
repli quand l'entité n'a pas de couleur).

## Tokens par défaut (du prototype → à mapper sur les variables HA)

| Domaine | Token (esprit) | Hex illustratif |
|---------|----------------|-----------------|
| light   | `state-light-on-color`        | `#FFB020` |
| cover   | `state-cover-open-color`      | `#4F8CFF` |
| media   | `state-media_player-playing-color` | `#A06CFF` |
| switch  | `state-switch-on-color`       | `#3DD68C` |
| média inactif | `state-media_player-off-color` | `#795548` |
| jauge de retour (neutre) | `gauge-return-color` | `#EDEDF0` |

Neutres communs : track arc `#232330`, icône off `#5F5E5A`, texte secondaire `#8A8A92`,
fonds `#0A0A0C` / `#0E0E12` / `#17171E`.

## État indisponible

Entité `unavailable` ou lien HA perdu → card **grisée** (track/arc `#232330`, icône
`#5F5E5A`), label « indisponible » (clé i18n `state.unavailable`), **interactions
désactivées**. Le drapeau `available` du modèle prime sur la résolution de couleur.

## Style composable

Le style (`style: neon`) est un **package de tokens** (`packages/styles/<style>.yaml`),
composable façon thèmes `esphome-tx-ultimate` (listes fusionnées, référencées par nom). Un
futur `style:` alternatif n'a qu'à fournir le même jeu de tokens.

## À faire (lors de l'implémentation du resolver)

- Mapper précisément chaque token sur le **nom de variable HA** correspondant et **figer le
  hex** sur la valeur par défaut HA (vérifier les valeurs HA courantes à l'implémentation).
- Couvrir les `device_class` pertinents (ex. `cover` : blind/shade/garage…).
