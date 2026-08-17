#!/usr/bin/env bash
#
# macOS build.
#
# Requires the Vulkan SDK for macOS (which supplies MoltenVK). The engine
# requests VK_KHR_portability_enumeration and sets the portability bit on the
# instance, without which the loader does not enumerate MoltenVK at all and
# vkCreateInstance returns VK_ERROR_INCOMPATIBLE_DRIVER.
#
# Note this produces a plain command-line executable, not a .app bundle. The
# previous version of this script claimed "macOS Universal Binary (.app)", but
# add_executable has no MACOSX_BUNDLE and no Info.plist is generated.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-macos"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [ -z "${VULKAN_SDK:-}" ]; then
    echo "[WARN] VULKAN_SDK is not set. Install the Vulkan SDK for macOS and source"
    echo "       setup-env.sh, or CMake will not find MoltenVK."
fi

echo "[INFO] Configuring for macOS (${BUILD_TYPE})..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "[INFO] Building..."
cmake --build "${BUILD_DIR}" --parallel

echo "[INFO] Running tests..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "[OK] Built ${BUILD_DIR}/SupersonicEngine"
echo "[INFO] Run it from the project root so relative asset paths resolve:"
echo "         (cd '${PROJECT_ROOT}' && '${BUILD_DIR}/SupersonicEngine')"
