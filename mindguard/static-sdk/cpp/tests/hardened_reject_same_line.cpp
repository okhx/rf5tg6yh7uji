#include "mindguard/static.hpp"

#include <string_view>

void rejected() {
  MG_WITH_STRING("first-secret", [](std::string_view) {}); MG_WITH_STRING("second-secret", [](std::string_view) {});
}
