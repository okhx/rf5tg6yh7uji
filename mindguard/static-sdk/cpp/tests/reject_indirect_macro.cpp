#include "../include/mindguard/static.hpp"

#define SECRET_LITERAL "secret"

int main() {
  MG_WITH_STRING(SECRET_LITERAL, [](auto) {});
}

