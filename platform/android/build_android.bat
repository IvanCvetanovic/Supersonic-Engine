@echo off
setlocal enabledelayedexpansion

rem Android build (Windows host).
rem
rem STATUS: NOT FUNCTIONAL YET. See platform/android/build_android.sh for the
rem full explanation: the engine creates its surface through GLFW, which has no
rem Android backend, and the manifest expects a NativeActivity shared library
rem that CMake does not currently produce.
rem
rem This script now fails on error instead of printing success regardless.

set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..\..

if "%ANDROID_NDK%"=="" (
    echo [ERROR] ANDROID_NDK is not set. Point it at your NDK installation.
    exit /b 1
)

if not exist "%ANDROID_NDK%\build\cmake\android.toolchain.cmake" (
    echo [ERROR] No android.toolchain.cmake under ANDROID_NDK=%ANDROID_NDK%
    exit /b 1
)

if "%ANDROID_ABI%"=="" set ANDROID_ABI=arm64-v8a
if "%ANDROID_API%"=="" set ANDROID_API=26

set BUILD_DIR=%PROJECT_ROOT%\build-android-%ANDROID_ABI%

echo [INFO] Configuring for Android (%ANDROID_ABI%, API %ANDROID_API%)...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" ^
    -DCMAKE_TOOLCHAIN_FILE="%ANDROID_NDK%\build\cmake\android.toolchain.cmake" ^
    -DANDROID_ABI=%ANDROID_ABI% ^
    -DANDROID_PLATFORM=android-%ANDROID_API% ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DSUPERSONIC_BUILD_TESTS=OFF ^
    -DSUPERSONIC_BUILD_SCRIPT_PLUGIN=OFF
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [INFO] Building...
cmake --build "%BUILD_DIR%" --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [OK] Native build finished in %BUILD_DIR%.
echo [WARN] This is NOT a runnable app yet - see build_android.sh for details.
exit /b 0
