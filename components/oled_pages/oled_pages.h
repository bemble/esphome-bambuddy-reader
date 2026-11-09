#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/display/display.h"
#include "esphome/components/font/font.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace esphome::oled_pages {

class OledPages;

class LineWriter {
 public:
  LineWriter(display::Display &display, font::Font &font) : display_(display), font_(font) {}
  void operator()(int y, const std::string &text, bool highlight = false);

 protected:
  display::Display &display_;
  font::Font &font_;
};

class Page {
 public:
  using Writer = std::function<void(display::Display &, LineWriter &)>;
  explicit Page(OledPages *parent) : parent_(parent) {}
  void set_writer(Writer writer) { writer_ = std::move(writer); }
  void render(display::Display &display, LineWriter &line) { writer_(display, line); }
  OledPages *get_parent() const { return parent_; }
  void show();

 protected:
  OledPages *parent_;
  Writer writer_;
};

class ButtonEventTrigger : public Trigger<> {};

class OledPages : public Component {
 public:
  void set_display(display::Display *display) { display_ = display; }
  void set_font(font::Font *font) { font_ = font; }
  void set_initial_page(Page *page) { active_page_ = page; }
  void add_page(Page *page) { pages_.push_back(page); }
  void add_binding(Page *page, const std::string &button, const std::string &timing, ButtonEventTrigger *trigger);
  void setup() override;
  void dump_config() override;
  void render(display::Display &display);
  void show(Page *page);
  void dispatch(const std::string &button, const std::string &timing);
  Page *get_active_page() const { return active_page_; }

 protected:
  struct Binding {
    Page *page;
    std::string button, timing;
    ButtonEventTrigger *trigger;
  };
  display::Display *display_{nullptr};
  font::Font *font_{nullptr};
  Page *active_page_{nullptr};
  std::vector<Page *> pages_;
  std::vector<Binding> bindings_;
};

template<typename... Ts> class ShowAction : public Action<Ts...> {
 public:
  explicit ShowAction(Page *page) : page_(page) {}
  void play(const Ts &...) override { page_->show(); }
 protected:
  Page *page_;
};

template<typename... Ts> class DispatchAction : public Action<Ts...> {
 public:
  DispatchAction(OledPages *parent, std::string button, std::string timing)
      : parent_(parent), button_(std::move(button)), timing_(std::move(timing)) {}
  void play(const Ts &...) override { parent_->dispatch(button_, timing_); }
 protected:
  OledPages *parent_;
  std::string button_, timing_;
};

}  // namespace esphome::oled_pages
