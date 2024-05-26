import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import remote_base
from esphome.const import (
    CONF_BUFFER_SIZE,
    CONF_DUMP,
    CONF_FILTER,
    CONF_ID,
    CONF_IDLE,
    CONF_PIN,
    CONF_TOLERANCE,
    CONF_VALUE,
    CONF_TYPE,
    CONF_MEMORY_BLOCKS,
)
from esphome.core import CORE, TimePeriod

CODEOWNERS = ["@hlyi"]

TYPE_PERCENTAGE = "percentage"
TYPE_TIME = "time"
CONF_SYNC_SPACE_IS_HIGH = "sync_space_is_high"
CONF_SYNC_SPACE_MIN = "sync_space_min"
CONF_SYNC_SPACE_MAX = "sync_space_max"
CONF_REPEAT_SPACE_MIN = "repeat_space_min"
CONF_EARLY_CHECK_THRES = "early_check_thres"
CONF_NUM_EDGE_MIN = "num_edge_min"


AUTO_LOAD = ["remote_base"]
remote_base_ns = cg.esphome_ns.namespace("remote_base")
remote_receiver_nf_ns = cg.esphome_ns.namespace("remote_receiver_nf")
RemoteReceiverNFComponent = remote_receiver_nf_ns.class_(
    "RemoteReceiverNFComponent", remote_base.RemoteReceiverBase, cg.Component
)

ToleranceMode = remote_base_ns.enum("ToleranceMode")

TOLERANCE_MODE = {
    TYPE_PERCENTAGE: ToleranceMode.TOLERANCE_MODE_PERCENTAGE,
    TYPE_TIME: ToleranceMode.TOLERANCE_MODE_TIME,
}

def validate_timing (value):
    if value[CONF_FILTER] >= value[CONF_REPEAT_SPACE_MIN] :
         raise cv.Invalid("filter has to be smaller than repeat_space_min")
    if value[CONF_REPEAT_SPACE_MIN] >= value[CONF_SYNC_SPACE_MIN] :
         raise cv.Invalid("repeat_space_min has to be smaller than sync_space_min")
    if value[CONF_SYNC_SPACE_MIN] >= value[CONF_SYNC_SPACE_MAX] :
         raise cv.Invalid("sync_space_min has to be smaller than sync_space_max")
    if value[CONF_SYNC_SPACE_MAX] >= value[CONF_IDLE] :
         raise cv.Invalid("sync_space_max has to be smaller than idle")
    return value


TOLERANCE_SCHEMA = cv.typed_schema(
    {
        TYPE_PERCENTAGE: cv.Schema(
            {cv.Required(CONF_VALUE): cv.All(cv.percentage_int, cv.uint32_t)}
        ),
        TYPE_TIME: cv.Schema(
            {
                cv.Required(CONF_VALUE): cv.All(
                    cv.positive_time_period_microseconds,
                    cv.Range(max=TimePeriod(microseconds=4294967295)),
                )
            }
        ),
    },
    lower=True,
    enum=TOLERANCE_MODE,
)

def validate_tolerance(value):
    if isinstance(value, dict):
        return TOLERANCE_SCHEMA(value)

    if "%" in str(value):
        type_ = TYPE_PERCENTAGE
    else:
        raise cv.Invalid(
            "Tolerance must be a percentage"
        )

    return TOLERANCE_SCHEMA(
        {
            CONF_VALUE: value,
            CONF_TYPE: type_,
        }
    )

MULTI_CONF = True
CONFIG_SCHEMA = cv.All( remote_base.validate_triggers(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(RemoteReceiverNFComponent),
            cv.Required(CONF_PIN): cv.All(pins.internal_gpio_input_pin_schema),
            cv.Optional(CONF_DUMP, default=[]): remote_base.validate_dumpers,
            cv.Optional(CONF_TOLERANCE, default="25%"): validate_tolerance,
            cv.SplitDefault(
                CONF_BUFFER_SIZE, esp32="10000b", esp8266="1000b"
            ): cv.validate_bytes,
            cv.Optional(
                CONF_FILTER, default="50us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_IDLE, default="10ms"
            ): cv.positive_time_period_microseconds,
            cv.Optional(CONF_SYNC_SPACE_IS_HIGH, default=False): cv.boolean,
            cv.Optional(
                CONF_SYNC_SPACE_MIN, default="8ms"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_SYNC_SPACE_MAX, default="10ms"
            ): cv.positive_time_period_microseconds,
            cv.Optional(
                CONF_REPEAT_SPACE_MIN, default="1100us"
            ): cv.positive_time_period_microseconds,
            cv.Optional(CONF_EARLY_CHECK_THRES, default=40): cv.int_,
            cv.Optional(CONF_NUM_EDGE_MIN, default=16): cv.int_,
            cv.Optional(CONF_MEMORY_BLOCKS, default=3): cv.Range(min=1, max=8),
        }
    ).extend(cv.COMPONENT_SCHEMA)
),
 validate_timing
)


async def to_code(config):
    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    if CORE.is_esp32:
        var = cg.new_Pvariable(config[CONF_ID], pin, config[CONF_MEMORY_BLOCKS])
    else:
        var = cg.new_Pvariable(config[CONF_ID], pin)

    dumpers = await remote_base.build_dumpers(config[CONF_DUMP])
    for dumper in dumpers:
        cg.add(var.register_dumper(dumper))

    await remote_base.build_triggers(config)
    await cg.register_component(var, config)

    cg.add(var.set_tolerance(config[CONF_TOLERANCE][CONF_VALUE], config[CONF_TOLERANCE][CONF_TYPE]))
    cg.add(var.set_buffer_size(config[CONF_BUFFER_SIZE]))
    cg.add(var.set_filter_us(config[CONF_FILTER]))
    cg.add(var.set_idle_us(config[CONF_IDLE]))
    cg.add(var.set_space_lvl_high(config[CONF_SYNC_SPACE_IS_HIGH]))
    cg.add(var.set_sync_space_min_us(config[CONF_SYNC_SPACE_MIN]))
    cg.add(var.set_sync_space_max_us(config[CONF_SYNC_SPACE_MAX]))
    cg.add(var.set_rep_space_min_us(config[CONF_REPEAT_SPACE_MIN]))
    cg.add(var.set_early_check_thres(config[CONF_EARLY_CHECK_THRES]))
    cg.add(var.set_num_edge_min(config[CONF_NUM_EDGE_MIN]))
