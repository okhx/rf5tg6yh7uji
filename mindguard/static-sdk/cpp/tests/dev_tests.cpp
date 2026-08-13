#include "mindguard/static.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>
#include <type_traits>

enum class Mode : std::uint8_t { safe = 7 };

int main() {
  bool called = false;
  MG_WITH_STRING("a\0b", [&](std::string_view value) {
    assert(value.size() == 3);
    assert(value[1] == '\0');
    called = true;
  });
  assert(called);

  MG_WITH_BYTES("\x00\xff", [](std::string_view value) {
    assert(value.size() == 2);
    assert(static_cast<unsigned char>(value[1]) == 0xffU);
  });
  MG_WITH_STRING(R"delimiter(raw\ntext)delimiter", [](std::string_view value) {
    assert(value == R"delimiter(raw\ntext)delimiter");
  });
  MG_WITH_STRING(u"wide", [](std::u16string_view value) { assert(value.size() == 4); });
  MG_WITH_VALUE(-42, [](const auto& value) { assert(MG_VALUE_AS(int, value) == -42); });
  MG_WITH_VALUE(1.25f, [](const auto& value) { assert(MG_VALUE_AS(float, value) == 1.25f); });
  MG_WITH_VALUE('x', [](const auto& value) { assert(MG_VALUE_AS(char, value) == 'x'); });
  MG_WITH_ENUM(Mode::safe, [](const auto& value) {
    assert(MG_VALUE_AS(Mode, value) == Mode::safe);
  });

  auto string = MG_STRING("text");
  static_assert(!std::is_copy_constructible_v<decltype(string)>);
  assert(string.view() == "text");
  auto number = MG_VALUE(7U);
  static_assert(!std::is_copy_constructible_v<decltype(number)>);
  assert(MG_VALUE_AS(std::uint32_t, number) == 7U);
}
