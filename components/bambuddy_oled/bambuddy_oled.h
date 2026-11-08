#pragma once

#include "oled-state.h"
#include "esphome/core/component.h"

#include <atomic>

namespace esphome::bambuddy_oled {

class BambuddyOled : public Component {
 public:
  void set_url(const std::string &url) { url_ = url; }
  void set_api_key(const std::string &key) { api_key_ = key; }
  void loop() override;
  void dump_config() override;
  bool request(int operation);
  bool is_running() const { return in_flight_; }

 protected:
  struct Request {
    BambuddyOled *owner;
    spool_reader::Operation operation;
    uint32_t generation;
    std::string url, body, api_key;
  };
  struct Result {
    spool_reader::Operation operation;
    uint32_t generation;
    int status{0}, count{0};
    std::string error;
    spool_reader::Spool spool;
    std::vector<spool_reader::Location> locations;
  };

  static void worker_(void *argument);
  static void perform_(const Request &request, Result &result);
  static bool parse_spool_(const std::string &body, spool_reader::Spool &spool);
  static std::string body_(spool_reader::Operation operation);
  void fail_(const std::string &message, bool disconnected = false);

  std::string url_, api_key_;
  std::atomic<Result *> completed_{nullptr};
  bool in_flight_{false};
};

}  // namespace esphome::bambuddy_oled
