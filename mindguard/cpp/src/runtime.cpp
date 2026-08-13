#include "mindguard_runtime/runtime.hpp"

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mindguard_runtime {
namespace {

constexpr std::size_t kMetadataSize = 512;
constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kElfSectionHeaderSize = 64;
constexpr std::uint16_t kElfClass64 = 2;
constexpr std::uint16_t kElfDataLittle = 1;
constexpr std::uint16_t kElfMachineX86_64 = 62;
constexpr std::uint16_t kElfTypeExec = 2;
constexpr std::uint16_t kElfTypeDyn = 3;
constexpr std::uint32_t kShtNull = 0;
constexpr std::uint32_t kShtProgbits = 1;
constexpr std::uint32_t kShtStrtab = 3;
constexpr std::uint32_t kShtNobits = 8;
constexpr std::uint16_t kShnXindex = 0xffff;
constexpr std::array<std::uint8_t, 8> kMetadataMagic =
    {'M', 'G', 'V', '1', 'M', 'E', 'T', 'A'};


// RFC 8032 Ed25519 subgroup order, encoded little-endian. OpenSSL's EVP
// provider does not promise the strict S < L check required by protocol v1,
// so this scalar is compared byte-for-byte before invoking EVP.
constexpr std::array<std::uint8_t, 32> kEd25519Order = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

// The complete set of canonical compressed encodings for Ed25519 points
// whose order divides 8. These are protocol constants (not key material),
// and rejecting exactly this set closes the small-order acceptance gap in the
// OpenSSL EVP provider. The list is independently covered by C++ tests.
constexpr std::array<std::array<std::uint8_t, 32>, 8> kEd25519SmallOrder = {
    std::array<std::uint8_t, 32>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    std::array<std::uint8_t, 32>{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    std::array<std::uint8_t, 32>{0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    std::array<std::uint8_t, 32>{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80},
    std::array<std::uint8_t, 32>{0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a},
    std::array<std::uint8_t, 32>{0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f, 0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0xfa},
    std::array<std::uint8_t, 32>{0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05},
    std::array<std::uint8_t, 32>{0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0, 0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x85},
};
static_assert(kEd25519SmallOrder.size() == 8);
static_assert(kEd25519SmallOrder.front().size() == 32);

struct Section {
  std::uint32_t name = 0;
  std::uint32_t type = 0;
  std::uint64_t flags = 0;
  std::uint64_t addr = 0;
  std::uint64_t offset = 0;
  std::uint64_t size = 0;
  std::uint64_t link = 0;
  std::uint64_t info = 0;
  std::uint64_t addralign = 0;
  std::uint64_t entsize = 0;
};

struct ElfInfo {
  std::size_t metadata_index = 0;
  std::uint64_t metadata_offset = 0;
  std::uint64_t metadata_size = 0;
  std::vector<std::pair<std::uint64_t, std::uint64_t>> program_file_ranges;
};

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (b > std::numeric_limits<std::uint64_t>::max() - a) return false;
  out = a + b;
  return true;
}

bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) noexcept {
  if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) return false;
  out = a * b;
  return true;
}

bool range_in_file(std::uint64_t offset, std::uint64_t size,
                   std::size_t file_size) noexcept {
  std::uint64_t end = 0;
  return checked_add(offset, size, end) &&
         end <= static_cast<std::uint64_t>(file_size);
}

std::uint16_t read_u16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t read_u32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64(const std::uint8_t* p) noexcept {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(p[i]) << (8U * i);
  }
  return value;
}

bool is_known_elf_magic(const std::vector<std::uint8_t>& bytes) noexcept {
  return bytes.size() >= 4 && bytes[0] == 0x7f && bytes[1] == 'E' &&
         bytes[2] == 'L' && bytes[3] == 'F';
}

bool ranges_overlap(std::uint64_t a_off, std::uint64_t a_size,
                    std::uint64_t b_off, std::uint64_t b_size) noexcept {
  std::uint64_t a_end = 0;
  std::uint64_t b_end = 0;
  if (!checked_add(a_off, a_size, a_end) || !checked_add(b_off, b_size, b_end)) {
    return true;
  }
  return a_off < b_end && b_off < a_end;
}

VerifyError parse_elf(const std::vector<std::uint8_t>& bytes,
                      ElfInfo& info,
                      std::vector<Section>& sections,
                      std::uint64_t& section_table_offset,
                      std::uint64_t& section_table_size) {
  if (!is_known_elf_magic(bytes)) {
    // PE, Mach-O, and unknown containers are not part of this Linux slice.
    return VerifyError::UnsupportedFormat;
  }
  if (bytes.size() < kElfHeaderSize) return VerifyError::MalformedFormat;
  const auto* h = bytes.data();
  if (h[4] != kElfClass64 || h[5] != kElfDataLittle || h[6] != 1) {
    return VerifyError::UnsupportedFormat;
  }
  const auto e_type = read_u16(h + 16);
  const auto e_machine = read_u16(h + 18);
  const auto e_version = read_u32(h + 20);
  const auto e_phoff = read_u64(h + 32);
  const auto e_shoff = read_u64(h + 40);
  const auto e_ehsize = read_u16(h + 52);
  const auto e_phentsize = read_u16(h + 54);
  const auto e_phnum = read_u16(h + 56);
  const auto e_shentsize = read_u16(h + 58);
  const auto e_shnum = read_u16(h + 60);
  const auto e_shstrndx = read_u16(h + 62);
  if ((e_type != kElfTypeExec && e_type != kElfTypeDyn) ||
      e_machine != kElfMachineX86_64 || e_version != 1) {
    return VerifyError::UnsupportedFormat;
  }
  if (e_ehsize != kElfHeaderSize || e_shentsize != kElfSectionHeaderSize ||
      e_shnum == 0 || e_shstrndx == kShnXindex || e_shstrndx >= e_shnum ||
      e_phnum == 0xffff) {
    return VerifyError::MalformedFormat;
  }

  std::uint64_t ph_size = 0;
  if (e_phnum != 0 && (e_phentsize != 56 ||
                       !checked_mul(e_phentsize, e_phnum, ph_size))) {
    return VerifyError::MalformedFormat;
  }
  if (e_phnum == 0) {
    // ELF permits no program-header table only when e_phoff is also zero.
    if (e_phoff != 0) return VerifyError::MalformedFormat;
  } else {
    if (!range_in_file(e_phoff, ph_size, bytes.size()) ||
        ranges_overlap(e_phoff, ph_size, 0, kElfHeaderSize)) {
      return VerifyError::MalformedFormat;
    }
  }

  info.program_file_ranges.clear();
  for (std::uint16_t i = 0; i < e_phnum; ++i) {
    std::uint64_t entry_offset = 0;
    if (!checked_add(e_phoff,
                     static_cast<std::uint64_t>(i) * e_phentsize,
                     entry_offset) ||
        !range_in_file(entry_offset, e_phentsize, bytes.size())) {
      return VerifyError::MalformedFormat;
    }
    const auto* ph = bytes.data() + static_cast<std::size_t>(entry_offset);
    const auto p_offset = read_u64(ph + 8);
    const auto p_vaddr = read_u64(ph + 16);
    const auto p_filesz = read_u64(ph + 32);
    const auto p_memsz = read_u64(ph + 40);
    std::uint64_t endpoint = 0;
    if (p_filesz > p_memsz ||
        !checked_add(p_offset, p_filesz, endpoint) ||
        !range_in_file(p_offset, p_filesz, bytes.size()) ||
        !checked_add(p_vaddr, p_memsz, endpoint)) {
      return VerifyError::MalformedFormat;
    }
    if (p_filesz != 0) {
      info.program_file_ranges.emplace_back(p_offset, p_filesz);
    }
  }

  if (!checked_mul(e_shentsize, e_shnum, section_table_size) ||
      !range_in_file(e_shoff, section_table_size, bytes.size())) {
    return VerifyError::MalformedFormat;
  }
  if (ranges_overlap(e_shoff, section_table_size, 0, kElfHeaderSize) ||
      (e_phnum != 0 &&
       ranges_overlap(e_shoff, section_table_size, e_phoff, ph_size))) {
    return VerifyError::MalformedFormat;
  }
  section_table_offset = e_shoff;

  sections.clear();
  sections.reserve(e_shnum);
  for (std::uint16_t i = 0; i < e_shnum; ++i) {
    std::uint64_t off = 0;
    if (!checked_add(e_shoff,
                     static_cast<std::uint64_t>(i) * e_shentsize, off) ||
        !range_in_file(off, kElfSectionHeaderSize, bytes.size())) {
      return VerifyError::MalformedFormat;
    }
    const auto* sh = bytes.data() + static_cast<std::size_t>(off);
    Section s;
    s.name = read_u32(sh + 0);
    s.type = read_u32(sh + 4);
    s.flags = read_u64(sh + 8);
    s.addr = read_u64(sh + 16);
    s.offset = read_u64(sh + 24);
    s.size = read_u64(sh + 32);
    s.link = read_u32(sh + 40);
    s.info = read_u32(sh + 44);
    s.addralign = read_u64(sh + 48);
    s.entsize = read_u64(sh + 56);

    std::uint64_t endpoint = 0;
    if (!checked_add(s.addr, s.size, endpoint) ||
        (s.link != 0 && s.link >= e_shnum)) {
      return VerifyError::MalformedFormat;
    }
    if (s.type == kShtNull) {
      if (s.name != 0 || s.flags != 0 || s.addr != 0 || s.offset != 0 ||
          s.size != 0 || s.link != 0 || s.info != 0 || s.addralign != 0 ||
          s.entsize != 0) {
        return VerifyError::MalformedFormat;
      }
    } else {
      // NOBITS contributes no bytes to the file but its offset and virtual
      // endpoint still have to be representable; ordinary sections require
      // their complete non-empty range to be in the file.
      if (!range_in_file(s.offset, s.type == kShtNobits ? 0 : s.size,
                         bytes.size()) ||
          !checked_add(s.offset, s.size, endpoint)) {
        return VerifyError::MalformedFormat;
      }
    }
    sections.push_back(s);
  }
  if (sections.empty() || sections[0].type != kShtNull ||
      sections[0].name != 0 || sections[0].flags != 0 ||
      sections[0].addr != 0 || sections[0].offset != 0 || sections[0].size != 0 ||
      sections[0].link != 0 || sections[0].info != 0 ||
      sections[0].addralign != 0 || sections[0].entsize != 0) {
    return VerifyError::MalformedFormat;
  }

  const Section& shstr = sections[e_shstrndx];
  if (shstr.type != kShtStrtab || shstr.size == 0 ||
      !range_in_file(shstr.offset, shstr.size, bytes.size())) {
    return VerifyError::MalformedFormat;
  }
  const auto* strings = bytes.data() + static_cast<std::size_t>(shstr.offset);
  const auto strings_size = static_cast<std::size_t>(shstr.size);
  if (strings[0] != 0) return VerifyError::MalformedFormat;
  auto section_name = [&](const Section& s) -> std::optional<std::string_view> {
    if (s.name >= strings_size) return std::nullopt;
    const char* begin = reinterpret_cast<const char*>(strings + s.name);
    const std::size_t remain = strings_size - s.name;
    const void* nul = std::memchr(begin, '\0', remain);
    if (nul == nullptr) return std::nullopt;
    return std::string_view(begin, static_cast<const char*>(nul) - begin);
  };

  // Resolve every section name before any candidate result is returned. This
  // keeps malformed string references in the structural MG-V03 phase.
  std::vector<bool> metadata_candidates(sections.size(), false);
  std::size_t canonical_count = 0;
  std::size_t canonical_index = 0;
  bool unknown = false;
  for (std::size_t i = 0; i < sections.size(); ++i) {
    const auto name = section_name(sections[i]);
    if (!name.has_value()) return VerifyError::MalformedFormat;
    if (name->rfind(".mindguard", 0) == 0) {
      metadata_candidates[i] = true;
      if (*name == ".mindguard") {
        ++canonical_count;
        canonical_index = i;
      } else {
        unknown = true;
      }
    }
  }

  // Structural ordinary ranges are checked completely before candidate
  // selection. Candidate ranges are intentionally deferred so one exact
  // .mindguard with a bad placement/overlap is MG-V07, while duplicate/alias
  // precedence remains MG-V05/MG-V06 as specified by the protocol.
  std::vector<std::pair<std::uint64_t, std::uint64_t>> ordinary_ranges;
  for (std::size_t i = 0; i < sections.size(); ++i) {
    const Section& s = sections[i];
    if (metadata_candidates[i] || s.type == kShtNull ||
        s.type == kShtNobits || s.size == 0) {
      continue;
    }
    if (ranges_overlap(s.offset, s.size, 0, kElfHeaderSize) ||
        (e_phnum != 0 && ranges_overlap(s.offset, s.size, e_phoff, ph_size)) ||
        ranges_overlap(s.offset, s.size, section_table_offset, section_table_size)) {
      return VerifyError::MalformedFormat;
    }
    for (const auto& prior : ordinary_ranges) {
      if (ranges_overlap(s.offset, s.size, prior.first, prior.second)) {
        return VerifyError::MalformedFormat;
      }
    }
    ordinary_ranges.emplace_back(s.offset, s.size);
  }

  // Candidate precedence is normative: structural errors above always win;
  // then duplicate exact, aliases, absent, and only then fixed metadata.
  if (canonical_count > 1) return VerifyError::MetadataDuplicate;
  if (unknown) return VerifyError::MetadataUnknown;
  if (canonical_count == 0) return VerifyError::MetadataAbsent;

  const Section& m = sections[canonical_index];
  if (m.type != kShtProgbits || m.flags != 0 || m.addr != 0 ||
      m.size != kMetadataSize || m.link != 0 || m.info != 0 ||
      m.addralign != 1 || m.entsize != 0 ||
      !range_in_file(m.offset, m.size, bytes.size())) {
    return VerifyError::MetadataMalformed;
  }
  if (ranges_overlap(m.offset, m.size, 0, kElfHeaderSize) ||
      (e_phnum != 0 && ranges_overlap(m.offset, m.size, e_phoff, ph_size)) ||
      ranges_overlap(m.offset, m.size, section_table_offset, section_table_size)) {
    return VerifyError::MetadataMalformed;
  }
  for (const auto& program_range : info.program_file_ranges) {
    if (ranges_overlap(m.offset, m.size, program_range.first,
                       program_range.second)) {
      return VerifyError::MetadataMalformed;
    }
  }
  for (std::size_t i = 0; i < sections.size(); ++i) {
    if (i == canonical_index || sections[i].type == kShtNull ||
        sections[i].type == kShtNobits || sections[i].size == 0) {
      continue;
    }
    if (ranges_overlap(m.offset, m.size, sections[i].offset, sections[i].size)) {
      return VerifyError::MetadataMalformed;
    }
  }
  info.metadata_index = canonical_index;
  info.metadata_offset = m.offset;
  info.metadata_size = m.size;
  return VerifyError::Success;
}

bool sha256_ranges(const std::vector<std::uint8_t>& bytes,
                   std::uint64_t metadata_offset,
                   std::uint64_t metadata_size,
                   std::array<std::uint8_t, 32>& output) noexcept {
  if (!range_in_file(metadata_offset, metadata_size, bytes.size())) return false;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx == nullptr) return false;
  bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1;
  const auto md_off = static_cast<std::size_t>(metadata_offset);
  const auto md_size = static_cast<std::size_t>(metadata_size);
  if (ok && md_off != 0) {
    ok = EVP_DigestUpdate(ctx, bytes.data(), md_off) == 1;
  }
  const std::size_t after = md_off + md_size;
  if (ok && after < bytes.size()) {
    ok = EVP_DigestUpdate(ctx, bytes.data() + after, bytes.size() - after) == 1;
  }
  unsigned int out_len = 0;
  if (ok) ok = EVP_DigestFinal_ex(ctx, output.data(), &out_len) == 1 && out_len == output.size();
  EVP_MD_CTX_free(ctx);
  return ok;
}

bool sha256_key(const std::array<std::uint8_t, 32>& key,
                std::array<std::uint8_t, 32>& output) noexcept {
  unsigned int len = 0;
  return EVP_Digest(key.data(), key.size(), output.data(), &len, EVP_sha256(), nullptr) == 1 &&
         len == output.size();
}

bool constant_time_equal(const std::uint8_t* a, const std::uint8_t* b,
                         std::size_t n) noexcept {
  return CRYPTO_memcmp(a, b, n) == 0;
}

// p = 2^255 - 19, little-endian, with the sign bit excluded before compare.
constexpr std::array<std::uint8_t, 32> kEd25519FieldPrime = {
    0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f,
};

bool less_than_little_endian(const std::uint8_t* value,
                             const std::array<std::uint8_t, 32>& bound) noexcept {
  for (std::size_t i = bound.size(); i-- > 0;) {
    if (value[i] != bound[i]) return value[i] < bound[i];
  }
  return false;
}

bool is_small_order_encoding(const std::uint8_t* encoding) noexcept {
  for (const auto& candidate : kEd25519SmallOrder) {
    if (constant_time_equal(encoding, candidate.data(), candidate.size())) return true;
  }
  return false;
}

// OpenSSL's EVP Ed25519 provider accepts a 32-byte public-key blob and its
// EVP_PKEY_public_check() currently does not reject non-decompressible or
// small-order encodings.  Perform the RFC 8032 Edwards decompression ourselves
// before handing a point to EVP.  This is intentionally fail-closed: any
// allocation or arithmetic failure returns false (MG-V10 for the trust key,
// MG-V12 for R).
bool strict_point_encoding(const std::uint8_t* encoding) noexcept {
  std::array<std::uint8_t, 32> y_bytes{};
  std::copy_n(encoding, y_bytes.size(), y_bytes.begin());
  const bool sign = (y_bytes[31] & 0x80U) != 0;
  y_bytes[31] &= 0x7fU;
  if (!less_than_little_endian(y_bytes.data(), kEd25519FieldPrime) ||
      is_small_order_encoding(encoding)) {
    return false;
  }

  BN_CTX* ctx = BN_CTX_new();
  if (ctx == nullptr) return false;
  BN_CTX_start(ctx);
  BIGNUM* p = BN_CTX_get(ctx);
  BIGNUM* y = BN_CTX_get(ctx);
  BIGNUM* y2 = BN_CTX_get(ctx);
  BIGNUM* num = BN_CTX_get(ctx);
  BIGNUM* den = BN_CTX_get(ctx);
  BIGNUM* d = BN_CTX_get(ctx);
  BIGNUM* inv121666 = BN_CTX_get(ctx);
  BIGNUM* invden = BN_CTX_get(ctx);
  BIGNUM* x2 = BN_CTX_get(ctx);
  BIGNUM* x = BN_CTX_get(ctx);
  BIGNUM* check = BN_CTX_get(ctx);
  bool valid = false;
  if (p == nullptr || y == nullptr || y2 == nullptr || num == nullptr ||
      den == nullptr || d == nullptr || inv121666 == nullptr || invden == nullptr ||
      x2 == nullptr || x == nullptr || check == nullptr ||
      BN_lebin2bn(kEd25519FieldPrime.data(),
                  static_cast<int>(kEd25519FieldPrime.size()), p) == nullptr ||
      BN_lebin2bn(y_bytes.data(), static_cast<int>(y_bytes.size()), y) == nullptr) {
    goto cleanup;
  }

  // d = -121665 / 121666 (mod p), and x² = (y² - 1)/(d*y² + 1).
  if (BN_mod_sqr(y2, y, p, ctx) != 1 ||
      BN_mod_sub(num, y2, BN_value_one(), p, ctx) != 1 ||
      BN_set_word(d, 121665) != 1 ||
      BN_set_word(inv121666, 121666) != 1 ||
      BN_mod_inverse(inv121666, inv121666, p, ctx) == nullptr ||
      BN_mod_mul(d, d, inv121666, p, ctx) != 1 ||
      BN_mod_sub(d, p, d, p, ctx) != 1 ||
      BN_mod_mul(den, d, y2, p, ctx) != 1 ||
      BN_mod_add(den, den, BN_value_one(), p, ctx) != 1 ||
      BN_mod_inverse(invden, den, p, ctx) == nullptr ||
      BN_mod_mul(x2, num, invden, p, ctx) != 1) {
    goto cleanup;
  }

  if (BN_mod_sqrt(x, x2, p, ctx) == nullptr ||
      BN_mod_sqr(check, x, p, ctx) != 1 || BN_cmp(check, x2) != 0) {
    goto cleanup;
  }

  // The sign bit selects x's parity.  x == 0 has only the sign-zero
  // representation; all other values have exactly one parity-matching root.
  if (BN_is_zero(x)) {
    valid = !sign;
  } else {
    const bool odd = BN_is_odd(x) != 0;
    if (odd != sign && BN_sub(x, p, x) != 1) goto cleanup;
    valid = (BN_is_odd(x) != 0) == sign;
  }

cleanup:
  BN_CTX_end(ctx);
  BN_CTX_free(ctx);
  return valid;
}

bool strict_scalar(const std::uint8_t* scalar) noexcept {
  return less_than_little_endian(scalar, kEd25519Order);
}

bool strict_public_key(const std::array<std::uint8_t, 32>& public_key) noexcept {
  if (!strict_point_encoding(public_key.data())) return false;
  EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr, public_key.data(), public_key.size());
  if (pkey == nullptr) return false;
  EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new(pkey, nullptr);
  const bool valid = pctx != nullptr && EVP_PKEY_public_check(pctx) == 1;
  EVP_PKEY_CTX_free(pctx);
  EVP_PKEY_free(pkey);
  return valid;
}

bool strict_signature(const std::uint8_t* signature) noexcept {
  return strict_point_encoding(signature) && strict_scalar(signature + 32);
}

bool verify_ed25519(const std::array<std::uint8_t, 32>& public_key,
                    const std::uint8_t* message, std::size_t message_size,
                    const std::uint8_t* signature) noexcept {
  EVP_PKEY* pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                public_key.data(), public_key.size());
  if (pkey == nullptr) return false;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  bool ok = ctx != nullptr;
  if (ok) ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1;
  if (ok) ok = EVP_DigestVerify(ctx, signature, 64, message, message_size) == 1;
  EVP_MD_CTX_free(ctx);
  EVP_PKEY_free(pkey);
  return ok;
}

VerifyError verify_bytes(const std::vector<std::uint8_t>& bytes,
                         const std::array<std::uint8_t, 32>& trusted_key) {
  ElfInfo info;
  std::vector<Section> sections;
  std::uint64_t unused_shoff = 0;
  std::uint64_t unused_shsize = 0;
  const auto parse_result = parse_elf(bytes, info, sections, unused_shoff, unused_shsize);
  if (parse_result != VerifyError::Success) return parse_result;
  if (info.metadata_size != kMetadataSize ||
      !range_in_file(info.metadata_offset, kMetadataSize, bytes.size())) {
    return VerifyError::MetadataMalformed;
  }
  const auto* m = bytes.data() + static_cast<std::size_t>(info.metadata_offset);
  if (!std::equal(kMetadataMagic.begin(), kMetadataMagic.end(), m)) {
    return VerifyError::MetadataMalformed;
  }
  if (read_u16(m + 0x0a) != 32 ||
      read_u64(m + 0x18) != kMetadataSize ||
      read_u64(m + 0x10) != info.metadata_offset || m[0x0c] != 1) {
    return VerifyError::MetadataMalformed;
  }
  if (read_u16(m + 0x08) != 1) return VerifyError::VersionUnsupported;
  if (m[0x0d] != 1) return VerifyError::CoverageUnsupported;
  if (m[0x0e] != 0 || m[0x0f] != 0) return VerifyError::MetadataMalformed;
  for (std::size_t i = 0xa0; i < kMetadataSize; ++i) {
    if (m[i] != 0) return VerifyError::MetadataMalformed;
  }
  // OpenSSL EVP does not guarantee the protocol's canonical key, scalar, and
  // small-order checks. Validate those bytes before any key-id or signature
  // operation so malformed trust configuration is always MG-V10 and malformed
  // signatures are always MG-V12.
  if (!strict_public_key(trusted_key)) return VerifyError::KeyConfigInvalid;
  std::array<std::uint8_t, 32> key_id{};
  if (!sha256_key(trusted_key, key_id)) return VerifyError::Internal;
  if (!constant_time_equal(key_id.data(), m + 0x20, key_id.size())) {
    return VerifyError::KeyIdMismatch;
  }
  if (!strict_signature(m + 0x60)) return VerifyError::SignatureInvalid;
  if (!verify_ed25519(trusted_key, m, 96, m + 0x60)) {
    return VerifyError::SignatureInvalid;
  }
  std::array<std::uint8_t, 32> digest{};
  if (!sha256_ranges(bytes, info.metadata_offset, kMetadataSize, digest)) {
    return VerifyError::Internal;
  }
  if (!constant_time_equal(digest.data(), m + 0x40, digest.size())) {
    return VerifyError::DigestMismatch;
  }
  return VerifyError::Success;
}

bool read_file(const std::filesystem::path& file,
               std::vector<std::uint8_t>& bytes) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size > std::numeric_limits<std::size_t>::max()) return false;
  std::ifstream in(file, std::ios::binary);
  if (!in || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    return false;
  }
  bytes.resize(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in || static_cast<std::size_t>(in.gcount()) != bytes.size()) return false;
  }
  return true;
}

} // namespace

const char* verify_error_code(VerifyError error) noexcept {
  switch (error) {
    case VerifyError::Success: return "OK";
    case VerifyError::SelfFileIO: return "MG-V01";
    case VerifyError::UnsupportedFormat: return "MG-V02";
    case VerifyError::MalformedFormat: return "MG-V03";
    case VerifyError::MetadataAbsent: return "MG-V04";
    case VerifyError::MetadataDuplicate: return "MG-V05";
    case VerifyError::MetadataUnknown: return "MG-V06";
    case VerifyError::MetadataMalformed: return "MG-V07";
    case VerifyError::VersionUnsupported: return "MG-V08";
    case VerifyError::CoverageUnsupported: return "MG-V09";
    case VerifyError::KeyConfigInvalid: return "MG-V10";
    case VerifyError::KeyIdMismatch: return "MG-V11";
    case VerifyError::SignatureInvalid: return "MG-V12";
    case VerifyError::DigestMismatch: return "MG-V13";
    case VerifyError::Internal: return "MG-V14";
  }
  return "MG-V14";
}

VerifyError verify_file_for_diagnostics(
    const std::filesystem::path& file,
    const std::array<std::uint8_t, 32>& trusted_public_key) noexcept {
  try {
    std::vector<std::uint8_t> bytes;
    if (!read_file(file, bytes)) return VerifyError::SelfFileIO;
    return verify_bytes(bytes, trusted_public_key);
  } catch (...) {
    return VerifyError::Internal;
  }
}

void verify_or_terminate(const std::array<std::uint8_t, 32>& trusted_public_key) noexcept {
#if defined(__linux__)
  int fd = -1;
  try {
    fd = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) std::abort();
    struct stat st{};
    if (::fstat(fd, &st) != 0 || st.st_size < 0 ||
        static_cast<unsigned long long>(st.st_size) > std::numeric_limits<std::size_t>::max()) {
      std::abort();
    }
    const std::size_t size = static_cast<std::size_t>(st.st_size);
    std::vector<std::uint8_t> bytes(size);
    std::size_t done = 0;
    while (done < size) {
      const ssize_t n = ::read(fd, bytes.data() + done, size - done);
      if (n <= 0) std::abort();
      done += static_cast<std::size_t>(n);
    }
    struct stat st_after{};
    const bool stable = ::fstat(fd, &st_after) == 0 && st_after.st_size == st.st_size;
    ::close(fd);
    fd = -1;
    if (!stable || verify_bytes(bytes, trusted_public_key) != VerifyError::Success) std::abort();
  } catch (...) {
    if (fd >= 0) ::close(fd);
    std::abort();
  }
#else
  (void)trusted_public_key;
  std::abort();
#endif
}

} // namespace mindguard_runtime
