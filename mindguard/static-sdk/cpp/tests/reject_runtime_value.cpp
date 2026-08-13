#include "../include/mindguard/static.hpp"

int main() {
  int runtime_value = 7;
  MG_WITH_VALUE(runtime_value, [](int) {});
}

