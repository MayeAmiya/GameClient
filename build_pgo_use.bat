@echo off
setlocal
rem PGO step 3: after training runs produced *.pgc, relink with /USEPROFILE to get the
rem final optimized binary. Run this from the GameClient directory.
rem Usage: build_pgo_use.bat [build_dir]
rem        build_pgo_use.bat build\win32-vcpkg

set VSDEV=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat
set CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe

call "%VSDEV%"
if errorlevel 1 (
    echo [ERROR] vcvars32.bat failed
    exit /b 1
)

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build\win32-vcpkg

echo === PGO: switching to USE and relinking (final optimized build) ===
"%CMAKE%" -S . -B "%BUILD_DIR%" -DRTS_BUILD_OPTION_PGO=USE
if errorlevel 1 (
    echo [ERROR] cmake configure failed
    exit /b 1
)

"%CMAKE%" --build "%BUILD_DIR%" --config Release --target z_generals
set RC=%ERRORLEVEL%
echo === cmake exit code: %RC% ===
exit /b %RC%
