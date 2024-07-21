import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC
from .. import (
    CONF_MPQ4242_ID,
    ICON_THERMOMETER_ALERT,
    ICON_THERMOMETER_HIGH,
    MPQ4242_COMPONENT_SCHEMA,
    MPQ4242Component,
    mpq4242_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["mpq4242"]

CONF_CABLE_5A_CAPABLE = "cable_5a_capable"
CONF_CURRENT_MISMATCH = "current_mismatch"
CONF_GIVEBACK_FLAG = "giveback_flag"
CONF_OTW_THRESHOLD_1 = "otw_threshold_1"
CONF_OTW_THRESHOLD_2 = "otw_threshold_2"
CONF_PPS_MODE = "pps_mode"
CONF_SINK_ATTACHED = "sink_attached"

ICON_BATTERY_ARROW_DOWN_OUTLINE = "mdi:battery-arrow-down-outline"
ICON_NUMERIC_5_BOX_MULTIPLE = "mdi:numeric-5-box-multiple"
ICON_POWER_SETTINGS = "mdi:power-settings"
ICON_USB_C_PORT = "mdi:usb-c-port"

MPQ4242BinarySensor = mpq4242_ns.class_("MPQ4242BinarySensor", cg.Component)

CONFIG_SCHEMA = MPQ4242_COMPONENT_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(MPQ4242BinarySensor),
        cv.Optional(CONF_CABLE_5A_CAPABLE): binary_sensor.binary_sensor_schema(
            icon=ICON_NUMERIC_5_BOX_MULTIPLE,
        ),
        cv.Optional(CONF_CURRENT_MISMATCH): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_GIVEBACK_FLAG): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_BATTERY_ARROW_DOWN_OUTLINE,
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
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_USB_C_PORT,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_MPQ4242_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if cable_5a_capable_config := config.get(CONF_CABLE_5A_CAPABLE):
        sens = await binary_sensor.new_binary_sensor(cable_5a_capable_config)
        cg.add(var.set_cable_5a_capable_binary_sensor(sens))

    if current_mismatch_config := config.get(CONF_CURRENT_MISMATCH):
        sens = await binary_sensor.new_binary_sensor(current_mismatch_config)
        cg.add(var.set_current_mismatch_binary_sensor(sens))

    if giveback_flag_config := config.get(CONF_GIVEBACK_FLAG):
        sens = await binary_sensor.new_binary_sensor(giveback_flag_config)
        cg.add(var.set_giveback_flag_binary_sensor(sens))

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
