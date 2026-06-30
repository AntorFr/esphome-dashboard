#pragma once
// LAYER 1 — kid music launcher logic. HW/techno-agnostic: knows nothing about LVGL or HTTP,
// talks to music-library only through MusicLibraryBackend. See ADR-0007.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "music_library.h"

namespace esphome {
namespace ha_dashboard {

// What the current view should show.
enum class LauncherStatus : uint8_t {
  IDLE,     // not loaded yet
  LOADING,  // a fetch is in flight
  READY,    // items available
  EMPTY,    // fetch ok but no items (e.g. unknown owner)
  ERROR,    // fetch failed (offline / HTTP / parse)
};

// Which list is on screen: the favourites grid or a drill-down list.
enum class LauncherLevel : uint8_t {
  GRID,    // owner's favourites (cover grid)
  DETAIL,  // episodes / chapters of a podcast / audiobook
};

class LauncherModule {
 public:
  void set_backend(MusicLibraryBackend *backend) { this->backend_ = backend; }
  void set_owner(const std::string &owner) { this->owner_ = owner; }

  // Called whenever the view must be redrawn (status / list / level changed).
  void set_on_changed(std::function<void()> cb) { this->on_changed_ = std::move(cb); }

  // Enter the module: (re)load the owner's favourites. Resets to the GRID level.
  void load();

  // Primary action — tap on a tile (or a detail row): play it. For a podcast/audiobook
  // tile this plays the parent uri, which Music Assistant resumes from the saved position.
  // For a detail row it plays that specific episode/chapter. Works at both levels.
  void activate(int index);

  // Secondary action — the "list" button on a podcast/audiobook tile: drill into its
  // episodes/chapters (first page) so a specific item can be picked. Grid level only;
  // a no-op for tiles without children.
  void open_children(int index);

  // Load the next page of the detail list. Call when the user scrolls near the end.
  // No-op if there is no more / a page is already loading / not in DETAIL.
  void load_more_children();

  // Go up one level. Returns true if handled (was in DETAIL -> back to grid),
  // false if the module should be closed (was already on the grid).
  bool back();

  // Now-playing (header widget / "now playing" card): one-shot fetch + transport commands.
  void fetch_now_playing();
  void transport(const std::string &cmd);  // pause|resume|play_pause|stop|next|previous
  const NowPlaying &now_playing() const { return this->now_playing_; }

  // --- View accessors (read by the renderer) ---
  LauncherStatus status() const { return this->status_; }
  LauncherLevel level() const { return this->level_; }
  // The list currently on screen (favourites or children).
  const std::vector<QuickItem> &items() const {
    return this->level_ == LauncherLevel::DETAIL ? this->children_ : this->favorites_;
  }
  // Title of the drilled-in item (header of the DETAIL list).
  const std::string &detail_title() const { return this->detail_title_; }
  // Favourite index of the drilled-in item (to reuse its cover in the detail header), or -1.
  int detail_index() const { return this->detail_index_; }
  const std::string &owner() const { return this->owner_; }

  // DETAIL paging state (for the renderer's "load on scroll" + spinner row).
  bool has_more() const { return this->children_has_more_; }
  bool loading_more() const { return this->children_loading_more_; }

  // Page size for drill-down requests.
  static constexpr int PAGE_SIZE = 50;

 protected:
  void notify_() {
    if (this->on_changed_)
      this->on_changed_();
  }

  MusicLibraryBackend *backend_{nullptr};
  std::string owner_;
  std::function<void()> on_changed_;

  std::vector<QuickItem> favorites_;
  std::vector<QuickItem> children_;
  std::string detail_title_;
  int detail_index_{-1};
  NowPlaying now_playing_;

  // Drill-down paging. children_id_ = local id of the parent being browsed; has_more_ drives
  // "load on scroll"; loading_more_ guards against firing overlapping page requests.
  std::string children_id_;
  bool children_has_more_{false};
  bool children_loading_more_{false};

  LauncherStatus status_{LauncherStatus::IDLE};
  LauncherLevel level_{LauncherLevel::GRID};

  // Generation token: async callbacks from a superseded request are ignored, so a slow
  // response can never overwrite a newer view (navigation robustness, cf. ADR-0005 spirit).
  uint32_t gen_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
