#include "mindguard/detail/integrity_hash.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint16_t kAmd64 = 0x8664;
constexpr std::uint16_t kDll = 0x2000;
constexpr std::uint16_t kPe32Plus = 0x020b;
constexpr std::uint16_t kRequiredDllFlags = 0x0020 | 0x0040 | 0x0100;
constexpr std::uint32_t kCode = 0x00000020;
constexpr std::uint32_t kExecute = 0x20000000;
constexpr std::uint32_t kWrite = 0x80000000;

bool inside(const std::vector<std::uint8_t>& file, std::uint64_t offset,
            std::uint64_t size) noexcept {
  return offset <= file.size() && size <= file.size() - static_cast<std::size_t>(offset);
}

template <class T>
bool read(const std::vector<std::uint8_t>& file, std::size_t offset, T& value) noexcept {
  if (!inside(file, offset, sizeof(value))) return false;
  std::memcpy(&value, file.data() + offset, sizeof(value));
  return true;
}

template <class T>
void write(std::vector<std::uint8_t>& file, std::size_t offset, T value) noexcept {
  std::memcpy(file.data() + offset, &value, sizeof(value));
}

struct section final {
  std::array<char, 8> name{};
  std::uint32_t virtual_size = 0;
  std::uint32_t virtual_address = 0;
  std::uint32_t raw_size = 0;
  std::uint32_t raw_offset = 0;
  std::uint32_t characteristics = 0;
};

bool section_named(const section& value, std::string_view name) noexcept {
  std::array<char, 8> expected{};
  std::copy(name.begin(), name.end(), expected.begin());
  return value.name == expected;
}

bool load_section(const std::vector<std::uint8_t>& file, std::size_t offset,
                  section& value) noexcept {
  return read(file, offset, value.name) && read(file, offset + 8U, value.virtual_size) &&
         read(file, offset + 12U, value.virtual_address) &&
         read(file, offset + 16U, value.raw_size) &&
         read(file, offset + 20U, value.raw_offset) &&
         read(file, offset + 36U, value.characteristics);
}

const section* section_for_rva(const std::vector<section>& sections, std::uint32_t rva,
                               std::uint32_t size) noexcept {
  for (const auto& current : sections) {
    if (rva >= current.virtual_address &&
        static_cast<std::uint64_t>(rva) + size <=
            static_cast<std::uint64_t>(current.virtual_address) + current.raw_size) {
      return &current;
    }
  }
  return nullptr;
}

bool text_has_relocation(const std::vector<std::uint8_t>& file,
                         const std::vector<section>& sections, const section& text,
                         std::uint32_t relocation_rva,
                         std::uint32_t relocation_size) noexcept {
  if (relocation_rva == 0 || relocation_size == 0) return false;
  const auto* relocation = section_for_rva(sections, relocation_rva, relocation_size);
  if (relocation == nullptr) return true;
  const auto file_offset = static_cast<std::uint64_t>(relocation->raw_offset) +
                           relocation_rva - relocation->virtual_address;
  if (!inside(file, file_offset, relocation_size)) return true;
  std::size_t offset = static_cast<std::size_t>(file_offset);
  const auto end = offset + relocation_size;
  while (offset < end) {
    std::uint32_t page = 0;
    std::uint32_t block_size = 0;
    if (!read(file, offset, page) || !read(file, offset + 4U, block_size) || block_size < 8U ||
        (block_size & 1U) != 0 || block_size > end - offset) {
      return true;
    }
    for (std::size_t item = offset + 8U; item < offset + block_size; item += 2U) {
      std::uint16_t entry = 0;
      if (!read(file, item, entry)) return true;
      if ((entry >> 12U) == 0) continue;
      const auto target = static_cast<std::uint64_t>(page) + (entry & 0x0fffU);
      const auto text_end = static_cast<std::uint64_t>(text.virtual_address) +
                            text.virtual_size;
      if (target >= text.virtual_address && target < text_end) {
        return true;
      }
    }
    offset += block_size;
  }
  return offset != end;
}

}  // namespace

int main(int argc, char** argv) {
  const bool verify = argc == 3 && std::string_view(argv[1]) == "--verify";
  if ((!verify && argc != 2) || (verify && argc != 3)) return 64;
  const char* path = argv[verify ? 2 : 1];
  std::ifstream input(path, std::ios::binary);
  std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(input), {}};
  std::uint16_t dos_magic = 0;
  std::uint32_t pe_offset = 0;
  std::uint32_t signature = 0;
  std::uint16_t machine = 0;
  std::uint16_t section_count = 0;
  std::uint16_t optional_size = 0;
  std::uint16_t file_flags = 0;
  if (!read(file, 0, dos_magic) || dos_magic != 0x5a4d || !read(file, 0x3c, pe_offset) ||
      !read(file, pe_offset, signature) || signature != 0x00004550 ||
      !read(file, pe_offset + 4U, machine) || machine != kAmd64 ||
      !read(file, pe_offset + 6U, section_count) || section_count == 0 ||
      !read(file, pe_offset + 20U, optional_size) || optional_size < 240U ||
      !read(file, pe_offset + 22U, file_flags) || (file_flags & kDll) == 0) {
    std::cerr << "MindGuard seal: unsupported PE32+ AMD64 DLL\n";
    return 65;
  }
  const auto optional = static_cast<std::size_t>(pe_offset) + 24U;
  std::uint16_t optional_magic = 0;
  std::uint16_t dll_flags = 0;
  std::uint32_t directory_count = 0;
  if (!read(file, optional, optional_magic) || optional_magic != kPe32Plus ||
      !read(file, optional + 70U, dll_flags) ||
      (dll_flags & kRequiredDllFlags) != kRequiredDllFlags ||
      !read(file, optional + 108U, directory_count) || directory_count < 6U ||
      !inside(file, optional + 112U, 6U * 8U)) {
    std::cerr << "MindGuard seal: PE hardening flags or directories are missing\n";
    return 66;
  }
  std::uint32_t certificate_offset = 0;
  std::uint32_t certificate_size = 0;
  std::uint32_t relocation_rva = 0;
  std::uint32_t relocation_size = 0;
  read(file, optional + 112U + 4U * 8U, certificate_offset);
  read(file, optional + 116U + 4U * 8U, certificate_size);
  read(file, optional + 112U + 5U * 8U, relocation_rva);
  read(file, optional + 116U + 5U * 8U, relocation_size);
  if (!verify && (certificate_offset != 0 || certificate_size != 0)) {
    std::cerr << "MindGuard seal: refusing to modify an Authenticode-signed DLL\n";
    return 67;
  }
  const auto section_table = optional + optional_size;
  if (!inside(file, section_table, static_cast<std::uint64_t>(section_count) * 40U)) return 68;
  std::vector<section> sections(section_count);
  const section* text = nullptr;
  const section* seal = nullptr;
  for (std::size_t index = 0; index < sections.size(); ++index) {
    if (!load_section(file, section_table + index * 40U, sections[index])) return 68;
    if (section_named(sections[index], ".text")) text = &sections[index];
    if (section_named(sections[index], ".mgseal")) seal = &sections[index];
  }
  if (text == nullptr || seal == nullptr || text->virtual_size == 0 ||
      text->virtual_size > text->raw_size || !inside(file, text->raw_offset, text->raw_size) ||
      seal->virtual_size < sizeof(mindguard::detail::integrity_seal_marker) ||
      seal->raw_size < sizeof(mindguard::detail::integrity_seal_marker) ||
      !inside(file, seal->raw_offset, seal->raw_size) ||
      (text->characteristics & (kCode | kExecute)) != (kCode | kExecute) ||
      (seal->characteristics & (kExecute | kWrite)) != 0) {
    std::cerr << "MindGuard seal: required PE sections are invalid\n";
    return 69;
  }
  if (text_has_relocation(file, sections, *text, relocation_rva, relocation_size)) {
    std::cerr << "MindGuard seal: .text contains base relocations\n";
    return 70;
  }
  const auto digest = mindguard::detail::integrity_hash(
      std::span<const std::uint8_t>(file).subspan(text->raw_offset, text->virtual_size));
  std::array<std::uint64_t, 2> stored{};
  std::memcpy(stored.data(), file.data() + seal->raw_offset, sizeof(stored));
  if (verify) {
    if (stored != digest) {
      std::cerr << "MindGuard seal: verification failed\n";
      return 71;
    }
    return 0;
  }
  if (stored != mindguard::detail::integrity_seal_marker) {
    std::cerr << "MindGuard seal: placeholder is not pristine\n";
    return 72;
  }
  std::memcpy(file.data() + seal->raw_offset, digest.data(), sizeof(digest));
  write<std::uint32_t>(file, optional + 64U, 0U);
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return output ? 0 : 73;
}
