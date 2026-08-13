#include "mindguard/static.hpp"

#include <string_view>

extern "C" __declspec(dllexport) int mindguard_pe_probe() noexcept {
  int result = 0;
  MG_WITH_STRING("pe32plus-hardened-secret", [&](std::string_view value) {
    result = value == "pe32plus-hardened-secret" ? 73 : -1;
  });
  return result;
}
