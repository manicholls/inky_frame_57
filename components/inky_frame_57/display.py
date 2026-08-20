import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome.const import CONF_ID

DEPENDENCIES = ['spi']

inky_frame_57_ns = cg.esphome_ns.namespace('inky_frame_57')
InkyFrame57 = inky_frame_57_ns.class_('InkyFrame57', display.DisplayBuffer, spi.SPIDevice)

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(InkyFrame57),
}).extend(spi.spi_device_schema())

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    # THIS is the missing magic line that compiles your YAML lambda into C++!
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)
