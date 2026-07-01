#include "homeassistant_media_player.h"
#include "esphome/core/log.h"
#include "esphome/components/api/api_server.h"

namespace esphome {
namespace homeassistant_addon {

static const char *const TAG = "homeassistant_addon.media_player";

void HomeassistantMediaPlayer::setup() {
  // Subscribe to state
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, nullopt,
      [this](StringRef state) {
        std::string state_str = state.str();
        ESP_LOGD(TAG, "'%s' state: %s", this->entity_id_, state_str.c_str());
        
        MediaPlayerState new_state = MediaPlayerState::UNKNOWN;
        if (state_str == "off") {
          new_state = MediaPlayerState::OFF;
        } else if (state_str == "on") {
          new_state = MediaPlayerState::ON;
        } else if (state_str == "idle") {
          new_state = MediaPlayerState::IDLE;
        } else if (state_str == "playing") {
          new_state = MediaPlayerState::PLAYING;
        } else if (state_str == "paused") {
          new_state = MediaPlayerState::PAUSED;
        } else if (state_str == "standby") {
          new_state = MediaPlayerState::STANDBY;
        } else if (state_str == "buffering") {
          new_state = MediaPlayerState::BUFFERING;
        }
        
        if (new_state != this->state_) {
          this->state_ = new_state;
          this->state_callback_.call();
        }
      });

  // Subscribe to volume_level
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("volume_level"),
      [this](StringRef state) {
        std::string state_str = state.str();
        if (state_str.empty() || state_str == "None" || state_str == "unknown" || state_str == "unavailable") {
          return;
        }
        auto val = parse_number<float>(state_str);
        if (val.has_value()) {
          float new_vol = val.value();
          ESP_LOGD(TAG, "'%s' volume: %.2f", this->entity_id_, new_vol);
          if (std::abs(new_vol - this->volume_) > 0.001f) {
            this->volume_ = new_vol;
            this->state_callback_.call();
          }
        }
      });

  // Subscribe to is_volume_muted
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("is_volume_muted"),
      [this](StringRef state) {
        std::string state_str = state.str();
        bool new_muted = (state_str == "True" || state_str == "true" || state_str == "1");
        ESP_LOGD(TAG, "'%s' muted: %s", this->entity_id_, state_str.c_str());
        if (new_muted != this->muted_) {
          this->muted_ = new_muted;
          this->state_callback_.call();
        }
      });

  // Subscribe to media_title
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("media_title"),
      [this](StringRef state) {
        std::string state_str = state.str();
        if (state_str == "None" || state_str == "unknown" || state_str == "unavailable") {
          if (!this->media_title_.empty()) {
            this->media_title_ = "";
            this->state_callback_.call();
          }
          return;
        }
        ESP_LOGD(TAG, "'%s' title: %s", this->entity_id_, state_str.c_str());
        if (state_str != this->media_title_) {
          this->media_title_ = state_str;
          this->state_callback_.call();
        }
      });

  // Subscribe to media_artist
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("media_artist"),
      [this](StringRef state) {
        std::string state_str = state.str();
        if (state_str == "None" || state_str == "unknown" || state_str == "unavailable") {
          if (!this->media_artist_.empty()) {
            this->media_artist_ = "";
            this->state_callback_.call();
          }
          return;
        }
        ESP_LOGD(TAG, "'%s' artist: %s", this->entity_id_, state_str.c_str());
        if (state_str != this->media_artist_) {
          this->media_artist_ = state_str;
          this->state_callback_.call();
        }
      });

  // Subscribe to source
  api::global_api_server->subscribe_home_assistant_state(
      this->entity_id_, std::string("source"),
      [this](StringRef state) {
        std::string state_str = state.str();
        if (state_str == "None" || state_str == "unknown" || state_str == "unavailable") {
          if (!this->source_.empty()) {
            this->source_ = "";
            this->state_callback_.call();
          }
          return;
        }
        ESP_LOGD(TAG, "'%s' source: %s", this->entity_id_, state_str.c_str());
        if (state_str != this->source_) {
          this->source_ = state_str;
          this->state_callback_.call();
        }
      });
}

void HomeassistantMediaPlayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Home Assistant Media Player:");
  ESP_LOGCONFIG(TAG, "  Entity ID: %s", this->entity_id_);
  ESP_LOGCONFIG(TAG, "  Volume Step: %.2f", this->volume_step_);
}

void HomeassistantMediaPlayer::send_command_(const std::string &service) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  
  api::HomeassistantActionRequest req;
  std::string full_service = "media_player." + service;
  std::string entity_id_str = this->entity_id_;
  
  req.service = StringRef(full_service);
  req.data.init(1);
  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(entity_id_str);
  
  ESP_LOGD(TAG, "Calling %s on %s", full_service.c_str(), this->entity_id_);
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantMediaPlayer::send_command_with_data_(const std::string &service, 
                                                        const std::string &data_key, 
                                                        const std::string &data_value) {
  static constexpr auto ENTITY_ID_KEY = StringRef::from_lit("entity_id");
  
  api::HomeassistantActionRequest req;
  std::string full_service = "media_player." + service;
  std::string entity_id_str = this->entity_id_;
  
  req.service = StringRef(full_service);
  req.data.init(2);
  
  auto &entity_id_kv = req.data.emplace_back();
  entity_id_kv.key = ENTITY_ID_KEY;
  entity_id_kv.value = StringRef(entity_id_str);
  
  auto &data_kv = req.data.emplace_back();
  data_kv.key = StringRef(data_key);
  data_kv.value = StringRef(data_value);
  
  ESP_LOGD(TAG, "Calling %s on %s with %s=%s", full_service.c_str(), 
           this->entity_id_, data_key.c_str(), data_value.c_str());
  api::global_api_server->send_homeassistant_action(req);
}

void HomeassistantMediaPlayer::send_command_with_float_(const std::string &service,
                                                         const std::string &data_key,
                                                         float data_value) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.3f", data_value);
  this->send_command_with_data_(service, data_key, std::string(buf));
}

void HomeassistantMediaPlayer::play() {
  this->send_command_("media_play");
}

void HomeassistantMediaPlayer::pause() {
  this->send_command_("media_pause");
}

void HomeassistantMediaPlayer::play_pause() {
  this->send_command_("media_play_pause");
}

void HomeassistantMediaPlayer::stop() {
  this->send_command_("media_stop");
}

void HomeassistantMediaPlayer::next_track() {
  this->send_command_("media_next_track");
}

void HomeassistantMediaPlayer::previous_track() {
  this->send_command_("media_previous_track");
}

void HomeassistantMediaPlayer::volume_up() {
  this->send_command_("volume_up");
}

void HomeassistantMediaPlayer::volume_down() {
  this->send_command_("volume_down");
}

void HomeassistantMediaPlayer::set_volume(float volume) {
  if (volume < 0.0f) volume = 0.0f;
  if (volume > 1.0f) volume = 1.0f;
  this->send_command_with_float_("volume_set", "volume_level", volume);
}

void HomeassistantMediaPlayer::mute() {
  this->send_command_with_data_("volume_mute", "is_volume_muted", "true");
}

void HomeassistantMediaPlayer::unmute() {
  this->send_command_with_data_("volume_mute", "is_volume_muted", "false");
}

void HomeassistantMediaPlayer::turn_on() {
  this->send_command_("turn_on");
}

void HomeassistantMediaPlayer::turn_off() {
  this->send_command_("turn_off");
}

}  // namespace homeassistant_addon
}  // namespace esphome
