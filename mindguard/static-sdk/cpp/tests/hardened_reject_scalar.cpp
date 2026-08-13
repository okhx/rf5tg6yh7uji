#include "mindguard/static.hpp"

void rejected() {
  MG_WITH_VALUE(1.0L, [](auto) {});
}
