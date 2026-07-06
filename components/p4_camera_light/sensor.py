"""sensor platform for p4_camera_light (ADR-0008): mean RAW8 luma as an ambient-light proxy.

Pulls Espressif's esp_video (V4L2) stack + the SC2356 driver and enables the required
sdkconfig. Stage 1 (integration gate): prove esp_video links/boots under ESPHome on the P4.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.components.esp32 import (
    add_idf_component,
    add_idf_sdkconfig_option,
    only_on_variant,
)
from esphome.components.esp32.const import VARIANT_ESP32P4
from esphome.const import (
    DEVICE_CLASS_ILLUMINANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
)

from . import p4_camera_light_ns

DEPENDENCIES = ["esp32"]

P4CameraLight = p4_camera_light_ns.class_(
    "P4CameraLight", cg.PollingComponent, sensor.Sensor
)

CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        P4CameraLight,
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_ILLUMINANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    ).extend(cv.polling_component_schema("60s")),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    # Espressif V4L2 camera stack + camera sensor drivers (includes SC2356).
    add_idf_component(name="espressif/esp_video", ref="1.2.0")

    # Enable the SC2356 (RAW8 720p) on MIPI-CSI + the ISP pipeline; disable DVP. Values match
    # the Seeed factory firmware sdkconfig (see ADR-0008).
    for opt, val in [
        ("CONFIG_CAMERA_SC2356", True),
        ("CONFIG_CAMERA_SC2356_MIPI_RAW8_1280X720_30FPS", True),
        ("CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE", True),
        ("CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER", True),
        ("CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE", False),
    ]:
        add_idf_sdkconfig_option(opt, val)
