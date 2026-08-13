#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cargo_bin=${CARGO:-cargo}
rustc_bin=${RUSTC:-rustc}
rustdoc_bin=${RUSTDOC:-rustdoc}
mingw_root=${MINDGUARD_MINGW_ROOT:-/usr}
mingw_target="$mingw_root/x86_64-w64-mingw32"
[[ -d "$mingw_root/lib/gcc/x86_64-w64-mingw32" ]] || {
  printf 'ОШИБКА: MINDGUARD_MINGW_ROOT не содержит x86_64 MinGW GCC runtime\n' >&2
  exit 2
}
gcc_runtime=$(find "$mingw_root/lib/gcc/x86_64-w64-mingw32" -mindepth 1 -maxdepth 1 \
  -type d -name '*-posix' -print -quit)
for command_name in "$cargo_bin" "$rustc_bin" "$rustdoc_bin" clang++-18 cmake \
    find grep llvm-config-18 llvm-nm llvm-objdump llvm-readobj llvm-strip mktemp python3 strings; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf 'ОШИБКА: отсутствует команда: %s\n' "$command_name" >&2
    exit 2
  }
done
[[ -f "$mingw_target/include/windows.h" && -n "$gcc_runtime" ]] || {
  printf 'ОШИБКА: MINDGUARD_MINGW_ROOT не содержит x86_64 MinGW sysroot\n' >&2
  exit 2
}

old_umask=$(umask)
umask 077
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mindguard-hardened-pe.XXXXXX")
seed="$tmp_dir/seed"
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  find "$tmp_dir" -depth -delete 2>/dev/null || true
  umask "$old_umask" || true
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

python3 -c 'import os, sys; open(sys.argv[1], "wb").write(os.urandom(32))' "$seed"
export CARGO_TARGET_DIR="$tmp_dir/tools" RUSTC="$rustc_bin" RUSTDOC="$rustdoc_bin"
"$cargo_bin" build --quiet --offline --locked --manifest-path "$repo_root/static-sdk/build/Cargo.toml"
builder="$CARGO_TARGET_DIR/debug/mindguard-build"
scanner="$CARGO_TARGET_DIR/debug/mindguard-scan"
cmake -S "$repo_root/static-sdk/llvm-pass" -B "$tmp_dir/llvm-pass" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)" >/dev/null
cmake --build "$tmp_dir/llvm-pass" --parallel >/dev/null
pass_plugin="$tmp_dir/llvm-pass/MindGuardPass.so"
clang++-18 -std=c++20 -O2 -I "$repo_root/static-sdk/cpp/include" \
  "$repo_root/static-sdk/cpp/tools/seal_pe.cpp" -o "$tmp_dir/mindguard_seal_pe"

cxx_headers="$gcc_runtime/include/c++"
cross_flags="--sysroot=$mingw_target -isystem $cxx_headers -isystem $cxx_headers/x86_64-w64-mingw32 -B$mingw_root/bin"
cross_link_flags="-L$gcc_runtime"
cmake_args=(
  -S "$repo_root/static-sdk/cpp/tests/pe32plus_project"
  -DCMAKE_SYSTEM_NAME=Windows
  -DCMAKE_SYSTEM_PROCESSOR=x86_64
  -DCMAKE_CXX_COMPILER=clang++-18
  -DCMAKE_CXX_COMPILER_TARGET=x86_64-w64-windows-gnu
  -DCMAKE_CXX_FLAGS="$cross_flags"
  -DCMAKE_EXE_LINKER_FLAGS="$cross_link_flags"
  -DCMAKE_SHARED_LINKER_FLAGS="$cross_link_flags"
  -DCMAKE_AR="$mingw_root/bin/x86_64-w64-mingw32-ar"
  -DCMAKE_RANLIB="$mingw_root/bin/x86_64-w64-mingw32-ranlib"
  -DCMAKE_BUILD_TYPE=Release
  -DMINDGUARD_PROFILE=Hardened
  -DMINDGUARD_BUILD_TOOL="$builder"
  -DMINDGUARD_SCAN_TOOL="$scanner"
  -DMINDGUARD_LLVM_PASS_PLUGIN="$pass_plugin"
)

if PATH="$mingw_root/bin:$PATH" MG_BUILD_SEED_FILE="$seed" cmake "${cmake_args[@]}" \
    -B "$tmp_dir/missing-sealer" >/dev/null 2>&1; then
  printf 'ОШИБКА: cross CMake принял отсутствующий host PE sealer\n' >&2
  exit 1
fi
if PATH="$mingw_root/bin:$PATH" MG_BUILD_SEED_FILE="$seed" cmake "${cmake_args[@]}" \
    -B "$tmp_dir/executable" -DMINDGUARD_SEAL_TOOL="$tmp_dir/mindguard_seal_pe" \
    -DMINDGUARD_PE_NEGATIVE_EXECUTABLE=ON >/dev/null 2>&1; then
  printf 'ОШИБКА: Windows Hardened принял EXE вместо DLL\n' >&2
  exit 1
fi
PATH="$mingw_root/bin:$PATH" MG_BUILD_SEED_FILE="$seed" cmake "${cmake_args[@]}" \
  -B "$tmp_dir/build" -DMINDGUARD_SEAL_TOOL="$tmp_dir/mindguard_seal_pe" >/dev/null
PATH="$mingw_root/bin:$PATH" MG_BUILD_SEED_FILE="$seed" \
  cmake --build "$tmp_dir/build" --parallel >/dev/null

artifact=$(find "$tmp_dir/build" -name 'mindguard_hardened_pe.dll' -print -quit)
[[ -n "$artifact" ]]
"$tmp_dir/mindguard_seal_pe" --verify "$artifact"
llvm-readobj --file-headers --sections --coff-exports "$artifact" >"$tmp_dir/pe.txt"
grep -q 'Format: COFF-x86-64' "$tmp_dir/pe.txt"
grep -q 'Magic: 0x20B' "$tmp_dir/pe.txt"
grep -q 'IMAGE_FILE_DLL' "$tmp_dir/pe.txt"
grep -q 'IMAGE_DLL_CHARACTERISTICS_DYNAMIC_BASE' "$tmp_dir/pe.txt"
grep -q 'IMAGE_DLL_CHARACTERISTICS_HIGH_ENTROPY_VA' "$tmp_dir/pe.txt"
grep -q 'IMAGE_DLL_CHARACTERISTICS_NX_COMPAT' "$tmp_dir/pe.txt"
grep -q 'Name: .mgseal' "$tmp_dir/pe.txt"
grep -q 'Name: mindguard_pe_probe' "$tmp_dir/pe.txt"
llvm-nm -C "$artifact" >"$tmp_dir/pe.nm"
llvm-objdump -d -C "$artifact" >"$tmp_dir/pe.disasm"
grep -q '__mindguard_text_seal' "$tmp_dir/pe.nm"
grep -q '__mindguard_guard_thunk_' "$tmp_dir/pe.nm"
grep -q '__mindguard_threaded_targets' "$tmp_dir/pe.nm"
grep -Eq 'jmpq?[[:space:]]+\*' "$tmp_dir/pe.disasm"

python3 -c 'import os, struct, sys
data = bytearray(open(sys.argv[1], "rb").read())
pe = struct.unpack_from("<I", data, 0x3c)[0]
count = struct.unpack_from("<H", data, pe + 6)[0]
optional = struct.unpack_from("<H", data, pe + 20)[0]
table = pe + 24 + optional
sections = {}
for index in range(count):
    at = table + index * 40
    name = bytes(data[at:at + 8]).split(b"\0", 1)[0]
    sections[name] = struct.unpack_from("<IIII", data, at + 8)
for name, output in ((b".text", sys.argv[2]), (b".mgseal", sys.argv[3])):
    virtual_size, _, _, raw = sections[name]
    copy = bytearray(data)
    copy[raw + min(virtual_size - 1, 31)] ^= 1
    open(output, "wb").write(copy)
    os.chmod(output, os.stat(sys.argv[1]).st_mode)
signed = bytearray(data)
certificate = pe + 24 + 112 + 4 * 8
struct.pack_into("<II", signed, certificate, len(signed), 8)
open(sys.argv[4], "wb").write(signed)
os.chmod(sys.argv[4], os.stat(sys.argv[1]).st_mode)
open(sys.argv[5], "wb").write(data[:64])' "$artifact" \
  "$tmp_dir/tampered-text.dll" "$tmp_dir/tampered-seal.dll" "$tmp_dir/signed.dll" \
  "$tmp_dir/truncated.dll"
if "$tmp_dir/mindguard_seal_pe" --verify "$tmp_dir/tampered-text.dll" >/dev/null 2>&1 ||
   "$tmp_dir/mindguard_seal_pe" --verify "$tmp_dir/tampered-seal.dll" >/dev/null 2>&1; then
  printf 'ОШИБКА: PE seal verification приняла tampered DLL\n' >&2
  exit 1
fi
if "$tmp_dir/mindguard_seal_pe" "$artifact" >/dev/null 2>&1; then
  printf 'ОШИБКА: PE sealer повторно принял уже sealed DLL\n' >&2
  exit 1
fi
signed_output=$("$tmp_dir/mindguard_seal_pe" "$tmp_dir/signed.dll" 2>&1) && {
  printf 'ОШИБКА: PE sealer изменил Authenticode DLL\n' >&2
  exit 1
}
grep -q 'refusing to modify an Authenticode-signed DLL' <<<"$signed_output"
if "$tmp_dir/mindguard_seal_pe" "$tmp_dir/truncated.dll" >/dev/null 2>&1; then
  printf 'ОШИБКА: PE sealer принял truncated input\n' >&2
  exit 1
fi

llvm-strip --strip-all "$artifact"
"$tmp_dir/mindguard_seal_pe" --verify "$artifact"
generated=$(find "$tmp_dir/build/mindguard/mindguard_hardened_pe" \
  -name mindguard_generated.hpp -print -quit)
"$scanner" artifact-cpp --artifact "$artifact" \
  --input "$repo_root/static-sdk/cpp/tests/pe32plus_project/dll.cpp" \
  --generated "$generated" --audit "$tmp_dir/final.audit"
grep -qx 'profile=hardened' "$tmp_dir/final.audit"
grep -qx 'sites=1' "$tmp_dir/final.audit"
if strings "$artifact" | grep -Eq 'pe32plus-hardened-secret|/home/ubuntu1/mindguard'; then
  printf 'ОШИБКА: plaintext или checkout path найден в PE32+ DLL\n' >&2
  exit 1
fi

printf 'MindGuard C++ Hardened PE32+ DLL E2E: УСПЕХ (seal, ASLR/NX, tamper)\n'
