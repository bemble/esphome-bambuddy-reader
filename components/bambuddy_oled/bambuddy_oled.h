#pragma once

#include "oled-state.h"
#include "esphome/core/component.h"

#include <atomic>

namespace esphome::bambuddy_oled {

class BambuddyOled : public Component {
 public:
  void set_url(const std::string &url) { url_ = url; }
  void set_api_key(const std::string &key) { api_key_ = key; }
  void set_spoolman(bool spoolman) { spoolman_ = spoolman; }
  void set_location_exclude_regex(const std::string &pattern) { location_exclude_regex_ = pattern; }
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
    bool spoolman;
    std::string tray_uid, card_uid;
    bool link_tag{false};
    std::string location_exclude_regex;
    std::string current_location;
  };
  struct Result {
    spool_reader::Operation operation;
    uint32_t generation;
    int status{0}, count{0};
    std::string error;
    spool_reader::Spool spool;
    std::vector<spool_reader::Location> locations;
    bool current_location_excluded{false};
    std::vector<int> duplicate_ids;
  };

  static void worker_(void *argument);
  static void perform_(const Request &request, Result &result);
  static bool parse_spool_(const std::string &body, spool_reader::Spool &spool);
  static std::string body_(spool_reader::Operation operation, bool spoolman);
  void fail_(const std::string &message, bool disconnected = false);

  std::string url_, api_key_, location_exclude_regex_;
  std::atomic<Result *> completed_{nullptr};
  bool in_flight_{false};
  bool spoolman_{false};
};

}  // namespace esphome::bambuddy_oled
