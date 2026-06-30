// LAYER 1 — LauncherModule implementation. See launcher_module.h / ADR-0007.
#include "launcher_module.h"

namespace esphome {
namespace ha_dashboard {

void LauncherModule::load() {
  this->level_ = LauncherLevel::GRID;
  this->children_.clear();
  this->detail_title_.clear();
  this->children_uri_.clear();
  this->children_has_more_ = false;
  this->children_loading_more_ = false;

  if (this->backend_ == nullptr) {
    this->status_ = LauncherStatus::ERROR;
    this->notify_();
    return;
  }

  this->status_ = LauncherStatus::LOADING;
  this->notify_();

  const uint32_t gen = ++this->gen_;
  this->backend_->fetch_favorites(this->owner_, [this, gen](bool ok, std::vector<QuickItem> items) {
    if (gen != this->gen_)
      return;  // a newer request superseded this one
    if (!ok) {
      this->status_ = LauncherStatus::ERROR;
    } else {
      this->favorites_ = std::move(items);
      this->status_ = this->favorites_.empty() ? LauncherStatus::EMPTY : LauncherStatus::READY;
    }
    this->notify_();
  });
}

void LauncherModule::activate(int index) {
  const std::vector<QuickItem> &list = this->items();
  if (index < 0 || index >= static_cast<int>(list.size()))
    return;
  // Play the item. For a podcast/audiobook tile this is the parent uri, which Music
  // Assistant resumes from the saved position; for a detail row it is that episode/chapter.
  // Seek 0: explicit chapter offsets are a later concern.
  if (this->backend_ != nullptr)
    this->backend_->play(list[index].uri, 0);
}

void LauncherModule::open_children(int index) {
  if (this->level_ != LauncherLevel::GRID)
    return;
  if (index < 0 || index >= static_cast<int>(this->favorites_.size()))
    return;
  const QuickItem item = this->favorites_[index];  // copy: list may change during the fetch
  if (!item.has_children)
    return;

  this->level_ = LauncherLevel::DETAIL;
  this->detail_title_ = item.title;
  this->children_uri_ = item.uri;
  this->children_.clear();
  this->children_has_more_ = false;
  this->children_loading_more_ = false;
  this->status_ = LauncherStatus::LOADING;
  this->notify_();

  if (this->backend_ == nullptr) {
    this->status_ = LauncherStatus::ERROR;
    this->notify_();
    return;
  }

  const uint32_t gen = ++this->gen_;
  this->backend_->fetch_children(
      this->children_uri_, 0, PAGE_SIZE,
      [this, gen](bool ok, std::vector<QuickItem> items, bool has_more) {
        if (gen != this->gen_)
          return;
        if (!ok) {
          this->status_ = LauncherStatus::ERROR;
        } else {
          this->children_ = std::move(items);
          this->children_has_more_ = has_more;
          this->status_ = this->children_.empty() ? LauncherStatus::EMPTY : LauncherStatus::READY;
        }
        this->notify_();
      });
}

void LauncherModule::load_more_children() {
  if (this->level_ != LauncherLevel::DETAIL)
    return;
  if (!this->children_has_more_ || this->children_loading_more_)
    return;
  if (this->backend_ == nullptr)
    return;

  this->children_loading_more_ = true;
  this->notify_();  // let the renderer show a spinner row

  const int offset = static_cast<int>(this->children_.size());
  const uint32_t gen = this->gen_;  // same session: no bump, page belongs to this drill
  this->backend_->fetch_children(
      this->children_uri_, offset, PAGE_SIZE,
      [this, gen](bool ok, std::vector<QuickItem> items, bool has_more) {
        if (gen != this->gen_)
          return;  // user navigated away / reopened -> drop this page
        this->children_loading_more_ = false;
        if (ok) {
          this->children_.insert(this->children_.end(), items.begin(), items.end());
          this->children_has_more_ = has_more;
          if (!this->children_.empty())
            this->status_ = LauncherStatus::READY;
        }
        // On error we keep what we have; has_more stays true so a later scroll can retry.
        this->notify_();
      });
}

bool LauncherModule::back() {
  if (this->level_ != LauncherLevel::DETAIL)
    return false;  // already on the grid -> caller closes the module

  // Invalidate any in-flight children/page fetch and return to the favourites grid.
  ++this->gen_;
  this->level_ = LauncherLevel::GRID;
  this->children_.clear();
  this->detail_title_.clear();
  this->children_uri_.clear();
  this->children_has_more_ = false;
  this->children_loading_more_ = false;
  this->status_ = this->favorites_.empty() ? LauncherStatus::EMPTY : LauncherStatus::READY;
  this->notify_();
  return true;
}

}  // namespace ha_dashboard
}  // namespace esphome
