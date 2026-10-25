#!/usr/bin/env bash
set -euo pipefail
: "${PS3DEV:?Set PS3DEV to your ps3dev toolchain directory}"
export PSL1GHT="${PSL1GHT:-$PS3DEV}"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${BUILD_DIR:-$repo/build-bdev01}"
out="${OUTPUT_DIR:-$repo/dist}"
mkdir -p "$build" "$out"
build="$(cd -- "$build" && pwd)"
out="$(cd -- "$out" && pwd)"
commit="${SOURCE_COMMIT:-$(git -C "$repo" rev-parse --short HEAD 2>/dev/null || printf 'source-archive')}"
date="${SOURCE_DATE:-$(git -C "$repo" log -1 --format=%cs 2>/dev/null || printf '2026-10-25')}"
cmake -S "$repo" -B "$build" -G Ninja \
    -DPLATFORM=ps3 -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/ppu.cmake" \
    -DDONUT_SPIDER_DEV_BUILD=OFF -DDONUT_SPIDER_FRAME_TRACE=OFF \
    -DDONUT_SPIDER_DEV_PRELOAD_WAD=OFF -DENABLE_VM_STUB_LOGS=ON -DENABLE_VIDEO=ON \
    -DDONUT_SPIDER_VERSION=bdev01 '-DDONUT_SPIDER_DISPLAY_NAME=Spider Donut bdev01' \
    "-DSPIDER_DONUT_COMMIT_HASH=$commit" "-DSPIDER_DONUT_COMMIT_DATE=$date"
cmake --build "$build" --parallel "${JOBS:-4}"
python3 "$repo/tools/sanitize_binary_paths.py" "$build/donut-spider.elf"
stage="$(mktemp -d "$build/package.XXXXXX")"
mkdir -p "$stage/USRDIR"
cp "$repo/PKG_TEMPLATE/ICON0.PNG" "$stage/ICON0.PNG"
content_id=UP0001-DONSD0001_00-SPIDERDONUTBDEV1
make_self_npdrm "$build/donut-spider.elf" "$stage/USRDIR/EBOOT.BIN" "$content_id"
sfo --fromxml "$repo/packaging/bdev01.xml" "$stage/PARAM.SFO"
pkg --contentid="$content_id" "$stage" "$out/spider-donut-bdev01.pkg"
package_finalize "$out/spider-donut-bdev01.pkg"
(cd "$out" && sha256sum spider-donut-bdev01.pkg > spider-donut-bdev01.pkg.sha256)
printf 'Package: %s/spider-donut-bdev01.pkg\nStaging: %s\n' "$out" "$stage"
