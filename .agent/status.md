# Status — esphome-dashboard
> MàJ : 2026-09-27

**État :** lib LVGL multi-écrans en service sur 2 reTerminal D1001 (Émilie, Timothée) ; glissement Réglages depuis le bord haut corrigé ; crash lanceur (bad_alloc) corrigé.

**Prochaines étapes :**
- [ ] Liste d'épisodes lente (Laylo, Spotify à froid ~45 s) : la tablette restait 5 s puis retombait sur les favoris. Corrigé (reste sur la liste, maintien `keepalive=1`, annulation au retour) — à flasher sur Timothée après déploiement de music-library
- [ ] Flashs blancs après quelques heures (permanents, disparaissent au reboot) — capture série en cours sur la tablette d'Émilie pour confirmer un décrochage MIPI-DSI (underrun)
- [x] Plantage abort() capturé (bad_alloc, corps HTTP du lanceur en RAM interne) — corps déplacé en PSRAM
- [ ] Test en cours sur la tablette d'Émilie (flashée en local le 2026-09-24 21:44) : malloc > 4 Ko en PSRAM — valider l'absence de plantage au lanceur, puis pousser + flasher Timothée
- [x] Fuite mémoire interne (CbData jamais libérées) — corrigée, validée (mémoire remonte à ~320 Ko au repos)
- [x] Plantages watchdog du lanceur (reconstruction N², vignette sur ligne détruite) — corrigés, validés 15 min / 442 images
- [ ] « Charger plus » en ajout incrémental (plus de retour en haut) — en test sur la tablette d'Émilie
- [ ] Mémoire interne basse sur la tablette de Timothée (123 Ko, 22 % frag) — suivre l'historique HA
