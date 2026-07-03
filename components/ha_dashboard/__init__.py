"""ha_dashboard — composant cœur (modèle de cards + navigation + renderer LVGL).

Schéma destiné à l'utilisateur final : il ne décrit que profile/groups/cards.
Voir docs/config-reference.md et docs/architecture.md.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    climate,
    cover,
    http_request,
    online_image,
    sensor,
    switch,
    time as time_comp,
)
from esphome.components.homeassistant_addon import HomeassistantMediaPlayer
from esphome.components.lvgl import lv_validation as lvalid
from esphome.const import CONF_FORMAT, CONF_ID, CONF_NAME, CONF_TIME_ID, CONF_TYPE, CONF_URL
from esphome.core import CORE, ID

CODEOWNERS = ["@AntorFR"]
DEPENDENCIES = ["lvgl"]


# sensor / binary_sensor / switch sont toujours référencés par le C++ (encodeur/bouton du
# Dial, binding switch), même si le board courant ne les utilise pas -> AUTO_LOAD.
# `online_image` (+ image/runtime_image) n'est chargé QUE si un groupe `music_library` existe :
# on génère nous-mêmes les slots de covers/vignettes (cf. to_code) au lieu d'un bloc YAML manuel.
# Chargement conditionnel car online_image dépend de http_request (absent sur le Dial).
AUTO_LOAD = ["sensor", "binary_sensor", "switch", "time", "cover", "climate", "homeassistant_addon"]

ha_dashboard_ns = cg.esphome_ns.namespace("ha_dashboard")
HaDashboard = ha_dashboard_ns.class_("HaDashboard", cg.Component)

CONF_PROFILE = "profile"
CONF_LANGUAGE = "language"
CONF_WEATHER = "weather"
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
CONF_COVER_KIND = "cover_kind"
CONF_CLIMATE_ID = "climate_id"
CONF_MEDIA_PLAYER_ID = "media_player_id"
CONF_BASE_URL = "base_url"
CONF_OWNER = "owner"
CONF_PLAYER = "player"
CONF_PLAYER_NAME = "player_name"
CONF_IMAGE_FORMAT = "image_format"
CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_COVER_SLOTS = "cover_slots"
CONF_THUMB_SLOTS = "thumb_slots"
CONF_FONT_SMALL = "font_small"
CONF_FONT_MEDIUM = "font_medium"
CONF_FONT_LARGE = "font_large"
CONF_FONT_WEATHER = "font_weather"
CONF_FONT_ICONS = "font_icons"
CONF_FONT_ICONS_LG = "font_icons_lg"
CONF_FONT_VOICE = "font_voice"

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
            # Cover presentation override (else auto from HA device_class): shutter|garage|gate.
            cv.Optional(CONF_COVER_KIND): cv.one_of("shutter", "garage", "gate", lower=True),
        }
    ),
    _validate_card,
)

# A group is either a normal entity group (cards) or a "music_library" launcher tab
# (D1001) backed by the music-library REST API.
GROUP_TYPES = ["entities", "music_library"]


def _validate_group(group):
    if str(group.get(CONF_TYPE, "entities")) == "music_library":
        for key in (CONF_BASE_URL, CONF_OWNER, CONF_PLAYER, CONF_HTTP_REQUEST_ID):
            if key not in group:
                raise cv.Invalid(f"Un groupe 'music_library' requiert '{key}'")
    elif CONF_CARDS not in group:
        raise cv.Invalid("Un groupe 'entities' requiert 'cards'")
    return group


GROUP_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_NAME): cv.string,
            cv.Optional(CONF_ICON, default=""): cv.string,
            cv.Optional(CONF_TYPE, default="entities"): cv.one_of(*GROUP_TYPES, lower=True),
            cv.Optional(CONF_CARDS): cv.All(cv.ensure_list(CARD_SCHEMA), cv.Length(min=1)),
            # music_library launcher fields:
            cv.Optional(CONF_BASE_URL): cv.string,
            cv.Optional(CONF_OWNER): cv.string,
            cv.Optional(CONF_PLAYER): cv.string,
            cv.Optional(CONF_PLAYER_NAME): cv.string,  # friendly name for the launch toast
            # Cover/thumbnail encoding requested from music-library: jpg (small) or bmp (no-DCT
            # decode -> cheaper on the ESP loop). Must match the online_image slots' `format:`.
            cv.Optional(CONF_IMAGE_FORMAT, default="jpg"): cv.one_of("jpg", "bmp", lower=True),
            cv.Optional(CONF_HTTP_REQUEST_ID): cv.use_id(http_request.HttpRequestComponent),
            # Cover/thumbnail pools are AUTO-GENERATED (no manual online_image block): give a
            # count. Grid covers = one per favourite shown; detail slot 0 = header/now-playing,
            # slots 1.. = episode thumbnails. Kept apart so drilling in/out never disturbs the grid.
            cv.Optional(CONF_COVER_SLOTS, default=10): cv.int_range(min=1, max=32),
            cv.Optional(CONF_THUMB_SLOTS, default=16): cv.int_range(min=1, max=32),
        }
    ),
    _validate_group,
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
        # Weather module: bind a HA weather entity (header shows temperature + condition).
        cv.Optional(CONF_WEATHER): cv.Schema(
            {cv.Required(CONF_ENTITY): cv.entity_id}
        ),
        # Text fonts (with accented glyphs). Validated via lvgl's lv_font so they get
        # converted to lv_font_t (get_lv_font available). Optional: falls back to the
        # built-in lv_font_montserrat_* (ASCII only) if not provided.
        cv.Optional(CONF_FONT_SMALL): lvalid.lv_font,
        cv.Optional(CONF_FONT_MEDIUM): lvalid.lv_font,
        cv.Optional(CONF_FONT_LARGE): lvalid.lv_font,
        cv.Optional(CONF_FONT_WEATHER): lvalid.lv_font,
        cv.Optional(CONF_FONT_ICONS): lvalid.lv_font,
        cv.Optional(CONF_FONT_ICONS_LG): lvalid.lv_font,
        cv.Optional(CONF_FONT_VOICE): lvalid.lv_font,
        cv.Required(CONF_GROUPS): cv.All(cv.ensure_list(GROUP_SCHEMA), cv.Length(min=1)),
    }
).extend(cv.COMPONENT_SCHEMA)


# Placeholder URL for a generated cover/thumbnail slot — the launcher overwrites it at runtime
# (OnlineImage::set_url) with the real signed cover/thumb URL. Only needs to be a valid URL.
_ML_SLOT_URL = "http://ha-dashboard.invalid/_.jpg"


async def _make_ml_slot(name, http_id, fmt):
    """Generate one `online_image` slot by reusing online_image's own codegen (so we get its
    exact C++ class + build config: the JPEG/BMP decoder library + defines, component
    registration, http parenting). Returns the created variable."""
    conf = online_image.CONFIG_SCHEMA(
        {
            CONF_ID: ID(name, is_declaration=True, type=online_image.OnlineImage),
            CONF_URL: _ML_SLOT_URL,
            CONF_FORMAT: fmt,  # "JPG" | "BMP" (online_image upper-cases; JPG == JPEG)
            CONF_TYPE: "RGB565",
            CONF_HTTP_REQUEST_ID: http_id,
        }
    )
    # declare_id normally registers component ids during validation; we build this one at
    # to_code time, so register it ourselves (register_component consumes it).
    CORE.component_ids.add(str(conf[CONF_ID]))
    await online_image.to_code(conf)
    return await cg.get_variable(conf[CONF_ID])


async def to_code(config):
    # NB: the Montserrat font sizes used by the C++ renderer are enabled via
    # board build flags (-DLV_FONT_MONTSERRAT_xx), because this component is coded
    # after lvgl and add_define would be too late for lv_conf.h generation.
    # homeassistant_addon is AUTO_LOADed (all its .cpp compiled), and its HA state/service
    # API calls are guarded by these defines -> enable them unconditionally.
    cg.add_define("USE_API_HOMEASSISTANT_STATES")
    cg.add_define("USE_API_HOMEASSISTANT_SERVICES")

    # The music_library launcher (HTTP adapter) is compiled only when a launcher group is
    # configured. http_request has no USE_ define of its own, so we gate on our own flag.
    if any(str(g.get(CONF_TYPE, "entities")) == "music_library" for g in config[CONF_GROUPS]):
        cg.add_define("USE_HA_DASHBOARD_LAUNCHER")

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
    if CONF_WEATHER in config:
        cg.add(var.set_weather_entity(config[CONF_WEATHER][CONF_ENTITY]))
    for conf, setter in (
        (CONF_FONT_SMALL, var.set_font_small),
        (CONF_FONT_MEDIUM, var.set_font_medium),
        (CONF_FONT_LARGE, var.set_font_large),
        (CONF_FONT_WEATHER, var.set_font_weather),
        (CONF_FONT_ICONS, var.set_font_icons),
        (CONF_FONT_ICONS_LG, var.set_font_icons_lg),
        (CONF_FONT_VOICE, var.set_font_voice),
    ):
        if conf in config:
            cg.add(setter(await cg.get_variable(config[conf])))

    for group_index, group in enumerate(config[CONF_GROUPS]):
        if str(group.get(CONF_TYPE, "entities")) == "music_library":
            http = await cg.get_variable(group[CONF_HTTP_REQUEST_ID])
            cg.add(
                var.add_launcher_group(
                    group[CONF_NAME],
                    group[CONF_ICON],
                    http,
                    group[CONF_BASE_URL],
                    group[CONF_OWNER],
                    group[CONF_PLAYER],
                )
            )
            cg.add(var.set_launcher_image_format(group[CONF_IMAGE_FORMAT]))
            if CONF_PLAYER_NAME in group:
                cg.add(var.add_launcher_player_name(group[CONF_PLAYER_NAME]))
            # We generate online_image components here (no user `online_image:` block). Source
            # files are copied per domain KEY present in CORE.config (see writer.copy_src_tree /
            # iter_components), so inject the keys — validation is already done, and the value
            # is irrelevant to the source copy. This avoids AUTO_LOAD's empty-config validation.
            for _dom in ("image", "runtime_image", "online_image"):
                CORE.loaded_integrations.add(_dom)
                CORE.config.setdefault(_dom, [])
            # Auto-generate the online_image cover/thumbnail pools (no manual YAML block). The
            # slot decoder format must match what the launcher requests (image_format).
            slot_fmt = "BMP" if group[CONF_IMAGE_FORMAT] == "bmp" else "JPG"
            http_id = group[CONF_HTTP_REQUEST_ID]
            for i in range(group[CONF_COVER_SLOTS]):
                slot = await _make_ml_slot(f"mldash{group_index}_cov{i}", http_id, slot_fmt)
                cg.add(var.add_launcher_cover_slot(slot))
            for i in range(group[CONF_THUMB_SLOTS]):
                slot = await _make_ml_slot(f"mldash{group_index}_thm{i}", http_id, slot_fmt)
                cg.add(var.add_launcher_thumb_slot(slot))
            continue
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
                cg.add(var.add_cover_card(group_index, obj, name, color, has_color,
                                          card.get(CONF_COVER_KIND, "")))
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
