#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace spool_reader {

enum class Screen { HOME, SCAN, NOT_FOUND, INFO, LOCATIONS, CONFIRM };
enum class Operation { COUNT, LOOKUP, CREATE, LOCATIONS, UPDATE };

struct Scan {
  std::string card_uid, tray_uid, material, subtype, color_name, color, filament_code, production_date;
  float label_weight{NAN}, min_temp{NAN}, max_temp{NAN};
};

struct Spool {
  int id{0}, location_id{0};
  std::string material, subtype, color, location;
  float remaining{NAN};
};

inline bool remaining_weight(float label, float used, float &remaining) {
  if (!std::isfinite(label) || !std::isfinite(used) || label < 0 || used < 0)
    return false;
  remaining = std::max(0.0f, label - used);
  return true;
}

struct Location {
  int id;
  std::string name;
};

struct State {
  Screen screen{Screen::HOME};
  Scan scan;
  Spool spool;
  std::vector<Location> locations;
  size_t selected{0};
  bool busy{false}, connected{false}, create_uncertain{false};
  int count{-1};
  uint32_t generation{0};
  std::string error;

  void home() {
    ++generation;
    screen = Screen::HOME;
    scan = {};
    spool = {};
    locations.clear();
    selected = 0;
    busy = false;
    create_uncertain = false;
    error.clear();
  }

  void scanned(Scan value) {
    home();
    scan = std::move(value);
    screen = Screen::SCAN;
  }

  void found(Spool value) {
    spool = std::move(value);
    locations.clear();
    selected = 0;
    create_uncertain = false;
    screen = Screen::INFO;
  }

  bool set_locations(std::vector<Location> values) {
    values.insert(values.begin(), {0, ""});
    size_t current = 0;
    if (spool.location_id != 0 || !spool.location.empty()) {
      bool found = false;
      for (size_t i = 1; i < values.size(); ++i) {
        if ((spool.location_id != 0 && values[i].id == spool.location_id) ||
            (spool.location_id == 0 && values[i].name == spool.location)) {
          current = i;
          found = true;
          break;
        }
      }
      if (!found)
        return false;
    }
    locations = std::move(values);
    selected = current;
    screen = Screen::LOCATIONS;
    return true;
  }

  void next_location() {
    if (screen == Screen::LOCATIONS && !busy && !locations.empty()) {
      selected = (selected + 1) % locations.size();
      error.clear();
    }
  }

  void confirm() {
    if (screen == Screen::LOCATIONS && !busy && selected < locations.size()) {
      screen = Screen::CONFIRM;
      error.clear();
    }
  }

  bool current_location(size_t index) const {
    return index < locations.size() &&
           ((spool.location_id != 0 && spool.location_id == locations[index].id) ||
            (spool.location_id == 0 && spool.location.empty() && locations[index].id == 0) ||
            (spool.location_id == 0 && !spool.location.empty() && spool.location == locations[index].name));
  }
};

inline State state;

// Only one array object is buffered, irrespective of the inventory size.
class ObjectArrayParser {
 public:
  using Callback = std::function<bool(const std::string &)>;
  explicit ObjectArrayParser(Callback callback) : callback_(std::move(callback)) {}

  bool feed(char c) {
    if (failed_)
      return false;
    if (in_object_) {
      object_ += c;
      if (object_.size() > 8192)
        return fail_();
      if (in_string_) {
        if (escaped_)
          escaped_ = false;
        else if (c == '\\')
          escaped_ = true;
        else if (c == '"')
          in_string_ = false;
      } else if (c == '"') {
        in_string_ = true;
      } else if (c == '{' || c == '[') {
        ++depth_;
      } else if (c == '}' || c == ']') {
        if (--depth_ == 0) {
          if (c != '}' || !callback_(object_))
            return fail_();
          object_.clear();
          in_object_ = false;
          phase_ = Phase::SEPARATOR;
        }
      }
      return true;
    }
    if (c == ' ' || c == '\r' || c == '\n' || c == '\t')
      return true;
    if (phase_ == Phase::START && c == '[') {
      phase_ = Phase::FIRST;
      return true;
    }
    if ((phase_ == Phase::FIRST || phase_ == Phase::VALUE) && c == '{') {
      in_object_ = true;
      depth_ = 1;
      object_ = "{";
      return true;
    }
    if ((phase_ == Phase::FIRST || phase_ == Phase::SEPARATOR) && c == ']') {
      phase_ = Phase::DONE;
      return true;
    }
    if (phase_ == Phase::SEPARATOR && c == ',') {
      phase_ = Phase::VALUE;
      return true;
    }
    return fail_();
  }

  bool complete() const { return !failed_ && phase_ == Phase::DONE && !in_object_; }

 private:
  enum class Phase { START, FIRST, VALUE, SEPARATOR, DONE };
  bool fail_() { failed_ = true; return false; }
  Callback callback_;
  Phase phase_{Phase::START};
  std::string object_;
  int depth_{0};
  bool in_object_{false}, in_string_{false}, escaped_{false}, failed_{false};
};

}  // namespace spool_reader
