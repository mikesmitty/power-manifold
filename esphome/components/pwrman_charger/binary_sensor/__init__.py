import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC, ENTITY_CATEGORY_NONE
from .. import (
    CONF_PWRMAN_CHARGER_ID,
    PWRMAN_CHARGER_SCHEMA,
    pwrman_charger_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["pwrman_charger"]

CONF_CABLE_5A_CAPABLE = "cable_5a_capable"
CONF_OTW_THRESHOLD_1 = "otw_threshold_1"
CONF_OTW_THRESHOLD_2 = "otw_threshold_2"
CONF_PPS_MODE = "pps_mode"
CONF_SINK_ATTACHED = "sink_attached"

ICON_BATTERY_ARROW_DOWN_OUTLINE = "mdi:battery-arrow-down-outline"
ICON_NUMERIC_5_BOX_MULTIPLE = "mdi:numeric-5-box-multiple"
ICON_POWER_SETTINGS = "mdi:power-settings"
ICON_THERMOMETER_ALERT = "mdi:thermometer-alert"
ICON_THERMOMETER_HIGH = "mdi:thermometer-high"
ICON_USB_C_PORT = "mdi:usb-c-port"

PwrmanChargerBinarySensor = pwrman_charger_ns.class_(
    "PwrmanChargerBinarySensor", cg.PollingComponent
)

CONFIG_SCHEMA = PWRMAN_CHARGER_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(PwrmanChargerBinarySensor),
        cv.Optional(CONF_CABLE_5A_CAPABLE): binary_sensor.binary_sensor_schema(
            icon=ICON_NUMERIC_5_BOX_MULTIPLE,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_1): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_THERMOMETER_HIGH,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_2): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_THERMOMETER_ALERT,
        ),
        cv.Optional(CONF_PPS_MODE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_POWER_SETTINGS,
        ),
        cv.Optional(CONF_SINK_ATTACHED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_NONE,
            icon=ICON_USB_C_PORT,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_PWRMAN_CHARGER_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if cable_5a_capable_config := config.get(CONF_CABLE_5A_CAPABLE):
        sens = await binary_sensor.new_binary_sensor(cable_5a_capable_config)
        cg.add(var.set_cable_5a_capable_binary_sensor(sens))

    if otw_threshold_1_config := config.get(CONF_OTW_THRESHOLD_1):
        sens = await binary_sensor.new_binary_sensor(otw_threshold_1_config)
        cg.add(var.set_otw_threshold_1_binary_sensor(sens))

    if otw_threshold_2_config := config.get(CONF_OTW_THRESHOLD_2):
        sens = await binary_sensor.new_binary_sensor(otw_threshold_2_config)
        cg.add(var.set_otw_threshold_2_binary_sensor(sens))

    if pps_mode_config := config.get(CONF_PPS_MODE):
        sens = await binary_sensor.new_binary_sensor(pps_mode_config)
        cg.add(var.set_pps_mode_binary_sensor(sens))

    if sink_attached_config := config.get(CONF_SINK_ATTACHED):
        sens = await binary_sensor.new_binary_sensor(sink_attached_config)
        cg.add(var.set_sink_attached_binary_sensor(sens))
