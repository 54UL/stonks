#!/bin/bash
# One-time dev environment setup: checks prerequisites and bootstraps vcpkg.
set -euo pipefail

MISSING=()
for cmd in cmake git pkg-config; do
    if ! command -v "$cmd" &>/dev/null; then
        MISSING+=("$cmd")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "Missing prerequisites: ${MISSING[*]}"
    echo "Install them with your package manager, e.g.:"
    echo "  Ubuntu/Debian: sudo apt install ${MISSING[*]}"
    echo "  macOS:         brew install ${MISSING[*]}"
    exit 1
fi

if [ -z "${VCPKG_ROOT:-}" ]; then
    DEFAULT_VCPKG="$HOME/.vcpkg"
    echo "VCPKG_ROOT is not set. Cloning vcpkg to $DEFAULT_VCPKG ..."
    if [ ! -d "$DEFAULT_VCPKG" ]; then
        git clone https://github.com/microsoft/vcpkg.git "$DEFAULT_VCPKG"
    fi
    "$DEFAULT_VCPKG/bootstrap-vcpkg.sh" -disableMetrics
    export VCPKG_ROOT="$DEFAULT_VCPKG"
    echo ""
    echo "Add to your shell profile:"
    echo "  export VCPKG_ROOT=\"$DEFAULT_VCPKG\""
else
    echo "VCPKG_ROOT is set to: $VCPKG_ROOT"
    if [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
        echo "Bootstrapping vcpkg ..."
        "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
    fi
fi

echo ""
echo "Setup complete. Next steps:"
echo "  1. cp dev_env/setup_env.sh .env.sh && edit .env.sh with your API keys"
echo "  2. source .env.sh"
echo "  3. ./dev_env/build.sh            # build GUI"
echo "  4. ./dev_env/build.sh --server   # build headless server"
