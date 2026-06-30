# ADR-0007 — Music launcher module (D1001) via direct REST to music-library

**Status**: Accepted

## Context

We want a kid-facing module on the **D1001** (portrait 800×1280) that lets children
start their favourite playlist / podcast / audiobook by tapping a cover. The content and
playback live in a sibling project, **music-library** (FastAPI app over Music Assistant,
repo `AntorFr/music-library`), which already ships a web "quick launcher" with the exact
flow we want: pick a profile (`owner` tag) + a speaker, browse a cover grid, tap to play;
podcasts/audiobooks drill into episodes/chapters.

This is **not** a Home-Assistant-entity card. It has no `switch`/`cover`/`climate`/
`media_player` entity behind it — it is a *launcher* that lists remote items and triggers
playback. It must therefore sit beside the entity cards, reachable from a dedicated menu
entry, without bending the `Card`/`Group` model that ADR-0002 and `model.h` define.

Two questions had to be answered: **where** the data exchange happens, and **how** the
module slots into the layered architecture (`docs/architecture.md`).

### Constraints / facts

- Covers are HTTP images regardless of the transport (music-library exposes stable
  `/covers/{id}.jpg`, 300×300, explicitly meant "for ESPHome screens").
- The Dial (240×240 round) is a poor fit for a kid cover grid → **D1001 only**.
- Layer 1 must stay HW- and techno-agnostic (no LVGL, no HTTP types leaking in).
- All outbound commands are throttled (lessons-learned anti-flood rule).

## Decision

### 1. Exchange mode — direct REST, ESP → music-library

The firmware talks **HTTP/JSON straight to music-library**, not through Home Assistant.
The module is an on-device twin of the web `/quick` launcher. Rationale: covers are HTTP
either way, music-library already exposes purpose-built endpoints, and we avoid a fragile
HA automation hop.

Contract (music-library side, shipped in its PR #1 / `v0.12.0-beta`):

| Step | Call | Notes |
|------|------|-------|
| favourites | `GET /api/v1/quick/{owner}` | compact JSON: `id,title,media_type,uri,cover_url,has_children` |
| cover | `GET /covers/{id}.jpg` | 300×300, decoded on device |
| play | `POST /api/v1/ma/play?queue_id=&uri=&seek=` | `queue_id` = the device's fixed speaker |
| episodes/chapters | *(open — see below)* | only when `has_children` |

### 2. Speaker + profile fixed in YAML, per device

The kid's-room device is configured once with its `owner` and target `player` (queue_id).
The child gets **true one-tap**: tap a cover → it plays on the room speaker. No on-screen
profile/speaker pickers in v1 (the mockup keeps the chips as a later option).

### 3. Layering — a first-class `LauncherModule`, not a `CardType`

- **Layer 1**: new `LauncherModule` (HW-agnostic) holding config (`base_url`, `owner`,
  `queue_id`) and runtime state (favourites list, selection, optional children list). It is
  exposed in the menu as a dedicated entry/tab, parallel to `Group`.
- **New port — `MusicLibraryBackend`** (out): `fetch_favorites(owner, cb)`,
  `play(uri, seek)`, `fetch_children(uri, cb)`. The adapter performs HTTP; Layer 1 only
  sees this interface — same separation as the HA backend port.
- **Renderer**: two new D1001 views — `render_launcher()` (cover grid) and
  `render_launcher_detail()` (episode/chapter list). New `NavState` values
  `LAUNCHER` and `LAUNCHER_DETAIL`. Covers render via ESPHome `online_image`
  (URL → decoded JPEG → LVGL image), so the renderer never touches HTTP.
- **Input**: handled by the existing `InputTouch` (tap tile, tap row, back). No new gestures.
- After playback starts we can hand off to the existing `media_player` card as the
  "now playing" surface (mockup screen D), or show a confirmation toast (screen E).

Config sketch (final schema deferred to the implementation milestone):

```yaml
dashboard:
  groups: [ ... ]            # entity cards (unchanged)
  music_library:            # creates one dedicated menu tab on D1001
    base_url: http://music-library.local:8000
    owner: lea
    player: <ma_queue_id>   # fixed target speaker for this device
```

## Consequences

- (+) Reuses music-library's existing, tested endpoints; the firmware stays a thin client.
- (+) The `Card`/`Group` model is untouched; the launcher is additive and isolated.
- (+) One-tap UX for kids; no HA automation to maintain.
- (−) Adds an **HTTP client + JPEG decode** path on the device (acceptable on ESP32-P4 +
  PSRAM; would be tight on the Dial — hence D1001-only).
- (−) Couples the firmware to music-library's base URL/availability; needs graceful
  offline/empty states (unknown owner already returns an empty list by design).
- (−) The device holds no auth today; music-library is assumed reachable on the LAN.
  Token/header support is a later concern.

## Open question

**Episode/chapter drill-down transport.** music-library currently exposes episodes and
chapters as **HTML partials** (`/media/{id}/episodes`, `/media/{id}/chapters`), built for
HTMX, not for a microcontroller. Before implementing screen C we decide between:
(a) adding a compact JSON endpoint (e.g. `GET /api/v1/quick/item/{id}/children`) mirroring
the `/quick` shape, or (b) parsing the existing HTML on device (rejected — brittle).
Leaning towards (a), as a follow-up PR on music-library. Tracked in `roadmap.md`.

See the visual spec in `music-launcher-mockups.html` (5 D1001 screens) and the technical
analysis that produced this ADR.
