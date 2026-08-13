#include "mindguard/detail/integrity_hash.hpp"

#include <elf.h>

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

template <class T>
const T* object_at(const std::vector<std::uint8_t>& file, std::size_t offset) {
  if (offset > file.size() || file.size() - offset < sizeof(T)) return nullptr;
  return reinterpret_cast<const T*>(file.data() + offset);
}

bool inside(const std::vector<std::uint8_t>& file, std::uint64_t offset,
            std::uint64_t size) {
  return offset <= file.size() && size <= file.size() - static_cast<std::size_t>(offset);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) return 64;
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(input), {}};
  const auto* header = object_at<Elf64_Ehdr>(file, 0);
  if (header == nullptr || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
      header->e_ident[EI_CLASS] != ELFCLASS64 || header->e_ident[EI_DATA] != ELFDATA2LSB ||
      header->e_shentsize != sizeof(Elf64_Shdr) || header->e_shstrndx >= header->e_shnum ||
      !inside(file, header->e_shoff,
              static_cast<std::uint64_t>(header->e_shnum) * sizeof(Elf64_Shdr))) {
    std::cerr << "MindGuard seal: unsupported ELF\n";
    return 65;
  }
  const auto* sections = object_at<Elf64_Shdr>(file, header->e_shoff);
  const auto& strings = sections[header->e_shstrndx];
  if (!inside(file, strings.sh_offset, strings.sh_size)) return 66;
  const auto* names = reinterpret_cast<const char*>(file.data() + strings.sh_offset);
  const Elf64_Shdr* text = nullptr;
  const Elf64_Shdr* seal = nullptr;
  for (std::size_t index = 0; index < header->e_shnum; ++index) {
    if (sections[index].sh_name >= strings.sh_size) return 67;
    const std::string_view name(names + sections[index].sh_name);
    if (name == ".text") text = &sections[index];
    if (name == ".mindguard.seal") seal = &sections[index];
  }
  if (text == nullptr || seal == nullptr || !inside(file, text->sh_offset, text->sh_size) ||
      seal->sh_size != sizeof(mindguard::detail::integrity_seal_marker) ||
      !inside(file, seal->sh_offset, seal->sh_size)) {
    std::cerr << "MindGuard seal: required sections are missing\n";
    return 68;
  }
  if (!std::equal(mindguard::detail::integrity_seal_marker.begin(),
                  mindguard::detail::integrity_seal_marker.end(),
                  reinterpret_cast<const std::uint64_t*>(file.data() + seal->sh_offset))) {
    std::cerr << "MindGuard seal: placeholder is not pristine\n";
    return 69;
  }
  const auto digest = mindguard::detail::integrity_hash(
      std::span<const std::uint8_t>(file).subspan(text->sh_offset, text->sh_size));
  std::memcpy(file.data() + seal->sh_offset, digest.data(), sizeof(digest));
  std::ofstream output(argv[1], std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  return output ? 0 : 70;
}
