#!/usr/bin/env bash
#
# iOS build.
#
# STATUS: NOT FUNCTIONAL YET, for the same reason as Android: the engine creates
# its window and surface through GLFW, which has no iOS backend. A UIKit /
# CAMetalLayer path plus VK_EXT_metal_surface is required before this can
# produce anything runnable.
#
# It also needs the Xcode generator - the default Makefile generator cannot
# produce a signable bundle - and a development team for code signing.
#
# Kept as a starting point, but it no longer reports success over a failed build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-ios"

if ! command -v xcodebuild >/dev/null 2>&1; then
    echo "[ERROR] Xcode is required for iOS builds." >&2
    exit 1
fi

echo "[WARN] The iOS target cannot run yet - see the header comment in this script."
echo "[INFO] Configuring for iOS..."

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DSUPERSONIC_BUILD_TESTS=OFF \
    -DSUPERSONIC_BUILD_SCRIPT_PLUGIN=OFF

echo "[INFO] Building..."
cmake --build "${BUILD_DIR}" --config Release

echo "[OK] Configure and build completed in ${BUILD_DIR}."
