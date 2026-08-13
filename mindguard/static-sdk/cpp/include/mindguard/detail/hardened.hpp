#pragma once

#include "mindguard/detail/core.hpp"
#include "mindguard/detail/literal.hpp"

#include MINDGUARD_GENERATED_HEADER

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#if !defined(MINDGUARD_OBFUSCATION_SEED_0) || !defined(MINDGUARD_OBFUSCATION_SEED_1) || \
    !defined(MINDGUARD_OBFUSCATION_SEED_2) || !defined(MINDGUARD_OBFUSCATION_SEED_3)
#error "MindGuard Hardened requires the CMake per-build obfuscation seed"
#endif

extern "C" [[gnu::used, gnu::visibility("hidden")]] inline const std::uint64_t
    __mindguard_tu_obfuscation_seed[4] = {
        MINDGUARD_OBFUSCATION_SEED_0, MINDGUARD_OBFUSCATION_SEED_1,
        MINDGUARD_OBFUSCATION_SEED_2, MINDGUARD_OBFUSCATION_SEED_3};

#define MINDGUARD_NOINLINE __attribute__((noinline))

namespace mindguard {

template <class T>
class protected_value final {
 public:
  explicit protected_value(T value) noexcept : value_(value) {}
  ~protected_value() {
    volatile unsigned char* byte = reinterpret_cast<volatile unsigned char*>(&value_);
    for (std::size_t i = 0; i < sizeof(value_); ++i) byte[i] = 0;
  }
  protected_value(const protected_value&) = delete;
  protected_value& operator=(const protected_value&) = delete;
  protected_value(protected_value&&) = delete;
  protected_value& operator=(protected_value&&) = delete;

 private:
  template <class To, class From>
  friend To value_as(const protected_value<From>&);
  T value_;
};

template <class To, class T>
To value_as(const protected_value<T>& value) {
  static_assert(std::integral<To> || ((std::same_as<To, float> || std::same_as<To, double>) &&
                                     std::numeric_limits<To>::is_iec559),
                "MindGuard Hardened scalar cast type is unsupported");
  return static_cast<To>(value.value_);
}

}  // namespace mindguard

namespace mindguard::detail {

consteval std::uint64_t site_watermark_value(std::uint64_t site,
                                             std::uint8_t variant) {
  auto value = site ^ 0x6d696e6467756172ULL ^
               static_cast<std::uint64_t>(variant) * 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

template <class Generated>
[[gnu::used, gnu::visibility("hidden")]] inline const volatile std::uint64_t
    site_watermark = site_watermark_value(
        Generated::id, static_cast<std::uint8_t>(Generated::id >> 62U));

template <class Generated>
MINDGUARD_NOINLINE void materialize_generated(plaintext& out) noexcept {
  constexpr auto variant = static_cast<std::uint8_t>(Generated::id >> 62U);
  validate_watermark(Generated::id, site_watermark<Generated>, variant);
  if constexpr (variant == 0) {
    materialize_embedded_0(Generated::material, Generated::blob_size,
                           Generated::id, Generated::kind, out);
  } else if constexpr (variant == 1) {
    materialize_embedded_1(Generated::material, Generated::blob_size,
                           Generated::id, Generated::kind, out);
  } else if constexpr (variant == 2) {
    materialize_embedded_2(Generated::material, Generated::blob_size,
                           Generated::id, Generated::kind, out);
  } else {
    materialize_embedded_3(Generated::material, Generated::blob_size,
                           Generated::id, Generated::kind, out);
  }
}

template <std::uint32_t Line, class Callback>
MINDGUARD_NOINLINE void with_generated_bytes(Callback&& callback) {
  using generated = mindguard::generated::site<Line>;
  static_assert(generated::kind == 1 || generated::kind == 2, "MindGuard generated site kind mismatch");
  using result_type = std::invoke_result_t<Callback, std::span<const std::uint8_t>>;
  static_assert(std::is_void_v<result_type>, "MindGuard Hardened callback must return void");
  constexpr auto key = static_cast<std::uint32_t>(generated::id ^ MINDGUARD_OBFUSCATION_SEED_0);
  constexpr std::uint32_t kDecode = 0x29U;
  constexpr std::uint32_t kCallback = 0xd7U;
  volatile std::uint32_t state = kDecode ^ key;
  plaintext value;
  const auto decode_timing = hardened_runtime_enter(generated::id);
  for (;;) {
    switch (state ^ key) {
      case kDecode:
        materialize_generated<generated>(value);
        state = kCallback ^ key;
        continue;
      case kCallback:
        hardened_runtime_leave(decode_timing, generated::id);
        {
          const auto callback_timing = hardened_callback_enter(generated::id ^ 0x03414c4c4241434bULL);
        std::invoke(std::forward<Callback>(callback), value.bytes());
          hardened_runtime_leave(callback_timing, generated::id ^ 0x03414c4c4241434bULL);
        }
        return;
      default:
        __mindguard_fail();
    }
  }
}

template <std::uint32_t Line, class Callback>
MINDGUARD_NOINLINE void with_generated_string(Callback&& callback) {
  with_generated_bytes<Line>([&callback](std::span<const std::uint8_t> value) {
    std::invoke(std::forward<Callback>(callback),
                std::string_view(reinterpret_cast<const char*>(value.data()), value.size()));
  });
}

template <class T>
MINDGUARD_NOINLINE T parse_generated_scalar(std::span<const std::uint8_t> bytes) {
  static_assert(std::integral<T> || ((std::same_as<T, float> || std::same_as<T, double>) &&
                                    std::numeric_limits<T>::is_iec559),
                "MindGuard Hardened scalar type is unsupported");
  if constexpr (std::same_as<T, bool>) {
    if (bytes.size() == 4 && std::equal(bytes.begin(), bytes.end(), "true")) return true;
    if (bytes.size() == 5 && std::equal(bytes.begin(), bytes.end(), "false")) return false;
    __mindguard_fail();
  } else if constexpr (std::same_as<T, char>) {
    unsigned value{};
    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const auto [last, error] = std::from_chars(first, first + bytes.size(), value, 10);
    if (error != std::errc{} || last != first + bytes.size() || value > 0x7f) __mindguard_fail();
    return static_cast<char>(value);
  } else if constexpr (std::same_as<T, float> || std::same_as<T, double>) {
    T value{};
    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const auto [last, error] =
        std::from_chars(first, first + bytes.size(), value, std::chars_format::general);
    if (error != std::errc{} || last != first + bytes.size()) __mindguard_fail();
    return value;
  } else {
    T value{};
    const auto* first = reinterpret_cast<const char*>(bytes.data());
    const auto [last, error] = std::from_chars(first, first + bytes.size(), value, 10);
    if (error != std::errc{} || last != first + bytes.size()) __mindguard_fail();
    return value;
  }
}

template <std::uint32_t Line, class T, class Callback>
MINDGUARD_NOINLINE void with_generated_value(Callback&& callback) {
  using generated = mindguard::generated::site<Line>;
  static_assert(generated::kind == 3, "MindGuard generated site kind mismatch");
  plaintext plaintext_value;
  using result_type = std::invoke_result_t<Callback, mindguard::protected_value<T>>;
  static_assert(std::is_void_v<result_type>, "MindGuard Hardened callback must return void");
  constexpr auto key = static_cast<std::uint32_t>(generated::id ^ MINDGUARD_OBFUSCATION_SEED_1);
  constexpr std::uint32_t kDecode = 0x3bU;
  constexpr std::uint32_t kParse = 0x85U;
  constexpr std::uint32_t kCallback = 0xe9U;
  volatile std::uint32_t state = kDecode ^ key;
  T value{};
  const auto decode_timing = hardened_runtime_enter(generated::id);
  for (;;) {
    switch (state ^ key) {
      case kDecode:
        materialize_generated<generated>(plaintext_value);
        state = kParse ^ key;
        continue;
      case kParse:
        value = parse_generated_scalar<T>(plaintext_value.bytes());
        state = kCallback ^ key;
        continue;
      case kCallback:
        hardened_runtime_leave(decode_timing, generated::id);
        {
          const auto callback_timing = hardened_callback_enter(generated::id ^ 0x03414c4c4241434bULL);
        std::invoke(std::forward<Callback>(callback), mindguard::protected_value<T>(value));
          hardened_runtime_leave(callback_timing, generated::id ^ 0x03414c4c4241434bULL);
        }
        return;
      default:
        __mindguard_fail();
    }
  }
}

}  // namespace mindguard::detail

#undef MINDGUARD_NOINLINE

#define MG_WITH_STRING(literal, callback)                                                    \
  (::mindguard::detail::require_string_literal<#literal>(),                                 \
   ::mindguard::detail::with_generated_string<static_cast<std::uint32_t>(__LINE__)>(callback))
#define MG_WITH_BYTES(literal, callback)                                                     \
  (::mindguard::detail::require_string_literal<#literal>(),                                 \
   ::mindguard::detail::with_generated_bytes<static_cast<std::uint32_t>(__LINE__)>(callback))

#define MG_WITH_VALUE(literal, callback)                                                    \
  ::mindguard::detail::with_generated_value<static_cast<std::uint32_t>(__LINE__),           \
                                            decltype(literal)>(callback)
#define MG_WITH_ENUM(...) static_assert(false, "MindGuard Hardened enum generation is not implemented")
#define MG_VALUE_AS(type, value) ::mindguard::value_as<type>((value))
#define MG_STRING(...) static_assert(false, "MindGuard Hardened requires scoped callback API")
#define MG_BYTES(...) static_assert(false, "MindGuard Hardened requires scoped callback API")
#define MG_VALUE(...) static_assert(false, "MindGuard Hardened scalar RAII is not implemented")
#define MG_ENUM(...) static_assert(false, "MindGuard Hardened enum generation is not implemented")
