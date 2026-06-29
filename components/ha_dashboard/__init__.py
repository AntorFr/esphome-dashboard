"""ha_dashboard — composant cœur (modèle de cards + navigation + renderer LVGL).

Schéma destiné à l'utilisateur final : il ne décrit que profile/groups/cards.
Voir docs/config-reference.md et docs/architecture.md.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, cover, sensor, switch, time as time_comp
from esphome.components.homeassistant_addon import HomeassistantMediaPlayer
from esphome.components.lvgl import lv_validation as lvalid
from esphome.const import CONF_ID, CONF_NAME, CONF_TIME_ID, CONF_TYPE

CODEOWNERS = ["@AntorFR"]
DEPENDENCIES = ["lvgl"]
# sensor / binary_sensor / switch sont toujours référencés par le C++ (encodeur/bouton du
# Dial, binding switch), même si le board courant ne les utilise pas -> AUTO_LOAD.
AUTO_LOAD = [
    "sensor",
    "binary_sensor",
    "switch",
    "time",
    "cover",
    "climate",
    "homeassistant_addon",
]

ha_dashboard_ns = cg.esphome_ns.namespace("ha_dashboard")
HaDashboard = ha_dashboard_ns.class_("HaDashboard", cg.Component)

CONF_PROFILE = "profile"
CONF_LANGUAGE = "language"
CONF_INACTIVITY_TIMEOUT = "inactivity_timeout"
CONF_ENCODER = "encoder"
CONF_ENCODER_BUTTON = "encoder_button"
CONF_GROUPS = "groups"
CONF_ICON = "icon"
CONF_CARDS = "cards"
CONF_ENTITY = "entity"
CONF_COLOR = "color"
CONF_SWITCH_ID = "switch_id"
CONF_COVER_ID = "cover_id"
CONF_CLIMATE_ID = "climate_id"
CONF_MEDIA_PLAYER_ID = "media_player_id"
CONF_FONT_SMALL = "font_small"
CONF_FONT_MEDIUM = "font_medium"
CONF_FONT_LARGE = "font_large"

PROFILES = ["dial", "reterminal_d1001"]

# Valeurs alignées sur l'enum C++ CardType (model.h).
CARD_TYPES = {"light": 0, "switch": 1, "cover": 2, "media_player": 3, "climate": 4}


def _hex_color(value):
    value = cv.string(value).lstrip("#")
    if len(value) != 6:
        raise cv.Invalid("La couleur doit être au format '#RRGGBB'")
    try:
        int(value, 16)
    except ValueError as err:
        raise cv.Invalid("Couleur hexadécimale invalide") from err
    return f"#{value}"


# Required binding id per card type (cv.enum yields the string key). switch -> esphome
# switch ; cover/climate/media -> homeassistant_addon objects ; light -> entity (stub).
_REQUIRED_ID = {
    "switch": CONF_SWITCH_ID,
    "cover": CONF_COVER_ID,
    "climate": CONF_CLIMATE_ID,
    "media_player": CONF_MEDIA_PLAYER_ID,
    "light": CONF_ENTITY,
}


def _validate_card(card):
    needed = _REQUIRED_ID[str(card[CONF_TYPE])]
    if needed not in card:
        raise cv.Invalid(f"Une card '{card[CONF_TYPE]}' requiert '{needed}'")
    return card


CARD_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_TYPE): cv.enum(CARD_TYPES, lower=True),
            cv.Optional(CONF_SWITCH_ID): cv.use_id(switch.Switch),
            cv.Optional(CONF_COVER_ID): cv.use_id(cover.Cover),
            cv.Optional(CONF_CLIMATE_ID): cv.use_id(climate.Climate),
            cv.Optional(CONF_MEDIA_PLAYER_ID): cv.use_id(HomeassistantMediaPlayer),
            cv.Optional(CONF_ENTITY): cv.entity_id,
            cv.Optional(CONF_NAME): cv.string,
            cv.Optional(CONF_COLOR): _hex_color,
        }
    ),
    _validate_card,
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
        cv.Optional(CONF_LANGUAGE, default="en"): cv.one_of("fr", "en", lower=True),
        cv.Optional(
            CONF_INACTIVITY_TIMEOUT, default="30s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ENCODER): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_ENCODER_BUTTON): cv.use_id(binary_sensor.BinarySensor),
        cv.Optional(CONF_TIME_ID): cv.use_id(time_comp.RealTimeClock),
        # Text fonts (with accented glyphs). Validated via lvgl's lv_font so they get
        # converted to lv_font_t (get_lv_font available). Optional: falls back to the
        # built-in lv_font_montserrat_* (ASCII only) if not provided.
        cv.Optional(CONF_FONT_SMALL): lvalid.lv_font,
        cv.Optional(CONF_FONT_MEDIUM): lvalid.lv_font,
        cv.Optional(CONF_FONT_LARGE): lvalid.lv_font,
        cv.Required(CONF_GROUPS): cv.All(cv.ensure_list(GROUP_SCHEMA), cv.Length(min=1)),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # NB: the Montserrat font sizes used by the C++ renderer are enabled via
    # board build flags (-DLV_FONT_MONTSERRAT_xx), because this component is coded
    # after lvgl and add_define would be too late for lv_conf.h generation.
    # homeassistant_addon is AUTO_LOADed (all its .cpp compiled), and its HA state/service
    # API calls are guarded by these defines -> enable them unconditionally.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    cg.add_define("USE_API_HOMEASSISTANT_SERVICES")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_profile(config[CONF_PROFILE]))
    cg.add(var.set_language(config[CONF_LANGUAGE]))
    cg.add(var.set_inactivity_timeout(config[CONF_INACTIVITY_TIMEOUT]))

    if CONF_ENCODER in config:
        enc = await cg.get_variable(config[CONF_ENCODER])
        cg.add(var.set_encoder(enc))
    if CONF_ENCODER_BUTTON in config:
        btn = await cg.get_variable(config[CONF_ENCODER_BUTTON])
        cg.add(var.set_button(btn))
    if CONF_TIME_ID in config:
        rtc = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(rtc))
    for conf, setter in (
        (CONF_FONT_SMALL, var.set_font_small),
        (CONF_FONT_MEDIUM, var.set_font_medium),
        (CONF_FONT_LARGE, var.set_font_large),
    ):
        if conf in config:
            cg.add(setter(await cg.get_variable(config[conf])))

    for group_index, group in enumerate(config[CONF_GROUPS]):
        cg.add(var.add_group(group[CONF_NAME], group[CONF_ICON]))
        for card in group[CONF_CARDS]:
            color = 0
            has_color = False
            if CONF_COLOR in card:
                color = int(card[CONF_COLOR].lstrip("#"), 16)
                has_color = True
            name = card.get(CONF_NAME, "")
            if CONF_SWITCH_ID in card:
                obj = await cg.get_variable(card[CONF_SWITCH_ID])
                cg.add(var.add_switch_card(group_index, obj, name, color, has_color))
            elif CONF_COVER_ID in card:
                obj = await cg.get_variable(card[CONF_COVER_ID])
                cg.add(var.add_cover_card(group_index, obj, name, color, has_color))
            elif CONF_CLIMATE_ID in card:
                obj = await cg.get_variable(card[CONF_CLIMATE_ID])
                cg.add(var.add_climate_card(group_index, obj, name, color, has_color))
            elif CONF_MEDIA_PLAYER_ID in card:
                obj = await cg.get_variable(card[CONF_MEDIA_PLAYER_ID])
                cg.add(var.add_media_card(group_index, obj, name, color, has_color))
            else:  # light (stub)
                cg.add(
                    var.add_card(
                        group_index,
                        CARD_TYPES[str(card[CONF_TYPE])],
                        card.get(CONF_ENTITY, ""),
                        name,
                        color,
                        has_color,
                    )
                )
