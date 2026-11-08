#include "bambuddy_oled.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <memory>
#include <cctype>

namespace esphome::bambuddy_oled {

using namespace spool_reader;
static const char *const TAG = "bambuddy";

static std::string normalized_tag(std::string tag) {
  tag.erase(std::remove_if(tag.begin(), tag.end(), [](unsigned char c) {
    return c == ':' || c == '-' || std::isspace(c);
  }), tag.end());
  for (char &c : tag)
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return tag;
}

void BambuddyOled::dump_config() {
  ESP_LOGCONFIG(TAG, "Asynchronous Bambuddy OLED inventory client");
}

void BambuddyOled::fail_(const std::string &message, bool disconnected) {
  ESP_LOGW(TAG, "%s", message.c_str());
  state.error = message;
  if (disconnected) {
    state.connected = false;
    state.count = -1;
  }
}

std::string BambuddyOled::body_(Operation operation) {
  if (operation == Operation::UPDATE) {
    return json::build_json([](JsonObject root) {
      const auto &location = state.locations[state.selected];
      if (location.id == 0)
        root["location_id"] = nullptr;
      else
        root["location_id"] = location.id;
    });
  }
  if (operation != Operation::CREATE)
    return "";
  return json::build_json([](JsonObject root) {
    const auto &scan = state.scan;
    root["material"] = scan.material;
    root["subtype"] = scan.subtype;
    root["color_name"] = scan.color_name;
    root["rgba"] = scan.color.substr(1) + "FF";
    root["brand"] = "Bambu Lab";
    root["note"] = "Erstellt von Spool-Reader";
    root["label_weight"] = static_cast<int>(scan.label_weight);
    root["slicer_filament"] = scan.filament_code;
    root["slicer_filament_name"] = "Bambu " + scan.material + (scan.subtype.empty() ? "" : " " + scan.subtype);
    if (std::isfinite(scan.min_temp))
      root["nozzle_temp_min"] = static_cast<int>(scan.min_temp);
    if (std::isfinite(scan.max_temp))
      root["nozzle_temp_max"] = static_cast<int>(scan.max_temp);
    root["tag_uid"] = scan.card_uid;
    root["tray_uuid"] = scan.tray_uid;
    root["data_origin"] = "esp-reader";
    root["tag_type"] = "bambulab";
  });
}

bool BambuddyOled::request(int requested) {
  if (in_flight_) {
    fail_("Anfrage laeuft noch");
    return false;
  }
  if (requested < static_cast<int>(Operation::COUNT) || requested > static_cast<int>(Operation::UPDATE)) {
    fail_("Ungueltige Anfrage");
    return false;
  }
  const auto operation = static_cast<Operation>(requested);
  if (operation == Operation::CREATE &&
      (state.scan.material.empty() || !std::isfinite(state.scan.label_weight) || state.scan.label_weight <= 0 ||
       state.scan.color.size() != 7 || state.scan.color[0] != '#')) {
    fail_("Ungueltige Scandaten");
    return false;
  }
  if (operation == Operation::UPDATE && (state.spool.id <= 0 || state.selected >= state.locations.size())) {
    fail_("Ungueltige Auswahl");
    return false;
  }
  std::string base = url_;
  while (!base.empty() && base.back() == '/')
    base.pop_back();
  const std::string locations_url = base + "/api/v1/inventory/locations";
  base += spoolman_ ? "/api/v1/spoolman/inventory" : "/api/v1/inventory";
  std::string url;
  switch (operation) {
    case Operation::COUNT: url = base + "/spools?include_archived=true"; break;
    case Operation::LOOKUP:
      url = spoolman_ ? base + "/spools?include_archived=true" :
            base + "/spools/by-tag?include_archived=true&tray_uuid=" + state.scan.tray_uid +
            "&tag_uid=" + state.scan.card_uid;
      break;
    case Operation::CREATE: url = base + "/spools"; break;
    case Operation::LOCATIONS: url = locations_url; break;
    case Operation::UPDATE: url = base + "/spools/" + std::to_string(state.spool.id); break;
  }
  auto request = std::make_unique<Request>(Request{this, operation, state.generation, url, body_(operation), api_key_,
                                                 spoolman_, state.scan.tray_uid, state.scan.card_uid});
  state.error.clear();
  state.busy = true;
  in_flight_ = true;
  if (xTaskCreate(worker_, "bambuddy_http", 8192, request.get(), 1, nullptr) != pdPASS) {
    in_flight_ = false;
    state.busy = false;
    fail_("HTTP-Task nicht gestartet");
    return false;
  }
  request.release();
  return true;
}

bool BambuddyOled::parse_spool_(const std::string &body, Spool &spool) {
  JsonDocument document;
  if (deserializeJson(document, body) || !document.is<JsonObject>())
    return false;
  JsonObject root = document.as<JsonObject>();
  if (!root["id"].is<int>() || root["id"].as<int>() <= 0 || !root["material"].is<const char *>() ||
      root["material"].as<std::string>().empty() || !root["label_weight"].is<float>() ||
      !root["weight_used"].is<float>())
    return false;
  for (const char *key : {"subtype", "color_name", "rgba", "storage_location"})
    if (!root[key].isNull() && !root[key].is<const char *>())
      return false;
  if (!root["location_id"].isNull() &&
      (!root["location_id"].is<int>() || root["location_id"].as<int>() <= 0))
    return false;
  spool.id = root["id"].as<int>();
  spool.material = root["material"].as<std::string>();
  spool.subtype = root["subtype"].is<const char *>() ? root["subtype"].as<std::string>() : "";
  spool.color = root["color_name"].is<const char *>() ? root["color_name"].as<std::string>() : "";
  if (spool.color.empty() && root["rgba"].is<const char *>())
    spool.color = "#" + root["rgba"].as<std::string>().substr(0, 6);
  spool.location_id = root["location_id"].is<int>() ? root["location_id"].as<int>() : 0;
  spool.location = root["storage_location"].is<const char *>() ? root["storage_location"].as<std::string>() : "";
  return remaining_weight(root["label_weight"].as<float>(), root["weight_used"].as<float>(), spool.remaining);
}

void BambuddyOled::perform_(const Request &request, Result &result) {
  esp_http_client_config_t config{};
  config.url = request.url.c_str();
  config.timeout_ms = 10000;
  config.user_agent = "ESPHome/Spool-Reader";
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.method = request.operation == Operation::CREATE ? HTTP_METHOD_POST :
                  request.operation == Operation::UPDATE ? HTTP_METHOD_PATCH : HTTP_METHOD_GET;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    result.error = "HTTP-Client nicht gestartet";
    return;
  }
  auto cleanup = [](esp_http_client *client) { esp_http_client_cleanup(client); };
  std::unique_ptr<esp_http_client, decltype(cleanup)> guard(client, cleanup);
  if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
      (!request.api_key.empty() && esp_http_client_set_header(client, "X-API-Key", request.api_key.c_str()) != ESP_OK) ||
      esp_http_client_open(client, request.body.size()) != ESP_OK) {
    result.error = "Netzwerkfehler";
    return;
  }
  size_t written = 0;
  while (written < request.body.size()) {
    int length = esp_http_client_write(client, request.body.data() + written, request.body.size() - written);
    if (length <= 0) {
      result.error = "Senden fehlgeschlagen";
      return;
    }
    written += length;
  }
  if (esp_http_client_fetch_headers(client) < 0) {
    result.error = "HTTP-Header fehlen";
    return;
  }
  result.status = esp_http_client_get_status_code(client);
  if (result.status != 200 && !(request.operation == Operation::CREATE && result.status == 201)) {
    if (request.operation != Operation::LOOKUP || result.status != 404)
      result.error = "HTTP " + std::to_string(result.status);
    return;
  }

  int match_rank = 0;
  ObjectArrayParser parser([&](const std::string &object) {
    JsonDocument document;
    if (deserializeJson(document, object) || !document.is<JsonObject>())
      return false;
    JsonObject root = document.as<JsonObject>();
    if (!root["id"].is<int>() || root["id"].as<int>() <= 0)
      return false;
    if (request.operation == Operation::LOOKUP && request.spoolman) {
      const auto tray = normalized_tag(root["tray_uuid"].is<const char *>() ? root["tray_uuid"].as<std::string>() : "");
      const auto card = normalized_tag(root["tag_uid"].is<const char *>() ? root["tag_uid"].as<std::string>() : "");
      int rank = !request.tray_uid.empty() && tray == normalized_tag(request.tray_uid) ? 2 :
                 !request.card_uid.empty() && card == normalized_tag(request.card_uid) ? 1 : 0;
      if (rank > 0 && (rank > match_rank || (rank == match_rank && root["id"].as<int>() < result.spool.id))) {
        if (!parse_spool_(object, result.spool))
          return false;
        match_rank = rank;
      }
    }
    if (request.operation == Operation::LOCATIONS) {
      if (!root["name"].is<const char *>() || root["name"].as<std::string>().empty())
        return false;
      result.locations.push_back({root["id"].as<int>(), root["name"].as<std::string>()});
    }
    ++result.count;
    return true;
  });
  const bool array = request.operation == Operation::COUNT || request.operation == Operation::LOCATIONS ||
                     (request.operation == Operation::LOOKUP && request.spoolman);
  std::string body;
  char buffer[512];
  TickType_t last_data = xTaskGetTickCount();
  // Headers may already have buffered a complete body; drain it before checking completion.
  while (true) {
    int length = esp_http_client_read(client, buffer, sizeof(buffer));
    if (length == 0 && esp_http_client_is_complete_data_received(client))
      break;
    if (length < 0 || xTaskGetTickCount() - last_data > pdMS_TO_TICKS(10000)) {
      result.error = "Antwort unvollstaendig";
      return;
    }
    if (length > 0) {
      last_data = xTaskGetTickCount();
      bool valid = true;
      if (array) {
        for (int i = 0; i < length && valid; ++i)
          valid = parser.feed(buffer[i]);
      } else {
        body.append(buffer, length);
        valid = body.size() <= 8192;
      }
      if (!valid) {
        result.error = "Antwort ungueltig/zu gross";
        return;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  if (array && !parser.complete()) {
    result.error = "Antwort unvollstaendig";
    return;
  }
  if (!array && !parse_spool_(body, result.spool))
    result.error = "Ungueltige Spulendaten";
  if (request.link_tag && result.error.empty()) {
    JsonDocument document;
    if (deserializeJson(document, body) ||
        normalized_tag(document["tray_uuid"].as<std::string>()) != normalized_tag(request.tray_uid) ||
        normalized_tag(document["tag_uid"].as<std::string>()) != normalized_tag(request.card_uid))
      result.error = "NFC-Link nicht uebernommen";
  }
  if (request.operation == Operation::CREATE && request.spoolman && result.error.empty()) {
    // Spoolman's create schema ignores NFC fields; link the new spool explicitly.
    JsonDocument document;
    document["tray_uuid"] = request.tray_uid;
    document["tag_uid"] = request.card_uid;
    std::string link_body;
    serializeJson(document, link_body);
    Request link = request;
    link.operation = Operation::UPDATE;
    link.url += "/" + std::to_string(result.spool.id) + "/tag";
    link.body = std::move(link_body);
    link.link_tag = true;
    Result linked;
    guard.reset();
    perform_(link, linked);
    if (!linked.error.empty() || linked.spool.id != result.spool.id) {
      result.error = "Spule erstellt, NFC-Link fehlt";
      ESP_LOGW(TAG, "Created spool %d, NFC linking failed: %s", result.spool.id, linked.error.c_str());
    } else {
      result.spool = std::move(linked.spool);
    }
  }
}

void BambuddyOled::worker_(void *argument) {
  {
    std::unique_ptr<Request> request(static_cast<Request *>(argument));
    auto result = std::make_unique<Result>();
    result->operation = request->operation;
    result->generation = request->generation;
    perform_(*request, *result);
    request->owner->completed_.store(result.release(), std::memory_order_release);
  }
  vTaskDelete(nullptr);
}

void BambuddyOled::loop() {
  std::unique_ptr<Result> result(completed_.exchange(nullptr, std::memory_order_acquire));
  if (!result)
    return;
  in_flight_ = false;
  if (result->generation != state.generation) {
    // UI cancellation does not undo a write already accepted by the server.
    if (result->operation == Operation::CREATE || result->operation == Operation::UPDATE)
      state.count = -1;
    if (state.screen == Screen::HOME)
      this->request(static_cast<int>(Operation::COUNT));
    return;
  }
  state.busy = false;
  state.connected = result->status >= 200 && result->status < 300;
  if (result->error.empty())
    state.error.clear();
  if (result->operation == Operation::LOOKUP && result->error.empty() &&
      (result->status == 404 || (spoolman_ && result->status == 200 && result->spool.id == 0))) {
    state.connected = true;
    state.create_uncertain = false;
    state.screen = Screen::NOT_FOUND;
    return;
  }
  if (!result->error.empty()) {
    if (result->operation == Operation::COUNT)
      state.count = -1;
    if (result->operation == Operation::CREATE) {
      state.count = -1;
      if (result->spool.id > 0) {
        state.found(std::move(result->spool));
      } else {
        state.create_uncertain = true;
        state.screen = Screen::SCAN;
      }
    }
    fail_(result->error, !state.connected || result->status == 0);
    return;
  }
  if (result->operation == Operation::COUNT) {
    state.count = result->count;
    return;
  }
  if (result->operation == Operation::LOCATIONS) {
    if (!state.set_locations(std::move(result->locations)))
      fail_("Ort fehlt: neu laden");
    return;
  }
  if (result->operation == Operation::UPDATE &&
      (result->spool.id != state.spool.id || result->spool.location_id != state.locations[state.selected].id)) {
    fail_("Ort nicht uebernommen");
    return;
  }
  const bool created = result->operation == Operation::CREATE;
  state.found(std::move(result->spool));
  if (created && state.count >= 0)
    ++state.count;
}

}  // namespace esphome::bambuddy_oled
