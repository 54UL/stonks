#!/bin/bash
# One-time configure for headless server build (no graphics)
cd "$(dirname "$0")/.."
rm -rf src/build
mkdir -p src/build && cd src/build
cmake -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_OVERLAY_PORTS=../../overlay-ports \
      -DVCPKG_MANIFEST_FEATURES="" \
      -DSTNKS_HEADLESS=ON \
      ..
