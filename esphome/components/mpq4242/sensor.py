import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_VOLT,
    UNIT_WATT,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_SIGNAL,
    ICON_FLASH,
)
from . import CONF_MPQ4242_ID, MPQ4242Component

DEPENDENCIES = ["mpq4242"]
CONF_CONTRACT_POWER = "contract_power"
CONF_FW_REV = "firmware_revision"
CONF_OTW_THRESHOLD_1 = "otw_threshold_1"
CONF_OTW_THRESHOLD_2 = "otw_threshold_2"
CONF_OTP_THRESHOLD = "otp_threshold"
CONF_PDO_MAX_CURRENT = "pdo_max_current"
CONF_PDO_MIN_VOLTAGE = "pdo_min_voltage"
CONF_PDO_VOLTAGE = "pdo_voltage"
CONF_REQUESTED_CURRENT = "requested_current"
CONF_SELECTED_PDO = "selected_pdo"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPQ4242_ID): cv.use_id(MPQ4242Component),
        cv.Optional(CONF_CONTRACT_POWER): sensor.sensor_schema(
            device_class=DEVICE_CLASS_POWER,
            unit_of_measurement=UNIT_WATT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_FW_REV): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_OTP_THRESHOLD): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            unit_of_measurement=UNIT_CELSIUS,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_1): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            unit_of_measurement=UNIT_CELSIUS,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_2): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            unit_of_measurement=UNIT_CELSIUS,
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
        cv.Optional(CONF_REQUESTED_CURRENT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            unit_of_measurement=UNIT_AMPERE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
        cv.Optional(CONF_SELECTED_PDO): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=ICON_SIGNAL,
        ),
    }
)


async def to_code(config):
    mpq4242_component = await cg.get_variable(config[CONF_MPQ4242_ID])
    if contract_power_config := config.get(CONF_CONTRACT_POWER):
        sens = await sensor.new_sensor(contract_power_config)
        cg.add(mpq4242_component.set_contract_power_sensor(sens))
    if fw_rev_config := config.get(CONF_FW_REV):
        sens = await sensor.new_sensor(fw_rev_config)
        cg.add(mpq4242_component.set_fw_rev_sensor(sens))
    if otp_threshold_config := config.get(CONF_OTP_THRESHOLD):
        sens = await sensor.new_sensor(otp_threshold_config)
        cg.add(mpq4242_component.set_otp_threshold_sensor(sens))
    if otw_threshold_1_config := config.get(CONF_OTW_THRESHOLD_1):
        sens = await sensor.new_sensor(otw_threshold_1_config)
        cg.add(mpq4242_component.set_otw_threshold_1_sensor(sens))
    if otw_threshold_2_config := config.get(CONF_OTW_THRESHOLD_2):
        sens = await sensor.new_sensor(otw_threshold_2_config)
        cg.add(mpq4242_component.set_otw_threshold_2_sensor(sens))
    if pdo_max_current_config := config.get(CONF_PDO_MAX_CURRENT):
        sens = await sensor.new_sensor(pdo_max_current_config)
        cg.add(mpq4242_component.set_pdo_max_current_sensor(sens))
    if pdo_min_voltage_config := config.get(CONF_PDO_MIN_VOLTAGE):
        sens = await sensor.new_sensor(pdo_min_voltage_config)
        cg.add(mpq4242_component.set_pdo_min_voltage_sensor(sens))
    if pdo_voltage_config := config.get(CONF_PDO_VOLTAGE):
        sens = await sensor.new_sensor(pdo_voltage_config)
        cg.add(mpq4242_component.set_pdo_voltage_sensor(sens))
    if requested_current_config := config.get(CONF_REQUESTED_CURRENT):
        sens = await sensor.new_sensor(requested_current_config)
        cg.add(mpq4242_component.set_requested_current_sensor(sens))
    if selected_pdo_config := config.get(CONF_SELECTED_PDO):
        sens = await sensor.new_sensor(selected_pdo_config)
        cg.add(mpq4242_component.set_selected_pdo_sensor(sens))
