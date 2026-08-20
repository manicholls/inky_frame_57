import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import display, spi
from esphome.const import CONF_ID, CONF_LAMBDA

DEPENDENCIES = ['spi']

inky_frame_57_ns = cg.esphome_ns.namespace('inky_frame_57')
InkyFrame57 = inky_frame_57_ns.class_('InkyFrame57', display.DisplayBuffer, spi.SPIDevice)

CONFIG_SCHEMA = display.FULL_DISPLAY_SCHEMA.extend({
    cv.GenerateID(): cv.declare_id(InkyFrame57),
}).extend(spi.spi_device_schema())

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    
    # 1. Register the display and SPI device natively
    await display.register_display(var, config)
    await spi.register_spi_device(var, config)
    
    # 2. THE MISSING LINK: FORCE LAMBDA COMPILATION
    # This explicitly translates your YAML lambda into a C++ function 
    # and attaches it to the display buffer's graphics engine.
    if CONF_LAMBDA in config:
        display_ns = cg.esphome_ns.namespace('display')
        DisplayBufferRef = display_ns.class_('DisplayBuffer').operator('ptr')
        
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(DisplayBufferRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
