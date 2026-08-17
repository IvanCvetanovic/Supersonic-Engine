#!/usr/bin/env bash
#
# Android build.
#
# STATUS: NOT FUNCTIONAL YET. This configures and builds, but the result is not
# a runnable app. Two things are missing and neither is a small change:
#
#   1. Windowing. The engine creates its surface through GLFW
#      (Window::Window -> glfwCreateWindow, VulkanDevice::createSurface ->
#      glfwCreateWindowSurface). GLFW 3.4 has no Android backend, so a native
#      ANativeWindow path plus VK_KHR_android_surface is required first.
#   2. Packaging. AndroidManifest.xml declares NativeActivity with
#      android.app.lib_name=GameEngine, so the runtime dlopen()s
#      libGameEngine.so and calls ANativeActivity_onCreate. CMake currently
#      produces an executable, and there is no glue source, no APK step and no
#      signing.
#
# The script is kept because the CMake toolchain wiring below is correct and is
# the right starting point. It now fails loudly instead of printing success over
# a failed build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [ -z "${ANDROID_NDK:-}" ]; then
    echo "[ERROR] ANDROID_NDK is not set. Point it at your NDK installation." >&2
    exit 1
fi

if [ ! -f "${ANDROID_NDK}/build/cmake/android.toolchain.cmake" ]; then
    echo "[ERROR] No android.toolchain.cmake under ANDROID_NDK=${ANDROID_NDK}" >&2
    exit 1
fi

ABI="${ANDROID_ABI:-arm64-v8a}"
API="${ANDROID_API:-26}"
BUILD_DIR="${PROJECT_ROOT}/build-android-${ABI}"

echo "[INFO] Configuring for Android (${ABI}, API ${API})..."
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${ANDROID_NDK}/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="android-${API}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DENGINE_BUILD_TESTS=OFF \
    -DENGINE_BUILD_SCRIPT_PLUGIN=OFF

echo "[INFO] Building..."
cmake --build "${BUILD_DIR}" --parallel

echo "[OK] Native build finished in ${BUILD_DIR}."
echo "[WARN] This is NOT a runnable app yet - see the header comment in this script."
