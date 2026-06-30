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

  // Tap a tile in the current list. On the grid: a podcast/audiobook drills into its
  // children, anything else plays immediately. On the detail list: plays the child.
  void select(int index);

  // Go up one level. Returns true if handled (was in DETAIL -> back to grid),
  // false if the module should be closed (was already on the grid).
  bool back();

  // --- View accessors (read by the renderer) ---
  LauncherStatus status() const { return this->status_; }
  LauncherLevel level() const { return this->level_; }
  // The list currently on screen (favourites or children).
  const std::vector<QuickItem> &items() const {
    return this->level_ == LauncherLevel::DETAIL ? this->children_ : this->favorites_;
  }
  // Title of the drilled-in item (header of the DETAIL list).
  const std::string &detail_title() const { return this->detail_title_; }
  const std::string &owner() const { return this->owner_; }

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

  LauncherStatus status_{LauncherStatus::IDLE};
  LauncherLevel level_{LauncherLevel::GRID};

  // Generation token: async callbacks from a superseded request are ignored, so a slow
  // response can never overwrite a newer view (navigation robustness, cf. ADR-0005 spirit).
  uint32_t gen_{0};
};

}  // namespace ha_dashboard
}  // namespace esphome
