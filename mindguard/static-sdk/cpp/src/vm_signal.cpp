#include "vm_signal.hpp"

#include <array>
#include <cpuid.h>
#include <cstdio>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <string_view>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

namespace mindguard::detail {
namespace {

bool known_virtual_mac(std::string_view value) noexcept {
  constexpr std::array prefixes = {"00:05:69", "00:0c:29", "00:1c:14", "00:50:56",
                                   "08:00:27", "52:54:00", "00:15:5d"};
  for (const auto prefix : prefixes) {
    if (value.starts_with(prefix)) return true;
  }
  return false;
}

bool has_virtual_mac() noexcept {
  auto* directory = ::opendir("/sys/class/net");
  if (directory == nullptr) return false;
  bool found = false;
  while (const auto* entry = ::readdir(directory)) {
    if (entry->d_name[0] == '.') continue;
    char path[256]{};
    const auto length = std::snprintf(path, sizeof(path), "/sys/class/net/%s/address",
                                      entry->d_name);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(path)) continue;
    const int descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) continue;
    char address[32]{};
    const auto count = ::read(descriptor, address, sizeof(address) - 1U);
    ::close(descriptor);
    if (count > 0 && known_virtual_mac(std::string_view(address, static_cast<std::size_t>(count)))) {
      found = true;
      break;
    }
  }
  ::closedir(directory);
  return found;
}

}  // namespace

unsigned vm_analysis_score() noexcept {
  unsigned score = 0;
  unsigned eax = 0;
  unsigned ebx = 0;
  unsigned ecx = 0;
  unsigned edx = 0;
  if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0 && (ecx & (1U << 31U)) != 0) ++score;
  if (has_virtual_mac()) ++score;
  const auto started = __rdtsc();
  for (unsigned index = 0; index < 64; ++index) {
    static_cast<void>(::syscall(SYS_getpid));
  }
  const auto cycles = __rdtsc() - started;
  if (cycles > 64U * 20000U) ++score;
  return score;
}

}  // namespace mindguard::detail
