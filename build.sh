#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
private="$root/pc"
build="${GRAPE_BUILD_DIR:-$root/build-private-win}"
output="$root/output"

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

command -v cmake >/dev/null
command -v geode >/dev/null

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
    if ! command -v clang-cl >/dev/null; then
        echo "clang-cl is missing" >&2
        exit 1
    fi
    cross="${GRAPE_CROSS_TOOLS:-$HOME/.local/share/Geode/cross-tools}"
    toolchain="${GRAPE_TOOLCHAIN_FILE:-$cross/clang-msvc-sdk/clang-cl-msvc.cmake}"
    splat="${GRAPE_SPLAT_DIR:-$cross/splat}"
    if [[ ! -f "$toolchain" || ! -d "$splat" ]]; then
        echo "Windows cross-tools are missing; run: geode sdk install-linux" >&2
        exit 1
    fi
    clang_cl="$(command -v clang-cl)"
    llvm_major="$("$clang_cl" --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -n1)"
    if [[ -z "$llvm_major" ]]; then
        echo "Could not determine the clang-cl version" >&2
        exit 1
    fi
    llvm_path="${GRAPE_LLVM_PATH:-/usr/lib/llvm-$llvm_major/bin}"
    if [[ -x "$llvm_path/clang-cl" && -x "$llvm_path/lld-link" ]]; then
        export PATH="$llvm_path:$PATH"
        configure+=(
            "-DLLVM_VER=$llvm_major"
            "-DCLANG_VER=$llvm_major"
            "-DLLVM_PATH=$llvm_path"
            "-DCMAKE_LINKER=$llvm_path/lld-link"
            "-DLLD_LINK_PATH=$llvm_path/lld-link"
            "-DLLVM_LIB_PATH=$llvm_path/llvm-lib"
            "-DLLVM_MT_PATH=$llvm_path/llvm-mt"
            "-DLLVM_RC_PATH=$llvm_path/llvm-rc"
        )
    fi
    configure+=(
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
        "-DSPLAT_DIR=$splat"
        -DHOST_ARCH=x64
        -DGEODE_TARGET_PLATFORM=Win64
    )
fi

cmake "${configure[@]}"
jobs="${GRAPE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
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
