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
- Validate changes with the local ESPHome venv:
  `. .venv/bin/activate && esphome config tests/test_dial.yaml tests/test_d1001.yaml`,
  and `esphome compile` for the affected target.
- Do not commit `secrets.yaml` or `.esphome/` build artifacts. Keep `reference/` (local
  clones of inspiration libs) out of git.
