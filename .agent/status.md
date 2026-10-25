# Status — esphome-dashboard
> MàJ : 2026-10-25

**État :** lib LVGL multi-écrans en service sur 2 reTerminal D1001 (Émilie, Timothée) ; glissement Réglages depuis le bord haut corrigé ; crash lanceur (bad_alloc) corrigé.

**Ambiance Halloween (2026-10-25, local, non poussé)** : option `ambiance: {entity: …}` — décor
Halloween de l'horloge de veille (citrouille, chauves-souris, fantôme en glyphes MDI) et teinte
violet nuit des gris des autres écrans. Testé sur la tablette du bureau (bascule aller-retour OK,
passe de teinte : 137 objets en 1 ms). À voir à l'œil par l'utilisateur.

**Prochaines étapes :**
- [ ] Tablette de Timothée qui s'éteignait au débranchement de l'USB : broche de maintien d'alim (EXP P8) jamais pilotée — corrigé et validé sur Timothée (flash local), branche `fix/d1001-power-hold` à merger + pousser, puis reflasher Émilie depuis `main`
- [ ] Liste d'épisodes lente (Laylo, Spotify à froid ~45 s) : la tablette restait 5 s puis retombait sur les favoris. Corrigé (reste sur la liste, maintien `keepalive=1`, annulation au retour) — à flasher sur Timothée après déploiement de music-library
- [ ] Flashs blancs après quelques heures — liés à la batterie d'après l'utilisateur (2026-10-25), pas au matériel d'affichage
- [x] Plantage abort() capturé (bad_alloc, corps HTTP du lanceur en RAM interne) — corps déplacé en PSRAM
- [ ] Test en cours sur la tablette d'Émilie (flashée en local le 2026-09-24 21:44) : malloc > 4 Ko en PSRAM — valider l'absence de plantage au lanceur, puis pousser + flasher Timothée
- [x] Fuite mémoire interne (CbData jamais libérées) — corrigée, validée (mémoire remonte à ~320 Ko au repos)
- [x] Plantages watchdog du lanceur (reconstruction N², vignette sur ligne détruite) — corrigés, validés 15 min / 442 images
- [ ] « Charger plus » en ajout incrémental (plus de retour en haut) — en test sur la tablette d'Émilie
- [ ] Mémoire interne basse relevée un jour sur la tablette de Timothée (123 Ko, 22 % frag) — pas de raison qu'elle diffère des autres (utilisateur, 2026-10-25)
