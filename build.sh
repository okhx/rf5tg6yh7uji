#!/bin/bash
geode build -- -DCMAKE_TOOLCHAIN_FILE=$HOME/.local/share/Geode/cross-tools/clang-msvc-sdk/clang-cl-msvc.cmake -DCMAKE_BUILD_PARALLEL_LEVEL=24 -DGEODE_CLI=/usr/local/bin/geode

