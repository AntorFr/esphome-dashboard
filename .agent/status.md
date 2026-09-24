# Status — esphome-dashboard
> MàJ : 2026-09-24

**État :** lib LVGL multi-écrans en service sur 2 reTerminal D1001 (Émilie, Timothée) ; glissement Réglages depuis le bord haut corrigé ; crash lanceur (bad_alloc) corrigé.

**Prochaines étapes :**
- [ ] Flashs blancs après quelques heures (permanents, disparaissent au reboot) — capture série en cours sur la tablette d'Émilie pour confirmer un décrochage MIPI-DSI (underrun)
- [x] Plantage abort() capturé (bad_alloc, corps HTTP du lanceur en RAM interne) — corps déplacé en PSRAM
- [ ] Plantage watchdog (tablette d'Émilie, veille) non encore capturé — cause inconnue
- [ ] Mémoire interne basse sur la tablette de Timothée (123 Ko, 22 % frag) — suivre l'historique HA
