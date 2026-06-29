"""GSL3670 (SiLead) touch controller — used by the Seeed reTerminal D1001.

Provides a ``touchscreen: - platform: gsl3670`` platform (see touchscreen/).
Native ESPHome driver (direct I2C); the SiLead firmware is vendored from the Seeed
BSP (see touchscreen/gsl3670_firmware.h).
"""
import esphome.codegen as cg

CODEOWNERS = ["@AntorFR"]

gsl3670_ns = cg.esphome_ns.namespace("gsl3670")
