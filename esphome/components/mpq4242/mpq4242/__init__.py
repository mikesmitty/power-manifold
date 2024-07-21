import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2c
from esphome.const import CONF_ID, CONF_MAX_CURRENT

CODEOWNERS = ["@mikesmitty"]
DEPENDENCIES = ["i2c"]
MULTI_CONF = True

CONF_12V_PDO_NUMBER = "12v_pdo_number"
CONF_12V_PDO_ENABLED = "12v_pdo_enabled"
CONF_GPIO1_FUNCTION = "gpio1_function"
CONF_GPIO2_FUNCTION = "gpio2_function"
CONF_MPQ4242_ID = "mpq4242_id"

mpq4242_ns = cg.esphome_ns.namespace("mpq4242")
MPQ4242Component = mpq4242_ns.class_("MPQ4242Component", cg.Component, i2c.I2CDevice)

MPQ4242PdoType = mpq4242_ns.enum("MPQ4242PdoType")
PDO_TYPE = {
    "fixed": MPQ4242PdoType.MPQ4242_PDO_FIXED,
    "pps": MPQ4242PdoType.MPQ4242_PDO_PPS,
}

MPQ4242Gpio1Function = mpq4242_ns.enum("MPQ4242Gpio1Function")
GPIO1_FUNCTION = {
    "power_share_low": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_POWER_SHARE_LOW,
    "gate": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_GATE,
    "fault": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_FAULT,
    "ntc2": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_NTC2,
    "attach_flt_alt": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_ATTACH_FLT_ALT,
    "imon": MPQ4242Gpio1Function.MPQ4242_GPIO1_FN_IMON,
}

MPQ4242Gpio2Function = mpq4242_ns.enum("MPQ4242Gpio2Function")
GPIO2_FUNCTION = {
    "disabled": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_DISABLED,
    "polarity": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_POLARITY,
    "ntc": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_NTC,
    "vconn_in": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_VCONN_IN,
    "led_pwm": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_LED_PWM,
    "attach": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_ATTACH,
    "power_share_high": MPQ4242Gpio2Function.MPQ4242_GPIO2_FN_POWER_SHARE_HIGH,
}

MPQ4242_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_MPQ4242_ID): cv.use_id(MPQ4242Component),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MPQ4242Component),
            cv.Optional(CONF_12V_PDO_ENABLED, default=False): cv.templatable(
                cv.boolean
            ),
            cv.Optional(CONF_12V_PDO_NUMBER, default=3): cv.templatable(
                cv.int_range(2, 7)
            ),
            cv.Optional(CONF_MAX_CURRENT, default=3.0): cv.templatable(
                cv.All(cv.current, cv.Range(min=0.0, max=5.0))
            ),
            cv.Optional(CONF_GPIO1_FUNCTION): cv.templatable(cv.enum(GPIO1_FUNCTION)),
            cv.Optional(CONF_GPIO2_FUNCTION): cv.templatable(cv.enum(GPIO2_FUNCTION)),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x61))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_12v_pdo_enabled(config[CONF_12V_PDO_ENABLED]))
    cg.add(var.set_12v_pdo_number(config[CONF_12V_PDO_NUMBER]))
    cg.add(var.set_pdo_current(config[CONF_MAX_CURRENT]))
    if gpio1_function := config.get(CONF_GPIO1_FUNCTION):
        cg.add(var.set_gpio1_function(gpio1_function))
    if gpio2_function := config.get(CONF_GPIO2_FUNCTION):
        cg.add(var.set_gpio2_function(gpio2_function))
