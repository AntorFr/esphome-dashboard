#pragma once
// HTTP/JSON adapter implementing the MusicLibraryBackend port against music-library's REST
// API (ESP -> music-library -> Music Assistant). See ADR-0007.
//
// Guarded by USE_HA_DASHBOARD_LAUNCHER so it only compiles when the http_request component is in the
// build (i.e. when a music_library launcher is configured). Synchronous: ESPHome's
// http_request blocks the loop for the request — payloads are small, results are delivered
// to the callback inline.
#include "esphome/core/defines.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include <string>
#include <vector>
#include "esphome/components/http_request/http_request.h"
#include "music_library.h"

namespace esphome {
namespace ha_dashboard {

class HttpMusicLibrary : public MusicLibraryBackend {
 public:
  void set_http(http_request::HttpRequestComponent *http) { this->http_ = http; }
  void set_base_url(const std::string &base_url) {
    this->base_url_ = base_url;
    while (!this->base_url_.empty() && this->base_url_.back() == '/')
      this->base_url_.pop_back();
  }
  void set_queue_id(const std::string &queue_id) { this->queue_id_ = queue_id; }

  void fetch_favorites(const std::string &owner, QuickItemsCallback cb) override;
  void fetch_children(const std::string &item_id, int offset, int limit, QuickPageCallback cb) override;
  void play(const std::string &uri, int seek_s) override;
  void fetch_now_playing(NowPlayingCallback cb) override;
  void transport(const std::string &cmd) override;

 protected:
  bool http_get_(const std::string &url, std::string &body);
  bool http_post_(const std::string &url);

  http_request::HttpRequestComponent *http_{nullptr};
  std::string base_url_;
  std::string queue_id_;
};

}  // namespace ha_dashboard
}  // namespace esphome
#endif  // USE_HA_DASHBOARD_LAUNCHER
