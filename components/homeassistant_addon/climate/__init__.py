"""
Climate platform for homeassistant_addon component.

Usage:
  climate:
    - platform: homeassistant_addon
      id: my_thermostat
      entity_id: climate.living_room
      temperature_step: 0.5
      min_temperature: 15
      max_temperature: 30
"""
import esphome.codegen as cg
from esphome.components import climate
import esphome.config_validation as cv
from esphome.const import CONF_ENTITY_ID, CONF_ID

from .. import homeassistant_addon_ns, DEPENDENCIES

CONF_TEMPERATURE_STEP = "temperature_step"
CONF_MIN_TEMPERATURE = "min_temperature"
CONF_MAX_TEMPERATURE = "max_temperature"

HomeassistantClimate = homeassistant_addon_ns.class_(
    "HomeassistantClimate", climate.Climate, cg.Component
)

CONFIG_SCHEMA = climate.climate_schema(HomeassistantClimate).extend(
    {
        cv.Required(CONF_ENTITY_ID): cv.entity_id,
        cv.Optional(CONF_TEMPERATURE_STEP, default=0.5): cv.float_range(min=0.1, max=2.0),
        cv.Optional(CONF_MIN_TEMPERATURE, default=7.0): cv.float_range(min=-20, max=50),
        cv.Optional(CONF_MAX_TEMPERATURE, default=35.0): cv.float_range(min=-20, max=50),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Enable required API features
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    cg.add_define("USE_API_HOMEASSISTANT_SERVICES")
    
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    
    cg.add(var.set_entity_id(config[CONF_ENTITY_ID]))
    cg.add(var.set_temperature_step(config[CONF_TEMPERATURE_STEP]))
    cg.add(var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
    cg.add(var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
