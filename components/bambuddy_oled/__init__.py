import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.const import CONF_ID, CONF_URL, CONF_TRIGGER_ID

DEPENDENCIES = ["esp32", "http_request"]
AUTO_LOAD = ["json"]

CONF_API_KEY = "api_key"
CONF_SPOOLMAN = "spoolman"
CONF_LOCATION_EXCLUDE_REGEX = "location_exclude_regex"
namespace = cg.esphome_ns.namespace("bambuddy_oled")
BambuddyOled = namespace.class_("BambuddyOled", cg.Component)
ScreenChangeTrigger = namespace.class_("ScreenChangeTrigger", automation.Trigger.template(cg.int_))

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BambuddyOled),
        cv.Required(CONF_URL): cv.url,
        cv.Optional(CONF_API_KEY, default=""): cv.string_strict,
        cv.Optional(CONF_SPOOLMAN, default=False): cv.boolean,
        cv.Optional(CONF_LOCATION_EXCLUDE_REGEX, default=""): cv.string_strict,
        cv.Optional("on_screen_change"): automation.validate_automation({
            cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScreenChangeTrigger),
        }),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_url(config[CONF_URL]))
    cg.add(var.set_api_key(config[CONF_API_KEY]))
    cg.add(var.set_spoolman(config[CONF_SPOOLMAN]))
    cg.add(var.set_location_exclude_regex(config[CONF_LOCATION_EXCLUDE_REGEX]))
    for conf in config.get("on_screen_change", []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.int_, "screen")], conf)
