# Repository conventions

Modular ESPHome + LVGL dashboard library for Home Assistant (M5Stack Dial + Seeed
reTerminal D1001). Knowledge base lives in [`docs/`](docs/) — start with
[`docs/architecture.md`](docs/architecture.md) and [`docs/adr/`](docs/adr/).

## Language policy

- **Code comments and documentation MUST be written in English.** This covers all source
  comments (C++, Python, YAML) and every file under `docs/` (architecture, ADRs,
  lessons-learned, UX/interaction specs, color-system, config-reference, README).
- **Exceptions (may stay in French):**
  - Claude memory files (under the Claude projects `memory/` directory).
  - Progress / tracking files — i.e. [`docs/roadmap.md`](docs/roadmap.md).

## Engineering conventions

- Follow the `esphome-tx-ultimate` external-component conventions:
  `components/<name>/{__init__.py, *.h, *.cpp}`, Python schema validates + codegens,
  tests validate via `esphome config`.
- Respect the layered architecture (ports & adapters) and the touch/view-lifecycle rules
  in [ADR-0005](docs/adr/0005-touch-view-lifecycle.md) — non negotiable.
- **Always target the latest released ESPHome.** The ESP32-P4 + `esp32_hosted` (C6) and the
  audio/speaker stack are moving fast upstream; before debugging a component issue, upgrade the
  venv (`pip install -U esphome`) and re-test — many bugs are already fixed in a newer release.
  Keep the venv on the newest version rather than pinning to an old one.
- **Mono-MP3 playback patch (interim).** ESPHome 2026.5.x–2026.6.x has a regression where mono MP3
  (local files, HA media, and HA TTS — which is mono) decodes its header then plays nothing;
  stereo MP3 and FLAC are fine (issue esphome#16829, fixed by PR #17106, ships in 2026.7.0). Until
  the venv is on >= 2026.7.0, run `scripts/patch_esphome_mono_mp3.sh` once after (re)creating the
  venv (idempotent). Remove this step after upgrading past the fix.
- Validate changes with the local ESPHome venv:
  `. .venv/bin/activate && esphome config tests/test_dial.yaml tests/test_d1001.yaml`,
  and `esphome compile` for the affected target.
- Do not commit `secrets.yaml` or `.esphome/` build artifacts. Keep `reference/` (local
  clones of inspiration libs) out of git.
