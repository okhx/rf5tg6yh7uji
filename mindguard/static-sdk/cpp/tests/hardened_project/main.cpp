#include "mindguard/static.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <span>
#include <string_view>
#include <thread>

int main() {
  bool string_called = false;
  MG_WITH_STRING("generated\0hardened-secret", [&](std::string_view value) {
    assert(value.size() == 25);
    assert(value[9] == '\0');
    if (std::getenv("MINDGUARD_E2E_SLOW_CALLBACK") != nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    string_called = true;
  });
  assert(string_called);

  bool bytes_called = false;
  MG_WITH_BYTES("\x00\xffpacked-byte-secret", [&](std::span<const std::uint8_t> value) {
    assert(value.size() == 20);
    assert(value[0] == 0 && value[1] == 0xff);
    bytes_called = true;
  });
  assert(bytes_called);

  MG_WITH_VALUE(-42, [](const auto& value) {
    assert(MG_VALUE_AS(int, value) == -42);
  });
  MG_WITH_VALUE(0xffU, [](const auto& value) {
    assert(MG_VALUE_AS(unsigned, value) == 255U);
  });
  MG_WITH_VALUE('x', [](const auto& value) {
    assert(MG_VALUE_AS(char, value) == 'x');
  });
  MG_WITH_VALUE(1.25f, [](const auto& value) {
    assert(MG_VALUE_AS(float, value) == 1.25f);
  });
}
