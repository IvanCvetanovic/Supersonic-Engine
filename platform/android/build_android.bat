@echo off
echo ============================================================
echo      Building GameEngine for Android (arm64-v8a) via NDK
echo ============================================================

if "%ANDROID_NDK%" == "" (
    echo [ERROR] ANDROID_NDK environment variable is not set!
    echo Please set ANDROID_NDK to your NDK installation path.
    exit /b 1
)

set BUILD_DIR=build_android

cmake -B %BUILD_DIR% ^
    -DCMAKE_TOOLCHAIN_FILE=%ANDROID_NDK%\build\cmake\android.toolchain.cmake ^
    -DANDROID_ABI=arm64-v8a ^
    -DANDROID_PLATFORM=android-26 ^
    -DCMAKE_BUILD_TYPE=Release

cmake --build %BUILD_DIR%

echo [SUCCESS] Android arm64-v8a target compiled into %BUILD_DIR%
