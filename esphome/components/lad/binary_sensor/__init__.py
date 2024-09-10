import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, uart
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_BATTERY_CHARGING,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from .. import (
    CONF_LAD_ID,
    LAD_SCHEMA,
    lad_component_ns,
)

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["lad"]

CONF_AC_POWER_FAULT = "ac_power_fault"
CONF_BATTERY_CHARGED = "battery_charged"
CONF_BATTERY_CHARGING = "battery_charging"
CONF_BATTERY_IMBALANCE = "battery_imbalance"
CONF_BATTERY_POWER_FAULT = "battery_power_fault"
CONF_BATTERY_REVERSED = "battery_reversed"
CONF_BATTERY_SWITCH = "battery_switch"
CONF_BATTERY1_FAULT = "battery1_fault"
CONF_BATTERY2_FAULT = "battery2_fault"
CONF_BATTERY3_FAULT = "battery3_fault"
CONF_BATTERY4_FAULT = "battery4_fault"
CONF_FORCE_START = "force_start"
CONF_LINK_CONTROL_STATUS = "link_control_status"
CONF_LOW_BATTERY = "low_battery"
CONF_OUTPUT_OVERLOAD = "output_overload"
CONF_OVP_ACTIVE = "ovp_active"
CONF_STANDBY_POWER_ACTIVE = "standby_power_active"

ICON_BATTERY_ARROW_DOWN_OUTLINE = "mdi:battery-arrow-down-outline"
ICON_NUMERIC_5_BOX_MULTIPLE = "mdi:numeric-5-box-multiple"
ICON_POWER_SETTINGS = "mdi:power-settings"
ICON_USB_C_PORT = "mdi:usb-c-port"

LadBinarySensor = lad_component_ns.class_(
    "LadBinarySensor", uart.UARTDevice, cg.PollingComponent
)

CONFIG_SCHEMA = LAD_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(LadBinarySensor),
        cv.Optional(CONF_AC_POWER_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_CHARGED): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_CHARGING): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY_CHARGING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_IMBALANCE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_POWER_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_REVERSED): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_SWITCH): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY1_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY2_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY3_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY4_FAULT): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_FORCE_START): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LINK_CONTROL_STATUS): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_LOW_BATTERY): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_OUTPUT_OVERLOAD): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_OVP_ACTIVE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_STANDBY_POWER_ACTIVE): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.polling_component_schema("60s"))


async def to_code(config):
    paren = await cg.get_variable(config[CONF_LAD_ID])
    var = cg.new_Pvariable(config[CONF_ID], paren)
    await cg.register_component(var, config)

    if ac_power_fault_config := config.get(CONF_AC_POWER_FAULT):
        sens = await binary_sensor.new_binary_sensor(ac_power_fault_config)
        cg.add(var.set_ac_power_fault_binary_sensor(sens))

    if battery_charged_config := config.get(CONF_BATTERY_CHARGED):
        sens = await binary_sensor.new_binary_sensor(battery_charged_config)
        cg.add(var.set_battery_charged_binary_sensor(sens))

    if battery_charging_config := config.get(CONF_BATTERY_CHARGING):
        sens = await binary_sensor.new_binary_sensor(battery_charging_config)
        cg.add(var.set_battery_charging_binary_sensor(sens))

    if battery_imbalance_config := config.get(CONF_BATTERY_IMBALANCE):
        sens = await binary_sensor.new_binary_sensor(battery_imbalance_config)
        cg.add(var.set_battery_imbalance_binary_sensor(sens))

    if battery_power_fault_config := config.get(CONF_BATTERY_POWER_FAULT):
        sens = await binary_sensor.new_binary_sensor(battery_power_fault_config)
        cg.add(var.set_battery_power_fault_binary_sensor(sens))

    if battery_reversed_config := config.get(CONF_BATTERY_REVERSED):
        sens = await binary_sensor.new_binary_sensor(battery_reversed_config)
        cg.add(var.set_battery_reversed_binary_sensor(sens))

    if battery_switch_config := config.get(CONF_BATTERY_SWITCH):
        sens = await binary_sensor.new_binary_sensor(battery_switch_config)
        cg.add(var.set_battery_switch_binary_sensor(sens))

    if battery1_fault_config := config.get(CONF_BATTERY1_FAULT):
        sens = await binary_sensor.new_binary_sensor(battery1_fault_config)
        cg.add(var.set_battery1_fault_binary_sensor(sens))

    if battery2_fault_config := config.get(CONF_BATTERY2_FAULT):
        sens = await binary_sensor.new_binary_sensor(battery2_fault_config)
        cg.add(var.set_battery2_fault_binary_sensor(sens))

    if battery3_fault_config := config.get(CONF_BATTERY3_FAULT):
        sens = await binary_sensor.new_binary_sensor(battery3_fault_config)
        cg.add(var.set_battery3_fault_binary_sensor(sens))

    if battery4_fault_config := config.get(CONF_BATTERY4_FAULT):
        sens = await binary_sensor.new_binary_sensor(battery4_fault_config)
        cg.add(var.set_battery4_fault_binary_sensor(sens))

    if force_start_config := config.get(CONF_FORCE_START):
        sens = await binary_sensor.new_binary_sensor(force_start_config)
        cg.add(var.set_force_start_binary_sensor(sens))

    if link_control_status_config := config.get(CONF_LINK_CONTROL_STATUS):
        sens = await binary_sensor.new_binary_sensor(link_control_status_config)
        cg.add(var.set_link_control_status_binary_sensor(sens))

    if low_battery_config := config.get(CONF_LOW_BATTERY):
        sens = await binary_sensor.new_binary_sensor(low_battery_config)
        cg.add(var.set_low_battery_binary_sensor(sens))

    if output_overload_config := config.get(CONF_OUTPUT_OVERLOAD):
        sens = await binary_sensor.new_binary_sensor(output_overload_config)
        cg.add(var.set_output_overload_binary_sensor(sens))

    if ovp_active_config := config.get(CONF_OVP_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(ovp_active_config)
        cg.add(var.set_ovp_active_binary_sensor(sens))

    if standby_power_active_config := config.get(CONF_STANDBY_POWER_ACTIVE):
        sens = await binary_sensor.new_binary_sensor(standby_power_active_config)
        cg.add(var.set_standby_power_active_binary_sensor(sens))
