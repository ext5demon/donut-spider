#!/usr/bin/env bash
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$repo/build-tests}"
extra=()
if [ "${SANITIZE:-OFF}" = ON ]; then
    extra+=('-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie')
    extra+=('-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie')
fi
cmake -S "$repo" -B "$build" -G Ninja \
    -DPLATFORM=desktop -DDESKTOP_BACKEND=glfw3 -DAUDIO_BACKEND=none \
    -DENABLE_MODERN_GL=OFF -DENABLE_VIDEO=OFF -DENABLE_ASAN=OFF \
    -DBUILD_PS3_REGRESSION_TESTS=ON -DBUILD_GC_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug "${extra[@]}"
cmake --build "$build" --parallel "${JOBS:-4}"
ctest --test-dir "$build" --output-on-failure
