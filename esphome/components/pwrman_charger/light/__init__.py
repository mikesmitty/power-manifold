import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID
from .. import CONF_PWRMAN_CHARGER_ID, PWRMAN_CHARGER_SCHEMA, pwrman_charger_ns

PwrmanChargerLightOutput = pwrman_charger_ns.class_(
    "PwrmanChargerLight", light.LightOutput, cg.Component
)

CONFIG_SCHEMA = (
    light.RGB_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_PWRMAN_CHARGER_ID): cv.declare_id(
                PwrmanChargerLightOutput
            ),
        }
    )
    .extend(PWRMAN_CHARGER_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_PWRMAN_CHARGER_ID])
    await light.register_light(var, config)

    # out = await cg.get_variable(config[CONF_OUTPUT])
    # cg.add(var.set_output(out))
