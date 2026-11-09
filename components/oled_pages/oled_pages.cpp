#include "oled_pages.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::oled_pages {

static const char *const TAG = "oled_pages";

void LineWriter::operator()(int y, const std::string &text, bool highlight) {
  int width, height, x_offset, baseline;
  std::string visible = text;
  display_.get_text_bounds(0, y, visible.c_str(), &font_, display::TextAlign::TOP_LEFT,
                           &x_offset, &baseline, &width, &height);
  if (width > display_.get_width()) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < text.size(); ++i)
      if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
        starts.push_back(i);
    size_t step = (millis() / 700) % (starts.size() + 5);
    if (step > 2 && step - 3 < starts.size())
      visible = text.substr(starts[step - 3]);
  }
  if (highlight)
    display_.filled_rectangle(0, y, display_.get_width(), font_.get_height());
  display_.printf(0, y, &font_, highlight ? display::COLOR_OFF : display::COLOR_ON, "%s", visible.c_str());
}

void Page::show() { parent_->show(this); }

void OledPages::setup() {
  display_->set_writer([this](display::Display &display) { this->render(display); });
}

void OledPages::dump_config() {
  ESP_LOGCONFIG(TAG, "OLED pages: %u pages, %u event bindings", static_cast<unsigned>(pages_.size()),
                static_cast<unsigned>(bindings_.size()));
}

void OledPages::add_binding(Page *page, const std::string &button, const std::string &timing,
                            ButtonEventTrigger *trigger) {
  bindings_.push_back({page, button, timing, trigger});
}

void OledPages::render(display::Display &display) {
  if (active_page_ == nullptr || font_ == nullptr) {
    ESP_LOGE(TAG, "No active page or font configured");
    return;
  }
  LineWriter line(display, *font_);
  active_page_->render(display, line);
}

void OledPages::show(Page *page) {
  if (std::find(pages_.begin(), pages_.end(), page) == pages_.end()) {
    ESP_LOGE(TAG, "Cannot show a page belonging to a different OLED instance");
    return;
  }
  active_page_ = page;
  if (display_ != nullptr)
    display_->update();
}

void OledPages::dispatch(const std::string &button, const std::string &timing) {
  ButtonEventTrigger *fallback = nullptr;
  for (const auto &binding : bindings_) {
    if (binding.button != button || binding.timing != timing)
      continue;
    if (binding.page == active_page_ && binding.page != nullptr) {
      binding.trigger->trigger();
      return;
    }
    if (binding.page == nullptr)
      fallback = binding.trigger;
  }
  if (fallback != nullptr)
    fallback->trigger();
}

}  // namespace esphome::oled_pages
