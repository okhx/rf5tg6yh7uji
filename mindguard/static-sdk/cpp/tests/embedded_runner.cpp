#include "mindguard/detail/core.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

extern "C" const std::uint8_t _binary_blob_start[];
extern "C" const std::uint8_t _binary_blob_end[];
extern "C" const std::uint8_t _binary_share_start[];
extern "C" const std::uint8_t _binary_share_end[];

int main() {
  const std::span blob(_binary_blob_start,
                       static_cast<std::size_t>(_binary_blob_end - _binary_blob_start));
  const std::span share_bytes(
      _binary_share_start, static_cast<std::size_t>(_binary_share_end - _binary_share_start));
  if (share_bytes.size() != 256) return 64;
  std::array<std::uint8_t, 256> share{};
  std::copy(share_bytes.begin(), share_bytes.end(), share.begin());
  bool written = false;
  const auto error = mindguard::detail::with_decoded_packed(
      blob, share, 0x0123456789abcdefULL, 2, [&](std::span<const std::uint8_t> plaintext) {
        written = std::fwrite(plaintext.data(), 1, plaintext.size(), stdout) == plaintext.size();
      });
  if (error != mindguard::detail::decode_error::success) {
    return 1;
  }
  return written ? 0 : 2;
}
