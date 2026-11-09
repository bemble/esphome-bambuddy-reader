#include "oled_pages_platform_mock.h"
#include "../components/oled_pages/oled_pages.h"

#include <cassert>
#include <iostream>

using namespace esphome;
using namespace esphome::oled_pages;

int main() {
  display::Display screen;
  font::Font font;
  OledPages ui;
  Page home(&ui), info(&ui), foreign(&ui);
  ui.set_display(&screen);
  ui.set_font(&font);
  ui.add_page(&home);
  ui.add_page(&info);
  ui.set_initial_page(&home);
  home.set_writer([](display::Display &, LineWriter &line) {
    line(0, "Home", true);
    line(13, "second line");
  });
  info.set_writer([](display::Display &, LineWriter &line) { line(0, "Info"); });
  ui.setup();
  screen.update();
  assert((screen.lines == std::vector<std::string>{"Home", "second line"}));
  assert(screen.colors[0] == display::COLOR_OFF);
  assert(screen.rectangle_width == screen.width && screen.rectangle_height == 12);

  int global_count = 0, page_count = 0, third_count = 0, fourth_count = 0;
  ButtonEventTrigger global, local, third, fourth, back;
  global.add_callback([&]() { ++global_count; });
  local.add_callback([&]() { ++page_count; });
  third.add_callback([&]() { ++third_count; info.show(); });
  fourth.add_callback([&]() { ++fourth_count; });
  back.add_callback([&]() { home.show(); });
  ui.add_binding(nullptr, "navigation", "short", &global);
  ui.add_binding(&info, "navigation", "short", &local);
  ui.add_binding(&home, "third-button", "double", &third);
  ui.add_binding(&info, "fourth-button", "hold", &fourth);
  ui.add_binding(nullptr, "back", "press", &back);
  ui.dispatch("navigation", "short");
  assert(global_count == 1 && page_count == 0);
  ui.dispatch("third-button", "double");
  assert(third_count == 1 && ui.get_active_page() == &info);
  assert((screen.lines == std::vector<std::string>{"Info"}));
  ui.dispatch("navigation", "short");
  assert(global_count == 1 && page_count == 1);
  ui.dispatch("fourth-button", "hold");
  assert(fourth_count == 1);
  ui.dispatch("missing", "short");
  ui.dispatch("navigation", "undefined");
  assert(global_count == 1 && page_count == 1);
  ui.dispatch("back", "press");
  assert(ui.get_active_page() == &home);
  ui.dispatch("fourth-button", "hold");
  assert(fourth_count == 1);
  ui.show(&foreign);
  assert(ui.get_active_page() == &home);
  ShowAction<int> show_info(&info);
  show_info.play(42);
  assert(ui.get_active_page() == &info);
  DispatchAction<int> dispatch_back(&ui, "back", "press");
  dispatch_back.play(42);
  assert(ui.get_active_page() == &home);

  screen.width = 32;
  LineWriter line(screen, font);
  const std::string text = "A\xC3\xA4" "BCDEFG";
  test_millis = 4 * 700;
  line(0, text, true);
  assert(screen.lines.back() == text.substr(1));
  test_millis = 5 * 700;
  line(0, text);
  assert(screen.lines.back() == text.substr(3));
  assert(screen.rectangle_width == 32);
  test_millis = 0;
  line(0, "");
  assert(screen.lines.back().empty());
  std::cout << "OLED page routing, actions and rendering tests passed\n";
}
