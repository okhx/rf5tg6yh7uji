#pragma once

#include "../include/mindguard/static.hpp"

inline void protected_header_site() {
  MG_WITH_STRING("forbidden", [](auto) {});
}

