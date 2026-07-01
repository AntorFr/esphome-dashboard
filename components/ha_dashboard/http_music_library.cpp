#include "http_music_library.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard.ml";

// A unit of work: `work` runs on the worker task (blocking HTTP + parse), `deliver` runs on
// the main loop (invokes the user callback). `deliver` is empty for fire-and-forget POSTs.
struct HttpJob {
  std::function<void()> work;
  std::function<void()> deliver;
};

// Percent-encode a query-string value (handles UTF-8 owner names, uris, etc.).
static std::string url_encode(const std::string &s) {
  static const char *const HEX = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(HEX[c >> 4]);
      out.push_back(HEX[c & 0x0F]);
    }
  }
  return out;
}

// Cover/thumbnail URLs are built by the server from the request host, so they share the API
// scheme but the server (behind a TLS-terminating proxy) may emit them as http. online_image
// won't follow a cross-scheme 301, so align the cover scheme to the configured base_url:
// https deployment -> force https (avoids the proxy redirect); plaintext :80 -> keep http.
static std::string match_base_scheme(std::string u, const std::string &base) {
  const bool base_https = base.rfind("https://", 0) == 0;
  if (base_https && u.rfind("http://", 0) == 0)
    u.replace(0, 7, "https://");
  else if (!base_https && u.rfind("https://", 0) == 0)
    u.replace(0, 8, "http://");
  return u;
}

// Read a full response body into `out`. Blocks until COMPLETE / error / timeout (on the
// worker task, so the main loop is never frozen).
static bool read_body(http_request::HttpContainer *c, std::string &out, uint32_t timeout_ms) {
  uint8_t buf[256];
  uint32_t last = millis();
  while (true) {
    int r = c->read(buf, sizeof(buf));
    auto res = http_request::http_read_loop_result(r, last, timeout_ms, c->is_read_complete());
    if (res == http_request::HttpReadLoopResult::DATA) {
      out.append(reinterpret_cast<char *>(buf), r);
      continue;
    }
    if (res == http_request::HttpReadLoopResult::RETRY) {
      delay(1);
      continue;
    }
    return res == http_request::HttpReadLoopResult::COMPLETE;
  }
}

bool HttpMusicLibrary::http_get_(const std::string &url, std::string &body) {
  if (this->http_ == nullptr)
    return false;
  auto container = this->http_->get(url);
  if (container == nullptr) {
    ESP_LOGW(TAG, "GET %s -> no response", url.c_str());
    return false;
  }
  bool ok = false;
  if (http_request::is_success(container->status_code)) {
    ok = read_body(container.get(), body, this->http_->get_timeout());
  } else {
    ESP_LOGW(TAG, "GET %s -> HTTP %d", url.c_str(), container->status_code);
  }
  container->end();
  return ok;
}

bool HttpMusicLibrary::http_post_(const std::string &url) {
  if (this->http_ == nullptr)
    return false;
  auto container = this->http_->post(url, "");
  if (container == nullptr) {
    ESP_LOGW(TAG, "POST %s -> no response", url.c_str());
    return false;
  }
  bool ok = http_request::is_success(container->status_code);
  if (!ok)
    ESP_LOGW(TAG, "POST %s -> HTTP %d", url.c_str(), container->status_code);
  container->end();
  return ok;
}

// --- Async plumbing ---------------------------------------------------------------------

void HttpMusicLibrary::ensure_worker_() {
  if (this->worker_ != nullptr)
    return;
  this->work_q_ = xQueueCreate(8, sizeof(HttpJob *));
  this->done_q_ = xQueueCreate(8, sizeof(HttpJob *));
  // Plaintext HTTP + small JSON -> a modest stack. No core affinity: the scheduler runs it on
  // a free core while the main loop keeps LVGL responsive.
  xTaskCreate(&HttpMusicLibrary::worker_task_, "ml_http", 8192, this, 5, &this->worker_);
}

void HttpMusicLibrary::worker_task_(void *param) {
  auto *self = static_cast<HttpMusicLibrary *>(param);
  HttpJob *job = nullptr;
  for (;;) {
    if (xQueueReceive(self->work_q_, &job, portMAX_DELAY) == pdTRUE && job != nullptr) {
      if (job->work)
        job->work();
      xQueueSend(self->done_q_, &job, portMAX_DELAY);
    }
  }
}

void HttpMusicLibrary::enqueue_(std::function<void()> work, std::function<void()> deliver) {
  this->ensure_worker_();
  auto *job = new HttpJob{std::move(work), std::move(deliver)};
  if (this->work_q_ == nullptr || xQueueSend(this->work_q_, &job, 0) != pdTRUE)
    delete job;  // queue full / not ready -> drop (the launcher will retry)
}

void HttpMusicLibrary::process_pending() {
  if (this->done_q_ == nullptr)
    return;
  HttpJob *job = nullptr;
  while (xQueueReceive(this->done_q_, &job, 0) == pdTRUE) {
    if (job != nullptr) {
      if (job->deliver)
        job->deliver();
      delete job;
    }
  }
}

// --- Request result holders (filled on the worker, read on the main loop) ----------------

namespace {
struct FavResult {
  bool ok{false};
  std::vector<QuickItem> items;
};
struct ChildResult {
  bool ok{false};
  std::vector<QuickItem> items;
  bool has_more{false};
};
struct NpResult {
  bool ok{false};
  NowPlaying np;
};
}  // namespace

// --- Backend port (all async) ------------------------------------------------------------

void HttpMusicLibrary::fetch_favorites(const std::string &owner, QuickItemsCallback cb) {
  const std::string url = this->base_url_ + "/api/v1/quick/" + url_encode(owner);
  const std::string base = this->base_url_;
  auto result = std::make_shared<FavResult>();
  this->enqueue_(
      [this, url, base, result]() {
        std::string body;
        bool ok = this->http_get_(url, body);
        if (ok) {
          ok = json::parse_json(body, [result, &base](JsonObject root) -> bool {
            JsonArray arr = root["items"].as<JsonArray>();
            if (arr.isNull())
              return false;
            for (JsonVariant v : arr) {
              JsonObject it = v.as<JsonObject>();
              QuickItem q;
              q.id = it["id"] | "";
              q.title = it["title"] | "";
              q.media_type = it["media_type"] | "";
              q.uri = it["uri"] | "";
              q.cover_url = match_base_scheme(it["cover_url"] | "", base);
              q.has_children = it["has_children"] | false;
              result->items.push_back(std::move(q));
            }
            return true;
          });
        }
        result->ok = ok;
      },
      [cb, result]() { cb(result->ok, std::move(result->items)); });
}

void HttpMusicLibrary::fetch_children(const std::string &item_id, int offset, int limit,
                                      QuickPageCallback cb) {
  const std::string url = this->base_url_ + "/api/v1/quick/item/" + url_encode(item_id) +
                          "/children?offset=" + std::to_string(offset) +
                          "&limit=" + std::to_string(limit);
  const std::string base = this->base_url_;
  auto result = std::make_shared<ChildResult>();
  this->enqueue_(
      [this, url, base, result]() {
        std::string body;
        bool ok = this->http_get_(url, body);
        if (ok) {
          ok = json::parse_json(body, [result, &base](JsonObject root) -> bool {
            result->has_more = root["has_more"] | false;
            JsonArray arr = root["items"].as<JsonArray>();
            if (arr.isNull())
              return false;
            for (JsonVariant v : arr) {
              JsonObject it = v.as<JsonObject>();
              QuickItem q;
              q.title = it["title"] | "";
              q.uri = it["uri"] | "";
              q.cover_url = match_base_scheme(it["cover_url"] | "", base);  // null for chapters -> ""
              q.seek = it["seek"] | 0;
              result->items.push_back(std::move(q));
            }
            return true;
          });
        }
        result->ok = ok;
      },
      [cb, result]() { cb(result->ok, std::move(result->items), result->has_more); });
}

void HttpMusicLibrary::fetch_now_playing(NowPlayingCallback cb) {
  const std::string url =
      this->base_url_ + "/api/v1/ma/now_playing?queue_id=" + url_encode(this->queue_id_);
  const std::string base = this->base_url_;
  auto result = std::make_shared<NpResult>();
  this->enqueue_(
      [this, url, base, result]() {
        std::string body;
        bool ok = this->http_get_(url, body);
        if (ok) {
          ok = json::parse_json(body, [result, &base](JsonObject root) -> bool {
            NowPlaying &np = result->np;
            np.available = root["available"] | false;
            np.state = root["state"] | "idle";
            np.title = root["title"] | "";
            np.artist = root["artist"] | "";
            np.cover_url = match_base_scheme(root["cover_url"] | "", base);
            np.position_s = root["position_s"] | 0;
            np.duration_s = root["duration_s"] | 0;
            np.volume = root["volume"] | -1;
            np.muted = root["muted"] | false;
            np.shuffle = root["shuffle"] | false;
            np.repeat = root["repeat"] | "off";
            return true;
          });
        }
        result->ok = ok;
      },
      [cb, result]() { cb(result->ok, std::move(result->np)); });
}

void HttpMusicLibrary::play(const std::string &uri, int seek_s) {
  std::string url = this->base_url_ + "/api/v1/ma/play?queue_id=" + url_encode(this->queue_id_) +
                    "&uri=" + url_encode(uri);
  if (seek_s > 0)
    url += "&seek=" + std::to_string(seek_s);
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::transport(const std::string &cmd) {
  const std::string url =
      this->base_url_ + "/api/v1/ma/" + cmd + "?queue_id=" + url_encode(this->queue_id_);
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::volume_step(const std::string &direction) {
  const std::string url = this->base_url_ + "/api/v1/ma/volume_step?queue_id=" +
                          url_encode(this->queue_id_) + "&direction=" + direction;
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::set_volume(int level) {
  if (level < 0)
    level = 0;
  if (level > 100)
    level = 100;
  const std::string url = this->base_url_ + "/api/v1/ma/volume?queue_id=" +
                          url_encode(this->queue_id_) + "&level=" + std::to_string(level);
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::set_mute(bool muted) {
  const std::string url = this->base_url_ + "/api/v1/ma/mute?queue_id=" +
                          url_encode(this->queue_id_) + "&muted=" + (muted ? "true" : "false");
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::set_shuffle(bool enabled) {
  const std::string url = this->base_url_ + "/api/v1/ma/shuffle?queue_id=" +
                          url_encode(this->queue_id_) + "&enabled=" + (enabled ? "true" : "false");
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

void HttpMusicLibrary::set_repeat(const std::string &mode) {
  const std::string url = this->base_url_ + "/api/v1/ma/repeat?queue_id=" +
                          url_encode(this->queue_id_) + "&mode=" + mode;
  this->enqueue_([this, url]() { this->http_post_(url); }, nullptr);
}

}  // namespace ha_dashboard
}  // namespace esphome
#endif  // USE_HA_DASHBOARD_LAUNCHER
