#pragma once

#include <array>
#include <cstdint>
#include <filesystem>

namespace mindguard_runtime {

enum class VerifyError : std::uint8_t {
  Success = 0,
  SelfFileIO = 1,          // MG-V01
  UnsupportedFormat = 2,   // MG-V02
  MalformedFormat = 3,     // MG-V03
  MetadataAbsent = 4,      // MG-V04
  MetadataDuplicate = 5,   // MG-V05
  MetadataUnknown = 6,     // MG-V06
  MetadataMalformed = 7,   // MG-V07
  VersionUnsupported = 8,  // MG-V08
  CoverageUnsupported = 9, // MG-V09
  KeyConfigInvalid = 10,   // MG-V10
  KeyIdMismatch = 11,      // MG-V11
  SignatureInvalid = 12,   // MG-V12
  DigestMismatch = 13,     // MG-V13
  Internal = 14,            // MG-V14
};

[[nodiscard]] const char* verify_error_code(VerifyError error) noexcept;

/// Diagnostic/test API. Reads and verifies an ELF64 little-endian x86_64 file.
/// The supplied 32-byte public key is the trust root.
[[nodiscard]] VerifyError verify_file_for_diagnostics(
    const std::filesystem::path& file,
    const std::array<std::uint8_t, 32>& trusted_public_key) noexcept;

/// Production API. Verifies /proc/self/exe on Linux and aborts on every failure.
/// Returns only when verification succeeds.
void verify_or_terminate(
    const std::array<std::uint8_t, 32>& trusted_public_key) noexcept;

} // namespace mindguard_runtime
