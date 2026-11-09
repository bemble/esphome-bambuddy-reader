import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
import esphome.final_validate as fv
from esphome.components import binary_sensor, display, font
from esphome.const import (
    CONF_ID, CONF_TRIGGER_ID, CONF_LAMBDA, CONF_TIMING, CONF_STATE,
    CONF_MIN_LENGTH, CONF_MAX_LENGTH, CONF_INVALID_COOLDOWN,
)

DEPENDENCIES = ["display", "font"]
MULTI_CONF = True
namespace = cg.esphome_ns.namespace("oled_pages")
OledPages = namespace.class_("OledPages", cg.Component)
Page = namespace.class_("Page")
ButtonEventTrigger = namespace.class_("ButtonEventTrigger", automation.Trigger.template())
DispatchAction = namespace.class_("DispatchAction", automation.Action)
ShowAction = namespace.class_("ShowAction", automation.Action)

NAME = cv.All(cv.string_strict, cv.Length(min=1))
EVENT_SCHEMA = automation.validate_automation({
    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ButtonEventTrigger),
    cv.Required("button_id"): NAME,
    cv.Required("timing_id"): NAME,
})
TIMING_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): NAME,
    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(binary_sensor.MultiClickTrigger),
    cv.Required(CONF_TIMING): cv.All(
        [binary_sensor.parse_multi_click_timing_str],
        binary_sensor.validate_multi_click_timing,
        cv.Length(min=1, max=255),
    ),
    cv.Optional(CONF_INVALID_COOLDOWN, default="1s"): cv.positive_time_period_milliseconds,
})
BUTTON_SCHEMA = cv.Schema({
    cv.Required(CONF_ID): NAME,
    cv.Required("binary_sensor_id"): cv.use_id(binary_sensor.BinarySensor),
    cv.Required("timings"): cv.All(cv.ensure_list(TIMING_SCHEMA), cv.Length(min=1)),
})
PAGE_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Page),
    cv.Required(CONF_LAMBDA): cv.lambda_,
    cv.Optional("on_button"): EVENT_SCHEMA,
})


def validate_bindings(config):
    buttons = {}
    for button in config["buttons"]:
        name = button[CONF_ID]
        if name in buttons:
            raise cv.Invalid(f"Duplicate button id '{name}'")
        timings = [timing[CONF_ID] for timing in button["timings"]]
        if len(set(timings)) != len(timings):
            raise cv.Invalid(f"Duplicate timing id on button '{name}'")
        buttons[name] = set(timings)
        for timing in button["timings"]:
            timing["dispatch_automation"] = automation.validate_automation(single=True)({
                "then": [{"oled_pages.dispatch": {
                    CONF_ID: config[CONF_ID], "button_id": name, "timing_id": timing[CONF_ID],
                }}],
            })
    for owner in [config, *config["pages"]]:
        pairs = set()
        for binding in owner.get("on_button", []):
            pair = (binding["button_id"], binding["timing_id"])
            if pair[0] not in buttons or pair[1] not in buttons[pair[0]]:
                raise cv.Invalid(f"Unknown button/timing pair {pair}")
            if pair in pairs:
                raise cv.Invalid(f"Duplicate button binding {pair}")
            pairs.add(pair)
    if config["initial_page"] not in [page[CONF_ID] for page in config["pages"]]:
        raise cv.Invalid("initial_page must belong to this oled_pages instance")
    return config


CONFIG_SCHEMA = cv.All(cv.Schema({
    cv.GenerateID(): cv.declare_id(OledPages),
    cv.Required("display_id"): cv.use_id(display.Display),
    cv.Required("font_id"): cv.use_id(font.Font),
    cv.Required("initial_page"): cv.use_id(Page),
    cv.Optional("buttons", default=[]): cv.ensure_list(BUTTON_SCHEMA),
    cv.Optional("on_button"): EVENT_SCHEMA,
    cv.Required("pages"): cv.All(cv.ensure_list(PAGE_SCHEMA), cv.Length(min=1)),
}).extend(cv.COMPONENT_SCHEMA), validate_bindings)


def final_validate(config):
    full = fv.full_config.get()
    screen = full.get_config_for_path(full.get_path_for_id(config["display_id"])[:-1])
    if CONF_LAMBDA in screen or "pages" in screen or screen.get("show_test_card"):
        raise cv.Invalid("oled_pages requires a display without lambda, native pages or show_test_card")
    owners = full.data.setdefault("oled_pages_displays", set())
    display_id = str(config["display_id"])
    if display_id in owners:
        raise cv.Invalid("Each display can only be managed by one oled_pages instance")
    owners.add(display_id)
    if screen.get("auto_clear_enabled", display.CONF_UNSPECIFIED) == display.CONF_UNSPECIFIED:
        screen["auto_clear_enabled"] = True
    return config


FINAL_VALIDATE_SCHEMA = final_validate


async def build_bindings(var, page, bindings):
    for binding in bindings:
        trigger = cg.new_Pvariable(binding[CONF_TRIGGER_ID])
        cg.add(var.add_binding(page, binding["button_id"], binding["timing_id"], trigger))
        await automation.build_automation(trigger, [], binding)


async def to_code(config):
    if config["buttons"]:
        cg.add_define("USE_BINARY_SENSOR_MULTI_CLICK_TRIGGER")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    screen = await cg.get_variable(config["display_id"])
    text_font = await cg.get_variable(config["font_id"])
    cg.add(var.set_display(screen))
    cg.add(var.set_font(text_font))
    for page_config in config["pages"]:
        page = cg.new_Pvariable(page_config[CONF_ID], var)
        cg.add(var.add_page(page))
    for page_config in config["pages"]:
        page = await cg.get_variable(page_config[CONF_ID])
        writer = await cg.process_lambda(
            page_config[CONF_LAMBDA],
            [(display.Display.operator("ref"), "it"), (namespace.class_("LineWriter").operator("ref"), "line")],
            return_type=cg.void,
        )
        cg.add(page.set_writer(writer))
        await build_bindings(var, page, page_config.get("on_button", []))
    cg.add(var.set_initial_page(await cg.get_variable(config["initial_page"])))
    await build_bindings(var, cg.nullptr, config.get("on_button", []))
    for button in config["buttons"]:
        sensor = await cg.get_variable(button["binary_sensor_id"])
        for timing in button["timings"]:
            events = [
                cg.StructInitializer(
                    binary_sensor.MultiClickTriggerEvent,
                    ("state", event[CONF_STATE]),
                    ("min_length", event[CONF_MIN_LENGTH]),
                    ("max_length", event.get(CONF_MAX_LENGTH, 4294967294)),
                )
                for event in timing[CONF_TIMING]
            ]
            trigger = cg.new_Pvariable(timing[CONF_TRIGGER_ID], cg.TemplateArguments(len(events)), sensor, events)
            cg.add(trigger.set_invalid_cooldown(timing[CONF_INVALID_COOLDOWN]))
            await cg.register_component(trigger, timing)
            await automation.build_automation(trigger, [], timing["dispatch_automation"])


@automation.register_action("oled_pages.dispatch", DispatchAction, cv.Schema({
    cv.Required(CONF_ID): cv.use_id(OledPages),
    cv.Required("button_id"): NAME,
    cv.Required("timing_id"): NAME,
}), synchronous=True)
async def dispatch_action(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent, config["button_id"], config["timing_id"])


@automation.register_action("oled_pages.show", ShowAction, automation.maybe_simple_id({
    cv.Required(CONF_ID): cv.use_id(Page),
}), synchronous=True)
async def show_action(config, action_id, template_arg, args):
    page = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, page)
