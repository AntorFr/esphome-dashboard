# ADR-0005 — Robustesse tactile & cycle de vie des vues

**Statut** : Accepté — **contrainte non négociable**

## Contexte
Bug majeur et récurrent sur `esphome-dial` : au changement de tuile/card, le **touch se
perdait** car la zone tactile n'était pas rechargée/recalculée à la transition. Beaucoup de
temps de debug. Voir [lessons-learned §1](../lessons-learned.md). On veut une implémentation
**robuste par conception**, pas un correctif a posteriori.

## Décision
Le renderer LVGL respecte 5 règles imposées :

1. **Événements LVGL natifs sur de vrais `lv_obj`.** Chaque élément interactif est un objet
   LVGL avec sa propre hit-area ; LVGL fait le hit-test → **toujours cohérent avec le layout
   courant**. **Proscrire** le hit-testing manuel par coordonnées cachées.
   - *Exception* : menu radial Dial, si un hit-test manuel est nécessaire, les zones sont
     **recalculées par la même fonction de layout que le rendu**, à chaque (re)mount, et
     **liées à l'instance de vue** (jamais globales/persistantes).
2. **Update-in-place > destroy/recreate.** Pool d'objets réutilisés par vue ; on met à jour
   valeurs/couleurs/visibilité plutôt que de détruire/recréer (évite handlers pendouillants).
3. **Contrat de cycle de vie de vue** strict et idempotent : `mount()` (crée + enregistre
   handlers) / `update(state)` (rafraîchit) / `unmount()` (détache proprement). Toute
   transition = `unmount` complet de l'ancienne vue puis `mount` de la nouvelle.
4. **`indev` tactile rattaché une seule fois**, persistant aux changements d'écran
   (`lv_scr_load`) ; jamais ré-enregistré par vue. L'InputAdapter route vers **un seul
   dispatcher** indexé par l'état du Controller.
5. **Invalidation explicite** (`lv_obj_update_layout` / invalidate) après tout changement de
   layout, pour recalculer les hit-areas avant la prochaine lecture tactile.

## Conséquences
- (+) Élimine par conception la classe de bugs « zone tactile morte après transition ».
- (+) Moins de churn mémoire LVGL.
- (−) Impose une discipline de cycle de vie (à tester : invariant mount/unmount équilibré).
- Tests : vérifier qu'après N transitions A→B→A, chaque zone reste cliquable.
