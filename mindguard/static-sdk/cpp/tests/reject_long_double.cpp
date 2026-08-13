#include "../include/mindguard/static.hpp"

int main() {
  MG_WITH_VALUE(1.0L, [](long double) {});
}

