#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pc="$root/pc"
build="${GRAPE_BUILD_DIR:-$root/build-private-win}"
output="$root/output"
jobs="${GRAPE_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [[ ! -f "$pc/CMakeLists.txt" ]]; then
    echo "PC source was not found at $pc" >&2
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

configure=(
    -S "$pc"
    -B "$build"
    -DCMAKE_BUILD_TYPE=Release
    -DGEODE_DONT_INSTALL_MODS=ON
    "-DGEODE_CLI=$(command -v geode)"
)

if command -v ninja >/dev/null; then
    configure+=(-G Ninja)
fi

if [[ "$(uname -s)" == Linux* ]]; then
    llvm_major=19
    llvm_path="${GRAPE_LLVM_PATH:-/usr/lib/llvm-$llvm_major/bin}"
    for tool in clang-cl lld-link llvm-lib llvm-mt llvm-rc; do
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

package="$build/grape/okhx.grape.geode"
if [[ ! -f "$package" ]]; then
    echo "Build finished without $package" >&2
    exit 1
fi

mkdir -p "$output"
rm -f "$output"/okhx.grape*.geode
cp "$package" "$output/"
printf 'Built: %s\n' "$output/okhx.grape.geode"
