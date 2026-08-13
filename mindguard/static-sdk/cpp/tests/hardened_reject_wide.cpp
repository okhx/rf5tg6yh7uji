#include "mindguard/static.hpp"

#include <string_view>

void rejected() {
  MG_WITH_STRING(u"wide-secret", [](std::u16string_view) {});
}
