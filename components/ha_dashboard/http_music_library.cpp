#include "http_music_library.h"
#ifdef USE_HA_DASHBOARD_LAUNCHER
#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace ha_dashboard {

static const char *const TAG = "ha_dashboard.ml";

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

// music-library returns http cover URLs that 301-redirect to https (which online_image
// doesn't follow across schemes) -> fetch https directly.
static std::string upgrade_https(std::string u) {
  if (u.rfind("http://", 0) == 0)
    u.replace(0, 7, "https://");
  return u;
}

// Read a full response body into `out`. Blocks until COMPLETE / error / timeout.
static bool read_body(http_request::HttpContainer *c, std::string &out, uint32_t timeout_ms) {
  uint8_t buf[256];
  uint32_t last = millis();
  while (true) {
    int r = c->read(buf, sizeof(buf));
    App.feed_wdt();
    yield();
    auto res = http_request::http_read_loop_result(r, last, timeout_ms, c->is_read_complete());
    if (res == http_request::HttpReadLoopResult::DATA) {
      out.append(reinterpret_cast<char *>(buf), r);
      continue;
    }
    if (res == http_request::HttpReadLoopResult::RETRY)
      continue;
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

void HttpMusicLibrary::fetch_favorites(const std::string &owner, QuickItemsCallback cb) {
  const std::string url = this->base_url_ + "/api/v1/quick/" + url_encode(owner);
  std::string body;
  std::vector<QuickItem> items;
  bool ok = this->http_get_(url, body);
  if (ok) {
    ok = json::parse_json(body, [&items](JsonObject root) -> bool {
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
        q.cover_url = upgrade_https(it["cover_url"] | "");
        q.has_children = it["has_children"] | false;
        items.push_back(std::move(q));
      }
      return true;
    });
  }
  cb(ok, std::move(items));
}

void HttpMusicLibrary::fetch_children(const std::string &item_id, int offset, int limit,
                                      QuickPageCallback cb) {
  const std::string url = this->base_url_ + "/api/v1/quick/item/" + url_encode(item_id) +
                          "/children?offset=" + std::to_string(offset) +
                          "&limit=" + std::to_string(limit);
  std::string body;
  std::vector<QuickItem> items;
  bool has_more = false;
  bool ok = this->http_get_(url, body);
  if (ok) {
    ok = json::parse_json(body, [&items, &has_more](JsonObject root) -> bool {
      has_more = root["has_more"] | false;
      JsonArray arr = root["items"].as<JsonArray>();
      if (arr.isNull())
        return false;
      for (JsonVariant v : arr) {
        JsonObject it = v.as<JsonObject>();
        QuickItem q;
        q.title = it["title"] | "";
        q.uri = it["uri"] | "";
        q.cover_url = upgrade_https(it["cover_url"] | "");  // null for chapters -> ""
        q.seek = it["seek"] | 0;
        items.push_back(std::move(q));
      }
      return true;
    });
  }
  cb(ok, std::move(items), has_more);
}

void HttpMusicLibrary::play(const std::string &uri, int seek_s) {
  std::string url = this->base_url_ + "/api/v1/ma/play?queue_id=" + url_encode(this->queue_id_) +
                    "&uri=" + url_encode(uri);
  if (seek_s > 0)
    url += "&seek=" + std::to_string(seek_s);
  this->http_post_(url);
}

void HttpMusicLibrary::fetch_now_playing(NowPlayingCallback cb) {
  const std::string url =
      this->base_url_ + "/api/v1/ma/now_playing?queue_id=" + url_encode(this->queue_id_);
  std::string body;
  NowPlaying np;
  bool ok = this->http_get_(url, body);
  if (ok) {
    ok = json::parse_json(body, [&np](JsonObject root) -> bool {
      np.available = root["available"] | false;
      np.state = root["state"] | "idle";
      np.title = root["title"] | "";
      np.artist = root["artist"] | "";
      np.position_s = root["position_s"] | 0;
      np.duration_s = root["duration_s"] | 0;
      np.volume = root["volume"] | -1;
      return true;
    });
  }
  cb(ok, std::move(np));
}

void HttpMusicLibrary::transport(const std::string &cmd) {
  this->http_post_(this->base_url_ + "/api/v1/ma/" + cmd + "?queue_id=" + url_encode(this->queue_id_));
}

}  // namespace ha_dashboard
}  // namespace esphome
#endif  // USE_HA_DASHBOARD_LAUNCHER
