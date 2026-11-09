"""Run inside the ESPHome Docker image; never contacts Bambuddy."""

import copy
import pathlib
import subprocess
import tempfile

import yaml

repo = pathlib.Path(__file__).resolve().parent.parent


class ConfigLoader(yaml.SafeLoader):
    pass


ConfigLoader.add_constructor("!secret", lambda loader, node: loader.construct_scalar(node))
application = yaml.load((repo / "esphome.yaml").read_text(), Loader=ConfigLoader)
pages = {page["id"]: page for page in application["oled_pages"]["pages"]}
assert set(pages) == {
    "page_home", "page_scan", "page_duplicates", "page_not_found",
    "page_info", "page_locations", "page_confirm",
}
assert application["oled_pages"]["initial_page"] == "page_home"
assert "lambda" not in application["display"][0] and "pages" not in application["display"][0]
assert "oled_pages" in application["external_components"][0]["components"]
assert "on_screen_change" in application["bambuddy_oled"]
assert "oled_short_press" not in {script["id"] for script in application["script"]}
for sensor in application["binary_sensor"]:
    if sensor.get("id") in {"reader_button_1", "reader_button_2"}:
        assert not any(key.startswith("on_") for key in sensor)
for page_id, operation in {
    "page_scan": 1, "page_duplicates": 1, "page_not_found": 2,
    "page_info": 3, "page_confirm": 4,
}.items():
    binding = pages[page_id]["on_button"][0]
    assert (binding["button_id"], binding["timing_id"]) == ("navigation", "short")
    assert binding["then"][0]["script.execute"] == {"id": "oled_bambuddy_request", "requested": operation}
assert {binding["timing_id"] for binding in pages["page_locations"]["on_button"]} == {"short", "long"}
back = application["oled_pages"]["on_button"][0]
assert (back["button_id"], back["timing_id"]) == ("back", "press")
assert back["then"] == [{"script.execute": "reader_reset"}, {"oled_pages.show": "page_home"}]
print("PASS complete seven-page application migration")

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    for header in [
        "esphome/core/component.h",
        "esphome/core/automation.h",
        "esphome/core/hal.h",
        "esphome/core/log.h",
        "esphome/components/display/display.h",
        "esphome/components/font/font.h",
    ]:
        path = temporary / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "oled_pages_platform_mock.h"\n')
    binary = temporary / "oled-pages-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I", str(temporary), "-I", str(repo / "tests"),
        str(repo / "tests/oled_pages_test.cpp"),
        str(repo / "components/oled_pages/oled_pages.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

    config = {
        "esphome": {"name": "oled-pages-test"},
        "esp32": {"board": "esp32-c3-devkitm-1"},
        "external_components": [{
            "source": {"type": "local", "path": str(repo / "components")},
            "components": ["oled_pages"],
        }],
        "i2c": {"sda": "GPIO6", "scl": "GPIO7"},
        "display": [{"platform": "ssd1306_i2c", "id": "screen", "model": "SSD1306 128x64"}],
        "font": [{"id": "small", "file": "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "size": 12}],
        "binary_sensor": [{"platform": "template", "id": f"input_{i}"} for i in range(4)],
        "oled_pages": {
            "id": "ui", "display_id": "screen", "font_id": "small", "initial_page": "home",
            "buttons": [
                {"id": f"button_{i}", "binary_sensor_id": f"input_{i}", "timings": [
                    {"id": "short", "timing": ["ON for 40ms to 799ms", "OFF for at least 50ms"]},
                    {"id": "long", "timing": ["ON for at least 800ms"]},
                    {"id": "double", "invalid_cooldown": "500ms", "timing": [
                        "ON for 40ms to 200ms", "OFF for 40ms to 200ms",
                        "ON for 40ms to 200ms", "OFF for at least 250ms",
                    ]},
                ]} for i in range(4)
            ],
            "on_button": [{"button_id": "button_3", "timing_id": "short",
                           "then": [{"oled_pages.show": "home"}]}],
            "pages": [
                {"id": "home", "lambda": 'line(0, "Home", true);', "on_button": [
                    {"button_id": "button_0", "timing_id": "double", "then": [{"oled_pages.show": "info"}]},
                ]},
                {"id": "info", "lambda": 'line(0, "Info");', "on_button": [
                    {"button_id": "button_3", "timing_id": "short", "then": [{"oled_pages.show": "info"}]},
                ]},
            ],
        },
    }
    fonts = list(pathlib.Path("/usr/share/fonts").rglob("*.ttf"))
    if not fonts:
        fonts = list(pathlib.Path("/config").rglob("*.ttf"))
    if not fonts:
        raise RuntimeError("A font is required; run after the firmware build")
    config["font"][0]["file"] = str(fonts[0])

    def check(name, candidate, expected_error=None, auto_clear=True):
        path = temporary / "config.yaml"
        path.write_text(yaml.safe_dump(candidate, sort_keys=False))
        result = subprocess.run(
            ["esphome", "config" if expected_error else "compile", str(path)] +
            ([] if expected_error else ["--only-generate"]),
            capture_output=True, text=True,
        )
        output = result.stdout + result.stderr
        if expected_error:
            assert result.returncode != 0 and expected_error in output, (name, output)
        else:
            assert result.returncode == 0, (name, output)
            source = (temporary / ".esphome/build/oled-pages-test/src/main.cpp").read_text()
            assert f"set_auto_clear({str(auto_clear).lower()})" in source
        print(f"PASS {name}")

    check("four buttons, short/long/double and forward page references", config)
    without_buttons = copy.deepcopy(config)
    without_buttons.pop("binary_sensor")
    without_buttons["oled_pages"]["buttons"] = []
    without_buttons["oled_pages"].pop("on_button")
    for page in without_buttons["oled_pages"]["pages"]:
        page.pop("on_button", None)
    check("render-only manager without buttons", without_buttons)
    explicit_clear = copy.deepcopy(without_buttons)
    explicit_clear["display"][0]["auto_clear_enabled"] = False
    check("explicit auto-clear setting is respected", explicit_clear, auto_clear=False)
    two_displays = copy.deepcopy(without_buttons)
    two_displays["display"].append({
        "platform": "ssd1306_i2c", "id": "second_screen", "model": "SSD1306 128x64", "address": 0x3D,
    })
    second = copy.deepcopy(two_displays["oled_pages"])
    second.update(id="second_ui", display_id="second_screen", initial_page="second_home")
    second["pages"] = [{"id": "second_home", "lambda": 'line(0, "Second");'}]
    two_displays["oled_pages"] = [two_displays["oled_pages"], second]
    check("independent managers on distinct displays", two_displays)
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["pages"][0]["on_button"][0]["button_id"] = "unknown"
    check("unknown button", invalid, "Unknown button/timing pair")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["pages"][0]["on_button"][0]["timing_id"] = "unknown"
    check("unknown timing", invalid, "Unknown button/timing pair")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["buttons"].append(copy.deepcopy(invalid["oled_pages"]["buttons"][0]))
    check("duplicate button name", invalid, "Duplicate button id")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["buttons"][0]["timings"].append(
        copy.deepcopy(invalid["oled_pages"]["buttons"][0]["timings"][0]))
    check("duplicate timing name", invalid, "Duplicate timing id")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["on_button"] *= 2
    check("duplicate binding", invalid, "Duplicate button binding")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["initial_page"] = "unknown"
    check("foreign initial page", invalid, "initial_page must belong")
    invalid = copy.deepcopy(config)
    invalid["oled_pages"]["buttons"][0]["timings"][0]["timing"] = ["ON for 800ms to 40ms"]
    check("invalid native multi-click timing", invalid, "must be larger")
    for key, value in [
        ("lambda", 'it.printf(0, 0, id(small), "native");'),
        ("pages", [{"id": "native", "lambda": 'it.printf(0, 0, id(small), "native");'}]),
        ("show_test_card", True),
    ]:
        invalid = copy.deepcopy(config)
        invalid["display"][0][key] = value
        check(f"display conflict: {key}", invalid, "requires a display without")
    invalid = copy.deepcopy(without_buttons)
    second = copy.deepcopy(invalid["oled_pages"])
    second["id"] = "second_ui"
    second["initial_page"] = "second_home"
    second["pages"] = [{"id": "second_home", "lambda": 'line(0, "Second");'}]
    invalid["oled_pages"] = [invalid["oled_pages"], second]
    check("two managers sharing a display", invalid, "only be managed by one")

print("OLED pages runtime and configuration tests passed")
