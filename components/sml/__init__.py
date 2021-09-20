import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@alengwenus"]

DEPENDENCIES = ["uart"]

sml_ns = cg.esphome_ns.namespace("esphome::sml")
Sml = sml_ns.class_("Sml", cg.Component, uart.UARTDevice)

CONF_SML_ID = "sml_id"
CONF_OBIS = "obis"
CONF_SERVER_ID = "server_id"
CONF_LOGGING = "logging"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(Sml),
        cv.Optional(CONF_LOGGING, default=False): cv.boolean,
    }
).extend(uart.UART_DEVICE_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], config[CONF_LOGGING])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)