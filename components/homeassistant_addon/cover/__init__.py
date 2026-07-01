"""
Cover platform for homeassistant_addon component.

Usage:
  cover:
    - platform: homeassistant_addon
      id: my_gate
      entity_id: cover.front_gate
"""
import esphome.codegen as cg
from esphome.components import cover
import esphome.config_validation as cv
from esphome.const import CONF_ENTITY_ID, CONF_ID

from .. import homeassistant_addon_ns, DEPENDENCIES

HomeassistantCover = homeassistant_addon_ns.class_(
    "HomeassistantCover", cover.Cover, cg.Component
)

CONFIG_SCHEMA = cover.cover_schema(HomeassistantCover).extend(
    {
        cv.Required(CONF_ENTITY_ID): cv.entity_id,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Enable required API features
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    cg.add_define("USE_API_HOMEASSISTANT_SERVICES")
    
    var = await cover.new_cover(config)
    await cg.register_component(var, config)
    
    cg.add(var.set_entity_id(config[CONF_ENTITY_ID]))
