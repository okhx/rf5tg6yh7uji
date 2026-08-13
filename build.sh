#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
private="$root/pc"
build="${GRAPE_BUILD_DIR:-$root/build-private-win}"
output="$root/output"
mindguard="$root/mindguard/static-sdk"
jobs="${GRAPE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [[ ! -f "$private/CMakeLists.txt" ]]; then
    echo "Private PC source was not found at $private" >&2
    exit 1
fi

if [[ -f "$private/.env" ]]; then
    set -a
    source "$private/.env"
    set +a
fi

if [[ -z "${GRAPE_LICENSE_URL:-}" ]]; then
    echo "Set GRAPE_LICENSE_URL or copy pc/.env.example to pc/.env" >&2
    exit 1
fi

if [[ ! "$GRAPE_LICENSE_URL" =~ ^https:// ]]; then
    echo "GRAPE_LICENSE_URL must use HTTPS" >&2
    exit 1
fi

if [[ -z "${GEODE_SDK:-}" || ! -d "$GEODE_SDK" ]]; then
    echo "GEODE_SDK is not set or does not exist" >&2
    exit 1
fi

for tool in cmake geode; do
    if ! command -v "$tool" >/dev/null; then
        echo "$tool is missing" >&2
        exit 1
    fi
done

cargo_bin="$(command -v cargo || true)"
if [[ -z "$cargo_bin" && -x "${HOME:-}/.cargo/bin/cargo" ]]; then
    cargo_bin="${HOME}/.cargo/bin/cargo"
fi
if [[ -z "$cargo_bin" ]]; then
    echo "cargo is missing; install Rust 1.97.1 with rustup" >&2
    exit 1
fi

"$cargo_bin" build --locked --release --manifest-path "$mindguard/build/Cargo.toml"
umask 077
mindguard_seed="$(mktemp "${TMPDIR:-/tmp}/grape-mindguard.XXXXXX")"
trap 'rm -f "$mindguard_seed"' EXIT
dd if=/dev/urandom of="$mindguard_seed" bs=32 count=1 status=none
export MG_BUILD_SEED_FILE="$mindguard_seed"

configure=(
    -S "$private"
    -B "$build"
    -DCMAKE_BUILD_TYPE=Release
    -DGEODE_DONT_INSTALL_MODS=ON
    "-DGEODE_CLI=$(command -v geode)"
    "-DGRAPE_LICENSE_URL=$GRAPE_LICENSE_URL"
)

if [[ -n "${GRAPE_VMPROTECT_SDK:-}" ]]; then
    configure+=("-DGRAPE_VMPROTECT_SDK=$GRAPE_VMPROTECT_SDK")
fi

if command -v ninja >/dev/null; then
    configure+=(-G Ninja)
fi

if [[ "$(uname -s)" == Linux* ]]; then
    llvm_major=19
    llvm_path="${GRAPE_LLVM_PATH:-/usr/lib/llvm-$llvm_major/bin}"
    for tool in clang-cl clang++ lld-link llvm-config llvm-lib llvm-mt llvm-rc; do
        if [[ ! -x "$llvm_path/$tool" ]]; then
            echo "$llvm_path/$tool is missing; this build requires LLVM 19.x" >&2
            exit 1
        fi
    done
    cross="${GRAPE_CROSS_TOOLS:-$HOME/.local/share/Geode/cross-tools}"
    toolchain="${GRAPE_TOOLCHAIN_FILE:-$cross/clang-msvc-sdk/clang-cl-msvc.cmake}"
    splat="${GRAPE_SPLAT_DIR:-$cross/splat}"
    if [[ ! -f "$toolchain" || ! -d "$splat" ]]; then
        echo "Windows cross-tools are missing; run: geode sdk install-linux" >&2
        exit 1
    fi

    mindguard_host="$build/mindguard-host"
    mindguard_plugin_build="$mindguard_host/llvm-pass-$llvm_major"
    cmake -S "$mindguard/llvm-pass" -B "$mindguard_plugin_build" \
        -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_CXX_COMPILER=$llvm_path/clang++" \
        "-DLLVM_DIR=$("$llvm_path/llvm-config" --cmakedir)"
    cmake --build "$mindguard_plugin_build" --parallel "$jobs"
    mindguard_plugin="$mindguard_plugin_build/MindGuardPass.so"
    mindguard_sealer="$mindguard_host/mindguard_seal_pe"
    "$llvm_path/clang++" -std=c++20 -O2 \
        -I "$mindguard/cpp/include" \
        "$mindguard/cpp/tools/seal_pe.cpp" \
        -o "$mindguard_sealer"

    export PATH="$llvm_path:$PATH"
    configure+=(
        "-DLLVM_VER=$llvm_major"
        "-DCLANG_VER=$llvm_major"
        "-DLLVM_PATH=$llvm_path"
        "-DCMAKE_C_COMPILER=$llvm_path/clang-cl"
        "-DCMAKE_CXX_COMPILER=$llvm_path/clang-cl"
        "-DCMAKE_LINKER=$llvm_path/lld-link"
        "-DLLD_LINK_PATH=$llvm_path/lld-link"
        "-DLLVM_LIB_PATH=$llvm_path/llvm-lib"
        "-DLLVM_MT_PATH=$llvm_path/llvm-mt"
        "-DLLVM_RC_PATH=$llvm_path/llvm-rc"
        "-DMINDGUARD_LLVM_PASS_PLUGIN=$mindguard_plugin"
        "-DMINDGUARD_SEAL_TOOL=$mindguard_sealer"
    )
    configure+=(
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        "-DSPLAT_DIR=$splat"
        -DHOST_ARCH=x64
        -DGEODE_TARGET_PLATFORM=Win64
    )
fi

cmake "${configure[@]}"
cmake --build "$build" --config Release --parallel "$jobs"

mapfile -d '' packages < <(find "$build" -type f -name '*.geode' -print0)
if (( ${#packages[@]} == 0 )); then
    echo "Build finished without a .geode package" >&2
    exit 1
fi

mkdir -p "$output"
for package in "${packages[@]}"; do
    cp "$package" "$output/"
done

printf 'Built: %s\n' "$output"/*.geode
