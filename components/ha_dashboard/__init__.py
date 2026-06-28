"""ha_dashboard — composant cœur (modèle de cards + navigation + renderer LVGL).

Schéma destiné à l'utilisateur final : il ne décrit que profile/groups/cards.
Voir docs/config-reference.md et docs/architecture.md.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor
from esphome.const import CONF_ID, CONF_NAME, CONF_TYPE

CODEOWNERS = ["@AntorFR"]
DEPENDENCIES = ["lvgl"]
# sensor / binary_sensor sont toujours référencés par le C++ (encodeur/bouton du Dial),
# même si le board courant ne les utilise pas (ex. D1001 tactile seul) -> AUTO_LOAD.
AUTO_LOAD = ["sensor", "binary_sensor"]

ha_dashboard_ns = cg.esphome_ns.namespace("ha_dashboard")
HaDashboard = ha_dashboard_ns.class_("HaDashboard", cg.Component)

CONF_PROFILE = "profile"
CONF_INACTIVITY_TIMEOUT = "inactivity_timeout"
CONF_ENCODER = "encoder"
CONF_ENCODER_BUTTON = "encoder_button"
CONF_GROUPS = "groups"
CONF_ICON = "icon"
CONF_CARDS = "cards"
CONF_ENTITY = "entity"
CONF_COLOR = "color"

PROFILES = ["dial", "reterminal_d1001"]

# Valeurs alignées sur l'enum C++ CardType (model.h).
CARD_TYPES = {"light": 0, "switch": 1}


def _hex_color(value):
    value = cv.string(value).lstrip("#")
    if len(value) != 6:
        raise cv.Invalid("La couleur doit être au format '#RRGGBB'")
    try:
        int(value, 16)
    except ValueError as err:
        raise cv.Invalid("Couleur hexadécimale invalide") from err
    return f"#{value}"


CARD_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TYPE): cv.enum(CARD_TYPES, lower=True),
        cv.Required(CONF_ENTITY): cv.entity_id,
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_COLOR): _hex_color,
    }
)

GROUP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string,
        cv.Optional(CONF_ICON, default=""): cv.string,
        cv.Required(CONF_CARDS): cv.All(cv.ensure_list(CARD_SCHEMA), cv.Length(min=1)),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HaDashboard),
        cv.Optional(CONF_PROFILE, default="dial"): cv.one_of(*PROFILES, lower=True),
        cv.Optional(
            CONF_INACTIVITY_TIMEOUT, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ENCODER): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_ENCODER_BUTTON): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_GROUPS): cv.All(cv.ensure_list(GROUP_SCHEMA), cv.Length(min=1)),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_profile(config[CONF_PROFILE]))
    cg.add(var.set_inactivity_timeout(config[CONF_INACTIVITY_TIMEOUT]))

    if CONF_ENCODER in config:
        enc = await cg.get_variable(config[CONF_ENCODER])
        cg.add(var.set_encoder(enc))
    if CONF_ENCODER_BUTTON in config:
        btn = await cg.get_variable(config[CONF_ENCODER_BUTTON])
        cg.add(var.set_button(btn))

    for group_index, group in enumerate(config[CONF_GROUPS]):
        cg.add(var.add_group(group[CONF_NAME], group[CONF_ICON]))
        for card in group[CONF_CARDS]:
            color = 0
            has_color = False
            if CONF_COLOR in card:
                color = int(card[CONF_COLOR].lstrip("#"), 16)
                has_color = True
            cg.add(
                var.add_card(
                    group_index,
                    card[CONF_TYPE],
                    card[CONF_ENTITY],
                    card.get(CONF_NAME, ""),
                    color,
                    has_color,
                )
            )
