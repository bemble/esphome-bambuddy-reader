import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_URL

DEPENDENCIES = ["esp32", "http_request"]
AUTO_LOAD = ["json"]

CONF_API_KEY = "api_key"
namespace = cg.esphome_ns.namespace("bambuddy_oled")
BambuddyOled = namespace.class_("BambuddyOled", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BambuddyOled),
        cv.Required(CONF_URL): cv.url,
        cv.Optional(CONF_API_KEY, default=""): cv.string_strict,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_api_key(config[CONF_API_KEY]))
