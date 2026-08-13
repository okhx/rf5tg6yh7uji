#include "../include/mindguard/static.hpp"

constexpr char32_t oversized[16'386]{};

int main() {
  mindguard::detail::with_string(
      oversized, [](auto) {}, mindguard::detail::make_site(std::source_location::current(), 0));
}

