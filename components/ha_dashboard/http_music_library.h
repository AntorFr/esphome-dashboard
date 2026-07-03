#pragma once
// HTTP/JSON adapter implementing the MusicLibraryBackend port against music-library's REST
// API (ESP -> music-library -> Music Assistant). See ADR-0007.
//
// Guarded by USE_HA_DASHBOARD_LAUNCHER so it only compiles when the http_request component is
// in the build. NON-BLOCKING: each request runs on a background FreeRTOS worker task (the
// http_request API itself is synchronous and would otherwise freeze the LVGL loop, especially
// for Music-Assistant-backed calls). Results are delivered on the main loop via
// process_pending(), so the typed callbacks (which touch launcher/LVGL state) run on the main
// thread. IMPORTANT: the worker uses its OWN http_request client — it must not share one with
// online_image (covers), which runs on the main thread (concurrent use = corruption).
#include "esphome/core/defines.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include <functional>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
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
  // Cover/thumbnail encoding requested from the server: "" / "jpg" = default JPEG; "bmp" =
  // uncompressed BMP (no on-device DCT decode — cheaper for the ESP's loop, bigger transfer).
  void set_image_format(const std::string &fmt) { this->image_format_ = (fmt == "jpg") ? "" : fmt; }

  void fetch_favorites(const std::string &owner, QuickItemsCallback cb) override;
  void fetch_children(const std::string &item_id, int offset, int limit, QuickPageCallback cb) override;
  void play(const std::string &uri, int seek_s) override;
  void fetch_now_playing(NowPlayingCallback cb) override;
  void transport(const std::string &cmd) override;
  void volume_step(const std::string &direction) override;
  void set_volume(int level) override;
  void set_mute(bool muted) override;
  void set_shuffle(bool enabled) override;
  void set_repeat(const std::string &mode) override;

  // Main loop: deliver the results of completed requests (runs their callbacks here).
  void process_pending();

 protected:
  bool http_get_(const std::string &url, std::string &body);
  bool http_post_(const std::string &url);

  // Queue `work` (blocking HTTP+parse, runs on the worker task) then `deliver` (runs on the
  // main loop, invokes the user callback). `deliver` may be null for fire-and-forget POSTs.
  void enqueue_(std::function<void()> work, std::function<void()> deliver);
  void ensure_worker_();
  static void worker_task_(void *param);

  http_request::HttpRequestComponent *http_{nullptr};
  std::string base_url_;
  std::string queue_id_;
  std::string image_format_;  // "" = JPEG (default); "bmp" = request BMP covers/thumbs

  TaskHandle_t worker_{nullptr};
  QueueHandle_t work_q_{nullptr};  // HttpJob* -> worker
  QueueHandle_t done_q_{nullptr};  // HttpJob* -> main loop
};

}  // namespace ha_dashboard
}  // namespace esphome
#endif  // USE_HA_DASHBOARD_LAUNCHER
