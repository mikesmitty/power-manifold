import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import (
    CONF_POWER,
    DEVICE_CLASS_OUTLET,
    DEVICE_CLASS_SWITCH,
    ENTITY_CATEGORY_CONFIG,
)
from .. import (
    CONF_PWRMAN_CHARGER_ID,
    PWRMAN_CHARGER_SCHEMA,
    pwrman_charger_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["pwrman_charger"]

CONF_ENABLE_12V = "enable_12v"

PwrmanChargerSwitch = pwrman_charger_ns.class_(
    "PwrmanChargerSwitch", switch.Switch, cg.Component
)
PwrmanCharger12vSwitch = pwrman_charger_ns.class_(
    "PwrmanCharger12vSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = PWRMAN_CHARGER_SCHEMA.extend(
    {
        cv.Optional(CONF_POWER): switch.switch_schema(
            PwrmanChargerSwitch,
            block_inverted=True,
            device_class=DEVICE_CLASS_OUTLET,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_ENABLE_12V): switch.switch_schema(
            PwrmanCharger12vSwitch,
            block_inverted=True,
            device_class=DEVICE_CLASS_SWITCH,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    pwrman_charger_component = await cg.get_variable(config[CONF_PWRMAN_CHARGER_ID])
    if power_config := config.get(CONF_POWER):
        s = await switch.new_switch(power_config)
        await cg.register_parented(s, config[CONF_PWRMAN_CHARGER_ID])
        cg.add(pwrman_charger_component.set_power_switch(s))
    if enable_12v_config := config.get(CONF_ENABLE_12V):
        s = await switch.new_switch(enable_12v_config)
        await cg.register_parented(s, config[CONF_PWRMAN_CHARGER_ID])
        cg.add(pwrman_charger_component.set_enable_12v_switch(s))
