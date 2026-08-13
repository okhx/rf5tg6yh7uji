#include "mindguard/detail/core.hpp"
#include "mindguard/detail/integrity_hash.hpp"

#include "vm_signal.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <link.h>
#include <mutex>
#include <span>
#include <string_view>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>
#include <x86intrin.h>

namespace mindguard::detail {

extern "C" [[gnu::used, gnu::visibility("hidden"), gnu::section(".mindguard.seal")]]
const volatile std::uint64_t __mindguard_text_seal[2] = {
    integrity_seal_marker[0], integrity_seal_marker[1]};
using runtime_entry = std::uint64_t (*)(std::uint64_t) noexcept;
extern "C" [[gnu::used, gnu::visibility("hidden"), gnu::section(".data.rel.ro")]]
runtime_entry const volatile __mindguard_runtime_relocation = &hardened_runtime_enter;

namespace {

std::once_flag debugger_once;
std::once_flag timing_once;
std::once_flag text_region_once;
std::atomic<std::uint64_t> timing_limit{0};
std::atomic<std::uint64_t> maps_deadline{0};
std::atomic<pid_t> expected_tracer{0};
const std::uint8_t* text_memory = nullptr;
std::size_t text_size = 0;

std::uint64_t timestamp() noexcept {
  unsigned auxiliary = 0;
  _mm_lfence();
  const auto value = __rdtscp(&auxiliary);
  _mm_lfence();
  return value;
}

bool file_contains(int descriptor, std::span<const std::string_view> needles) noexcept {
  std::array<char, 8192> buffer{};
  std::size_t carried = 0;
  for (;;) {
    const auto count = ::read(descriptor, buffer.data() + carried, buffer.size() - carried);
    if (count <= 0) return false;
    const auto size = carried + static_cast<std::size_t>(count);
    for (std::size_t index = 0; index < size; ++index) {
      if (buffer[index] >= 'A' && buffer[index] <= 'Z') buffer[index] += 'a' - 'A';
    }
    const std::string_view text(buffer.data(), size);
    for (const auto needle : needles) {
      if (text.find(needle) != std::string_view::npos) return true;
    }
    carried = std::min<std::size_t>(128U, size);
    std::copy_n(buffer.data() + size - carried, carried, buffer.data());
  }
}

pid_t tracer_pid() noexcept {
  const int descriptor = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) __mindguard_fail();
  std::array<char, 4096> buffer{};
  const auto count = ::read(descriptor, buffer.data(), buffer.size() - 1U);
  ::close(descriptor);
  if (count <= 0) __mindguard_fail();
  const std::string_view text(buffer.data(), static_cast<std::size_t>(count));
  const auto label = text.find("TracerPid:");
  if (label == std::string_view::npos) __mindguard_fail();
  auto current = label + std::string_view("TracerPid:").size();
  while (current < text.size() && (text[current] == ' ' || text[current] == '\t')) ++current;
  pid_t value = 0;
  const auto result = std::from_chars(text.data() + current, text.data() + text.size(), value);
  if (result.ec != std::errc{}) __mindguard_fail();
  return value;
}

void check_tracer_status() noexcept {
  const auto actual = tracer_pid();
  const auto expected = expected_tracer.load(std::memory_order_acquire);
  if (actual != expected && !(expected != 0 && actual == 0)) __mindguard_fail();
}

void establish_ptrace_guard() noexcept {
  check_tracer_status();
  errno = 0;
  if (::syscall(SYS_ptrace, PTRACE_TRACEME, 0, nullptr, nullptr) == -1) {
    if (errno != EPERM || tracer_pid() != 0) __mindguard_fail();
    return;
  }
  expected_tracer.store(::getppid(), std::memory_order_release);
}

void check_instrumentation_maps() noexcept {
  if (const auto* preload = std::getenv("LD_PRELOAD"); preload != nullptr && *preload != '\0') {
    __mindguard_fail();
  }
  const int descriptor = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) __mindguard_fail();
  constexpr std::array needles = {std::string_view("frida"), std::string_view("libgum"),
                                  std::string_view("gum-js-loop"),
                                  std::string_view("re.frida.server")};
  const bool found = file_contains(descriptor, needles);
  ::close(descriptor);
  if (found) __mindguard_fail();
}

void check_instrumentation_maps_at_use() noexcept {
  if (const auto* preload = std::getenv("LD_PRELOAD"); preload != nullptr && *preload != '\0') {
    __mindguard_fail();
  }
  const auto now = timestamp();
  auto deadline = maps_deadline.load(std::memory_order_acquire);
  if (now < deadline) return;
  const auto interval = std::max<std::uint64_t>(1U,
      timing_limit.load(std::memory_order_acquire) / 100U);
  if (maps_deadline.compare_exchange_strong(deadline, now + interval,
                                            std::memory_order_acq_rel)) {
    check_instrumentation_maps();
  }
}

std::uintptr_t executable_base() noexcept {
  std::uintptr_t base = 0;
  ::dl_iterate_phdr(
      [](dl_phdr_info* info, std::size_t, void* output) {
        if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
          *static_cast<std::uintptr_t*>(output) = static_cast<std::uintptr_t>(info->dlpi_addr);
          return 1;
        }
        return 0;
      },
      &base);
  return base;
}

bool relocation_anchor_layout() noexcept {
  struct ranges final {
    std::uintptr_t anchor = 0;
    std::uintptr_t target = 0;
    bool anchor_relro = false;
    bool target_executable = false;
  } state{reinterpret_cast<std::uintptr_t>(&__mindguard_runtime_relocation),
          reinterpret_cast<std::uintptr_t>(__mindguard_runtime_relocation)};
  ::dl_iterate_phdr(
      [](dl_phdr_info* info, std::size_t, void* opaque) {
        auto& current = *static_cast<ranges*>(opaque);
        if (info->dlpi_name != nullptr && info->dlpi_name[0] != '\0') return 0;
        for (std::size_t index = 0; index < info->dlpi_phnum; ++index) {
          const auto& header = info->dlpi_phdr[index];
          const auto begin = static_cast<std::uintptr_t>(info->dlpi_addr + header.p_vaddr);
          const auto end = begin + header.p_memsz;
          if (header.p_type == PT_GNU_RELRO && current.anchor >= begin && current.anchor < end) {
            current.anchor_relro = true;
          }
          if (header.p_type == PT_LOAD && current.target >= begin && current.target < end &&
              (header.p_flags & PF_X) != 0) {
            current.target_executable = true;
          }
        }
        return 1;
      },
      &state);
  return state.anchor_relro && state.target_executable;
}

void check_relocation_anchor() noexcept {
  static const bool layout_valid = relocation_anchor_layout();
  if (__mindguard_runtime_relocation != &hardened_runtime_enter || !layout_valid) __mindguard_fail();
}

void initialize_text_region() noexcept {
  const int descriptor = ::open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
  if (descriptor < 0) __mindguard_fail();
  Elf64_Ehdr header{};
  if (::pread(descriptor, &header, sizeof(header), 0) != sizeof(header) ||
      std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
      header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
      header.e_shentsize != sizeof(Elf64_Shdr) || header.e_shnum == 0 ||
      header.e_shstrndx >= header.e_shnum) {
    ::close(descriptor);
    __mindguard_fail();
  }
  std::vector<Elf64_Shdr> sections(header.e_shnum);
  const auto section_bytes = sections.size() * sizeof(Elf64_Shdr);
  if (::pread(descriptor, sections.data(), section_bytes, static_cast<off_t>(header.e_shoff)) !=
      static_cast<ssize_t>(section_bytes)) {
    ::close(descriptor);
    __mindguard_fail();
  }
  const auto& names_section = sections[header.e_shstrndx];
  std::vector<char> names(names_section.sh_size);
  if (::pread(descriptor, names.data(), names.size(), static_cast<off_t>(names_section.sh_offset)) !=
      static_cast<ssize_t>(names.size())) {
    ::close(descriptor);
    __mindguard_fail();
  }
  ::close(descriptor);
  const Elf64_Shdr* text = nullptr;
  for (const auto& section : sections) {
    if (section.sh_name >= names.size()) __mindguard_fail();
    if (std::string_view(names.data() + section.sh_name) == ".text") {
      text = &section;
      break;
    }
  }
  if (text == nullptr || text->sh_size == 0) __mindguard_fail();
  text_memory = reinterpret_cast<const std::uint8_t*>(executable_base() + text->sh_addr);
  text_size = static_cast<std::size_t>(text->sh_size);
}

void check_text_integrity() noexcept {
  std::call_once(text_region_once, initialize_text_region);
  const auto actual = integrity_hash({text_memory, text_size});
  const std::array<std::uint64_t, 2> expected = {__mindguard_text_seal[0],
                                                 __mindguard_text_seal[1]};
  if (actual != expected) __mindguard_fail();
}

template <class Function>
const std::uint8_t* code_address(Function function) noexcept {
  return reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uintptr_t>(function));
}

void check_prologue(const std::uint8_t* code) noexcept {
  if (code == nullptr || code[0] == 0xccU || code[0] == 0xe9U || code[0] == 0xebU ||
      (code[0] == 0xffU && code[1] == 0x25U) ||
      (code[0] == 0x48U && code[1] == 0xb8U)) {
    __mindguard_fail();
  }
}

void check_critical_prologues(std::uint64_t site) noexcept {
  constexpr std::array materializers = {&materialize_embedded_0, &materialize_embedded_1,
                                        &materialize_embedded_2, &materialize_embedded_3};
  check_prologue(code_address(materializers[site >> 62U]));
  check_prologue(code_address(&decode_share));
}

void calibrate_timing() noexcept {
  const auto wall_start = std::chrono::steady_clock::now();
  const auto cycle_start = timestamp();
  while (std::chrono::steady_clock::now() - wall_start < std::chrono::milliseconds(2)) {
    _mm_pause();
  }
  const auto elapsed_cycles = timestamp() - cycle_start;
  const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - wall_start).count();
  const auto cycles_per_ns = std::max<std::uint64_t>(1U, elapsed_cycles /
      static_cast<std::uint64_t>(std::max<std::int64_t>(1, elapsed_ns)));
  timing_limit.store(cycles_per_ns * 100000000ULL, std::memory_order_release);
}

void validate_runtime(std::uint64_t site) noexcept {
  std::call_once(debugger_once, establish_ptrace_guard);
  check_instrumentation_maps_at_use();
  check_relocation_anchor();
  check_text_integrity();
  check_critical_prologues(site);
  static const unsigned vm_score = vm_analysis_score();
  if (vm_score >= 2U) {
    check_relocation_anchor();
  }
}

}  // namespace

std::uint64_t hardened_runtime_enter(std::uint64_t site) noexcept {
  std::call_once(timing_once, calibrate_timing);
  return timestamp() ^ std::rotl(site, 17);
}

std::uint64_t hardened_callback_enter(std::uint64_t site) noexcept {
  validate_runtime(site);
  return timestamp() ^ std::rotl(site, 17);
}

void hardened_runtime_leave(std::uint64_t token, std::uint64_t site) noexcept {
  const auto started = token ^ std::rotl(site, 17);
  const auto elapsed = timestamp() - started;
  if (elapsed > timing_limit.load(std::memory_order_acquire)) __mindguard_fail();
}

}  // namespace mindguard::detail
