@echo off
set SHADER_DIR=%~dp0assets\shaders
set TOOL=%~dp0third_party\tools\bin\glslangValidator.exe

if exist "%TOOL%" (
    "%TOOL%" -V "%SHADER_DIR%\shader.vert" -o "%SHADER_DIR%\vert.spv"
    "%TOOL%" -V "%SHADER_DIR%\shader.frag" -o "%SHADER_DIR%\frag.spv"
) else (
    glslc "%SHADER_DIR%\shader.vert" -o "%SHADER_DIR%\vert.spv"
    glslc "%SHADER_DIR%\shader.frag" -o "%SHADER_DIR%\frag.spv"
)

echo Shaders compiled to SPIR-V successfully.
