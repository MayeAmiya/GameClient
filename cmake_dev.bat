@echo off
setlocal
set VSDEV=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

call "%VSDEV%"
if errorlevel 1 (
    echo [ERROR] vcvars32.bat failed
    exit /b 1
)

rem Forwards every argument verbatim to cmake, inside an initialized MSVC x86 environment.
rem   reconfigure :  cmake_dev.bat -DRTS_BUILD_OPTION_RENDER_AVX2=OFF build\win32-vcpkg
rem   build       :  cmake_dev.bat --build build\win32-vcpkg --config Release --target z_generals
"%CMAKE%" %*
set RC=%ERRORLEVEL%
echo === cmake exit code: %RC% ===
exit /b %RC%
