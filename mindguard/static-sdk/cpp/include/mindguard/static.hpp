#pragma once

#if defined(MINDGUARD_PROFILE_HARDENED)

#if !defined(MINDGUARD_GENERATED_HEADER)
#error "MindGuard Hardened requires MINDGUARD_GENERATED_HEADER"
#endif

#include "mindguard/detail/hardened.hpp"

#else

#if !defined(MINDGUARD_PROFILE_DEV)
#error "MindGuard profile is required; use the CMake integration"
#endif

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <limits>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "mindguard/detail/literal.hpp"

namespace mindguard {

inline constexpr std::size_t max_plaintext_bytes = 64U * 1024U;

struct site_identity final {
  std::uint64_t hash;
};

template <class T>
concept supported_scalar =
    std::is_enum_v<T> || std::is_integral_v<T> ||
    ((std::same_as<T, float> || std::same_as<T, double>) &&
     std::numeric_limits<T>::is_iec559);

template <class T>
class protected_value;

namespace detail {
template <class To, class T>
constexpr To value_as(const protected_value<T>& value);

[[nodiscard]] consteval bool direct_character_literal(std::string_view token) {
  std::size_t i = token.starts_with("u8") ? 2U
                  : (!token.empty() && (token[0] == 'u' || token[0] == 'U' || token[0] == 'L'))
                      ? 1U
                      : 0U;
  if (i >= token.size() || token[i] != '\'') return false;
  for (++i; i < token.size(); ++i) {
    if (token[i] == '\\') {
      if (++i == token.size()) return false;
    } else if (token[i] == '\'') {
      return i + 1U == token.size();
    }
  }
  return false;
}

[[nodiscard]] consteval bool direct_numeric_literal(std::string_view token) {
  std::size_t i = 0;
  if (!token.empty() && (token[0] == '+' || token[0] == '-')) {
    i = 1;
    while (i < token.size() && (token[i] == ' ' || token[i] == '\t')) ++i;
  }
  if (i == token.size() || (!((token[i] >= '0' && token[i] <= '9')) && token[i] != '.')) {
    return false;
  }
  bool digit = false;
  for (; i < token.size(); ++i) {
    const char c = token[i];
    if (c >= '0' && c <= '9') {
      digit = true;
      continue;
    }
    const bool allowed_letter =
        (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == 'x' || c == 'X' ||
        c == 'p' || c == 'P' || c == 'u' || c == 'U' || c == 'l' || c == 'L' ||
        c == 'z' || c == 'Z';
    if (allowed_letter || c == '.' || c == '\'') continue;
    if ((c == '+' || c == '-') && i != 0U &&
        (token[i - 1U] == 'e' || token[i - 1U] == 'E' || token[i - 1U] == 'p' || token[i - 1U] == 'P')) {
      continue;
    }
    return false;
  }
  return digit;
}

[[nodiscard]] consteval bool direct_enum_literal(std::string_view token) {
  std::size_t i = token.starts_with("::") ? 2U : 0U;
  unsigned parts = 0;
  while (i < token.size()) {
    const char first = token[i];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) return false;
    ++i;
    while (i < token.size()) {
      const char c = token[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
        ++i;
      } else {
        break;
      }
    }
    ++parts;
    if (i == token.size()) break;
    if (i + 1U >= token.size() || token[i] != ':' || token[i + 1U] != ':') return false;
    i += 2U;
  }
  return parts >= 2U;
}

template <fixed_string Token>
consteval void require_value_literal() {
  constexpr auto token = Token.view();
  static_assert(token == "true" || token == "false" || direct_character_literal(token) ||
                    direct_numeric_literal(token),
                "MindGuard requires a direct scalar literal");
}

template <fixed_string Token>
consteval void require_enum_literal() {
  static_assert(direct_enum_literal(Token.view()), "MindGuard requires a direct qualified enumerator");
}

[[nodiscard]] consteval site_identity make_site(std::source_location location, std::uint32_t ordinal) {
  const std::string_view source = location.file_name();
  if (!(source.ends_with(".cpp") || source.ends_with(".cc") || source.ends_with(".cxx"))) {
    throw "MindGuard protected sites are allowed only in target C++ source files";
  }
  std::uint64_t hash = 1469598103934665603ULL;
  auto mix = [&hash](std::string_view value) {
    for (const unsigned char byte : value) {
      hash = (hash ^ byte) * 1099511628211ULL;
    }
  };
  mix(MINDGUARD_TARGET_ID);
  mix(source);
  for (const std::uint32_t value : {static_cast<std::uint32_t>(location.line()),
                                    static_cast<std::uint32_t>(location.column()), ordinal}) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      hash = (hash ^ static_cast<std::uint8_t>(value >> shift)) * 1099511628211ULL;
    }
  }
  return {hash};
}
}

template <class Char>
class protected_string final {
 public:
  template <std::size_t N>
  explicit constexpr protected_string(const Char (&value)[N], site_identity site) noexcept
      : value_(value, N - 1U), site_(site) {
    static_assert((N - 1U) * sizeof(Char) <= max_plaintext_bytes,
                  "MindGuard literal exceeds 64 KiB");
  }

  protected_string(const protected_string&) = delete;
  protected_string& operator=(const protected_string&) = delete;
  protected_string(protected_string&&) noexcept = default;
  protected_string& operator=(protected_string&&) noexcept = default;

  [[nodiscard]] constexpr const Char* data() const noexcept { return value_.data(); }
  [[nodiscard]] constexpr std::size_t size() const noexcept { return value_.size(); }
  [[nodiscard]] constexpr std::basic_string_view<Char> view() const noexcept { return value_; }

 private:
  std::basic_string_view<Char> value_;
  site_identity site_;
};

template <class T>
class protected_value final {
  static_assert(supported_scalar<T>, "unsupported MindGuard scalar type");

 public:
  explicit constexpr protected_value(T value, site_identity site) noexcept : value_(value), site_(site) {}
  protected_value(const protected_value&) = delete;
  protected_value& operator=(const protected_value&) = delete;
  protected_value(protected_value&&) noexcept = default;
  protected_value& operator=(protected_value&&) noexcept = default;

 private:
  template <class To, class From>
  friend constexpr To detail::value_as(const protected_value<From>& value);

  T value_;
  site_identity site_;
};

namespace detail {

template <class Char, std::size_t N, class Callback>
constexpr decltype(auto) with_string(const Char (&value)[N], Callback&& callback, site_identity) {
  static_assert((N - 1U) * sizeof(Char) <= max_plaintext_bytes,
                "MindGuard literal exceeds 64 KiB");
  return std::forward<Callback>(callback)(std::basic_string_view<Char>(value, N - 1U));
}

template <class T, class Callback>
  requires supported_scalar<T>
constexpr decltype(auto) with_value(T value, Callback&& callback, site_identity site) {
  return std::forward<Callback>(callback)(protected_value<T>(value, site));
}

template <class To, class T>
constexpr To value_as(const protected_value<T>& value) {
  static_assert(supported_scalar<To>, "unsupported MindGuard scalar type");
  return static_cast<To>(value.value_);
}

template <class Char, std::size_t N>
constexpr auto make_string(const Char (&value)[N], site_identity site) {
  return protected_string<Char>(value, site);
}

template <class T>
  requires supported_scalar<T>
constexpr auto make_value(T value, site_identity site) {
  return protected_value<T>(value, site);
}

}  // namespace detail
}  // namespace mindguard

#define MG_SITE_ID() ::mindguard::detail::make_site(std::source_location::current(), __COUNTER__)
#define MG_WITH_STRING(literal, callback)                                                   \
  (::mindguard::detail::require_string_literal<#literal>(),                                \
   ::mindguard::detail::with_string((literal), (callback), MG_SITE_ID()))
#define MG_WITH_BYTES(literal, callback)                                                    \
  (::mindguard::detail::require_string_literal<#literal>(),                                \
   ::mindguard::detail::with_string((literal), (callback), MG_SITE_ID()))
#define MG_WITH_VALUE(literal, callback)                                                    \
  (::mindguard::detail::require_value_literal<#literal>(),                                 \
   ::mindguard::detail::with_value([]() consteval { return (literal); }(), (callback), MG_SITE_ID()))
#define MG_WITH_ENUM(enumerator, callback)                                                  \
  (::mindguard::detail::require_enum_literal<#enumerator>(),                               \
   ::mindguard::detail::with_value([]() consteval { return (enumerator); }(), (callback), MG_SITE_ID()))
#define MG_VALUE_AS(type, value) ::mindguard::detail::value_as<type>((value))
#define MG_STRING(literal)                                                                 \
  (::mindguard::detail::require_string_literal<#literal>(),                                \
   ::mindguard::detail::make_string((literal), MG_SITE_ID()))
#define MG_BYTES(literal)                                                                  \
  (::mindguard::detail::require_string_literal<#literal>(),                                \
   ::mindguard::detail::make_string((literal), MG_SITE_ID()))
#define MG_VALUE(literal)                                                                  \
  (::mindguard::detail::require_value_literal<#literal>(),                                 \
   ::mindguard::detail::make_value([]() consteval { return (literal); }(), MG_SITE_ID()))
#define MG_ENUM(enumerator)                                                                \
  (::mindguard::detail::require_enum_literal<#enumerator>(),                               \
   ::mindguard::detail::make_value([]() consteval { return (enumerator); }(), MG_SITE_ID()))

#endif
