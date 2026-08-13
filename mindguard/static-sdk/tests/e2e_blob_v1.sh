#!/usr/bin/env bash
set -Eeuo pipefail

repo_root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cargo_bin=${CARGO:-cargo}
rustc_bin=${RUSTC:-rustc}
rustdoc_bin=${RUSTDOC:-rustdoc}

for command_name in "$cargo_bin" "$rustc_bin" "$rustdoc_bin" clang++-18 cmake cmp cp dd find grep head llvm-objcopy llvm-strip mktemp nm objdump od stat strings truncate; do
  command -v "$command_name" >/dev/null 2>&1 || {
    printf 'ОШИБКА: отсутствует команда: %s\n' "$command_name" >&2
    exit 2
  }
done

old_umask=$(umask)
umask 077
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/mindguard-static-e2e.XXXXXX")
seed=
plain=
bench_plain=
scalar_plain=
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  [[ ! -f "$seed" ]] || shred -u -n 1 "$seed" 2>/dev/null || true
  [[ ! -f "$plain" ]] || shred -u -n 1 "$plain" 2>/dev/null || true
  [[ ! -f "$bench_plain" ]] || shred -u -n 1 "$bench_plain" 2>/dev/null || true
  [[ ! -f "$scalar_plain" ]] || shred -u -n 1 "$scalar_plain" 2>/dev/null || true
  find "$tmp_dir" -depth -delete 2>/dev/null || true
  umask "$old_umask" || true
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

seed="$tmp_dir/seed"
plain="$tmp_dir/plain"
bench_plain="$tmp_dir/bench-plain"
scalar_plain="$tmp_dir/scalar-plain"
site=0123456789abcdef
dd if=/dev/urandom of="$seed" bs=32 count=1 status=none
printf 'cross-language\0secret\xffpayload' >"$plain"
dd if=/dev/urandom of="$bench_plain" bs=4096 count=1 status=none
printf -- '-42' >"$scalar_plain"

export RUSTC="$rustc_bin" RUSTDOC="$rustdoc_bin"
export CARGO_TARGET_DIR="$tmp_dir/build-target"
"$cargo_bin" build --quiet --offline --locked --manifest-path "$repo_root/static-sdk/build/Cargo.toml"
builder="$CARGO_TARGET_DIR/debug/mindguard-build"
scanner="$CARGO_TARGET_DIR/debug/mindguard-scan"

build_one() {
  "$builder" --seed-file "$seed" --plaintext-file "${3:-$plain}" --output-dir "$1" \
    --project-id e2e-project --release-id e2e-release --site-id "$site" --kind bytes \
    --share-format "$2"
}
build_one "$tmp_dir/out-a" packed-v1
build_one "$tmp_dir/out-b" packed-v1
build_one "$tmp_dir/out-raw" raw-v1
build_one "$tmp_dir/out-bench" packed-v1 "$bench_plain"
"$builder" --seed-file "$seed" --plaintext-file "$scalar_plain" \
  --output-dir "$tmp_dir/out-scalar" --project-id e2e-project --release-id e2e-release \
  --site-id "$site" --kind scalar --share-format packed-v1
cmp "$tmp_dir/out-a/blob.bin" "$tmp_dir/out-b/blob.bin"
cmp "$tmp_dir/out-a/code-share.bin" "$tmp_dir/out-b/code-share.bin"
cmp "$tmp_dir/out-a/blob.bin" "$tmp_dir/out-raw/blob.bin"
[[ "$(stat -c '%a' "$tmp_dir/out-a")" == 700 ]]
[[ "$(stat -c '%a' "$tmp_dir/out-a/blob.bin")" == 600 ]]
[[ "$(stat -c '%s' "$tmp_dir/out-a/code-share.bin")" == 256 ]]
[[ "$(stat -c '%s' "$tmp_dir/out-raw/code-share.bin")" == 32 ]]
if build_one "$tmp_dir/out-a" packed-v1 >/dev/null 2>&1; then
  printf 'ОШИБКА: mindguard-build перезаписал существующий output\n' >&2
  exit 1
fi
for seed_size in 31 33; do
  bad_seed="$tmp_dir/seed-$seed_size"
  truncate -s "$seed_size" "$bad_seed"
  if "$builder" --seed-file "$bad_seed" --plaintext-file "$plain" \
      --output-dir "$tmp_dir/bad-seed-$seed_size" --project-id e2e-project \
      --release-id e2e-release --site-id "$site" --kind bytes >/dev/null 2>&1; then
    printf 'ОШИБКА: принят seed размером %s байт\n' "$seed_size" >&2
    exit 1
  fi
  [[ ! -e "$tmp_dir/bad-seed-$seed_size" ]]
done
if "$builder" --seed-file "$seed" --plaintext-file "$plain" \
    --output-dir "$tmp_dir/bad-share-format" --project-id e2e-project \
    --release-id e2e-release --site-id "$site" --kind bytes \
    --share-format unknown >/dev/null 2>&1; then
  printf 'ОШИБКА: принят неизвестный share-format\n' >&2
  exit 1
fi
[[ ! -e "$tmp_dir/bad-share-format" ]]
oversized="$tmp_dir/oversized"
truncate -s 65537 "$oversized"
if "$builder" --seed-file "$seed" --plaintext-file "$oversized" \
    --output-dir "$tmp_dir/oversized-output" --project-id e2e-project \
    --release-id e2e-release --site-id "$site" --kind bytes >/dev/null 2>&1; then
  printf 'ОШИБКА: принят plaintext больше 64 KiB\n' >&2
  exit 1
fi
[[ ! -e "$tmp_dir/oversized-output" ]]
if LC_ALL=C grep -aFq 'cross-language' "$tmp_dir/out-a/blob.bin" "$tmp_dir/out-a/audit.txt"; then
  printf 'ОШИБКА: plaintext найден в generated output\n' >&2
  exit 1
fi

export CARGO_TARGET_DIR="$tmp_dir/core-target"
"$cargo_bin" build --quiet --offline --locked --release \
  --manifest-path "$repo_root/static-sdk/rust-core/Cargo.toml" --example decode
rust_runner="$CARGO_TARGET_DIR/release/examples/decode"
cmake -S "$repo_root/static-sdk/cpp" -B "$tmp_dir/cpp" -DCMAKE_CXX_COMPILER=clang++-18 \
  -DCMAKE_BUILD_TYPE=Release -DMINDGUARD_PROFILE=Dev >/dev/null
cmake --build "$tmp_dir/cpp" --target mindguard_static_core_runner --parallel >/dev/null
cpp_runner="$tmp_dir/cpp/mindguard_static_core_runner"
rust_benchmark=$($rust_runner "$tmp_dir/out-bench/blob.bin" \
  "$tmp_dir/out-bench/code-share.bin" "$site" 2 --bench)
cpp_benchmark=$($cpp_runner "$tmp_dir/out-bench/blob.bin" \
  "$tmp_dir/out-bench/code-share.bin" "$site" 2 --bench)
[[ "$rust_benchmark" =~ ^[0-9]+\ [0-9]+$ ]]
[[ "$cpp_benchmark" =~ ^[0-9]+\ [0-9]+$ ]]

"$scanner" source --language cpp --input "$repo_root/static-sdk/cpp/tests/dev_tests.cpp" \
  --audit "$tmp_dir/source-cpp.audit"
"$scanner" source --language rust --input "$repo_root/static-sdk/rust/src/lib.rs" \
  --audit "$tmp_dir/source-rust.audit"
grep -qx 'sites=10' "$tmp_dir/source-cpp.audit"
grep -qx 'sites=6' "$tmp_dir/source-rust.audit"
"$scanner" artifact --artifact "$cpp_runner" --secret-file "$plain" --kind bytes \
  --audit "$tmp_dir/artifact.audit"
"$scanner" artifact --artifact "$tmp_dir/out-a/code-share.bin" \
  --secret-file "$tmp_dir/out-raw/code-share.bin" --kind bytes \
  --audit "$tmp_dir/packed-share.audit"

flip_byte() {
  cp "$1" "$3"
  local value flipped escaped
  value=$(od -An -tu1 -j "$2" -N1 "$3")
  flipped=$((value ^ 1))
  printf -v escaped '\\%03o' "$flipped"
  printf '%b' "$escaped" | dd of="$3" bs=1 seek="$2" count=1 conv=notrunc status=none
}

cp "$tmp_dir/out-a/blob.bin" "$tmp_dir/blob"
cp "$tmp_dir/out-a/code-share.bin" "$tmp_dir/share"
flip_byte "$tmp_dir/blob" 96 "$tmp_dir/blob-tampered"
(
  cd "$tmp_dir"
  llvm-objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata.mgblob,alloc,load,readonly,data,contents blob blob.o
  llvm-objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata.mgshare,alloc,load,readonly,data,contents share share.o
  llvm-objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata.mgblob,alloc,load,readonly,data,contents \
    blob-tampered blob-tampered.o
)
clang++-18 -std=c++20 -O2 -fPIE -pie -Wl,-z,noexecstack -ffile-prefix-map="$repo_root"=. \
  -I "$repo_root/static-sdk/cpp/include" "$repo_root/static-sdk/cpp/tests/embedded_runner.cpp" \
  "$tmp_dir/blob.o" "$tmp_dir/share.o" "$tmp_dir/cpp/libmindguard_static.a" \
  -o "$tmp_dir/cpp-embedded"
clang++-18 -std=c++20 -O2 -fPIE -pie -Wl,-z,noexecstack -ffile-prefix-map="$repo_root"=. \
  -I "$repo_root/static-sdk/cpp/include" "$repo_root/static-sdk/cpp/tests/embedded_runner.cpp" \
  "$tmp_dir/blob-tampered.o" "$tmp_dir/share.o" "$tmp_dir/cpp/libmindguard_static.a" \
  -Wl,--defsym,_binary_blob_start=_binary_blob_tampered_start \
  -Wl,--defsym,_binary_blob_end=_binary_blob_tampered_end \
  -o "$tmp_dir/cpp-tampered"
llvm-strip --strip-all "$tmp_dir/cpp-embedded"
llvm-strip --strip-all "$tmp_dir/cpp-tampered"

export CARGO_TARGET_DIR="$tmp_dir/embedded-rust-target"
MINDGUARD_TEST_BLOB="$tmp_dir/out-a/blob.bin" \
MINDGUARD_TEST_SHARE="$tmp_dir/out-a/code-share.bin" \
  "$cargo_bin" build --quiet --offline --locked --release \
    --manifest-path "$repo_root/static-sdk/rust-core/Cargo.toml" --example embedded \
    --features embedded-test
cp "$CARGO_TARGET_DIR/release/examples/embedded" "$tmp_dir/rust-embedded"
llvm-strip --strip-all "$tmp_dir/rust-embedded"
export CARGO_TARGET_DIR="$tmp_dir/tampered-rust-target"
MINDGUARD_TEST_BLOB="$tmp_dir/blob-tampered" \
MINDGUARD_TEST_SHARE="$tmp_dir/out-a/code-share.bin" \
  "$cargo_bin" build --quiet --offline --locked --release \
    --manifest-path "$repo_root/static-sdk/rust-core/Cargo.toml" --example embedded \
    --features embedded-test
cp "$CARGO_TARGET_DIR/release/examples/embedded" "$tmp_dir/rust-tampered"
llvm-strip --strip-all "$tmp_dir/rust-tampered"

for language in cpp rust; do
  embedded="$tmp_dir/$language-embedded"
  "$embedded" >"$tmp_dir/$language-recovered"
  cmp "$plain" "$tmp_dir/$language-recovered"
  "$scanner" artifact --artifact "$embedded" --secret-file "$plain" --kind bytes \
    --audit "$tmp_dir/$language-embedded.audit"
  "$scanner" artifact --artifact "$embedded" \
    --secret-file "$tmp_dir/out-raw/code-share.bin" --kind bytes \
    --audit "$tmp_dir/$language-share.audit"
  if strings "$embedded" | grep -Fq "$repo_root"; then
    printf 'ОШИБКА: absolute checkout path найден в %s shipping artifact\n' "$language" >&2
    exit 1
  fi
  if objdump -h "$embedded" | grep -Eq '\.debug_|\.symtab'; then
    printf 'ОШИБКА: debug/symbol section найден в %s shipping artifact\n' "$language" >&2
    exit 1
  fi
  if nm "$embedded" 2>/dev/null | grep -q mindguard; then
    printf 'ОШИБКА: MindGuard symbol найден в stripped %s artifact\n' "$language" >&2
    exit 1
  fi
  if "$tmp_dir/$language-tampered" >"$tmp_dir/$language-tampered.out" 2>/dev/null; then
    printf 'ОШИБКА: tampered embedded %s artifact принят\n' "$language" >&2
    exit 1
  fi
  [[ ! -s "$tmp_dir/$language-tampered.out" ]]
done

accept_both() {
  "$rust_runner" "$1" "$2" "$site" 2 "$plain" | grep -qx OK
  "$cpp_runner" "$1" "$2" "$site" 2 "$plain" | grep -qx OK
}
reject_both() {
  local rust_error cpp_error
  if rust_error=$("$rust_runner" "$1" "$2" "$site" 2 "$plain"); then return 1; fi
  if cpp_error=$("$cpp_runner" "$1" "$2" "$site" 2 "$plain"); then return 1; fi
  [[ "$rust_error" == "$3" && "$cpp_error" == "$3" ]]
}
reject_same() {
  local rust_error cpp_error
  if rust_error=$("$rust_runner" "$1" "$2" "$site" 2 "$plain"); then return 1; fi
  if cpp_error=$("$cpp_runner" "$1" "$2" "$site" 2 "$plain"); then return 1; fi
  [[ "$rust_error" == "$cpp_error" ]] || {
    printf 'Rust=%s C++=%s\n' "$rust_error" "$cpp_error" >&2
    return 1
  }
}
blob="$tmp_dir/out-a/blob.bin"
share="$tmp_dir/out-a/code-share.bin"
accept_both "$blob" "$share"
accept_both "$tmp_dir/out-raw/blob.bin" "$tmp_dir/out-raw/code-share.bin"
"$rust_runner" "$tmp_dir/out-scalar/blob.bin" "$tmp_dir/out-scalar/code-share.bin" \
  "$site" 3 "$scalar_plain" | grep -qx OK
"$cpp_runner" "$tmp_dir/out-scalar/blob.bin" "$tmp_dir/out-scalar/code-share.bin" \
  "$site" 3 "$scalar_plain" | grep -qx OK
flip_byte "$tmp_dir/out-scalar/blob.bin" 96 "$tmp_dir/scalar-tampered"
rust_scalar_error=$($rust_runner "$tmp_dir/scalar-tampered" \
  "$tmp_dir/out-scalar/code-share.bin" "$site" 3 "$scalar_plain" || true)
cpp_scalar_error=$($cpp_runner "$tmp_dir/scalar-tampered" \
  "$tmp_dir/out-scalar/code-share.bin" "$site" 3 "$scalar_plain" || true)
[[ "$rust_scalar_error" == Tag && "$cpp_scalar_error" == Tag ]]
for case in '0 Magic' '8 Version' '10 Profile' '11 Kind' '14 Header' '16 Site' \
            '24 Bounds' '72 Tag' '88 Header' '96 Tag'; do
  read -r offset expected_error <<<"$case"
  mutated="$tmp_dir/mutated-$offset"
  flip_byte "$blob" "$offset" "$mutated"
  reject_both "$mutated" "$share" "$expected_error"
done
share_size=$(stat -c '%s' "$share")
for ((offset = 0; offset < share_size; ++offset)); do
  flip_byte "$share" "$offset" "$tmp_dir/mutated-share"
  reject_both "$blob" "$tmp_dir/mutated-share" Tag
done
flip_byte "$tmp_dir/out-raw/code-share.bin" 0 "$tmp_dir/mutated-raw-share"
reject_both "$tmp_dir/out-raw/blob.bin" "$tmp_dir/mutated-raw-share" Tag
head -c 95 "$blob" >"$tmp_dir/truncated"
reject_both "$tmp_dir/truncated" "$share" Bounds
blob_size=$(stat -c '%s' "$blob")
for ((offset = 0; offset < blob_size; ++offset)); do
  flip_byte "$blob" "$offset" "$tmp_dir/mutated-all"
  reject_same "$tmp_dir/mutated-all" "$share" || {
    printf 'ОШИБКА: Rust/C++ tamper parity mismatch at blob offset %s\n' "$offset" >&2
    exit 1
  }
done

printf 'MindGuard Static Blob v1 E2E: УСПЕХ (Rust/C++ parity + tamper rejection; 4KiB Rust=%sns C++=%sns)\n' \
  "${rust_benchmark%% *}" "${cpp_benchmark%% *}"
