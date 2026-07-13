#!/bin/bash
# Usage:
#   ./dev_env/build.sh              # Build GUI (default)
#   ./dev_env/build.sh --server     # Build headless server
#   ./dev_env/build.sh --tests      # Build tests
#   ./dev_env/build.sh --all        # Build everything
#   ./dev_env/build.sh --clean      # Clean + reconfigure
#   ./dev_env/build.sh --release    # Release mode (default: Debug)
#
# Flags can be combined: ./dev_env/build.sh --server --release --clean
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"

BUILD_TYPE="Debug"
TARGET="Entry"
HEADLESS="OFF"
CLEAN=false
NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server)  TARGET="STNKS_SERVER"; HEADLESS="ON" ;;
        --tests)   TARGET="STNKS_TESTS" ;;
        --all)     TARGET="all" ;;
        --clean)   CLEAN=true ;;
        --release) BUILD_TYPE="Release" ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
    shift
done

BUILD_DIR="$SRC_DIR/build"

if [ -z "${VCPKG_ROOT:-}" ]; then
    echo "ERROR: VCPKG_ROOT is not set. Run setup-dev.sh or set it manually."
    exit 1
fi

if $CLEAN && [ -d "$BUILD_DIR" ]; then
    echo "[build] Cleaning $BUILD_DIR ..."
    rm -rf "$BUILD_DIR"
fi

if [ ! -d "$BUILD_DIR" ]; then
    echo "[build] Configuring ($BUILD_TYPE, HEADLESS=$HEADLESS) ..."
    mkdir -p "$BUILD_DIR"

    OVERLAY_ARG=""
    if [ -d "$PROJECT_ROOT/overlay-ports" ]; then
        OVERLAY_ARG="-DVCPKG_OVERLAY_PORTS=$PROJECT_ROOT/overlay-ports"
    fi

    cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        $OVERLAY_ARG \
        -DSTNKS_HEADLESS="$HEADLESS"
fi

echo "[build] Building target=$TARGET jobs=$NPROC ..."
cmake --build "$BUILD_DIR" --target "$TARGET" -j"$NPROC"

echo "[build] Done."
