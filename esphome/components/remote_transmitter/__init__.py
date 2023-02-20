import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import remote_base
from esphome.const import CONF_CARRIER_DUTY_PERCENT, CONF_ID, CONF_PIN
from esphome.core import CORE

AUTO_LOAD = ["remote_base"]
remote_transmitter_ns = cg.esphome_ns.namespace("remote_transmitter")
RemoteTransmitterComponent = remote_transmitter_ns.class_(
    "RemoteTransmitterComponent", remote_base.RemoteTransmitterBase, cg.Component
)

CONF_RMT_CHANNEL = "rmt_channel"

MULTI_CONF = True
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemoteTransmitterComponent),
        cv.Required(CONF_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_CARRIER_DUTY_PERCENT): cv.All(
            cv.percentage_int, cv.Range(min=1, max=100)
        ),
        cv.SplitDefault(CONF_RMT_CHANNEL, esp32=-1): cv.All( cv.Range(min=-1, max=7), cv.only_on_esp32),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    var = cg.new_Pvariable(config[CONF_ID], pin)
    await cg.register_component(var, config)

    cg.add(var.set_carrier_duty_percent(config[CONF_CARRIER_DUTY_PERCENT]))
    if CORE.is_esp32:
        cg.add(var.set_rmt_channel(config[CONF_RMT_CHANNEL]))
