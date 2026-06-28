# ADR-0002 — Core partagé + 2 renderers (Dial + D1001) en parallèle

**Statut** : Accepté

## Contexte
v1 cible deux écrans très différents (rond 240×240 / portrait 800×1280). Deux stratégies :
(a) finir le Dial puis décliner le D1001, (b) bâtir le core partagé puis adapter les deux
renderers en parallèle.

## Décision
**Core partagé + 2 renderers en parallèle.** Le modèle de card et le Controller sont
HW-agnostiques ; le renderer LVGL est partagé avec un moteur de layout responsive. On valide
la séparation des couches dès le **walking skeleton** (1 card `light` bout-en-bout sur Dial
*et* D1001).

## Conséquences
- (+) Force une vraie abstraction des couches dès le début (pas de dette « mono-écran »).
- (+) Détecte tôt les fuites d'abstraction (coords en dur, hypothèses de géométrie).
- (−) Risque P4/D1001 présent dès M1 → mitigé en gardant le Dial comme terrain stable de
  référence et en s'appuyant sur des configs P4 connues.
