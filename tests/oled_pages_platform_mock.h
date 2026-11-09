#pragma once

#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#define ESP_LOGCONFIG(...) ((void) 0)
#define ESP_LOGE(...) ((void) 0)

namespace esphome {
inline unsigned test_millis = 0;
inline unsigned millis() { return test_millis; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
};
template<typename... Ts> class Trigger {
 public:
  void add_callback(std::function<void(Ts...)> callback) { callback_ = std::move(callback); }
  void trigger(Ts... args) { callback_(args...); }
 protected:
  std::function<void(Ts...)> callback_;
};
template<typename... Ts> class Action {
 public:
  virtual ~Action() = default;
  virtual void play(const Ts &...args) = 0;
};
namespace font {
class Font {
 public:
  int get_height() { return 12; }
};
}
namespace display {
enum class TextAlign { TOP_LEFT };
inline constexpr int COLOR_OFF = 0, COLOR_ON = 1;
class Display {
 public:
  std::vector<std::string> lines;
  std::vector<int> colors;
  int width = 128, rectangle_width = 0, rectangle_height = 0, updates = 0;
  bool auto_clear = true;
  std::function<void(Display &)> writer;
  void set_writer(std::function<void(Display &)> callback) { writer = std::move(callback); }
  void update() {
    ++updates;
    if (auto_clear) { lines.clear(); colors.clear(); }
    if (writer) writer(*this);
  }
  int get_width() { return width; }
  void get_text_bounds(int, int, const char *text, font::Font *, TextAlign,
                       int *offset, int *baseline, int *w, int *h) {
    *offset = *baseline = 0;
    *w = static_cast<int>(std::string(text).size()) * 8;
    *h = 12;
  }
  void filled_rectangle(int, int, int w, int h) { rectangle_width = w; rectangle_height = h; }
  void printf(int, int, font::Font *, int color, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    lines.emplace_back(text);
    colors.push_back(color);
  }
};
}
}
