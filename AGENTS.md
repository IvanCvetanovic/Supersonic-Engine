# AGENTS.md - Build & Automation Instructions

This document provides exact build, compilation, and execution commands for AI agents and developers working on this project.

## Build Requirements
- **C++ Compiler**: Modern C++20 compliant compiler (GCC 11+, Clang 13+, MSVC 2019/2022).
- **Build System**: CMake (v3.20+).
- **Graphics Driver**: Vulkan-compatible driver installed on host system.

## Standard CMake Build Commands

### 1. Configure the Project
Generate build configuration in the `/build` directory:

```bash
# Debug Build (Default, enables Vulkan Validation Layers)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Release Build (Optimized)
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

On Windows with Visual Studio:
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
```

Or with Ninja / MinGW:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
```

### 2. Compile the Executable
Build target `GameEngine`:

```bash
cmake --build build --config Debug
```

### 3. Run the Executable
Run the compiled binary:

On Linux / macOS:
```bash
./build/GameEngine
```

On Windows:
```powershell
.\build\Debug\GameEngine.exe
# or if built with Ninja:
.\build\GameEngine.exe
```

### 4. Clean Build Directory
```bash
cmake --build build --target clean
# Or remove the build folder entirely:
rm -rf build
```
