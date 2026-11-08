#include "../components/bambuddy_oled/bambuddy_oled.h"
#include "bambuddy_platform_mock.h"

#include <iostream>

using namespace spool_reader;
using esphome::bambuddy_oled::BambuddyOled;

static void scenario(const std::string &response, int status = 200, bool blocked = false, bool fail = false) {
  assert(workers == 0);
  std::lock_guard<std::mutex> lock(server.mutex);
  server.response = response;
  server.status = status;
  server.blocked = blocked;
  server.fail_open = fail;
}

static void release() {
  {
    std::lock_guard<std::mutex> lock(server.mutex);
    server.blocked = false;
  }
  server.released.notify_all();
}

static void finish(BambuddyOled &client) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (client.is_running() || workers != 0) {
    assert(std::chrono::steady_clock::now() < deadline);
    client.loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

static const std::string spool = R"({"id":12,"material":"PLA","subtype":"Basic","color_name":"Blue","label_weight":750,"weight_used":125,"location_id":2,"storage_location":"Shelf 2"})";

int main() {
  BambuddyOled client;
  client.set_url("https://example.invalid/");
  client.set_api_key("test");
  Scan scan;
  scan.card_uid = "AABBCCDD";
  scan.tray_uid = "00112233445566778899AABBCCDDEEFF";
  scan.material = "PLA";
  scan.color = "#123456";
  scan.label_weight = 750;
  state.scanned(scan);

  scenario(spool, 200, true);
  auto started = std::chrono::steady_clock::now();
  assert(client.request(static_cast<int>(Operation::LOOKUP)));
  assert(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
  assert(state.busy);
  state.home();
  assert(!state.busy && state.screen == Screen::HOME);
  // New scan must survive the old spool lookup completing later.
  scan.card_uid = "11223344";
  state.scanned(scan);
  assert(!client.request(static_cast<int>(Operation::LOOKUP)));
  release();
  finish(client);
  assert(state.screen == Screen::SCAN && state.spool.id == 0 && state.scan.card_uid == "11223344");

  scenario(spool);
  assert(client.request(static_cast<int>(Operation::LOOKUP)));
  finish(client);
  assert(state.screen == Screen::INFO && state.spool.id == 12 && state.spool.remaining == 625);
  assert(server.method == HTTP_METHOD_GET && server.url.find("tray_uuid=" + scan.tray_uid) != std::string::npos);

  scenario(R"([{"id":1}])");
  assert(client.request(static_cast<int>(Operation::LOCATIONS)));
  finish(client);
  assert(state.screen == Screen::INFO && !state.error.empty());

  scenario(R"([{"id":1,"name":"Shelf 1"},{"id":2,"name":"Shelf 2"}])");
  assert(client.request(static_cast<int>(Operation::LOCATIONS)));
  finish(client);
  assert(state.screen == Screen::LOCATIONS && state.selected == 2 && state.locations.size() == 3);
  state.next_location();
  state.confirm();
  scenario(R"({"id":12,"material":"PLA","color_name":"Blue","label_weight":750,"weight_used":125,"location_id":null,"storage_location":null})");
  assert(client.request(static_cast<int>(Operation::UPDATE)));
  finish(client);
  assert(server.method == HTTP_METHOD_PATCH && state.spool.location.empty() && state.screen == Screen::INFO);
  JsonDocument document;
  assert(!deserializeJson(document, server.sent_body));
  assert(document["location_id"].isNull() && document.size() == 1);

  assert(state.set_locations({{2, "Shelf 2"}}));
  state.next_location();
  state.confirm();
  scenario(R"({"id":99,"material":"PLA","label_weight":750,"weight_used":125,"location_id":2})");
  assert(client.request(static_cast<int>(Operation::UPDATE)));
  finish(client);
  assert(state.screen == Screen::CONFIRM && state.spool.id == 12 && !state.error.empty());

  state.scanned(scan);
  scenario(R"({"detail":"Spool not found"})", 404);
  assert(client.request(static_cast<int>(Operation::LOOKUP)));
  finish(client);
  assert(state.screen == Screen::NOT_FOUND && state.connected && state.error.empty());
  scenario(spool, 201);
  assert(client.request(static_cast<int>(Operation::CREATE)));
  finish(client);
  assert(state.screen == Screen::INFO && server.method == HTTP_METHOD_POST);
  assert(!deserializeJson(document, server.sent_body));
  assert(document["label_weight"].as<int>() == 750 && document["rgba"].as<std::string>() == "123456FF");
  assert(document["tag_uid"].as<std::string>() == scan.card_uid);
  assert(document["tray_uuid"].as<std::string>() == scan.tray_uid);

  state.scanned(scan);
  scenario("{}", 500);
  assert(client.request(static_cast<int>(Operation::LOOKUP)));
  finish(client);
  assert(state.screen == Screen::SCAN && !state.error.empty());
  scenario("", 200, false, true);
  state.screen = Screen::NOT_FOUND;
  assert(client.request(static_cast<int>(Operation::CREATE)));
  finish(client);
  assert(state.screen == Screen::SCAN && state.create_uncertain && !state.connected);

  state.home();
  scenario("[]");
  assert(client.request(static_cast<int>(Operation::COUNT)));
  finish(client);
  assert(state.count == 0 && state.connected);
  std::string inventory = "[";
  for (int i = 0; i < 5000; ++i)
    inventory += (i ? "," : "") + std::string(R"({"id":1})");
  inventory += "]";
  scenario(inventory);
  assert(client.request(static_cast<int>(Operation::COUNT)));
  finish(client);
  assert(state.count == 5000);
  scenario("[{},]");
  assert(client.request(static_cast<int>(Operation::COUNT)));
  finish(client);
  assert(state.count == -1 && !state.error.empty());

  state.scanned(scan);
  scenario("[]", 200, true);
  assert(client.request(static_cast<int>(Operation::COUNT)));
  state.home();
  release();
  finish(client);
  assert(state.screen == Screen::HOME && state.scan.card_uid.empty() && state.spool.id == 0);
  assert(state.count == 0 && !state.busy);

  std::cout << "Async client, cancellation, JSON and mock GET/POST/PATCH tests passed\n";
}
