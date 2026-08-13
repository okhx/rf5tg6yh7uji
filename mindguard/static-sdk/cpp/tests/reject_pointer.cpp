#include "../include/mindguard/static.hpp"

int main() {
  const char* pointer = "secret";
  MG_WITH_STRING(pointer, [](auto) {});
}

