import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_MAX_CURRENT

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_PORT_NUMBER = "port_number"
CONF_PWRMAN_CHARGER_ID = "pwrman_charger_id"

pwrman_charger_ns = cg.esphome_ns.namespace("pwrman_charger")
PwrmanCharger = pwrman_charger_ns.class_("PwrmanCharger", cg.Component, i2c.I2CDevice)

PWRMAN_CHARGER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_PWRMAN_CHARGER_ID): cv.use_id(PwrmanCharger),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PwrmanCharger),
            # FIXME: add max_current
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x4D))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
