#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>

namespace mindguard::detail {

extern "C" [[noreturn]] void __mindguard_fail() noexcept;
[[nodiscard]] std::uint64_t hardened_runtime_enter(std::uint64_t site) noexcept;
[[nodiscard]] std::uint64_t hardened_callback_enter(std::uint64_t site) noexcept;
void hardened_runtime_leave(std::uint64_t token, std::uint64_t site) noexcept;
void validate_watermark(std::uint64_t site, std::uint64_t marker,
                        std::uint8_t variant) noexcept;

enum class decode_error : std::uint8_t {
  success,
  bounds,
  magic,
  header,
  version,
  profile,
  kind,
  site,
  tag,
};

class plaintext final {
 public:
  plaintext() = default;
  ~plaintext();
  plaintext(const plaintext&) = delete;
  plaintext& operator=(const plaintext&) = delete;
  plaintext(plaintext&& other) noexcept;
  plaintext& operator=(plaintext&& other) noexcept;

  [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

 private:
  friend decode_error decode(std::span<const std::uint8_t>,
                             const std::array<std::uint8_t, 32>&,
                             std::uint64_t, std::uint8_t, plaintext&) noexcept;
  friend decode_error decode_packed(std::span<const std::uint8_t>,
                                    const std::array<std::uint8_t, 256>&,
                                    std::uint64_t, std::uint8_t, plaintext&) noexcept;
  friend decode_error decode_share(std::span<const std::uint8_t>,
                                   std::span<const std::uint8_t>, bool,
                                   std::uint64_t, std::uint8_t, plaintext&) noexcept;
  friend decode_error decode_share_fast(std::span<const std::uint8_t>,
                                        std::span<const std::uint8_t>,
                                        std::uint64_t, std::uint8_t, plaintext&) noexcept;
  std::vector<std::uint8_t> bytes_;
};

[[nodiscard]] decode_error decode_share(
    std::span<const std::uint8_t> blob,
    std::span<const std::uint8_t> code_share,
    bool packed,
    std::uint64_t expected_site,
    std::uint8_t expected_kind,
    plaintext& out) noexcept;

[[nodiscard]] decode_error decode(
    std::span<const std::uint8_t> blob,
    const std::array<std::uint8_t, 32>& code_share,
    std::uint64_t expected_site,
    std::uint8_t expected_kind,
    plaintext& out) noexcept;

[[nodiscard]] decode_error decode_packed(
    std::span<const std::uint8_t> blob,
    const std::array<std::uint8_t, 256>& code_share,
    std::uint64_t expected_site,
    std::uint8_t expected_kind,
    plaintext& out) noexcept;

#define MG_DECLARE_EMBEDDED_DECODER(variant)                       \
  void materialize_embedded_##variant(                             \
      std::span<const std::uint8_t> material, std::size_t blob_size, \
      std::uint64_t expected_site, std::uint8_t expected_kind,      \
      plaintext& out) noexcept
MG_DECLARE_EMBEDDED_DECODER(0);
MG_DECLARE_EMBEDDED_DECODER(1);
MG_DECLARE_EMBEDDED_DECODER(2);
MG_DECLARE_EMBEDDED_DECODER(3);
#undef MG_DECLARE_EMBEDDED_DECODER

template <class Callback>
decode_error with_decoded(std::span<const std::uint8_t> blob,
                          const std::array<std::uint8_t, 32>& code_share,
                          std::uint64_t expected_site, std::uint8_t expected_kind,
                          Callback&& callback) {
  plaintext value;
  const auto error = decode(blob, code_share, expected_site, expected_kind, value);
  if (error == decode_error::success) std::invoke(std::forward<Callback>(callback), value.bytes());
  return error;
}

template <class Callback>
decode_error with_decoded_packed(std::span<const std::uint8_t> blob,
                                 const std::array<std::uint8_t, 256>& code_share,
                                 std::uint64_t expected_site, std::uint8_t expected_kind,
                                 Callback&& callback) {
  plaintext value;
  const auto error = decode_packed(blob, code_share, expected_site, expected_kind, value);
  if (error == decode_error::success) std::invoke(std::forward<Callback>(callback), value.bytes());
  return error;
}

}  // namespace mindguard::detail
