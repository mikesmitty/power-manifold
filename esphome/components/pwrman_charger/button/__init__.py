import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_RESTART,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ENTITY_CATEGORY_CONFIG,
    ICON_RESTART,
)
from .. import CONF_PWRMAN_CHARGER_ID, PwrmanCharger, pwrman_charger_ns

HardResetButton = pwrman_charger_ns.class_("HardResetButton", button.Button)
SrcCapButton = pwrman_charger_ns.class_("SrcCapButton", button.Button)

CONF_SEND_HARD_RESET = "send_hard_reset"
CONF_SEND_SRC_CAP = "send_src_cap"

ICON_DATABASE_EXPORT = "mdi:database-export"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_PWRMAN_CHARGER_ID): cv.use_id(PwrmanCharger),
    cv.Optional(CONF_SEND_HARD_RESET): button.button_schema(
        HardResetButton,
        device_class=DEVICE_CLASS_RESTART,
        entity_category=ENTITY_CATEGORY_CONFIG,
        icon=ICON_RESTART,
    ),
    cv.Optional(CONF_SEND_SRC_CAP): button.button_schema(
        SrcCapButton,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        icon=ICON_DATABASE_EXPORT,
    ),
}


async def to_code(config):
    paren = await cg.get_variable(config[CONF_PWRMAN_CHARGER_ID])
    if hard_reset_config := config.get(CONF_SEND_HARD_RESET):
        b = await button.new_button(hard_reset_config)
        await cg.register_parented(b, config[CONF_PWRMAN_CHARGER_ID])
        cg.add(paren.set_hard_reset_button(b))
    if src_cap_config := config.get(CONF_SEND_SRC_CAP):
        b = await button.new_button(src_cap_config)
        await cg.register_parented(b, config[CONF_PWRMAN_CHARGER_ID])
        cg.add(paren.set_src_cap_button(b))
