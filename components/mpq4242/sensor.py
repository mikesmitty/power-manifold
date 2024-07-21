import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    UNIT_AMPERE,
    UNIT_VOLT,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_SIGNAL,
    ICON_FLASH,
)
from . import CONF_MPQ4242_ID, MPQ4242Component

DEPENDENCIES = ["mpq4242"]
CONF_PDO_MAX_CURRENT = "pdo_max_current"
CONF_PDO_MIN_VOLTAGE = "pdo_min_voltage"
CONF_PDO_VOLTAGE = "pdo_voltage"
CONF_SELECTED_PDO = "selected_pdo"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPQ4242_ID): cv.use_id(MPQ4242Component),
        cv.Optional(CONF_SELECTED_PDO): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_PDO_MAX_CURRENT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            unit_of_measurement=UNIT_AMPERE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_PDO_MIN_VOLTAGE): sensor.sensor_schema(
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            unit_of_measurement=UNIT_VOLT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_FLASH,
        ),
        cv.Optional(CONF_PDO_VOLTAGE): sensor.sensor_schema(
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            unit_of_measurement=UNIT_VOLT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_FLASH,
        ),
    }
)


async def to_code(config):
    mpq4242_component = await cg.get_variable(config[CONF_MPQ4242_ID])
    if selected_pdo_config := config.get(CONF_SELECTED_PDO):
        sens = await sensor.new_sensor(selected_pdo_config)
        cg.add(mpq4242_component.set_selected_pdo_sensor(sens))
    if pdo_max_current_config := config.get(CONF_PDO_MAX_CURRENT):
        sens = await sensor.new_sensor(pdo_max_current_config)
        cg.add(mpq4242_component.set_pdo_max_current_sensor(sens))
    if pdo_min_voltage_config := config.get(CONF_PDO_MIN_VOLTAGE):
        sens = await sensor.new_sensor(pdo_min_voltage_config)
        cg.add(mpq4242_component.set_pdo_min_voltage_sensor(sens))
    if pdo_voltage_config := config.get(CONF_PDO_VOLTAGE):
        sens = await sensor.new_sensor(pdo_voltage_config)
        cg.add(mpq4242_component.set_pdo_voltage_sensor(sens))
