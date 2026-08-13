#include "vm_signal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>

#include <array>
#include <cstdint>
#include <intrin.h>
#include <vector>

namespace mindguard::detail {
namespace {

bool known_virtual_mac(const BYTE* address, ULONG length) noexcept {
  if (length < 3U) return false;
  constexpr std::array<std::array<BYTE, 3>, 7> prefixes = {{{0x00, 0x05, 0x69},
      {0x00, 0x0c, 0x29}, {0x00, 0x1c, 0x14}, {0x00, 0x50, 0x56},
      {0x08, 0x00, 0x27}, {0x52, 0x54, 0x00}, {0x00, 0x15, 0x5d}}};
  for (const auto& prefix : prefixes) {
    if (address[0] == prefix[0] && address[1] == prefix[1] && address[2] == prefix[2]) {
      return true;
    }
  }
  return false;
}

bool has_virtual_mac() noexcept {
  ULONG size = 15U * 1024U;
  std::vector<std::uint8_t> storage(size);
  auto status = ::GetAdaptersAddresses(AF_UNSPEC,
      GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
          GAA_FLAG_SKIP_DNS_SERVER,
      nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
  if (status == ERROR_BUFFER_OVERFLOW) {
    storage.resize(size);
    status = ::GetAdaptersAddresses(AF_UNSPEC,
        GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
            GAA_FLAG_SKIP_DNS_SERVER,
        nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()), &size);
  }
  if (status != NO_ERROR) return false;
  for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
       adapter != nullptr; adapter = adapter->Next) {
    if (known_virtual_mac(adapter->PhysicalAddress, adapter->PhysicalAddressLength)) return true;
  }
  return false;
}

}  // namespace

unsigned vm_analysis_score() noexcept {
  unsigned score = 0;
  int registers[4]{};
  __cpuid(registers, 1);
  if ((static_cast<unsigned>(registers[2]) & (1U << 31U)) != 0) ++score;
  if (has_virtual_mac()) ++score;
  const auto started = __rdtsc();
  for (unsigned index = 0; index < 64; ++index) {
    static_cast<void>(::GetCurrentProcessId());
    _ReadWriteBarrier();
  }
  if (__rdtsc() - started > 64U * 20000U) ++score;
  return score;
}

}  // namespace mindguard::detail
