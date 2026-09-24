# Status — esphome-dashboard
> MàJ : 2026-09-24

**État :** lib LVGL multi-écrans en service sur 2 reTerminal D1001 (Émilie, Timothée) ; glissement Réglages depuis le bord haut corrigé.

**Prochaines étapes :**
- [ ] Flashs blancs après quelques heures (permanents, disparaissent au reboot) — capture série en cours sur la tablette d'Émilie pour confirmer un décrochage MIPI-DSI (underrun)
- [ ] Plantages ~quotidiens des deux tablettes (watchdog / abort) — récupérer l'ELF du build déployé pour décoder
- [ ] Mémoire interne basse sur la tablette de Timothée (123 Ko, 22 % frag) — suivre l'historique HA
