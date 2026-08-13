#include "mindguard/detail/core.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace mindguard::detail {

extern "C" [[noreturn, gnu::visibility("hidden")]] void __mindguard_fail() noexcept {
  std::_Exit(134);
}

namespace {

#if defined(_MSC_VER)
#define MG_NOINLINE __declspec(noinline)
#else
#define MG_NOINLINE __attribute__((noinline))
#endif

#if defined(MINDGUARD_PROFILE_HARDENED)
#if !defined(MINDGUARD_OBFUSCATION_SEED_0) || !defined(MINDGUARD_OBFUSCATION_SEED_1) || \
    !defined(MINDGUARD_OBFUSCATION_SEED_2) || !defined(MINDGUARD_OBFUSCATION_SEED_3)
#error "MindGuard Hardened requires the per-build obfuscation seed"
#endif
extern "C" [[gnu::used, gnu::visibility("hidden")]] const std::uint64_t
    __mindguard_obfuscation_seed[4] = {
        MINDGUARD_OBFUSCATION_SEED_0, MINDGUARD_OBFUSCATION_SEED_1,
        MINDGUARD_OBFUSCATION_SEED_2, MINDGUARD_OBFUSCATION_SEED_3};
#endif

constexpr std::array<std::uint8_t, 8> kMagic = {'M', 'G', 'S', 'T', 'V', '1', 0, 0};
constexpr std::size_t kHeaderSize = 96;
constexpr std::size_t kMaxPlaintext = 64U * 1024U;
constexpr std::uint64_t kGolden = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t kMix = 0xd6e8feb86659fd93ULL;

void secure_wipe(std::span<std::uint8_t> bytes) noexcept {
  volatile std::uint8_t* current = bytes.data();
  for (std::size_t i = 0; i < bytes.size(); ++i) current[i] = 0;
}

void secure_wipe(std::span<std::uint64_t> words) noexcept {
  volatile std::uint64_t* current = words.data();
  for (std::size_t i = 0; i < words.size(); ++i) current[i] = 0;
}

struct scoped_material final {
  std::array<std::uint8_t, 32> bytes{};
  ~scoped_material() { secure_wipe(bytes); }
};

struct scoped_bytes final {
  explicit scoped_bytes(std::size_t size) : bytes(size) {}
  ~scoped_bytes() { secure_wipe(bytes); }
  std::vector<std::uint8_t> bytes;
};

std::uint16_t read_u16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset]) |
                                    static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8U * i);
  return value;
}

std::uint64_t read_u64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8U * i);
  return value;
}

std::array<std::uint64_t, 4> words(const std::array<std::uint8_t, 32>& material) noexcept {
  return {read_u64(material, 0), read_u64(material, 8), read_u64(material, 16), read_u64(material, 24)};
}

std::array<std::uint8_t, 32> arx_block(const std::array<std::uint8_t, 32>& material,
                                      std::uint64_t site, std::uint64_t diversifier,
                                      std::uint64_t counter) noexcept {
  constexpr std::array<std::array<unsigned, 4>, 4> rotations = {{
      {32, 24, 16, 63}, {31, 17, 47, 23}, {13, 37, 29, 43}, {7, 19, 41, 53}}};
  auto key = words(material);
  auto a = key[0] ^ site;
  auto b = key[1] ^ diversifier;
  auto c = key[2] ^ counter;
  auto d = key[3] ^ kGolden;
  for (std::uint64_t round = 0; round < 8; ++round) {
    const auto& r = rotations[static_cast<std::size_t>((diversifier ^ counter ^ round) & 3U)];
    a += b; d = std::rotl(d ^ a, static_cast<int>(r[0]));
    c += d; b = std::rotl(b ^ c, static_cast<int>(r[1]));
    a += b; d = std::rotl(d ^ a, static_cast<int>(r[2]));
    c += d; b = std::rotl(b ^ c, static_cast<int>(r[3]));
    a ^= kGolden + round;
    c ^= counter + round;
  }
  std::array<std::uint8_t, 32> out{};
  std::array values = {a, b, c, d};
  for (std::size_t i = 0; i < values.size(); ++i) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      out[i * 8U + shift / 8U] = static_cast<std::uint8_t>(values[i] >> shift);
    }
  }
  secure_wipe(values);
  secure_wipe(key);
  secure_wipe(std::span<std::uint64_t>(&a, 1));
  secure_wipe(std::span<std::uint64_t>(&b, 1));
  secure_wipe(std::span<std::uint64_t>(&c, 1));
  secure_wipe(std::span<std::uint64_t>(&d, 1));
  return out;
}

void crypt(std::span<std::uint8_t> data, const std::array<std::uint8_t, 32>& material,
           std::uint64_t site, std::uint64_t diversifier) noexcept {
  std::uint64_t counter = 0;
  while (!data.empty()) {
    auto stream = arx_block(material, site, diversifier, counter++);
    const std::size_t size = std::min(data.size(), stream.size());
    for (std::size_t i = 0; i < size; ++i) data[i] ^= stream[i];
    secure_wipe(stream);
    data = data.subspan(size);
  }
}

std::array<std::uint8_t, 16> compute_tag(
    std::span<const std::uint8_t> header, std::span<const std::uint8_t> payload,
    const std::array<std::uint8_t, 32>& material, std::uint64_t site,
    std::uint64_t diversifier) noexcept {
  auto key = words(material);
  auto t0 = key[0] ^ key[2] ^ site;
  auto t1 = key[1] ^ key[3] ^ diversifier;
  std::uint64_t index = 0;
  auto mix_bytes = [&](std::span<const std::uint8_t> bytes) {
    for (const auto byte : bytes) {
      t0 = std::rotl(t0 ^ (static_cast<std::uint64_t>(byte) + index * kGolden), 13) + t1;
      t1 = std::rotl(t1 + (static_cast<std::uint64_t>(byte) ^ index) + kMix, 29) ^ t0;
      ++index;
    }
  };
  mix_bytes(header.first(72));
  mix_bytes(header.subspan(88));
  mix_bytes(payload);
  for (std::uint64_t round = 0; round < 8; ++round) {
    t0 = std::rotl(t0 + t1 + round * kGolden, 17) ^ key[static_cast<std::size_t>(round & 3U)];
    t1 = std::rotl(t1 ^ t0, 41) + key[static_cast<std::size_t>((round + 1U) & 3U)];
  }
  std::array<std::uint8_t, 16> out{};
  for (unsigned shift = 0; shift < 64U; shift += 8U) {
    out[shift / 8U] = static_cast<std::uint8_t>(t0 >> shift);
    out[8U + shift / 8U] = static_cast<std::uint8_t>(t1 >> shift);
  }
  secure_wipe(key);
  secure_wipe(std::span<std::uint64_t>(&t0, 1));
  secure_wipe(std::span<std::uint64_t>(&t1, 1));
  return out;
}

bool constant_time_equal(std::span<const std::uint8_t> left,
                         std::span<const std::uint8_t> right) noexcept {
  if (left.size() != right.size()) return false;
  std::uint8_t difference = 0;
  for (std::size_t i = 0; i < left.size(); ++i) difference |= left[i] ^ right[i];
  return difference == 0;
}

std::size_t packed_position(std::size_t logical, std::uint64_t site,
                            std::uint64_t diversifier) noexcept {
  const auto odd = static_cast<std::uint8_t>(site ^ std::rotl(diversifier, 17)) | 1U;
  const auto offset = static_cast<std::uint8_t>(std::rotr(site, 11) ^ diversifier);
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(logical) * odd + offset);
}

std::uint8_t embedding_mask(std::uint64_t site, std::size_t index) noexcept {
  auto value = site ^ static_cast<std::uint64_t>(index) * kGolden;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return static_cast<std::uint8_t>(value ^ (value >> 31U));
}

std::size_t embedding_gcd(std::size_t left, std::size_t right) noexcept {
  while (right != 0) {
    const auto remainder = left % right;
    left = right;
    right = remainder;
  }
  return left;
}

std::size_t embedding_stride(std::uint64_t site, std::size_t size) noexcept {
  if (size <= 1) return 0;
  auto stride = (static_cast<std::size_t>(site) | 1U) % size;
  if (stride == 0) stride = 1;
  while (embedding_gcd(stride, size) != 1) {
    if (++stride == size) stride = 1;
  }
  return stride;
}

std::uint8_t embedding_next(std::uint8_t variant, std::uint8_t rolling,
                            std::uint8_t byte, std::size_t index) noexcept {
  switch (variant) {
    case 0:
      return static_cast<std::uint8_t>(std::rotl(rolling, 1) + byte +
                                       (static_cast<std::uint8_t>(index) ^ 0x5dU));
    case 1:
      return static_cast<std::uint8_t>(std::rotr(rolling, 1) + (byte ^ 0xa7U) -
                                       static_cast<std::uint8_t>(index));
    case 2:
      return static_cast<std::uint8_t>(std::rotl(rolling, 3) ^
                                       static_cast<std::uint8_t>(byte +
                                           (static_cast<std::uint8_t>(index) ^ 0x39U)));
    default:
      return static_cast<std::uint8_t>(std::rotr(rolling, 2) +
                                       (byte ^ static_cast<std::uint8_t>(index * 5U)) + 0x63U);
  }
}

MG_NOINLINE void output_junk_round(std::span<std::uint8_t> bytes,
                                   std::uint64_t seed) noexcept {
  auto state = seed ^ (static_cast<std::uint64_t>(bytes.size()) * kGolden);
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto mask = static_cast<std::uint8_t>(
        std::rotl(state ^ (static_cast<std::uint64_t>(index) * 0x94d049bb133111ebULL),
                  static_cast<int>((index & 7U) + 5U)));
    const auto input = bytes[index];
    bytes[index] = static_cast<std::uint8_t>(input ^ mask);
    state ^= static_cast<std::uint64_t>(input ^ bytes[index]) +
             static_cast<std::uint64_t>(index);
  }
}

}  // namespace

plaintext::~plaintext() { secure_wipe(bytes_); }

plaintext::plaintext(plaintext&& other) noexcept : bytes_(std::move(other.bytes_)) {}

plaintext& plaintext::operator=(plaintext&& other) noexcept {
  if (this != &other) {
    secure_wipe(bytes_);
    bytes_ = std::move(other.bytes_);
  }
  return *this;
}

MG_NOINLINE decode_error decode_share(std::span<const std::uint8_t> blob,
                                      std::span<const std::uint8_t> code_share,
                                      bool packed,
                                      std::uint64_t expected_site,
                                      std::uint8_t expected_kind,
                                      plaintext& out) noexcept {
  try {
    constexpr std::uint32_t kReset = 0x1dU;
    constexpr std::uint32_t kHeader = 0x53U;
    constexpr std::uint32_t kMaterial = 0x97U;
    constexpr std::uint32_t kTag = 0xc1U;
    constexpr std::uint32_t kRecover = 0xebU;
    const auto state_key = static_cast<std::uint32_t>(expected_site) ^
                           (static_cast<std::uint32_t>(expected_kind) << 19U);
    volatile std::uint32_t state = kReset ^ state_key;
    std::size_t plaintext_size = 0;
    std::size_t payload_size = 0;
    std::uint64_t diversifier = 0;
    scoped_material material;
    for (;;) {
      switch (state ^ state_key) {
        case kReset:
          out = plaintext{};
          if (code_share.size() != (packed ? 256U : 32U) || blob.size() < kHeaderSize) {
            return decode_error::bounds;
          }
          state = kHeader ^ state_key;
          continue;
        case kHeader:
          if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin())) return decode_error::magic;
          if (read_u16(blob, 12) != kHeaderSize || read_u16(blob, 14) != 0 ||
              std::any_of(blob.begin() + 88, blob.begin() + 96,
                          [](auto byte) { return byte != 0; })) {
            return decode_error::header;
          }
          if (read_u16(blob, 8) != 1) return decode_error::version;
          if (blob[10] != 1) return decode_error::profile;
          if (blob[11] != expected_kind || blob[11] < 1 || blob[11] > 3) {
            return decode_error::kind;
          }
          if (read_u64(blob, 16) != expected_site) return decode_error::site;
          plaintext_size = read_u32(blob, 24);
          payload_size = read_u32(blob, 28);
          if (plaintext_size > kMaxPlaintext || payload_size != plaintext_size ||
              payload_size > std::numeric_limits<std::size_t>::max() - kHeaderSize ||
              kHeaderSize + payload_size != blob.size()) {
            return decode_error::bounds;
          }
          diversifier = read_u64(blob, 32);
          state = kMaterial ^ state_key;
          continue;
        case kMaterial:
          for (std::size_t i = 0; i < material.bytes.size(); ++i) {
            std::uint8_t code_byte = 0;
            if (packed) {
              for (std::size_t lane = 0; lane < 8; ++lane) {
                code_byte ^=
                    code_share[packed_position(i + lane * 32U, expected_site, diversifier)];
              }
            } else {
              code_byte = code_share[i];
            }
            material.bytes[i] = blob[40 + i] ^ code_byte;
          }
          state = kTag ^ state_key;
          continue;
        case kTag: {
          auto computed = compute_tag(blob.first(kHeaderSize), blob.subspan(kHeaderSize),
                                      material.bytes, expected_site, diversifier);
          const bool valid = constant_time_equal(computed, blob.subspan(72, 16));
          secure_wipe(computed);
          if (!valid) return decode_error::tag;
          state = kRecover ^ state_key;
          continue;
        }
        case kRecover: {
          std::vector<std::uint8_t> recovered(blob.begin() + kHeaderSize, blob.end());
          crypt(recovered, material.bytes, expected_site, diversifier);
          const auto junk_seed = expected_site ^ std::rotl(diversifier, 23);
          output_junk_round(recovered, junk_seed);
          output_junk_round(recovered, junk_seed);
          plaintext replacement;
          replacement.bytes_ = std::move(recovered);
          out = std::move(replacement);
          return decode_error::success;
        }
        default:
          __mindguard_fail();
      }
    }
  } catch (...) {
    return decode_error::bounds;
  }
}

MG_NOINLINE void validate_watermark(std::uint64_t site, std::uint64_t marker,
                                    std::uint8_t variant) noexcept {
  auto expected = site ^ 0x6d696e6467756172ULL ^
                  static_cast<std::uint64_t>(variant) * kGolden;
  expected = (expected ^ (expected >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  expected = (expected ^ (expected >> 27U)) * 0x94d049bb133111ebULL;
  expected ^= expected >> 31U;
  if ((marker ^ expected) != 0) __mindguard_fail();
}

MG_NOINLINE decode_error decode_share_fast(std::span<const std::uint8_t> blob,
                                           std::span<const std::uint8_t> code_share,
                                           std::uint64_t expected_site,
                                           std::uint8_t expected_kind,
                                           plaintext& out) noexcept {
  try {
    out = plaintext{};
    if (code_share.size() != 256U || blob.size() < kHeaderSize) return decode_error::bounds;
    if (!std::equal(kMagic.begin(), kMagic.end(), blob.begin())) return decode_error::magic;
    if (read_u16(blob, 12) != kHeaderSize || read_u16(blob, 14) != 0 ||
        std::any_of(blob.begin() + 88, blob.begin() + 96,
                    [](auto byte) { return byte != 0; })) {
      return decode_error::header;
    }
    if (read_u16(blob, 8) != 1) return decode_error::version;
    if (blob[10] != 1) return decode_error::profile;
    if (blob[11] != expected_kind || blob[11] < 1 || blob[11] > 3) return decode_error::kind;
    if (read_u64(blob, 16) != expected_site) return decode_error::site;
    const std::size_t plaintext_size = read_u32(blob, 24);
    const std::size_t payload_size = read_u32(blob, 28);
    if (plaintext_size > kMaxPlaintext || payload_size != plaintext_size ||
        payload_size > std::numeric_limits<std::size_t>::max() - kHeaderSize ||
        kHeaderSize + payload_size != blob.size()) {
      return decode_error::bounds;
    }
    const auto diversifier = read_u64(blob, 32);
    scoped_material material;
    for (std::size_t index = 0; index < material.bytes.size(); ++index) {
      std::uint8_t code_byte = 0;
      for (std::size_t lane = 0; lane < 8; ++lane) {
        code_byte ^= code_share[packed_position(index + lane * 32U, expected_site, diversifier)];
      }
      material.bytes[index] = blob[40 + index] ^ code_byte;
    }
    auto computed = compute_tag(blob.first(kHeaderSize), blob.subspan(kHeaderSize),
                                material.bytes, expected_site, diversifier);
    const bool valid = constant_time_equal(computed, blob.subspan(72, 16));
    secure_wipe(computed);
    if (!valid) return decode_error::tag;
    std::vector<std::uint8_t> recovered(blob.begin() + kHeaderSize, blob.end());
    crypt(recovered, material.bytes, expected_site, diversifier);
    const auto junk_seed = expected_site ^ std::rotl(diversifier, 23);
    output_junk_round(recovered, junk_seed);
    output_junk_round(recovered, junk_seed);
    plaintext replacement;
    replacement.bytes_ = std::move(recovered);
    out = std::move(replacement);
    return decode_error::success;
  } catch (...) {
    return decode_error::bounds;
  }
}

decode_error decode(std::span<const std::uint8_t> blob,
                    const std::array<std::uint8_t, 32>& code_share,
                    std::uint64_t expected_site, std::uint8_t expected_kind,
                    plaintext& out) noexcept {
  return decode_share(blob, code_share, false, expected_site, expected_kind, out);
}

decode_error decode_packed(std::span<const std::uint8_t> blob,
                           const std::array<std::uint8_t, 256>& code_share,
                           std::uint64_t expected_site, std::uint8_t expected_kind,
                           plaintext& out) noexcept {
  return decode_share(blob, code_share, true, expected_site, expected_kind, out);
}

template <std::uint8_t Variant>
decode_error decode_embedded_variant(std::span<const std::uint8_t> encoded,
                                     std::size_t blob_size,
                                     std::uint64_t expected_site,
                                     std::uint8_t expected_kind,
                                     plaintext& out) noexcept {
  try {
    out = plaintext{};
    if (blob_size < kHeaderSize || blob_size > encoded.size() ||
        encoded.size() - blob_size != 256U) {
      return decode_error::bounds;
    }
    scoped_bytes material(encoded.size());
    auto rolling = static_cast<std::uint8_t>(
        expected_site ^ 0x37f2a1c96d8405beULL ^ static_cast<std::uint64_t>(Variant) * 0x4bU);
    const auto stride = embedding_stride(expected_site, encoded.size());
    const bool reverse = (Variant & 1U) != 0;
    const auto first_index = reverse ? encoded.size() - 1U : 0U;
    auto position = (first_index * stride +
                     static_cast<std::size_t>(std::rotr(expected_site, 17)) % encoded.size()) %
                    encoded.size();
    for (std::size_t step = 0; step < encoded.size(); ++step) {
      const auto index = reverse ? encoded.size() - 1U - step : step;
      const auto byte = static_cast<std::uint8_t>(
          encoded[position] ^
          embedding_mask(expected_site, index) ^ rolling);
      material.bytes[index] = byte;
      rolling = embedding_next(Variant, rolling, byte, index);
      if (reverse) {
        position = position >= stride ? position - stride : position + encoded.size() - stride;
      } else {
        position += stride;
        if (position >= encoded.size()) position -= encoded.size();
      }
    }
    return decode_share_fast(std::span<const std::uint8_t>(material.bytes).first(blob_size),
                             std::span<const std::uint8_t>(material.bytes).subspan(blob_size),
                             expected_site, expected_kind, out);
  } catch (...) {
    return decode_error::bounds;
  }
}

#define MG_DEFINE_EMBEDDED_DECODER(variant)                                      \
  MG_NOINLINE void materialize_embedded_##variant(                               \
      std::span<const std::uint8_t> encoded, std::size_t blob_size,               \
      std::uint64_t expected_site, std::uint8_t expected_kind, plaintext& out) noexcept { \
    if (decode_embedded_variant<variant>(encoded, blob_size, expected_site,       \
                                          expected_kind, out) != decode_error::success) { \
      __mindguard_fail();                                                         \
    }                                                                             \
  }
MG_DEFINE_EMBEDDED_DECODER(0)
MG_DEFINE_EMBEDDED_DECODER(1)
MG_DEFINE_EMBEDDED_DECODER(2)
MG_DEFINE_EMBEDDED_DECODER(3)
#undef MG_DEFINE_EMBEDDED_DECODER

}  // namespace mindguard::detail
