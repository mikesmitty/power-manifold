import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_FLASH,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_NONE,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_VOLT,
    UNIT_WATT,
)
from .. import (
    CONF_MPQ4242_ID,
    ICON_THERMOMETER_ALERT,
    ICON_THERMOMETER_HIGH,
    ICON_THERMOMETER_OFF,
    MPQ4242_COMPONENT_SCHEMA,
    mpq4242_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["mpq4242"]

CONF_CONTRACT_POWER = "contract_power"
CONF_FW_REV = "firmware_revision"
CONF_OTW_THRESHOLD_1 = "otw_threshold_1"
CONF_OTW_THRESHOLD_2 = "otw_threshold_2"
CONF_OTP_THRESHOLD = "otp_threshold"
CONF_PDO_MAX_CURRENT = "pdo_max_current"
CONF_PDO_MIN_VOLTAGE = "pdo_min_voltage"
CONF_PDO_VOLTAGE = "pdo_voltage"
CONF_MAX_REQUESTED_CURRENT = "max_requested_current"
CONF_SELECTED_PDO = "selected_pdo"

ICON_CURRENT_DC = "mdi:current-dc"
ICON_NUMERIC = "mdi:numeric"
ICON_POWER_PLUG = "mdi:power-plug"

MPQ4242Sensor = mpq4242_ns.class_("MPQ4242Sensor", cg.PollingComponent)

CONFIG_SCHEMA = MPQ4242_COMPONENT_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(MPQ4242Sensor),
        cv.Optional(CONF_CONTRACT_POWER): sensor.sensor_schema(
            device_class=DEVICE_CLASS_POWER,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_WATT,
            icon=ICON_POWER_PLUG,
        ),
        cv.Optional(CONF_FW_REV): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_NONE,
            icon=ICON_NUMERIC,
        ),
        cv.Optional(CONF_OTP_THRESHOLD): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER_OFF,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_1): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER_HIGH,
        ),
        cv.Optional(CONF_OTW_THRESHOLD_2): sensor.sensor_schema(
            device_class=DEVICE_CLASS_TEMPERATURE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_CELSIUS,
            icon=ICON_THERMOMETER_ALERT,
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
        cv.Optional(CONF_MAX_REQUESTED_CURRENT): sensor.sensor_schema(
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
            unit_of_measurement=UNIT_AMPERE,
            icon=ICON_CURRENT_DC,
        ),
        cv.Optional(CONF_SELECTED_PDO): sensor.sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_NONE,
            icon=ICON_NUMERIC,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_MPQ4242_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if contract_power_config := config.get(CONF_CONTRACT_POWER):
        sens = await sensor.new_sensor(contract_power_config)
        cg.add(var.set_contract_power_sensor(sens))
    if fw_rev_config := config.get(CONF_FW_REV):
        sens = await sensor.new_sensor(fw_rev_config)
        cg.add(var.set_fw_rev_sensor(sens))
    if otp_threshold_config := config.get(CONF_OTP_THRESHOLD):
        sens = await sensor.new_sensor(otp_threshold_config)
        cg.add(var.set_otp_threshold_sensor(sens))
    if otw_threshold_1_config := config.get(CONF_OTW_THRESHOLD_1):
        sens = await sensor.new_sensor(otw_threshold_1_config)
        cg.add(var.set_otw_threshold_1_sensor(sens))
    if otw_threshold_2_config := config.get(CONF_OTW_THRESHOLD_2):
        sens = await sensor.new_sensor(otw_threshold_2_config)
        cg.add(var.set_otw_threshold_2_sensor(sens))
    if pdo_max_current_config := config.get(CONF_PDO_MAX_CURRENT):
        sens = await sensor.new_sensor(pdo_max_current_config)
        cg.add(var.set_pdo_max_current_sensor(sens))
    if pdo_min_voltage_config := config.get(CONF_PDO_MIN_VOLTAGE):
        sens = await sensor.new_sensor(pdo_min_voltage_config)
        cg.add(var.set_pdo_min_voltage_sensor(sens))
    if pdo_voltage_config := config.get(CONF_PDO_VOLTAGE):
        sens = await sensor.new_sensor(pdo_voltage_config)
        cg.add(var.set_pdo_voltage_sensor(sens))
    if max_requested_current_config := config.get(CONF_MAX_REQUESTED_CURRENT):
        sens = await sensor.new_sensor(max_requested_current_config)
        cg.add(var.set_max_requested_current_sensor(sens))
    if selected_pdo_config := config.get(CONF_SELECTED_PDO):
        sens = await sensor.new_sensor(selected_pdo_config)
        cg.add(var.set_selected_pdo_sensor(sens))
