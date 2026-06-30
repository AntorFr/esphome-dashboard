// LAYER 1 — LauncherModule implementation. See launcher_module.h / ADR-0007.
#include "launcher_module.h"

namespace esphome {
namespace ha_dashboard {

void LauncherModule::load() {
  this->level_ = LauncherLevel::GRID;
  this->children_.clear();
  this->detail_title_.clear();

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

void LauncherModule::select(int index) {
  const std::vector<QuickItem> &list = this->items();
  if (index < 0 || index >= static_cast<int>(list.size()))
    return;
  // Copy: the source list may be mutated before/while the async callback runs.
  const QuickItem item = list[index];

  // Grid + has children -> drill into episodes/chapters.
  if (this->level_ == LauncherLevel::GRID && item.has_children) {
    this->level_ = LauncherLevel::DETAIL;
    this->detail_title_ = item.title;
    this->children_.clear();
    this->status_ = LauncherStatus::LOADING;
    this->notify_();

    if (this->backend_ == nullptr) {
      this->status_ = LauncherStatus::ERROR;
      this->notify_();
      return;
    }

    const uint32_t gen = ++this->gen_;
    this->backend_->fetch_children(item.uri, [this, gen](bool ok, std::vector<QuickItem> items) {
      if (gen != this->gen_)
        return;
      if (!ok) {
        this->status_ = LauncherStatus::ERROR;
      } else {
        this->children_ = std::move(items);
        this->status_ = this->children_.empty() ? LauncherStatus::EMPTY : LauncherStatus::READY;
      }
      this->notify_();
    });
    return;
  }

  // Leaf -> play. Seek 0 for now (resume position is a later concern).
  if (this->backend_ != nullptr)
    this->backend_->play(item.uri, 0);
}

bool LauncherModule::back() {
  if (this->level_ != LauncherLevel::DETAIL)
    return false;  // already on the grid -> caller closes the module

  // Invalidate any in-flight children fetch and return to the favourites grid.
  ++this->gen_;
  this->level_ = LauncherLevel::GRID;
  this->children_.clear();
  this->detail_title_.clear();
  this->status_ = this->favorites_.empty() ? LauncherStatus::EMPTY : LauncherStatus::READY;
  this->notify_();
  return true;
}

}  // namespace ha_dashboard
}  // namespace esphome
