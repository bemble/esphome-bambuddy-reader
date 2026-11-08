#pragma once

#include <ArduinoJson.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

using namespace ArduinoJson;

namespace esphome {
class Component {
 public:
  virtual ~Component() = default;
  virtual void loop() {}
  virtual void dump_config() {}
};
namespace json {
inline std::string build_json(const std::function<void(JsonObject)> &builder) {
  JsonDocument document;
  builder(document.to<JsonObject>());
  std::string body;
  serializeJson(document, body);
  return body;
}
}  // namespace json
}  // namespace esphome

#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGCONFIG(...) ((void) 0)

using TickType_t = uint64_t;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int pdPASS = 1;
inline TickType_t pdMS_TO_TICKS(uint64_t value) { return value; }
inline TickType_t xTaskGetTickCount() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
inline void vTaskDelete(void *) {}
inline std::atomic<int> workers{0};
inline int xTaskCreate(void (*entry)(void *), const char *, int stack, void *argument, int, void *) {
  assert(stack == 8192);
  ++workers;
  std::thread([entry, argument] { entry(argument); --workers; }).detach();
  return pdPASS;
}

enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_PATCH };
inline int esp_crt_bundle_attach(void *) { return ESP_OK; }
struct esp_http_client_config_t {
  const char *url;
  int timeout_ms;
  const char *user_agent;
  int (*crt_bundle_attach)(void *);
  esp_http_client_method_t method;
};
struct MockServer {
  std::mutex mutex;
  std::condition_variable released;
  std::string response, sent_body, url;
  int status{200};
  bool blocked{false}, fail_open{false};
  esp_http_client_method_t method{HTTP_METHOD_GET};
};
inline MockServer server;
struct esp_http_client {
  std::string response;
  size_t offset{0};
  int status;
};
using esp_http_client_handle_t = esp_http_client *;
inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
  assert(config->timeout_ms == 10000 && config->crt_bundle_attach != nullptr);
  std::lock_guard<std::mutex> lock(server.mutex);
  server.url = config->url;
  server.method = config->method;
  server.sent_body.clear();
  return new esp_http_client{server.response, 0, server.status};
}
inline void esp_http_client_cleanup(esp_http_client_handle_t client) { delete client; }
inline int esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *) { return ESP_OK; }
inline int esp_http_client_open(esp_http_client_handle_t, size_t) {
  std::unique_lock<std::mutex> lock(server.mutex);
  assert(server.released.wait_for(lock, std::chrono::seconds(5), [] { return !server.blocked; }));
  return server.fail_open ? ESP_FAIL : ESP_OK;
}
inline int esp_http_client_write(esp_http_client_handle_t, const char *data, size_t size) {
  std::lock_guard<std::mutex> lock(server.mutex);
  server.sent_body.append(data, size);
  return static_cast<int>(size);
}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { return client->response.size(); }
inline int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->status; }
inline bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) {
  return client->response.size() <= 512 || client->offset == client->response.size();
}
inline int esp_http_client_read(esp_http_client_handle_t client, char *buffer, size_t maximum) {
  size_t size = std::min({maximum, client->response.size() - client->offset, size_t{257}});
  std::memcpy(buffer, client->response.data() + client->offset, size);
  client->offset += size;
  return static_cast<int>(size);
}
