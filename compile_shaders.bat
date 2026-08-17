@echo off
setlocal enabledelayedexpansion

rem Compiles every GLSL source in assets/shaders to SPIR-V.
rem
rem CMake compiles shaders automatically when it can find a compiler; this
rem script exists for compiling by hand without reconfiguring. It reports real
rem failures instead of always printing success.

set SHADER_DIR=%~dp0assets\shaders
set TOOL=

rem 1) Vendored toolchain, 2) Vulkan SDK, 3) whatever is on PATH.
if exist "%~dp0third_party\tools\bin\glslangValidator.exe" set TOOL="%~dp0third_party\tools\bin\glslangValidator.exe"
if not defined TOOL if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslc.exe" set TOOL="%VULKAN_SDK%\Bin\glslc.exe"
if not defined TOOL where glslc >nul 2>&1 && set TOOL=glslc
if not defined TOOL where glslangValidator >nul 2>&1 && set TOOL=glslangValidator

if not defined TOOL (
    echo [ERROR] No GLSL compiler found.
    echo         Install the Vulkan SDK, or place glslangValidator.exe in
    echo         third_party\tools\bin\, then run this script again.
    exit /b 1
)

echo Using shader compiler: %TOOL%

rem glslc takes the source directly; glslangValidator needs -V.
echo %TOOL% | findstr /i "glslc" >nul
if errorlevel 1 (set VFLAG=-V) else (set VFLAG=)

set FAILED=0
call :compile shader.vert vert.spv
call :compile shader.frag frag.spv
call :compile grid.vert   grid_vert.spv
call :compile grid.frag   grid_frag.spv

if %FAILED% neq 0 (
    echo.
    echo [ERROR] %FAILED% shader^(s^) failed to compile.
    exit /b 1
)

echo.
echo [OK] All shaders compiled to SPIR-V.
exit /b 0

:compile
if not exist "%SHADER_DIR%\%~1" (
    echo [ERROR] missing source: %~1
    set /a FAILED+=1
    exit /b 0
)
%TOOL% %VFLAG% "%SHADER_DIR%\%~1" -o "%SHADER_DIR%\%~2"
if errorlevel 1 (
    echo [ERROR] %~1 failed to compile
    set /a FAILED+=1
) else (
    echo   %~1 -^> %~2
)
exit /b 0
