#pragma once
// LAYER 1 — music-library port (HW/techno-agnostic). See docs/adr/0007-music-launcher-module.md.
// Declares the data shape and the abstract backend interface the LauncherModule talks to.
// No LVGL, no HTTP, no ESPHome types here: the HTTP/JSON adapter lives elsewhere.
#include <functional>
#include <string>
#include <vector>

namespace esphome {
namespace ha_dashboard {

// One entry from music-library's compact quick API. Mirrors an item of
// GET /api/v1/quick/{owner} (and of the future children endpoint):
// {id, title, media_type, uri, cover_url, has_children}.
struct QuickItem {
  std::string id;            // local media id (used to fetch children)
  std::string title;
  std::string media_type;    // playlist | album | radio | podcast | audiobook | track
  std::string uri;           // pass verbatim to POST /api/v1/ma/play
  std::string cover_url;     // resolved cover; MAY BE EMPTY for drill-down rows (podcast
                             // episodes usually have a thumbnail, audiobook chapters do not
                             // -> the renderer falls back to a position number)
  bool has_children{false};  // podcast/audiobook -> a "list" button drills into episodes/chapters
  int seek{0};               // play offset (s): audiobook chapter start; 0 otherwise
};

// Async result: ok=false means the request failed (offline, HTTP error, parse error);
// items is then empty. Delivered on the main loop by the adapter (never blocks).
using QuickItemsCallback = std::function<void(bool ok, std::vector<QuickItem> items)>;

// Paged async result (drill-down): has_more=true means another page is available beyond
// this one (more episodes/chapters to load on scroll).
using QuickPageCallback = std::function<void(bool ok, std::vector<QuickItem> items, bool has_more)>;

// Compact playback snapshot of the target queue (GET /api/v1/ma/now_playing).
struct NowPlaying {
  bool available{false};         // a queue/player was found
  std::string state;             // playing | paused | idle | off
  std::string title;
  std::string artist;
  std::string cover_url;         // current item artwork (may be empty)
  int position_s{0};
  int duration_s{0};
  int volume{-1};                // 0..100, -1 if unknown
  bool muted{false};
  bool shuffle{false};
  std::string repeat;            // off | one | all
  bool playing() const { return this->state == "playing"; }
};
using NowPlayingCallback = std::function<void(bool ok, NowPlaying np)>;

// Port — the LauncherModule (layer 1) depends only on this, never on the HTTP adapter.
// One method per music-library REST call used by the launcher.
class MusicLibraryBackend {
 public:
  virtual ~MusicLibraryBackend() = default;

  // GET /api/v1/quick/{owner} -> the profile's favourites (cover grid). Few items, unpaged.
  virtual void fetch_favorites(const std::string &owner, QuickItemsCallback cb) = 0;

  // One page of a podcast/audiobook's episodes/chapters (drill-down), keyed by the parent's
  // local media id (GET /api/v1/quick/item/{id}/children). A series can hold hundreds of
  // items, so the list is loaded incrementally on scroll: [offset, offset+limit).
  virtual void fetch_children(const std::string &item_id, int offset, int limit,
                              QuickPageCallback cb) = 0;

  // POST /api/v1/ma/play?queue_id=<device speaker>&uri=<uri>&seek=<seek_s>.
  // queue_id (target speaker) is held by the adapter, fixed per device.
  virtual void play(const std::string &uri, int seek_s) = 0;

  // GET /api/v1/ma/now_playing?queue_id= -> current playback snapshot (one-shot, on demand).
  virtual void fetch_now_playing(NowPlayingCallback cb) = 0;

  // POST /api/v1/ma/<cmd>?queue_id= -> transport command
  // (pause | resume | play_pause | stop | next | previous).
  virtual void transport(const std::string &cmd) = 0;

  // POST /api/v1/ma/volume_step?queue_id=&direction=up|down
  virtual void volume_step(const std::string &direction) = 0;
  // POST /api/v1/ma/volume?queue_id=&level= (absolute, 0..100)
  virtual void set_volume(int level) = 0;
  // POST /api/v1/ma/mute?queue_id=&muted=
  virtual void set_mute(bool muted) = 0;
  // POST /api/v1/ma/shuffle?queue_id=&enabled=
  virtual void set_shuffle(bool enabled) = 0;
  // POST /api/v1/ma/repeat?queue_id=&mode=off|one|all
  virtual void set_repeat(const std::string &mode) = 0;
};

}  // namespace ha_dashboard
}  // namespace esphome
