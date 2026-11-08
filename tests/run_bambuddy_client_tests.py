import pathlib
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parent.parent
libraries = list(pathlib.Path("/config/.esphome/build/spoolscanner").glob(
    "managed_components/bblanchon__arduinojson/src/ArduinoJson.h"
))
if len(libraries) != 1:
    raise RuntimeError("Compile the ESPHome configuration first to provide its ArduinoJson dependency")

with tempfile.TemporaryDirectory() as directory:
    temporary = pathlib.Path(directory)
    for header in [
        "esphome/core/component.h",
        "esphome/core/log.h",
        "esphome/components/json/json_util.h",
        "esp_crt_bundle.h",
        "esp_http_client.h",
        "freertos/FreeRTOS.h",
        "freertos/task.h",
    ]:
        path = temporary / header
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "bambuddy_platform_mock.h"\n')
    binary = temporary / "bambuddy-tests"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
        "-fsanitize=address,undefined",
        "-I", str(temporary), "-I", str(repo / "tests"), "-I", str(libraries[0].parent),
        str(repo / "tests/bambuddy_client_test.cpp"),
        str(repo / "components/bambuddy_oled/bambuddy_oled.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
