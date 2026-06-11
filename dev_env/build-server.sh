#!/bin/bash
# Build the headless server (run configure-server.sh first)
cd "$(dirname "$0")/../src/build"
cmake --build . --target STNKS_SERVER -j$(nproc)
