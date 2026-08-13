#include "mindguard_runtime/runtime.hpp"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using mindguard_runtime::VerifyError;
using Key = std::array<std::uint8_t, 32>;
constexpr std::size_t kMetadataSize = 512;
constexpr std::size_t kMetadataOffset = 0x200;
constexpr std::array<std::array<std::uint8_t, 32>, 8> kSmallOrderEncodings = {
    std::array<std::uint8_t, 32>{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    std::array<std::uint8_t, 32>{{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    std::array<std::uint8_t, 32>{{0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}},
    std::array<std::uint8_t, 32>{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}},
    std::array<std::uint8_t, 32>{{0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a}},
    std::array<std::uint8_t, 32>{{0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0xfa}},
    std::array<std::uint8_t, 32>{{0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05}},
    std::array<std::uint8_t, 32>{{0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x85}},
};
static_assert(kSmallOrderEncodings.size() == 8);
static_assert(kSmallOrderEncodings.front().size() == 32);


void put16(std::vector<std::uint8_t>& b, std::size_t o, std::uint16_t v) {
  b[o] = static_cast<std::uint8_t>(v); b[o + 1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b[o + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
struct Fixture {
  std::vector<std::uint8_t> bytes;
  Key public_key{};
  EVP_PKEY* private_key = nullptr;
  std::filesystem::path path;

  Fixture() = default;
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture(Fixture&& other) noexcept
      : bytes(std::move(other.bytes)), public_key(other.public_key),
        private_key(std::exchange(other.private_key, nullptr)),
        path(std::move(other.path)) {}
  Fixture& operator=(Fixture&& other) noexcept {
    if (this != &other) {
      EVP_PKEY_free(private_key);
      bytes = std::move(other.bytes);
      public_key = other.public_key;
      private_key = std::exchange(other.private_key, nullptr);
      path = std::move(other.path);
    }
    return *this;
  }
  ~Fixture() { EVP_PKEY_free(private_key); }
};

std::array<std::uint8_t, 32> sha256(const std::uint8_t* p, std::size_t n) {
  std::array<std::uint8_t, 32> out{}; unsigned int len = 0;
  if (EVP_Digest(p, n, out.data(), &len, EVP_sha256(), nullptr) != 1 || len != out.size())
    throw std::runtime_error("sha256");
  return out;
}

std::array<std::uint8_t, 32> digest_coverage(const std::vector<std::uint8_t>& b) {
  EVP_MD_CTX* c = EVP_MD_CTX_new(); if (!c) throw std::runtime_error("ctx");
  if (EVP_DigestInit_ex(c, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(c, b.data(), kMetadataOffset) != 1 ||
      EVP_DigestUpdate(c, b.data() + kMetadataOffset + kMetadataSize,
                       b.size() - kMetadataOffset - kMetadataSize) != 1) {
    EVP_MD_CTX_free(c); throw std::runtime_error("digest");
  }
  std::array<std::uint8_t, 32> out{}; unsigned int len = 0;
  if (EVP_DigestFinal_ex(c, out.data(), &len) != 1 || len != out.size()) {
    EVP_MD_CTX_free(c); throw std::runtime_error("digest final");
  }
  EVP_MD_CTX_free(c); return out;
}

Fixture make_fixture() {
  Fixture f;
  f.private_key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
  if (!f.private_key) throw std::runtime_error("keygen");
  std::size_t pub_len = f.public_key.size();
  if (EVP_PKEY_get_raw_public_key(f.private_key, f.public_key.data(), &pub_len) != 1 ||
      pub_len != f.public_key.size()) throw std::runtime_error("pub");

  // Minimal strict ELF64 little-endian ET_EXEC container. One PT_LOAD-like empty
  // program header and active section table with .shstrtab + .text + .mindguard.
  constexpr std::size_t shstr_off = 0x100;
  constexpr std::size_t shstr_size = 32;
  constexpr std::size_t text_off = 0x180;
  constexpr std::size_t text_size = 32;
  constexpr std::size_t shoff = 0x500;
  constexpr std::size_t shnum = 4;
  f.bytes.assign(0x700, 0);
  f.bytes[0] = 0x7f; f.bytes[1] = 'E'; f.bytes[2] = 'L'; f.bytes[3] = 'F';
  f.bytes[4] = 2; f.bytes[5] = 1; f.bytes[6] = 1;
  put16(f.bytes, 16, 2); put16(f.bytes, 18, 62); put32(f.bytes, 20, 1);
  put64(f.bytes, 32, 64); put64(f.bytes, 40, shoff);
  put16(f.bytes, 52, 64); put16(f.bytes, 54, 56); put16(f.bytes, 56, 1);
  put16(f.bytes, 58, 64); put16(f.bytes, 60, shnum); put16(f.bytes, 62, 1);
  // Program header table entry is all zero, but lies in the covered header range.
  const char names[] = "\0.shstrtab\0.text\0.mindguard\0";
  std::memcpy(f.bytes.data() + shstr_off, names, sizeof(names) - 1);
  for (std::size_t i = 0; i < text_size; ++i) f.bytes[text_off + i] = static_cast<std::uint8_t>(0xA0 + i);
  // shstrtab
  std::size_t sh = shoff + 64;
  put32(f.bytes, sh + 0, 1); put32(f.bytes, sh + 4, 3); put64(f.bytes, sh + 24, shstr_off); put64(f.bytes, sh + 32, shstr_size); put64(f.bytes, sh + 48, 1);
  // .text
  sh += 64;
  put32(f.bytes, sh + 0, 11); put32(f.bytes, sh + 4, 1); put64(f.bytes, sh + 8, 0x6); put64(f.bytes, sh + 24, text_off); put64(f.bytes, sh + 32, text_size); put64(f.bytes, sh + 48, 16);
  // .mindguard
  sh += 64;
  put32(f.bytes, sh + 0, 17); put32(f.bytes, sh + 4, 1); put64(f.bytes, sh + 24, kMetadataOffset); put64(f.bytes, sh + 32, kMetadataSize); put64(f.bytes, sh + 48, 1);
  // Metadata placeholder and fields.
  auto* m = f.bytes.data() + kMetadataOffset;
  const std::uint8_t magic[8] = {'M','G','V','1','M','E','T','A'};
  std::memcpy(m, magic, sizeof(magic)); put16(f.bytes, kMetadataOffset + 0x08, 1); put16(f.bytes, kMetadataOffset + 0x0a, 32);
  m[0x0c] = 1; m[0x0d] = 1; put64(f.bytes, kMetadataOffset + 0x10, kMetadataOffset); put64(f.bytes, kMetadataOffset + 0x18, kMetadataSize);
  auto key_id = sha256(f.public_key.data(), f.public_key.size()); std::memcpy(m + 0x20, key_id.data(), key_id.size());
  auto digest = digest_coverage(f.bytes); std::memcpy(m + 0x40, digest.data(), digest.size());
  EVP_MD_CTX* c = EVP_MD_CTX_new(); if (!c) throw std::runtime_error("ctx");
  if (EVP_DigestSignInit(c, nullptr, nullptr, nullptr, f.private_key) != 1) throw std::runtime_error("sign init");
  std::size_t sig_len = 64;
  if (EVP_DigestSign(c, m + 0x60, &sig_len, m, 96) != 1 || sig_len != 64) throw std::runtime_error("sign");
  EVP_MD_CTX_free(c);
  return f;
}

Fixture clone_fixture(const Fixture& src) {
  Fixture f;
  f.bytes = src.bytes;
  f.public_key = src.public_key;
  return f;
}

Key generate_public_key() {
  EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
  if (!key) throw std::runtime_error("keygen");
  Key public_key{};
  std::size_t length = public_key.size();
  const bool ok = EVP_PKEY_get_raw_public_key(key, public_key.data(), &length) == 1 &&
                  length == public_key.size();
  EVP_PKEY_free(key);
  if (!ok) throw std::runtime_error("public key");
  return public_key;
}

VerifyError verify(Fixture& f, const char* label) {
  f.path = std::filesystem::temp_directory_path() / (std::string("mindguard_cpp_") + label + ".bin");
  std::ofstream out(f.path, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(f.bytes.data()), static_cast<std::streamsize>(f.bytes.size())); out.close();
  auto e = mindguard_runtime::verify_file_for_diagnostics(f.path, f.public_key);
  std::error_code ec; std::filesystem::remove(f.path, ec); return e;
}

void expect(VerifyError got, VerifyError wanted, const char* what) {
  if (got != wanted) { std::cerr << what << ": got " << mindguard_runtime::verify_error_code(got) << " expected " << mindguard_runtime::verify_error_code(wanted) << "\n"; std::abort(); }
}

} // namespace

int main() {
  Fixture valid = make_fixture();
  expect(verify(valid, "valid"), VerifyError::Success, "valid");
  // A canonical-sized byte string which does not decompress to an Edwards
  // point is a trust-configuration error before key_id or signature checks.
  { Fixture f = clone_fixture(valid); f.public_key.fill(0); f.public_key[0] = 2;
    expect(verify(f, "non_decompressible_key"), VerifyError::KeyConfigInvalid,
           "non-decompressible public key"); }
  { Fixture f = clone_fixture(valid); f.bytes[0] = 'M'; expect(verify(f, "unsupported"), VerifyError::UnsupportedFormat, "unsupported format"); }
  { Fixture f = clone_fixture(valid); f.bytes[0x180] ^= 1; expect(verify(f, "code_tamper"), VerifyError::DigestMismatch, "code tamper"); }
  { Fixture f = clone_fixture(valid); f.bytes[kMetadataOffset + 0x60] ^= 1; expect(verify(f, "sig_tamper"), VerifyError::SignatureInvalid, "sig tamper"); }
  for (const auto& small_order : kSmallOrderEncodings) {
    { Fixture f = clone_fixture(valid);
      f.public_key = small_order;
      expect(verify(f, "small_order_key"), VerifyError::KeyConfigInvalid,
             "small-order public key");
    }
    { Fixture f = clone_fixture(valid);
      std::copy(small_order.begin(), small_order.end(),
                f.bytes.begin() + kMetadataOffset + 0x60);
      expect(verify(f, "small_order_R"), VerifyError::SignatureInvalid,
             "small-order R");
    }
  }
  { Fixture f = clone_fixture(valid);
    std::fill(f.bytes.begin() + kMetadataOffset + 0x60 + 32,
              f.bytes.begin() + kMetadataOffset + 0x60 + 64, 0xff);
    expect(verify(f, "scalar_ge_L"), VerifyError::SignatureInvalid, "S >= L");
  }
  { Fixture f = clone_fixture(valid); put32(f.bytes, 0x500 + 3 * 64, 11); expect(verify(f, "missing_meta"), VerifyError::MetadataAbsent, "missing metadata"); }
  { Fixture f = clone_fixture(valid); put32(f.bytes, 0x500 + 2 * 64, 17); expect(verify(f, "duplicate"), VerifyError::MetadataDuplicate, "duplicate"); }
  { Fixture f = clone_fixture(valid);
    put32(f.bytes, 0x500 + 2 * 64, 17);
    put64(f.bytes, 40, 0xfffffffffffffff0ULL);
    expect(verify(f, "malformed_precedes_duplicate"), VerifyError::MalformedFormat,
           "malformed structure precedes duplicate");
  }

  // Structural checks complete before candidate selection: invalid sh_link,
  // program p_filesz/p_memsz, section overlap and a non-canonical section zero
  // all classify as MG-V03.
  { Fixture f = clone_fixture(valid); put32(f.bytes, 0x500 + 2 * 64 + 40, 4);
    expect(verify(f, "bad_sh_link"), VerifyError::MalformedFormat, "bad sh_link"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 64 + 32, 2); put64(f.bytes, 64 + 40, 1);
    expect(verify(f, "filesz_gt_memsz"), VerifyError::MalformedFormat,
           "p_filesz greater than p_memsz"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 0x500 + 2 * 64 + 24, 0x100);
    expect(verify(f, "ordinary_overlap"), VerifyError::MalformedFormat,
           "ordinary section overlap"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 0x500 + 16, 1);
    expect(verify(f, "section_zero"), VerifyError::MalformedFormat,
           "non-canonical section zero"); }
  // The exact candidate is selected after structural parsing. Its fixed
  // placement/metadata-range overlap is therefore MG-V07, not MG-V03.
  { Fixture f = clone_fixture(valid); put64(f.bytes, 0x500 + 3 * 64 + 24, 0x180);
    expect(verify(f, "metadata_overlaps_section"), VerifyError::MetadataMalformed,
           "metadata overlap is V07"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 0x500 + 3 * 64 + 24, 0);
    expect(verify(f, "metadata_overlaps_header"), VerifyError::MetadataMalformed,
           "metadata/header overlap is V07"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 64 + 8, kMetadataOffset);
    put64(f.bytes, 64 + 32, 1); put64(f.bytes, 64 + 40, 1);
    expect(verify(f, "metadata_overlaps_program"), VerifyError::MetadataMalformed,
           "metadata/program overlap is V07"); }
  // Every fixed ELF section field is normative and independently maps to V07.
  const std::array<std::pair<std::size_t, std::uint64_t>, 8> metadata_fields = {{
      {4, 2}, {8, 1}, {16, 1}, {32, 511}, {40, 1}, {44, 1}, {48, 2}, {56, 1}}};
  for (const auto& field : metadata_fields) {
    Fixture f = clone_fixture(valid);
    put64(f.bytes, 0x500 + 3 * 64 + field.first, field.second);
    expect(verify(f, "metadata_fixed_field"), VerifyError::MetadataMalformed,
           "metadata fixed section field");
  }
  { Fixture f = clone_fixture(valid); put16(f.bytes, kMetadataOffset + 8, 2); expect(verify(f, "version"), VerifyError::VersionUnsupported, "version"); }
  { Fixture f = clone_fixture(valid); f.bytes[kMetadataOffset + 0x0d] = 2; expect(verify(f, "coverage"), VerifyError::CoverageUnsupported, "coverage"); }
  // Use a freshly generated valid key so this proves key-id precedence over
  // signature verification, rather than accidentally testing MG-V10.
  { Fixture f = clone_fixture(valid); f.public_key = generate_public_key();
    expect(verify(f, "key_mismatch"), VerifyError::KeyIdMismatch, "key id before signature"); }
  { Fixture f = clone_fixture(valid); f.bytes[0x100 + 17 + 10] = 'X'; put32(f.bytes, 0x500 + 2 * 64, 17); expect(verify(f, "unknown"), VerifyError::MetadataUnknown, "unknown prefix"); }
  { Fixture f = clone_fixture(valid); f.bytes[kMetadataOffset + 0xa0] = 1; expect(verify(f, "reserved"), VerifyError::MetadataMalformed, "reserved"); }
  { Fixture f = clone_fixture(valid); f.bytes[40] = 0xff; expect(verify(f, "malformed"), VerifyError::MalformedFormat, "malformed"); }
  { Fixture f = clone_fixture(valid); put64(f.bytes, 0x500 + 3 * 64 + 24, 0xfffffffffffffff0ULL); expect(verify(f, "oob"), VerifyError::MalformedFormat, "oob"); }
  std::cout << "all diagnostic tests passed\n";
  return 0;
}
