#include "mindguard/detail/core.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
  if (argc != 6) return 64;
  auto blob = read_file(argv[1]);
  const auto share_bytes = read_file(argv[2]);
  if (share_bytes.size() != 32 && share_bytes.size() != 256) return 65;
  std::array<std::uint8_t, 32> raw_share{};
  std::array<std::uint8_t, 256> packed_share{};
  if (share_bytes.size() == raw_share.size()) {
    std::copy(share_bytes.begin(), share_bytes.end(), raw_share.begin());
  } else {
    std::copy(share_bytes.begin(), share_bytes.end(), packed_share.begin());
  }
  std::string site_text(argv[3]);
  if (site_text.starts_with("0x")) site_text.erase(0, 2);
  std::uint64_t site = 0;
  const auto site_result = std::from_chars(site_text.data(), site_text.data() + site_text.size(), site, 16);
  if (site_result.ec != std::errc{} || site_result.ptr != site_text.data() + site_text.size()) return 66;
  unsigned kind = 0;
  const std::string kind_text(argv[4]);
  const auto kind_result = std::from_chars(kind_text.data(), kind_text.data() + kind_text.size(), kind);
  if (kind_result.ec != std::errc{} || kind_result.ptr != kind_text.data() + kind_text.size() || kind > 255) return 67;
  mindguard::detail::plaintext plaintext;
  const auto run_decode = [&](std::span<const std::uint8_t> input,
                              mindguard::detail::plaintext& output) {
    if (share_bytes.size() == 32) {
      return mindguard::detail::decode(input, raw_share, site, static_cast<std::uint8_t>(kind), output);
    }
    return mindguard::detail::decode_packed(input, packed_share, site, static_cast<std::uint8_t>(kind), output);
  };
  const std::array<std::uint8_t, 31> invalid_share{};
  if (mindguard::detail::decode_share(blob, invalid_share, false, site,
                                      static_cast<std::uint8_t>(kind), plaintext) !=
          mindguard::detail::decode_error::bounds ||
      !plaintext.bytes().empty()) {
    return 72;
  }
  if (std::string_view(argv[5]) == "--bench") {
    constexpr std::uint64_t iterations = 1000;
    std::uint64_t checksum = 0;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < iterations; ++i) {
      mindguard::detail::plaintext sample;
      if (run_decode(blob, sample) != mindguard::detail::decode_error::success) return 70;
      checksum += sample.bytes()[static_cast<std::size_t>(i) % sample.bytes().size()];
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::cout << elapsed / static_cast<std::int64_t>(iterations) << ' ' << checksum << '\n';
    return 0;
  }
  const auto expected = read_file(argv[5]);
  const auto error = run_decode(blob, plaintext);
  if (error != mindguard::detail::decode_error::success) {
    constexpr std::array names = {"Success", "Bounds", "Magic", "Header", "Version",
                                  "Profile", "Kind", "Site", "Tag"};
    std::cout << names[static_cast<std::size_t>(error)] << '\n';
    return 1;
  }
  if (!std::equal(plaintext.bytes().begin(), plaintext.bytes().end(), expected.begin(), expected.end())) return 68;
  blob.back() ^= 1;
  if (run_decode(blob, plaintext) == mindguard::detail::decode_error::success ||
      !plaintext.bytes().empty()) {
    return 69;
  }
  std::cout << "OK\n";
}
