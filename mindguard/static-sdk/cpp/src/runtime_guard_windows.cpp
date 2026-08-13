#include "mindguard/detail/core.hpp"
#include "mindguard/detail/integrity_hash.hpp"

#include "vm_signal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <string_view>
#include <x86intrin.h>

namespace mindguard::detail {

#if defined(__MINGW32__)
extern "C" HANDLE(WINAPI* __imp_CreateToolhelp32Snapshot)(DWORD, DWORD);
#endif

#if defined(_MSC_VER)
#pragma section(".mgseal", read)
#pragma section(".rdata$mg", read)
#define MG_PE_ALLOCATE(section_name) __declspec(allocate(section_name))
#define MG_PE_USED
#else
#define MG_PE_ALLOCATE(section_name) __attribute__((section(section_name)))
#define MG_PE_USED __attribute__((used))
#endif

extern "C" MG_PE_ALLOCATE(".mgseal") MG_PE_USED const volatile std::uint64_t
    __mindguard_text_seal[2] = {integrity_seal_marker[0], integrity_seal_marker[1]};
using runtime_entry = std::uint64_t (*)(std::uint64_t) noexcept;
extern "C" MG_PE_ALLOCATE(".rdata$mg") MG_PE_USED runtime_entry const volatile
    __mindguard_runtime_relocation = &hardened_runtime_enter;

namespace {

std::once_flag timing_once;
std::once_flag text_region_once;
std::atomic<std::uint64_t> timing_limit{0};
std::atomic<std::uint64_t> module_scan_deadline{0};
const std::uint8_t* image_base = nullptr;
const std::uint8_t* text_memory = nullptr;
std::size_t text_size = 0;

std::uint64_t timestamp() noexcept {
  unsigned auxiliary = 0;
  _mm_lfence();
  const auto value = __rdtscp(&auxiliary);
  _mm_lfence();
  return value;
}

bool executable_protection(DWORD protection) noexcept {
  if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
  const auto base = protection & 0xffU;
  return base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ;
}

bool readonly_protection(DWORD protection) noexcept {
  if ((protection & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
  const auto base = protection & 0xffU;
  return base == PAGE_READONLY || base == PAGE_WRITECOPY;
}

template <class Function>
const std::uint8_t* code_address(Function function) noexcept {
  return reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uintptr_t>(function));
}

const IMAGE_NT_HEADERS64* image_headers(const std::uint8_t* base) noexcept {
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
      dos->e_lfanew > 1024 * 1024) {
    __mindguard_fail();
  }
  const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (headers->Signature != IMAGE_NT_SIGNATURE ||
      headers->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
      (headers->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 ||
      headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
      headers->OptionalHeader.SizeOfImage == 0 ||
      (headers->OptionalHeader.DllCharacteristics &
       (IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT |
        IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA)) !=
          (IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE | IMAGE_DLLCHARACTERISTICS_NX_COMPAT |
           IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA)) {
    __mindguard_fail();
  }
  return headers;
}

void initialize_text_region() noexcept {
  MEMORY_BASIC_INFORMATION module{};
  if (::VirtualQuery(code_address(&hardened_runtime_enter), &module, sizeof(module)) !=
          sizeof(module) ||
      module.AllocationBase == nullptr || module.Type != MEM_IMAGE) {
    __mindguard_fail();
  }
  image_base = static_cast<const std::uint8_t*>(module.AllocationBase);
  const auto* headers = image_headers(image_base);
  const auto* sections = IMAGE_FIRST_SECTION(headers);
  const IMAGE_SECTION_HEADER* text = nullptr;
  for (unsigned index = 0; index < headers->FileHeader.NumberOfSections; ++index) {
    if (std::memcmp(sections[index].Name, ".text", 5) == 0) {
      text = &sections[index];
      break;
    }
  }
  if (text == nullptr || text->Misc.VirtualSize == 0 ||
      text->Misc.VirtualSize > text->SizeOfRawData ||
      text->VirtualAddress > headers->OptionalHeader.SizeOfImage ||
      text->Misc.VirtualSize > headers->OptionalHeader.SizeOfImage - text->VirtualAddress) {
    __mindguard_fail();
  }
  text_memory = image_base + text->VirtualAddress;
  text_size = text->Misc.VirtualSize;
  MEMORY_BASIC_INFORMATION memory{};
  if (::VirtualQuery(text_memory, &memory, sizeof(memory)) != sizeof(memory) ||
      memory.AllocationBase != image_base || memory.Type != MEM_IMAGE ||
      !executable_protection(memory.Protect)) {
    __mindguard_fail();
  }
}

void check_text_integrity() noexcept {
  std::call_once(text_region_once, initialize_text_region);
  const auto actual = integrity_hash({text_memory, text_size});
  const std::array<std::uint64_t, 2> expected = {__mindguard_text_seal[0],
                                                 __mindguard_text_seal[1]};
  if (actual != expected) __mindguard_fail();
}

void check_debugger() noexcept {
  BOOL remote = FALSE;
  if (::IsDebuggerPresent() != FALSE ||
      ::CheckRemoteDebuggerPresent(::GetCurrentProcess(), &remote) == FALSE || remote != FALSE) {
    __mindguard_fail();
  }
}

bool contains_signal(std::wstring_view text) noexcept {
  constexpr std::array needles = {std::wstring_view(L"frida"), std::wstring_view(L"libgum"),
                                  std::wstring_view(L"gum-js-loop"),
                                  std::wstring_view(L"re.frida.server")};
  std::array<wchar_t, MAX_PATH> lowered{};
  const auto size = std::min(text.size(), lowered.size() - 1U);
  for (std::size_t index = 0; index < size; ++index) {
    const auto value = text[index];
    lowered[index] = value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
  }
  const std::wstring_view normalized(lowered.data(), size);
  return std::any_of(needles.begin(), needles.end(), [&](auto needle) {
    return normalized.find(needle) != std::wstring_view::npos;
  });
}

void check_instrumentation_modules() noexcept {
  HANDLE snapshot = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                          ::GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE || ::GetLastError() != ERROR_BAD_LENGTH) break;
  }
  if (snapshot == INVALID_HANDLE_VALUE) __mindguard_fail();
  MODULEENTRY32W module{};
  module.dwSize = sizeof(module);
  if (::Module32FirstW(snapshot, &module) == FALSE) {
    ::CloseHandle(snapshot);
    __mindguard_fail();
  }
  do {
    if (contains_signal(module.szModule) || contains_signal(module.szExePath)) {
      ::CloseHandle(snapshot);
      __mindguard_fail();
    }
  } while (::Module32NextW(snapshot, &module) != FALSE);
  if (::GetLastError() != ERROR_NO_MORE_FILES) {
    ::CloseHandle(snapshot);
    __mindguard_fail();
  }
  ::CloseHandle(snapshot);
}

void check_instrumentation_at_use() noexcept {
  const auto now = timestamp();
  auto deadline = module_scan_deadline.load(std::memory_order_acquire);
  if (now < deadline) return;
  const auto interval = std::max<std::uint64_t>(1U,
      timing_limit.load(std::memory_order_acquire) / 100U);
  if (module_scan_deadline.compare_exchange_strong(deadline, now + interval,
                                                    std::memory_order_acq_rel)) {
    check_instrumentation_modules();
  }
}

void check_import(const char* name, const void* current) noexcept {
  const auto kernel = ::GetModuleHandleW(L"kernel32.dll");
  const auto expected = kernel == nullptr ? nullptr : ::GetProcAddress(kernel, name);
  if (expected == nullptr || reinterpret_cast<std::uintptr_t>(expected) !=
                                 reinterpret_cast<std::uintptr_t>(current)) {
    __mindguard_fail();
  }
}

void check_critical_imports() noexcept {
  check_import("IsDebuggerPresent", reinterpret_cast<const void*>(&::IsDebuggerPresent));
  check_import("CheckRemoteDebuggerPresent",
               reinterpret_cast<const void*>(&::CheckRemoteDebuggerPresent));
  check_import("VirtualQuery", reinterpret_cast<const void*>(&::VirtualQuery));
#if defined(__MINGW32__)
  check_import("CreateToolhelp32Snapshot",
               reinterpret_cast<const void*>(__imp_CreateToolhelp32Snapshot));
#else
  check_import("CreateToolhelp32Snapshot",
               reinterpret_cast<const void*>(&::CreateToolhelp32Snapshot));
#endif
}

void check_relocation_anchor() noexcept {
  MEMORY_BASIC_INFORMATION anchor{};
  MEMORY_BASIC_INFORMATION target{};
  if (__mindguard_runtime_relocation != &hardened_runtime_enter ||
      ::VirtualQuery(const_cast<runtime_entry*>(&__mindguard_runtime_relocation),
                     &anchor, sizeof(anchor)) != sizeof(anchor) ||
      ::VirtualQuery(code_address(__mindguard_runtime_relocation), &target, sizeof(target)) !=
          sizeof(target) ||
      anchor.Type != MEM_IMAGE || target.Type != MEM_IMAGE ||
      anchor.AllocationBase != target.AllocationBase || !readonly_protection(anchor.Protect) ||
      !executable_protection(target.Protect)) {
    __mindguard_fail();
  }
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
  check_debugger();
  check_instrumentation_at_use();
  check_critical_imports();
  check_relocation_anchor();
  check_text_integrity();
  check_critical_prologues(site);
  static const unsigned vm_score = vm_analysis_score();
  if (vm_score >= 2U) check_relocation_anchor();
}

}  // namespace

std::uint64_t hardened_runtime_enter(std::uint64_t site) noexcept {
  std::call_once(timing_once, calibrate_timing);
  return timestamp() ^ std::rotl(site, 17);
}

std::uint64_t hardened_callback_enter(std::uint64_t site) noexcept {
  std::call_once(timing_once, calibrate_timing);
  validate_runtime(site);
  return timestamp() ^ std::rotl(site, 17);
}

void hardened_runtime_leave(std::uint64_t token, std::uint64_t site) noexcept {
  const auto started = token ^ std::rotl(site, 17);
  if (timestamp() - started > timing_limit.load(std::memory_order_acquire)) {
    __mindguard_fail();
  }
}

}  // namespace mindguard::detail

#undef MG_PE_ALLOCATE
#undef MG_PE_USED
