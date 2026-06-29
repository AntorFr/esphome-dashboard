#pragma once

#include "esphome/core/component.h"
#include "esphome/core/string_ref.h"
#include "esphome/components/api/custom_api_device.h"
#include <string>
#include <functional>

namespace esphome {
namespace homeassistant_addon {

enum class MediaPlayerState : uint8_t {
  UNKNOWN = 0,
  OFF,
  ON,
  IDLE,
  PLAYING,
  PAUSED,
  STANDBY,
  BUFFERING,
};

class HomeassistantMediaPlayer : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_entity_id(const char *entity_id) { this->entity_id_ = entity_id; }
  void set_volume_step(float step) { this->volume_step_ = step; }

  // Getters
  const char *get_entity_id() const { return this->entity_id_; }
  MediaPlayerState get_state() const { return this->state_; }
  float get_volume() const { return this->volume_; }
  bool is_muted() const { return this->muted_; }
  const std::string &get_media_title() const { return this->media_title_; }
  const std::string &get_media_artist() const { return this->media_artist_; }
  const std::string &get_source() const { return this->source_; }
  float get_volume_step() const { return this->volume_step_; }

  // Control methods
  void play();
  void pause();
  void play_pause();
  void stop();
  void next_track();
  void previous_track();
  void volume_up();
  void volume_down();
  void set_volume(float volume);
  void mute();
  void unmute();
  void turn_on();
  void turn_off();

  // Callback for state changes
  void add_on_state_callback(std::function<void()> &&callback) {
    this->state_callback_.add(std::move(callback));
  }

 protected:
  void send_command_(const std::string &service);
  void send_command_with_data_(const std::string &service, const std::string &data_key, const std::string &data_value);
  void send_command_with_float_(const std::string &service, const std::string &data_key, float data_value);

  const char *entity_id_{nullptr};
  float volume_step_{0.05f};

  // State
  MediaPlayerState state_{MediaPlayerState::UNKNOWN};
  float volume_{0.0f};
  bool muted_{false};
  std::string media_title_;
  std::string media_artist_;
  std::string source_;

  CallbackManager<void()> state_callback_;
};

}  // namespace homeassistant_addon
}  // namespace esphome
