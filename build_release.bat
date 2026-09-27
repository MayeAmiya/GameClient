@echo off
setlocal
set VSDEV=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

call "%VSDEV%"
if errorlevel 1 (
    echo [ERROR] vcvars32.bat failed
    exit /b 1
)

set BUILD_DIR=%~1
set TARGET=%~2
if "%BUILD_DIR%"=="" set BUILD_DIR=build\win32-vcpkg
if "%TARGET%"=="" set TARGET=z_generals

echo === Building %TARGET% in %BUILD_DIR% (Release) ===
"%CMAKE%" --build "%BUILD_DIR%" --config Release --target %TARGET%
set RC=%ERRORLEVEL%
echo === cmake exit code: %RC% ===
exit /b %RC%
