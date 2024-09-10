import esphome.codegen as cg
from esphome.components import sensor, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_BATTERY_VOLTAGE,
    CONF_CURRENT,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_NONE,
    ICON_FLASH,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_VOLT,
)
from .. import (
    CONF_LAD_ID,
    LAD_SCHEMA,
    lad_component_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["lad"]

CONF_BATTERY_1_VOLTAGE = "battery_1_voltage"
CONF_BATTERY_2_VOLTAGE = "battery_2_voltage"
CONF_BATTERY_3_VOLTAGE = "battery_3_voltage"
CONF_BATTERY_4_VOLTAGE = "battery_4_voltage"
CONF_INPUT_VOLTAGE = "input_voltage"

ICON_CURRENT_DC = "mdi:current-dc"

LadSensor = lad_component_ns.class_("LadSensor", uart.UARTDevice, cg.PollingComponent)

CONFIG_SCHEMA = LAD_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(LadSensor),
        cv.Optional(CONF_CURRENT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_AMPERE,
            icon=ICON_CURRENT_DC,
        ),
        cv.Optional(CONF_INPUT_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_1_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_2_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_3_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_BATTERY_4_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_LAD_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if current_config := config.get(CONF_CURRENT):
        sens = await sensor.new_sensor(current_config)
        cg.add(var.set_current_sensor(sens))
    if input_voltage_config := config.get(CONF_INPUT_VOLTAGE):
        sens = await sensor.new_sensor(input_voltage_config)
        cg.add(var.set_input_voltage_sensor(sens))
    if battery_voltage_config := config.get(CONF_BATTERY_VOLTAGE):
        sens = await sensor.new_sensor(battery_voltage_config)
        cg.add(var.set_battery_voltage_sensor(sens))
    if battery_1_voltage_config := config.get(CONF_BATTERY_1_VOLTAGE):
        sens = await sensor.new_sensor(battery_1_voltage_config)
        cg.add(var.set_battery_1_voltage_sensor(sens))
    if battery_2_voltage_config := config.get(CONF_BATTERY_2_VOLTAGE):
        sens = await sensor.new_sensor(battery_2_voltage_config)
        cg.add(var.set_battery_2_voltage_sensor(sens))
    if battery_3_voltage_config := config.get(CONF_BATTERY_3_VOLTAGE):
        sens = await sensor.new_sensor(battery_3_voltage_config)
        cg.add(var.set_battery_3_voltage_sensor(sens))
    if battery_4_voltage_config := config.get(CONF_BATTERY_4_VOLTAGE):
        sens = await sensor.new_sensor(battery_4_voltage_config)
        cg.add(var.set_battery_4_voltage_sensor(sens))
