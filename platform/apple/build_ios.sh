#!/bin/bash
echo "============================================================"
echo "          Building GameEngine for iOS (arm64)"
echo "============================================================"

BUILD_DIR="build_ios"

cmake -B $BUILD_DIR \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_MOLTENVK=ON

cmake --build $BUILD_DIR

echo "[SUCCESS] iOS target compiled into $BUILD_DIR"
