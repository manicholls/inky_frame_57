import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

inky_frame_ns = cg.esphome_ns.namespace("inky_frame_57")
InkyFrame57 = inky_frame_ns.class_(
    "InkyFrame57", display.DisplayBuffer, spi.SPIDevice
)

# Set cs_pin_required to False so we can manage it manually in C++
CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(InkyFrame57),
}).extend(cv.polling_component_schema("60s")).extend(spi.spi_device_schema(cs_pin_required=False))

async def to_code(config):
    cg.add_global(cg.RawStatement('#include "esphome/components/inky_frame_57/inky_frame_57.h"'))
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)
