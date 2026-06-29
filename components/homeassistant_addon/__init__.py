"""
homeassistant_addon component for ESPHome
Provides cover, climate, and media_player platforms imported from Home Assistant.

These entity types are not natively available in ESPHome's homeassistant platform.

Usage:
  cover:
    - platform: homeassistant_addon
      id: my_gate
      entity_id: cover.front_gate

  climate:
    - platform: homeassistant_addon
      id: my_thermostat
      entity_id: climate.living_room

  # Media player (custom, not a standard ESPHome platform):
  homeassistant_addon:
    media_players:
      - id: my_speaker
        entity_id: media_player.living_room
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ENTITY_ID, CONF_ID, CONF_INTERNAL

DEPENDENCIES = ["api"]
CODEOWNERS = ["@AntorFr"]

# Namespace - shared by all platforms
homeassistant_addon_ns = cg.esphome_ns.namespace("homeassistant_addon")

# Classes - exported for use by platforms
HomeassistantCover = homeassistant_addon_ns.class_(
    "HomeassistantCover", cg.Component
)
HomeassistantClimate = homeassistant_addon_ns.class_(
    "HomeassistantClimate", cg.Component
)
HomeassistantMediaPlayer = homeassistant_addon_ns.class_(
    "HomeassistantMediaPlayer", cg.Component
)

# Configuration keys for media player (not a standard platform)
CONF_MEDIA_PLAYERS = "media_players"
CONF_VOLUME_STEP = "volume_step"

# Media player schema (custom, since media_player is not a standard ESPHome platform)
MEDIA_PLAYER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HomeassistantMediaPlayer),
        cv.Required(CONF_ENTITY_ID): cv.entity_id,
        cv.Optional(CONF_INTERNAL, default=True): cv.boolean,
        cv.Optional(CONF_VOLUME_STEP, default=0.05): cv.float_range(min=0.01, max=0.2),
    }
).extend(cv.COMPONENT_SCHEMA)

# Main schema - only for media_players (cover and climate use platform syntax)
CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MEDIA_PLAYERS): cv.ensure_list(MEDIA_PLAYER_SCHEMA),
    }
)


async def to_code(config):
    # Only process media players here (cover and climate are handled by their platforms)
    for conf in config.get(CONF_MEDIA_PLAYERS, []):
        # Enable required API features
        cg.add_define("USE_API_HOMEASSISTANT_STATES")
        cg.add_define("USE_API_HOMEASSISTANT_SERVICES")
        
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        
        cg.add(var.set_entity_id(conf[CONF_ENTITY_ID]))
        cg.add(var.set_volume_step(conf[CONF_VOLUME_STEP]))
