#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cargo_bin=${CARGO:-cargo}
rustc_bin=${RUSTC:-rustc}
rustdoc_bin=${RUSTDOC:-rustdoc}
for command_name in "$cargo_bin" "$rustc_bin" "$rustdoc_bin" clang++-18 cmake cmp cp dd file find grep llvm-config-18 llvm-strip mktemp nm objdump od python3 readelf sed sort stat strings touch truncate wc; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf 'ОШИБКА: отсутствует команда: %s\n' "$command_name" >&2
    exit 2
  }
done

old_umask=$(umask)
umask 077
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mindguard-hardened-cpp.XXXXXX")
seed="$tmp_dir/seed"
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  [[ ! -f "$seed" ]] || shred -u -n 1 "$seed" 2>/dev/null || true
  find "$tmp_dir" -depth -delete 2>/dev/null || true
  umask "$old_umask" || true
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

dd if=/dev/urandom of="$seed" bs=32 count=1 status=none
export CARGO_TARGET_DIR="$tmp_dir/tools" RUSTC="$rustc_bin" RUSTDOC="$rustdoc_bin"
"$cargo_bin" build --quiet --offline --locked --manifest-path "$repo_root/static-sdk/build/Cargo.toml"
builder="$CARGO_TARGET_DIR/debug/mindguard-build"
scanner="$CARGO_TARGET_DIR/debug/mindguard-scan"
cmake -S "$repo_root/static-sdk/llvm-pass" -B "$tmp_dir/llvm-pass" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$(llvm-config-18 --cmakedir)" >/dev/null
cmake --build "$tmp_dir/llvm-pass" --parallel >/dev/null
pass_plugin="$tmp_dir/llvm-pass/MindGuardPass.so"
[[ -f "$pass_plugin" ]]

if env -u MG_BUILD_SEED_FILE cmake \
    -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$tmp_dir/missing-seed" \
    -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
    -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
    -DMINDGUARD_SCAN_TOOL="$scanner" >/dev/null 2>&1; then
  printf 'ОШИБКА: CMake Hardened принял отсутствующий MG_BUILD_SEED_FILE\n' >&2
  exit 1
fi
if MG_BUILD_SEED_FILE="$seed" cmake \
    -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$tmp_dir/missing-plugin" \
    -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
    -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
    -DMINDGUARD_SCAN_TOOL="$scanner" \
    -DMINDGUARD_LLVM_PASS_PLUGIN="$tmp_dir/absent.so" >/dev/null 2>&1; then
  printf 'ОШИБКА: CMake Hardened принял отсутствующий LLVM pass plugin\n' >&2
  exit 1
fi

MG_BUILD_SEED_FILE="$seed" cmake \
  -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$tmp_dir/build" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
  -DMINDGUARD_SCAN_TOOL="$scanner" \
  -DMINDGUARD_LLVM_PASS_PLUGIN="$pass_plugin" >/dev/null
MG_BUILD_SEED_FILE="$seed" cmake --build "$tmp_dir/build" --parallel >/dev/null
core_object=$(find "$tmp_dir/build/mindguard-static" -name 'core.cpp.o' -print -quit)
[[ -n "$core_object" ]]
file "$core_object" | grep -q 'ELF.*relocatable'
manifest="$tmp_dir/build/mindguard-static/mindguard/obfuscation-manifest.txt"
[[ -f "$manifest" && "$(stat -c '%a' "$manifest")" == 600 ]]
obfuscation_seed=$(sed -n 's/^seed=//p' "$manifest")
[[ "$obfuscation_seed" =~ ^[0-9a-f]{64}$ ]]
clang++-18 -std=c++20 -O2 -S -emit-llvm -fno-lto \
  "-fpass-plugin=$pass_plugin" -DMINDGUARD_PROFILE_HARDENED=1 \
  -DMINDGUARD_OBFUSCATION_SEED_0="0x${obfuscation_seed:0:16}ULL" \
  -DMINDGUARD_OBFUSCATION_SEED_1="0x${obfuscation_seed:16:16}ULL" \
  -DMINDGUARD_OBFUSCATION_SEED_2="0x${obfuscation_seed:32:16}ULL" \
  -DMINDGUARD_OBFUSCATION_SEED_3="0x${obfuscation_seed:48:16}ULL" \
  -I "$repo_root/static-sdk/cpp/include" "$repo_root/static-sdk/cpp/src/core.cpp" \
  -o "$tmp_dir/protected-core.ll"
grep -q 'indirectbr' "$tmp_dir/protected-core.ll"
grep -q '!mindguard.mba' "$tmp_dir/protected-core.ll"
grep -q '__mindguard_opaque_stack_thunk' "$tmp_dir/protected-core.ll"

MG_BUILD_SEED_FILE="$seed" cmake \
  -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$tmp_dir/build-other" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
  -DMINDGUARD_SCAN_TOOL="$scanner" \
  -DMINDGUARD_LLVM_PASS_PLUGIN="$pass_plugin" >/dev/null
MG_BUILD_SEED_FILE="$seed" cmake --build "$tmp_dir/build-other" --parallel >/dev/null
other_object=$(find "$tmp_dir/build-other/mindguard-static" -name 'core.cpp.o' -print -quit)
other_manifest="$tmp_dir/build-other/mindguard-static/mindguard/obfuscation-manifest.txt"
other_seed=$(sed -n 's/^seed=//p' "$other_manifest")
[[ "$other_seed" =~ ^[0-9a-f]{64}$ && "$other_seed" != "$obfuscation_seed" ]]
if cmp -s "$core_object" "$other_object"; then
  printf 'ОШИБКА: разные per-build seed дали одинаковый static core object\n' >&2
  exit 1
fi

MG_BUILD_SEED_FILE="$seed" cmake \
  -S "$repo_root/static-sdk/cpp/tests/hardened_project" -B "$tmp_dir/build-replay" \
  -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DMINDGUARD_PROFILE=Hardened -DMINDGUARD_BUILD_TOOL="$builder" \
  -DMINDGUARD_SCAN_TOOL="$scanner" \
  -DMINDGUARD_LLVM_PASS_PLUGIN="$pass_plugin" \
  -DMINDGUARD_OBFUSCATION_SEED="$obfuscation_seed" >/dev/null
MG_BUILD_SEED_FILE="$seed" cmake --build "$tmp_dir/build-replay" --parallel >/dev/null
replay_object=$(find "$tmp_dir/build-replay/mindguard-static" -name 'core.cpp.o' -print -quit)
cmp -s "$core_object" "$replay_object"
touch "$seed"
MG_BUILD_SEED_FILE="$seed" cmake --build "$tmp_dir/build" --parallel >/dev/null
artifact="$tmp_dir/build/hardened_generated"
"$artifact"
generated_early=$(find "$tmp_dir/build/mindguard/hardened_generated" -name mindguard_generated.hpp -print -quit)
python3 -c 'import os, re, struct, subprocess, sys
artifact = bytearray(open(sys.argv[1], "rb").read())
header = open(sys.argv[2], encoding="utf-8").read()
site = int(re.search(r"id = 0x([0-9a-f]{16})ULL", header).group(1), 16)
variant = site >> 62
value = site ^ 0x6d696e6467756172 ^ variant * 0x9e3779b97f4a7c15
value &= (1 << 64) - 1
value = ((value ^ (value >> 30)) * 0xbf58476d1ce4e5b9) & ((1 << 64) - 1)
value = ((value ^ (value >> 27)) * 0x94d049bb133111eb) & ((1 << 64) - 1)
marker = struct.pack("<Q", value ^ (value >> 31))
offset = artifact.find(marker)
assert offset >= 0 and artifact.find(marker, offset + 1) < 0
artifact[offset] ^= 1
open(sys.argv[3], "wb").write(artifact)
os.chmod(sys.argv[3], os.stat(sys.argv[1]).st_mode)
result = subprocess.run([sys.argv[3]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
assert result.returncode != 0 and not result.stdout' \
  "$artifact" "$generated_early" "$tmp_dir/tampered-watermark"
benchmark_output=$("$tmp_dir/build/hardened_benchmark_4k")
[[ "$benchmark_output" =~ ^[0-9]+\ [0-9]+$ ]]
benchmark_ns=${benchmark_output%% *}
[[ "$benchmark_ns" -lt 100000 ]]
slow_status=$(MINDGUARD_E2E_SLOW_CALLBACK=1 python3 -c 'import subprocess, sys
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
print(result.returncode)' "$artifact")
[[ "$slow_status" != 0 ]]
nm -C "$artifact" >"$tmp_dir/pre-strip.nm"
nm -C -S "$artifact" >"$tmp_dir/pre-strip-sizes.nm"
objdump -d -C "$artifact" >"$tmp_dir/pre-strip.disasm"
python3 -c 'import sys
sizes = [int(parts[1], 16) for line in open(sys.argv[1], encoding="utf-8")
         if len(parts := line.split(None, 3)) == 4 and "mindguard::detail::materialize_embedded_" in parts[3]]
assert sizes and max(sizes) <= 4096, sizes' "$tmp_dir/pre-strip-sizes.nm"
[[ "$(grep -c 'mindguard::detail::materialize_embedded_[0-3]' "$tmp_dir/pre-strip.nm")" -ge 2 ]]
[[ "$(grep -E '(call|jmp).*mindguard::detail::materialize_embedded_[0-3]' "$tmp_dir/pre-strip.disasm" | \
      sed -E 's/.*materialize_embedded_([0-3]).*/\1/' | sort -u | wc -l)" -ge 2 ]]
[[ "$(grep -c '__mindguard_cfg_guard_' "$tmp_dir/pre-strip.nm")" -ge 2 ]]
[[ "$(grep -Ec ' [rR] __mindguard_cfg_guard_' "$tmp_dir/pre-strip.nm")" -ge 2 ]]
[[ "$(grep -c '__mindguard_cfg_guard_' "$tmp_dir/pre-strip.disasm")" -ge 6 ]]
[[ "$(grep -c '__mindguard_guard_thunk_' "$tmp_dir/pre-strip.nm")" -ge 2 ]]
[[ "$(grep -c '__mindguard_threaded_targets' "$tmp_dir/pre-strip.nm")" -ge 6 ]]
[[ "$(grep -Ec 'jmp[[:space:]]+\*' "$tmp_dir/pre-strip.disasm")" -ge 6 ]]
[[ "$(grep -c 'mindguard::detail::(anonymous namespace)::output_junk_round' "$tmp_dir/pre-strip.nm")" -ge 1 ]]
[[ "$(grep -E 'call.*output_junk_round' "$tmp_dir/pre-strip.disasm" | wc -l)" -ge 2 ]]
python3 -c 'import os, re, struct, sys
artifact = bytearray(open(sys.argv[1], "rb").read())
header = open(sys.argv[2], encoding="utf-8").read()
variant = int(re.search(r"id = 0x([0-9a-f]{16})ULL", header).group(1), 16) >> 62
symbols = open(sys.argv[3], encoding="utf-8").read()
address = int(re.search(r"^([0-9a-f]+) [tT] .*materialize_embedded_%d\(" % variant,
                        symbols, re.M).group(1), 16)
program_offset = struct.unpack_from("<Q", artifact, 32)[0]
program_entry, program_count = struct.unpack_from("<HH", artifact, 54)
for index in range(program_count):
    item = struct.unpack_from("<IIQQQQQ", artifact, program_offset + index * program_entry)
    if item[0] == 1 and item[3] <= address < item[3] + item[5]:
        artifact[item[2] + address - item[3]] = 0xcc
        break
else:
    raise AssertionError("materializer is not mapped")
section_offset = struct.unpack_from("<Q", artifact, 40)[0]
section_entry, section_count, strings_index = struct.unpack_from("<HHH", artifact, 58)
sections = [struct.unpack_from("<IIQQQQIIQQ", artifact, section_offset + i * section_entry)
            for i in range(section_count)]
strings = artifact[sections[strings_index][4]:sections[strings_index][4] + sections[strings_index][5]]
for item in sections:
    if strings[item[0]:].split(b"\0", 1)[0] == b".mindguard.seal":
        artifact[item[4]:item[4] + 16] = struct.pack("<QQ", 0x4d475345414c3031,
                                                     0xb6b8acba9eb3cfce)
        break
open(sys.argv[4], "wb").write(artifact)
os.chmod(sys.argv[4], os.stat(sys.argv[1]).st_mode)' \
  "$artifact" "$generated_early" "$tmp_dir/pre-strip.nm" "$tmp_dir/int3-resealed"
"$tmp_dir/build/mindguard-static/mindguard_seal_elf" "$tmp_dir/int3-resealed"
int3_status=$(python3 -c 'import subprocess, sys
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
assert not result.stdout
print(result.returncode)' "$tmp_dir/int3-resealed")
[[ "$int3_status" != 0 ]]
preload_status=$(LD_PRELOAD="$tmp_dir/absent-frida-gadget.so" python3 -c 'import subprocess, sys
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
assert not result.stdout
print(result.returncode)' "$artifact" 2>/dev/null)
[[ "$preload_status" != 0 ]]
python3 -c 'import os, re, struct, subprocess, sys
artifact = bytearray(open(sys.argv[1], "rb").read())
symbols = open(sys.argv[2], encoding="utf-8").read()
match = re.search(r"^([0-9a-f]+) [rR] __mindguard_cfg_guard_", symbols, re.M)
assert match and artifact[:4] == b"\x7fELF" and artifact[4:6] == b"\x02\x01"
address = int(match.group(1), 16)
program_offset = struct.unpack_from("<Q", artifact, 32)[0]
entry_size, count = struct.unpack_from("<HH", artifact, 54)
file_offset = None
for index in range(count):
    offset = program_offset + index * entry_size
    kind, _, segment_offset, virtual, _, file_size, _ = struct.unpack_from("<IIQQQQQ", artifact, offset)
    if kind == 1 and virtual <= address < virtual + file_size:
        file_offset = segment_offset + address - virtual
        break
assert file_offset is not None
artifact[file_offset] ^= 1
open(sys.argv[3], "wb").write(artifact)
os.chmod(sys.argv[3], os.stat(sys.argv[1]).st_mode)
result = subprocess.run([sys.argv[3]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
assert result.returncode != 0 and not result.stdout' \
  "$artifact" "$tmp_dir/pre-strip.nm" "$tmp_dir/tampered-guard"
file "$artifact" | grep -q 'pie executable'
if objdump -h "$artifact" | grep -q '\.note\.gnu\.build-id'; then
  printf 'ОШИБКА: Hardened artifact содержит linker build-id\n' >&2
  exit 1
fi
readelf -W -l "$artifact" >"$tmp_dir/program-headers"
grep -q 'GNU_RELRO' "$tmp_dir/program-headers"
grep 'GNU_STACK' "$tmp_dir/program-headers" | grep -qv ' RWE '
readelf -W -d "$artifact" | grep -q 'BIND_NOW'
readelf -W -S "$artifact" | grep -q '\.mindguard\.seal'
llvm-strip --strip-all "$artifact"
if nm "$artifact" 2>/dev/null | grep -q mindguard; then
  printf 'ОШИБКА: MindGuard symbol остался после strip\n' >&2
  exit 1
fi
generated=$(find "$tmp_dir/build/mindguard/hardened_generated" -name mindguard_generated.hpp -print -quit)
audit=$(find "$tmp_dir/build/mindguard/hardened_generated" -name audit.txt -print -quit)
[[ -n "$generated" && -n "$audit" ]]
"$scanner" artifact-cpp --artifact "$artifact" \
  --input "$repo_root/static-sdk/cpp/tests/hardened_project/main.cpp" \
  --generated "$generated" \
  --audit "$tmp_dir/final.audit"
grep -qx 'profile=hardened' "$tmp_dir/final.audit"
grep -qx 'sites=6' "$tmp_dir/final.audit"
grep -qx 'embedded=6' "$tmp_dir/final.audit"
grep -qx 'absent=2' "$tmp_dir/final.audit"
grep -qx 'runtime_reconstruction=4' "$tmp_dir/final.audit"
cp "$artifact" "$tmp_dir/leaked"
printf 'generated\0hardened-secret' >>"$tmp_dir/leaked"
if "$scanner" artifact-cpp --artifact "$tmp_dir/leaked" \
    --input "$repo_root/static-sdk/cpp/tests/hardened_project/main.cpp" \
    --generated "$generated" --audit "$tmp_dir/leaked.audit" >/dev/null 2>&1; then
  printf 'ОШИБКА: artifact scanner не обнаружил deliberate plaintext leak\n' >&2
  exit 1
fi
[[ ! -e "$tmp_dir/leaked.audit" ]]
if strings "$artifact" | grep -Eq 'hardened-secret|packed-byte-secret|/home/ubuntu1/mindguard'; then
  printf 'ОШИБКА: plaintext или checkout path найден в Hardened artifact\n' >&2
  exit 1
fi
if objdump -h "$artifact" | grep -Eq '\.debug_|\.symtab'; then
  printf 'ОШИБКА: debug/symbol section найден в Hardened artifact\n' >&2
  exit 1
fi
python3 -c 'import os, re, sys
original = open(sys.argv[1], "rb").read()
header = open(sys.argv[2], encoding="utf-8").read()
ids = [int(value, 16) for value in re.findall(r"id = 0x([0-9a-f]{16})ULL", header)]
assert len(ids) == 6 and all(value >> 62 < 4 for value in ids) and len({value >> 62 for value in ids}) >= 2
match = re.search(r"material\{\{(.*?)\}\};", header, re.S)
assert match
masked = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(1)))
assert not masked.startswith(b"MGSTV1") and len(masked) > 256
offset = original.find(masked)
assert offset >= 0 and original.find(masked, offset + 1) < 0
for destination, index in ((sys.argv[3], 0), (sys.argv[4], len(masked) - 1)):
    artifact = bytearray(original)
    artifact[offset + index] ^= 1
    open(destination, "wb").write(artifact)
    os.chmod(destination, os.stat(sys.argv[1]).st_mode)' "$artifact" "$generated" \
  "$tmp_dir/tampered-head" "$tmp_dir/tampered-tail"
ulimit -c 0
for tamper_kind in head tail; do
  tampered="$tmp_dir/tampered-$tamper_kind"
  if "$scanner" artifact-cpp --artifact "$tampered" \
      --input "$repo_root/static-sdk/cpp/tests/hardened_project/main.cpp" \
      --generated "$generated" --audit "$tmp_dir/tampered-$tamper_kind.audit" >/dev/null 2>&1; then
    printf 'ОШИБКА: artifact scanner принял tampered %s material\n' "$tamper_kind" >&2
    exit 1
  fi
  [[ ! -e "$tmp_dir/tampered-$tamper_kind.audit" ]]
  tamper_status=$(python3 -c 'import subprocess, sys
result = subprocess.run([sys.argv[1]], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
open(sys.argv[2], "wb").write(result.stdout)
print(result.returncode)' "$tampered" "$tmp_dir/tampered-$tamper_kind.out")
  if [[ "$tamper_status" == 0 ]]; then
    printf 'ОШИБКА: tampered %s final Hardened artifact принят\n' "$tamper_kind" >&2
    exit 1
  fi
  [[ ! -s "$tmp_dir/tampered-$tamper_kind.out" ]]
done
python3 -c 'import os, re, subprocess, sys
original = open(sys.argv[1], "rb").read()
header = open(sys.argv[2], encoding="utf-8").read()
match = re.search(r"material\{\{(.*?)\}\};", header, re.S)
assert match
material = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", match.group(1)))
offset = original.find(material)
assert offset >= 0 and original.find(material, offset + 1) < 0
path = sys.argv[3]
for index in range(len(material)):
    mutated = bytearray(original)
    mutated[offset + index] ^= 1
    open(path, "wb").write(mutated)
    os.chmod(path, os.stat(sys.argv[1]).st_mode)
    result = subprocess.run([path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    assert result.returncode != 0 and not result.stdout, index
os.unlink(path)' "$artifact" "$generated" "$tmp_dir/tampered-each"
python3 -c 'import os, struct, subprocess, sys
original = open(sys.argv[1], "rb").read()
section_offset = struct.unpack_from("<Q", original, 40)[0]
entry_size, count, strings_index = struct.unpack_from("<HHH", original, 58)
sections = [struct.unpack_from("<IIQQQQIIQQ", original, section_offset + index * entry_size)
            for index in range(count)]
strings = original[sections[strings_index][4]:sections[strings_index][4] + sections[strings_index][5]]
by_name = {strings[item[0]:].split(b"\0", 1)[0]: item for item in sections}
for name, at_end, destination in ((b".text", True, sys.argv[2]),
                                  (b".mindguard.seal", False, sys.argv[3])):
    section = by_name[name]
    file_offset = section[4] + (section[5] - 1 if at_end else 0)
    mutated = bytearray(original)
    mutated[file_offset] ^= 1
    open(destination, "wb").write(mutated)
    os.chmod(destination, os.stat(sys.argv[1]).st_mode)
    result = subprocess.run([destination], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    assert result.returncode != 0 and not result.stdout, name' \
  "$artifact" "$tmp_dir/tampered-text" "$tmp_dir/tampered-seal"
[[ "$(stat -c '%a' "$generated")" == 600 && "$(stat -c '%a' "$audit")" == 600 ]]
if grep -aEq 'hardened-secret|packed-byte-secret' "$generated" "$audit"; then
  printf 'ОШИБКА: plaintext найден в generated output\n' >&2
  exit 1
fi
if find "$tmp_dir/build/mindguard" -type f ! -name mindguard_generated.hpp ! -name audit.txt | grep -q .; then
  printf 'ОШИБКА: staging material остался после генерации\n' >&2
  exit 1
fi

for fixture in hardened_reject_scalar hardened_reject_wide hardened_reject_same_line; do
  if "$scanner" prepare-cpp --input "$repo_root/static-sdk/cpp/tests/$fixture.cpp" \
      --output-dir "$tmp_dir/$fixture" --builder "$builder" --seed-file "$seed" \
      --target-id fixture --source-id "static-sdk/cpp/tests/$fixture.cpp" \
      --project-id e2e-project --release-id e2e-release >/dev/null 2>&1; then
    printf 'ОШИБКА: Hardened generator принял negative fixture: %s\n' "$fixture" >&2
    exit 1
  fi
  [[ ! -e "$tmp_dir/$fixture" ]]
done
borrow_output="$tmp_dir/hardened_reject_borrow"
"$scanner" prepare-cpp --input "$repo_root/static-sdk/cpp/tests/hardened_reject_borrow.cpp" \
  --output-dir "$borrow_output" --builder "$builder" --seed-file "$seed" \
  --target-id fixture --source-id static-sdk/cpp/tests/hardened_reject_borrow.cpp \
  --project-id e2e-project --release-id e2e-release >/dev/null
if clang++-18 -std=c++20 -DMINDGUARD_PROFILE_HARDENED=1 \
    '-DMINDGUARD_GENERATED_HEADER="mindguard_generated.hpp"' \
    -I "$repo_root/static-sdk/cpp/include" -I "$borrow_output" \
    -c "$repo_root/static-sdk/cpp/tests/hardened_reject_borrow.cpp" \
    -o "$tmp_dir/borrow.o" >/dev/null 2>&1; then
  printf 'ОШИБКА: Hardened callback позволил вернуть borrowed plaintext\n' >&2
  exit 1
fi
bad_seed="$tmp_dir/bad-seed"
truncate -s 31 "$bad_seed"
if "$scanner" prepare-cpp --input "$repo_root/static-sdk/cpp/tests/hardened_project/main.cpp" \
    --output-dir "$tmp_dir/bad-seed-output" --builder "$builder" --seed-file "$bad_seed" \
    --target-id fixture --source-id static-sdk/cpp/tests/hardened_project/main.cpp \
    --project-id e2e-project --release-id e2e-release >/dev/null 2>&1; then
  printf 'ОШИБКА: Hardened generator принял 31-byte seed\n' >&2
  exit 1
fi
[[ ! -e "$tmp_dir/bad-seed-output" ]]

printf 'MindGuard C++ Hardened generated E2E: УСПЕХ (6 sites, scalar, every-byte embedded tamper, 4KiB=%sns)\n' "$benchmark_ns"
