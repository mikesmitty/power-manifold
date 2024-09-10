import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_MAX_CURRENT

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["uart"]

CONF_LAD_ID = "lad_id"

lad_component_ns = cg.esphome_ns.namespace("lad_component")
LadComponent = lad_component_ns.class_("LadComponent", cg.Component, uart.UARTDevice)

LAD_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LAD_ID): cv.use_id(LadComponent),
    }
)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(LadComponent)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
