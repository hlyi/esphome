from esphome.components import number
import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import (
    CONF_ID,
    CONF_TYPE,
)
from .. import ld2410_ns, CONF_LD2410_ID, LD2410Component

DEPENDENCIES = ["ld2410"]

LD2410Number = ld2410_ns.class_("LD2410Number", number.Number, cg.Component)

LD2410ThresType = ld2410_ns.enum("LD2410ThresType");

THRESHOLD_TYPE = {
	"move" : LD2410ThresType.LD2410ThresMove,
	"still" : LD2410ThresType.LD2410ThresStill,
}

CONF_GATE_NUM = "gate_num"

THRES_SCHEMA = cv.All(
        number.number_schema(LD2410Number)
        .extend(
            {
#                cv.GenerateID(CONF_LD2410_ID): cv.declare_id(number.Number),
                cv.Required(CONF_GATE_NUM): cv.int_range(min=0, max=8),
                cv.Required(CONF_TYPE): cv.enum(THRESHOLD_TYPE),
            }
        )
        .extend(cv.COMPONENT_SCHEMA)
)

CONF_THRESHOLD = "threshold"

def validate_min_max(config):
    if config[CONF_MAX_VALUE] <= config[CONF_MIN_VALUE]:
        raise cv.Invalid("max_value must be greater than min_value")
    return config


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_LD2410_ID): cv.use_id(LD2410Component),
        cv.Optional(CONF_THRESHOLD): cv.ensure_list(THRES_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):

    paren = await cg.get_variable(config[CONF_LD2410_ID])

    if CONF_THRESHOLD in config.keys():
       for dev in config[CONF_THRESHOLD]:
          var = cg.new_Pvariable(dev[CONF_ID], dev[CONF_GATE_NUM], dev[CONF_TYPE]);
          await cg.register_component(var, dev)
          await number.register_number(
              var,
              dev,
              min_value =0,
              max_value =100,
              step=1,
          )
          cg.add(var.set_ld2410_parent(paren))

