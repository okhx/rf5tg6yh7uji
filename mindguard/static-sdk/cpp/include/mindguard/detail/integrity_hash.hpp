#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace mindguard::detail {

inline constexpr std::array<std::uint64_t, 2> integrity_seal_marker = {
    0x4d475345414c3031ULL, 0xb6b8acba9eb3cfceULL};

[[nodiscard]] inline std::array<std::uint64_t, 2> integrity_hash(
    std::span<const std::uint8_t> bytes) noexcept {
  std::uint64_t first = 0x243f6a8885a308d3ULL ^ bytes.size();
  std::uint64_t second = 0x13198a2e03707344ULL + bytes.size();
  std::size_t offset = 0;
  while (offset + 8U <= bytes.size()) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes.data() + offset, sizeof(word));
    first = std::rotl(first ^ word, 27) * 0x9e3779b185ebca87ULL + second;
    second = std::rotl(second + word * 0xc2b2ae3d27d4eb4fULL, 31) ^ first;
    offset += 8U;
  }
  std::uint64_t tail = 0;
  std::memcpy(&tail, bytes.data() + offset, bytes.size() - offset);
  first ^= tail ^ (static_cast<std::uint64_t>(bytes.size()) << 32U);
  second += std::rotl(tail, 19);
  first ^= first >> 30U;
  first *= 0xbf58476d1ce4e5b9ULL;
  second ^= second >> 27U;
  second *= 0x94d049bb133111ebULL;
  return {first ^ (first >> 31U) ^ second, second ^ (second >> 33U) ^ first};
}

}  // namespace mindguard::detail
