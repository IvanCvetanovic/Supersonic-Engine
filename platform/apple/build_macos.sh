#!/bin/bash
echo "============================================================"
echo "      Building GameEngine for macOS (Apple Silicon/Intel)"
echo "============================================================"

BUILD_DIR="build_macos"

cmake -B $BUILD_DIR \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_MOLTENVK=ON

cmake --build $BUILD_DIR

echo "[SUCCESS] macOS Universal Binary (.app) compiled into $BUILD_DIR"
