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
  std::string id;
  std::string title;
  std::string media_type;    // playlist | album | radio | podcast | audiobook | track
  std::string uri;           // pass verbatim to POST /api/v1/ma/play
  std::string cover_url;     // resolved cover; MAY BE EMPTY for drill-down rows (podcast
                             // episodes usually have a thumbnail, audiobook chapters do not
                             // -> the renderer falls back to a position number)
  bool has_children{false};  // podcast/audiobook -> a "list" button drills into episodes/chapters
};

// Async result: ok=false means the request failed (offline, HTTP error, parse error);
// items is then empty. Delivered on the main loop by the adapter (never blocks).
using QuickItemsCallback = std::function<void(bool ok, std::vector<QuickItem> items)>;

// Paged async result (drill-down): has_more=true means another page is available beyond
// this one (more episodes/chapters to load on scroll).
using QuickPageCallback = std::function<void(bool ok, std::vector<QuickItem> items, bool has_more)>;

// Port — the LauncherModule (layer 1) depends only on this, never on the HTTP adapter.
// One method per music-library REST call used by the launcher.
class MusicLibraryBackend {
 public:
  virtual ~MusicLibraryBackend() = default;

  // GET /api/v1/quick/{owner} -> the profile's favourites (cover grid). Few items, unpaged.
  virtual void fetch_favorites(const std::string &owner, QuickItemsCallback cb) = 0;

  // One page of a podcast/audiobook's episodes/chapters (drill-down). A series can hold
  // hundreds of items, so the list is loaded incrementally on scroll: [offset, offset+limit).
  virtual void fetch_children(const std::string &uri, int offset, int limit,
                              QuickPageCallback cb) = 0;

  // POST /api/v1/ma/play?queue_id=<device speaker>&uri=<uri>&seek=<seek_s>.
  // queue_id (target speaker) is held by the adapter, fixed per device.
  virtual void play(const std::string &uri, int seek_s) = 0;
};

}  // namespace ha_dashboard
}  // namespace esphome
