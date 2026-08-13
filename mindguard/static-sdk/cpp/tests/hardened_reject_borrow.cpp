#include "mindguard/static.hpp"

#include <string_view>

std::string_view rejected() {
  return MG_WITH_STRING("borrowed-secret", [](std::string_view value) { return value; });
}
