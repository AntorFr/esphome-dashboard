"""p4_camera_light — experimental ambient-light sensor from the D1001 MIPI-CSI camera.

Spike per ADR-0008. The sensor platform (schema + codegen) lives in sensor.py. ESP32-P4 only.
"""
import esphome.codegen as cg

CODEOWNERS = ["@AntorFr"]

p4_camera_light_ns = cg.esphome_ns.namespace("p4_camera_light")
