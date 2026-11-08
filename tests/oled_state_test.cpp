#include "../components/bambuddy_oled/oled-state.h"

#include <cassert>
#include <iostream>

using namespace spool_reader;

static void test_navigation() {
  State s;
  assert(s.screen == Screen::HOME);
  s.connected = true;
  s.count = 42;
  uint32_t pending_generation = s.generation;
  Scan scan;
  scan.card_uid = "AABBCCDD";
  scan.material = "PLA";
  s.scanned(scan);
  assert(pending_generation != s.generation);
  assert(s.screen == Screen::SCAN && s.scan.card_uid == "AABBCCDD");
  s.screen = Screen::NOT_FOUND;
  s.found({12, 2, "PLA", "Basic", "Blue", "Shelf 2", 350});
  assert(s.screen == Screen::INFO);
  assert(s.set_locations({{1, "Shelf 1"}, {2, "Shelf 2"}, {3, "Shelf 3"}}));
  assert(s.selected == 2 && s.current_location(2) && !s.current_location(0));
  s.next_location();
  assert(s.selected == 3);
  s.next_location();
  assert(s.selected == 0);
  s.busy = true;
  s.next_location();
  s.confirm();
  assert(s.screen == Screen::LOCATIONS && s.selected == 0);
  s.busy = false;
  s.confirm();
  assert(s.screen == Screen::CONFIRM);
  s.next_location();
  assert(s.selected == 0);
  s.home();
  assert(s.screen == Screen::HOME && s.scan.card_uid.empty() && s.spool.id == 0);
  assert(s.locations.empty() && !s.busy && s.error.empty());
  assert(s.count == 42 && s.connected);
  pending_generation = s.generation;
  s.home();
  assert(pending_generation != s.generation);
  s.confirm();
  assert(s.screen == Screen::HOME);

  s.found({12, 0, "PLA", "", "Blue", "", 350});
  assert(s.set_locations({}) && s.selected == 0 && s.current_location(0));
  s.next_location();
  assert(s.selected == 0);
  s.confirm();
  assert(s.screen == Screen::CONFIRM);

  s.found({12, 0, "PLA", "", "Blue", "Legacy", 350});
  assert(s.set_locations({{5, "Legacy"}}));
  assert(s.selected == 1 && s.current_location(1) && !s.current_location(0));
  s.found({12, 99, "PLA", "", "Blue", "Deleted", 350});
  assert(!s.set_locations({{5, "Legacy"}}) && s.screen == Screen::INFO);

  for (auto screen : {Screen::HOME, Screen::SCAN, Screen::NOT_FOUND, Screen::INFO,
                      Screen::LOCATIONS, Screen::CONFIRM}) {
    s.screen = screen;
    s.error = "Error";
    s.busy = true;
    s.create_uncertain = true;
    s.home();
    assert(s.screen == Screen::HOME && s.error.empty() && !s.create_uncertain && !s.busy);
  }
}

static void test_streaming() {
  std::vector<std::string> objects;
  ObjectArrayParser parser([&](const auto &object) { objects.push_back(object); return true; });
  std::string input = R"([ {"id":1,"name":"a\"b\\c","nested":[{"x":"]}"}]}, {"id":2} ])";
  for (char c : input)
    assert(parser.feed(c));
  assert(parser.complete() && objects.size() == 2 && objects[1] == R"({"id":2})");

  int count = 0;
  ObjectArrayParser large([&](const auto &) { ++count; return true; });
  assert(large.feed('['));
  for (int i = 0; i < 5000; ++i) {
    if (i)
      assert(large.feed(','));
    for (char c : std::string(R"({"id":1,"description":"full spool"})"))
      assert(large.feed(c));
  }
  assert(large.feed(']') && large.complete() && count == 5000);

  for (const auto &bad : {"", "[", "[{}", "[{},]", "[1]", "{}",
                         "[{}]x", "[{} {}]", "[{]", "[{\"x\":\"unterminated}]"}) {
    ObjectArrayParser malformed([](const auto &) { return true; });
    bool accepted = true;
    for (char c : std::string(bad))
      accepted = malformed.feed(c) && accepted;
    assert(!accepted || !malformed.complete());
  }
  ObjectArrayParser empty([](const auto &) { return true; });
  assert(empty.feed('[') && empty.feed(']') && empty.complete());
  ObjectArrayParser rejected([](const auto &) { return false; });
  assert(rejected.feed('[') && rejected.feed('{') && !rejected.feed('}'));
  ObjectArrayParser oversized([](const auto &) { return true; });
  bool accepted = true;
  for (char c : std::string("[{\"x\":\"") + std::string(8192, 'a') + "\"}]")
    accepted = oversized.feed(c) && accepted;
  assert(!accepted && !oversized.complete());
}

int main() {
  test_navigation();
  test_streaming();
  float remaining = NAN;
  assert(remaining_weight(750, 125, remaining) && remaining == 625);
  assert(remaining_weight(1000, 1100, remaining) && remaining == 0);
  assert(remaining_weight(0, 0, remaining) && remaining == 0);
  assert(!remaining_weight(-1, 0, remaining));
  assert(!remaining_weight(1000, -1, remaining));
  assert(!remaining_weight(NAN, 0, remaining));
  assert(!remaining_weight(1000, INFINITY, remaining));
  std::cout << "OLED navigation and streaming tests passed\n";
}
