#!/usr/bin/env bash
# MindGuard local ELF64LE x86_64 end-to-end integration test.
# All generated material (including the private seed and helper sources) stays in
# one 0700 temporary directory and is removed on every exit path.
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)

if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
  printf 'ОШИБКА: этот интеграционный тест требует Linux x86_64 (ELF64 little-endian)\n' >&2
  exit 2
fi

for command_name in cargo c++ chmod cmake cp find mktemp mkdir python3 stat; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    printf 'ОШИБКА: отсутствует обязательная команда: %s\n' "$command_name" >&2
    exit 2
  fi
done


old_umask=$(umask)
umask 077
tmp_dir=
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  # Do not print paths or command output containing generated key material.
  if [[ -n "${tmp_dir:-}" && -d "$tmp_dir" ]]; then
    if command -v shred >/dev/null 2>&1; then
      find "$tmp_dir" -depth -type f -exec shred -u -n 1 {} + 2>/dev/null || true
    fi
    find "$tmp_dir" -depth -delete 2>/dev/null || true
  fi
  umask "$old_umask" || true
  exit "$status"
}
trap cleanup EXIT
# Preserve a non-zero status when interrupted, while still running EXIT cleanup.
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mindguard-e2e.XXXXXX")
chmod 700 "$tmp_dir"
mkdir -- "$tmp_dir/cargo-target" "$tmp_dir/cargo-tmp"

# Build the protector from the current workspace sources into the private
# temporary directory.  No repository build artifact is used by this test.
(cd "$repo_root" && TMPDIR="$tmp_dir/cargo-tmp" CARGO_TARGET_DIR="$tmp_dir/cargo-target" cargo build --quiet --offline --locked -p mindguard-protector)
[[ -x "$tmp_dir/cargo-target/debug/mindguard-protector" ]] || {
  printf 'ОШИБКА: свежая сборка protector не создала исполняемый файл\n' >&2
  exit 1
}

# Build the independent C++ runtime from the current sources in a separate
# temporary build tree.
cmake -S "$repo_root/cpp" -B "$tmp_dir/cpp-build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$tmp_dir/cpp-build" --target mindguard_runtime --parallel
cpp_runtime="$tmp_dir/cpp-build/libmindguard_runtime.a"
[[ -f "$cpp_runtime" ]] || {
  printf 'ОШИБКА: свежая сборка C++ runtime не создала static library\n' >&2
  exit 1
}

seed_file="$tmp_dir/seed"
public_key_file="$tmp_dir/public.key"
input_file="$tmp_dir/input.elf"
protected_file="$tmp_dir/protected.elf"
tampered_file="$tmp_dir/tampered.elf"

# Generate exactly 32 random bytes; the seed is never sent to stdout/stderr.
(umask 077; if command -v openssl >/dev/null 2>&1; then
  openssl rand 32 >"$seed_file"
else
  dd if=/dev/urandom of="$seed_file" bs=32 count=1 status=none
fi)
[[ "$(stat -c '%s' "$seed_file")" == 32 ]] || { printf 'ОШИБКА: не удалось создать seed размером 32 байта\n' >&2; exit 1; }

# Use a real, harmless executable as input, copied only into the private temp dir.
input_source=
for candidate in /bin/true /usr/bin/true; do
  if [[ -f "$candidate" && -x "$candidate" ]]; then
    input_source=$candidate
    break
  fi
done
if [[ -z "$input_source" ]]; then
  printf 'ОШИБКА: не найден безопасный ELF executable (ожидался /bin/true или /usr/bin/true)\n' >&2
  exit 2
fi
cp -- "$input_source" "$input_file"
chmod 700 "$input_file"

"$tmp_dir/cargo-target/debug/mindguard-protector" protect \
  --input "$input_file" --output "$protected_file" --key-file "$seed_file"
[[ -x "$protected_file" ]] || { printf 'ОШИБКА: protector не создал исполняемый результат\n' >&2; exit 1; }

# Temporary helper: derive only the public key from the ephemeral seed.  The
# helper source and executable are also confined to the temporary directory.
cat >"$tmp_dir/public_key_helper.cpp" <<'CPP'
#include <array>
#include <cstddef>
#include <fstream>
#include <openssl/evp.h>
int main(int argc, char** argv) {
  if (argc != 3) return 64;
  std::array<unsigned char, 32> seed{};
  std::ifstream in(argv[1], std::ios::binary);
  in.read(reinterpret_cast<char*>(seed.data()), static_cast<std::streamsize>(seed.size()));
  if (!in || in.peek() != std::ifstream::traits_type::eof()) return 65;
  EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
  seed.fill(0);
  if (key == nullptr) return 66;
  std::array<unsigned char, 32> pub{};
  std::size_t len = pub.size();
  const int ok = EVP_PKEY_get_raw_public_key(key, pub.data(), &len);
  EVP_PKEY_free(key);
  if (ok != 1 || len != pub.size()) return 67;
  std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(pub.data()), static_cast<std::streamsize>(pub.size()));
  pub.fill(0);
  return out.good() ? 0 : 68;
}
CPP
c++ -std=c++20 -O2 -Wall -Wextra -Werror "$tmp_dir/public_key_helper.cpp" -lcrypto -o "$tmp_dir/public_key_helper"
"$tmp_dir/public_key_helper" "$seed_file" "$public_key_file"
[[ "$(stat -c '%s' "$public_key_file")" == 32 ]] || { printf 'ОШИБКА: helper открытого ключа создал неверный результат\n' >&2; exit 1; }

# Build a temporary C++ diagnostic runner against the independent C++ SDK.
cat >"$tmp_dir/cpp_runner.cpp" <<'CPP'
#include "mindguard_runtime/runtime.hpp"
#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  if (argc != 3) return 64;
  std::array<std::uint8_t, 32> key{};
  std::ifstream in(argv[2], std::ios::binary);
  in.read(reinterpret_cast<char*>(key.data()), static_cast<std::streamsize>(key.size()));
  if (!in || in.peek() != std::ifstream::traits_type::eof()) return 65;
  const auto result = mindguard_runtime::verify_file_for_diagnostics(argv[1], key);
  std::cout << mindguard_runtime::verify_error_code(result) << '\n';
  return result == mindguard_runtime::VerifyError::Success ? 0 : 1;
}
CPP
c++ -std=c++20 -O2 -Wall -Wextra -Werror \
  -I"$repo_root/cpp/include" "$tmp_dir/cpp_runner.cpp" \
  "$tmp_dir/cpp-build/libmindguard_runtime.a" -lcrypto -o "$tmp_dir/cpp_runner"

# Build a temporary Rust diagnostic runner against the independent Rust SDK.
mkdir -p "$tmp_dir/rust_runner/src"
cat >"$tmp_dir/rust_runner/Cargo.toml" <<EOF2
[package]
name = "mindguard-e2e-rust-runner"
version = "0.0.0"
edition = "2024"
publish = false
[dependencies]
mindguard-runtime-rust = { path = "$repo_root/crates/mindguard-runtime-rust" }
EOF2
cat >"$tmp_dir/rust_runner/src/main.rs" <<'RS'
use std::{env, fs, path::Path};
use mindguard_runtime_rust::verify_file_for_diagnostics;
fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() != 3 { std::process::exit(64); }
    let bytes = match fs::read(&args[2]) { Ok(v) => v, Err(_) => std::process::exit(65) };
    let key: [u8; 32] = match bytes.as_slice().try_into() { Ok(v) => v, Err(_) => std::process::exit(66) };
    match verify_file_for_diagnostics(Path::new(&args[1]), key) {
        Ok(()) => { println!("OK"); }
        Err(error) => { println!("{}", error.code()); std::process::exit(1); }
    }
}
RS
TMPDIR="$tmp_dir/cargo-tmp" CARGO_TARGET_DIR="$tmp_dir/rust-target" cargo build --quiet --offline --manifest-path "$tmp_dir/rust_runner/Cargo.toml"
rust_runner="$tmp_dir/rust-target/debug/mindguard-e2e-rust-runner"

run_both_accept() {
  local artifact=$1
  local rust_code cpp_code
  rust_code=$("$rust_runner" "$artifact" "$public_key_file")
  [[ "$rust_code" == OK ]] || { printf 'ОШИБКА: Rust SDK вернул %s для корректного артефакта\n' "$rust_code" >&2; exit 1; }
  cpp_code=$("$tmp_dir/cpp_runner" "$artifact" "$public_key_file")
  [[ "$cpp_code" == OK ]] || { printf 'ОШИБКА: C++ SDK вернул %s для корректного артефакта\n' "$cpp_code" >&2; exit 1; }
}
run_both_reject_digest() {
  local artifact=$1
  local rust_code cpp_code
  if rust_code=$("$rust_runner" "$artifact" "$public_key_file"); then
    printf 'ОШИБКА: Rust SDK принял изменённый артефакт\n' >&2; exit 1
  fi
  if [[ "$rust_code" != MG-V13 ]]; then
    printf 'ОШИБКА: Rust SDK вернул %s, ожидался MG-V13\n' "$rust_code" >&2; exit 1
  fi
  if cpp_code=$("$tmp_dir/cpp_runner" "$artifact" "$public_key_file"); then
    printf 'ОШИБКА: C++ SDK принял изменённый артефакт\n' >&2; exit 1
  fi
  if [[ "$cpp_code" != MG-V13 ]]; then
    printf 'ОШИБКА: C++ SDK вернул %s, ожидался MG-V13\n' "$cpp_code" >&2; exit 1
  fi
}

run_both_accept "$protected_file"
if ! "$protected_file"; then
  printf 'ОШИБКА: защищённый ELF завершился с ненулевым кодом\n' >&2
  exit 1
fi

# Mutate one byte in .text (a covered section) and no metadata byte.  The
# section table is read solely to identify the first .text byte, so this remains
# robust to the protector's appended metadata/table placement.
python3 - "$protected_file" "$tampered_file" <<'PY'
import struct, sys
src, dst = sys.argv[1:]
data = bytearray(open(src, 'rb').read())
if data[:4] != b'\x7fELF' or data[4:7] != b'\x02\x01\x01':
    raise SystemExit('input is not ELF64 little-endian')
shoff = struct.unpack_from('<Q', data, 40)[0]
shentsize = struct.unpack_from('<H', data, 58)[0]
shnum = struct.unpack_from('<H', data, 60)[0]
shstrndx = struct.unpack_from('<H', data, 62)[0]
def sh(index):
    off = shoff + index * shentsize
    return struct.unpack_from('<IIQQQQIIQQ', data, off)
_, _, _, _, str_off, str_size, _, _, _, _ = sh(shstrndx)
strings = data[str_off:str_off + str_size]
original = bytes(data)
text_offset = None
metadata_ranges = []
for index in range(shnum):
    name, typ, flags, addr, offset, size, *_ = sh(index)
    end = strings.find(b'\0', name)
    if end < 0:
        raise SystemExit('malformed section string table')
    section_name = strings[name:end]
    if section_name == b'.mindguard' and size:
        metadata_ranges.append((offset, offset + size))
    if section_name == b'.text' and size and text_offset is None:
        text_offset = offset
        if offset >= len(data):
            raise SystemExit('text section out of bounds')
if text_offset is None:
    raise SystemExit('no covered .text section found')
if any(start <= text_offset < end for start, end in metadata_ranges):
    raise SystemExit('selected .text byte lies inside .mindguard')
data[text_offset] ^= 1
if sum(a != b for a, b in zip(original, data)) != 1:
    raise SystemExit('mutation changed more than one byte')
open(dst, 'wb').write(data)
PY
run_both_reject_digest "$tampered_file"

printf 'MindGuard ELF64LE x86_64 E2E: УСПЕХ (Rust/C++ приняли; запуск успешен; изменение покрываемого байта => MG-V13)\n'
