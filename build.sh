#!/usr/bin/bash
set -e

# Spider Donut PS3 Build Script
# Requires PS3DEV and PSL1GHT environment variables set to toolchain path.

if [ -z "$PS3DEV" ]; then
    echo "Error: PS3DEV environment variable is not set."
    echo "Please set PS3DEV to your ps3dev toolchain installation directory."
    exit 1
fi

export PSL1GHT=${PSL1GHT:-$PS3DEV}
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH

BUILD_DIR="build-ps3-dev"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/ppu.cmake -DDONUT_SPIDER_DEV_BUILD=ON
make -j$(nproc 2>/dev/null || echo 4)

mkdir -p package-dev/USRDIR
cp ../PKG_TEMPLATE/ICON0.PNG package-dev/ICON0.PNG
sfo --fromxml ../packaging/sfo.xml package-dev/PARAM.SFO
make_self_npdrm donut-spider.elf package-dev/USRDIR/EBOOT.BIN IV0002-DONS00001_00-BUTTERSCOTCH4PS3
pkg --contentid IV0002-DONS00001_00-BUTTERSCOTCH4PS3 package-dev/ ../dist/spider-donut-A1.pkg
sha256sum ../dist/spider-donut-A1.pkg > ../dist/spider-donut-A1.pkg.sha256

echo "Build complete: dist/spider-donut-A1.pkg"
