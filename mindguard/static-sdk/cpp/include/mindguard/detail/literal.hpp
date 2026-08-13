#pragma once

#include <cstddef>
#include <string_view>

namespace mindguard::detail {

template <std::size_t N>
struct fixed_string final {
  char bytes[N]{};

  consteval fixed_string(const char (&value)[N]) {
    for (std::size_t i = 0; i < N; ++i) bytes[i] = value[i];
  }

  [[nodiscard]] constexpr std::string_view view() const noexcept { return {bytes, N - 1U}; }
};

[[nodiscard]] consteval bool direct_string_literal(std::string_view token) {
  std::size_t i = token.starts_with("u8") ? 2U
                  : (!token.empty() && (token[0] == 'u' || token[0] == 'U' || token[0] == 'L'))
                      ? 1U
                      : 0U;
  if (i >= token.size()) return false;
  if (token[i] == '"') {
    for (++i; i < token.size(); ++i) {
      if (token[i] == '\\') {
        if (++i == token.size()) return false;
      } else if (token[i] == '"') {
        return i + 1U == token.size();
      }
    }
    return false;
  }
  if (token[i] != 'R' || i + 1U >= token.size() || token[i + 1U] != '"') return false;
  const std::size_t delimiter = i + 2U;
  std::size_t open = delimiter;
  while (open < token.size() && token[open] != '(') {
    const char c = token[open];
    if (open - delimiter == 16U || c == ')' || c == '\\' || c == ' ' || c == '\t') return false;
    ++open;
  }
  if (open == token.size()) return false;
  const std::size_t delimiter_size = open - delimiter;
  if (token.size() < delimiter_size + 2U) return false;
  const std::size_t close = token.size() - delimiter_size - 2U;
  if (close <= open || token[close] != ')' || token.back() != '"') return false;
  for (std::size_t j = 0; j < delimiter_size; ++j) {
    if (token[delimiter + j] != token[close + 1U + j]) return false;
  }
  return true;
}

template <fixed_string Token>
consteval void require_string_literal() {
  static_assert(direct_string_literal(Token.view()), "MindGuard requires a direct string literal");
}

}  // namespace mindguard::detail
