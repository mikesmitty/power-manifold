import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_NONE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_FLASH,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_VOLT,
    UNIT_WATT,
)
from .. import (
    CONF_PWRMAN_CHARGER_ID,
    PWRMAN_CHARGER_SCHEMA,
    pwrman_charger_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["pwrman_charger"]

CONF_CONTRACT_POWER = "contract_power"
CONF_INPUT_VOLTAGE = "input_voltage"
CONF_OUTPUT_CURRENT = "output_current"
CONF_OUTPUT_VOLTAGE = "output_voltage"
CONF_PDO_MAX_CURRENT = "pdo_max_current"
CONF_PDO_MIN_VOLTAGE = "pdo_min_voltage"
CONF_PDO_VOLTAGE = "pdo_voltage"

ICON_CURRENT_DC = "mdi:current-dc"
ICON_NUMERIC = "mdi:numeric"
ICON_POWER_PLUG = "mdi:power-plug"

PwrmanChargerSensor = pwrman_charger_ns.class_(
    "PwrmanChargerSensor", cg.PollingComponent
)

CONFIG_SCHEMA = PWRMAN_CHARGER_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(PwrmanChargerSensor),
        cv.Optional(CONF_CONTRACT_POWER): sensor.sensor_schema(
            device_class=DEVICE_CLASS_POWER,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_WATT,
            icon=ICON_POWER_PLUG,
        ),
        cv.Optional(CONF_OUTPUT_CURRENT): sensor.sensor_schema(
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
        cv.Optional(CONF_OUTPUT_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_NONE,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_PDO_MAX_CURRENT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_AMPERE,
            icon=ICON_CURRENT_DC,
        ),
        cv.Optional(CONF_PDO_MIN_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_PDO_VOLTAGE): sensor.sensor_schema(
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_PWRMAN_CHARGER_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if contract_power_config := config.get(CONF_CONTRACT_POWER):
        sens = await sensor.new_sensor(contract_power_config)
        cg.add(var.set_contract_power_sensor(sens))
    if input_voltage_config := config.get(CONF_INPUT_VOLTAGE):
        sens = await sensor.new_sensor(input_voltage_config)
        cg.add(var.set_input_voltage_sensor(sens))
    if output_current_config := config.get(CONF_OUTPUT_CURRENT):
        sens = await sensor.new_sensor(output_current_config)
        cg.add(var.set_output_current_sensor(sens))
    if output_voltage_config := config.get(CONF_OUTPUT_VOLTAGE):
        sens = await sensor.new_sensor(output_voltage_config)
        cg.add(var.set_output_voltage_sensor(sens))
    if pdo_max_current_config := config.get(CONF_PDO_MAX_CURRENT):
        sens = await sensor.new_sensor(pdo_max_current_config)
        cg.add(var.set_pdo_max_current_sensor(sens))
    if pdo_min_voltage_config := config.get(CONF_PDO_MIN_VOLTAGE):
        sens = await sensor.new_sensor(pdo_min_voltage_config)
        cg.add(var.set_pdo_min_voltage_sensor(sens))
    if pdo_voltage_config := config.get(CONF_PDO_VOLTAGE):
        sens = await sensor.new_sensor(pdo_voltage_config)
        cg.add(var.set_pdo_voltage_sensor(sens))
