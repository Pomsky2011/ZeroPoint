@echo off
REM ZeroPoint Build Script for Windows (x64 & ARM64)

echo === ZeroPoint Windows Build Script ===
echo.

REM Check for CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Error: CMake is not installed or not in PATH.
    echo Please install CMake from https://cmake.org/download/
    echo Or install via: winget install Kitware.CMake
    exit /b 1
)
echo [OK] CMake is installed

REM Check for Visual Studio / MSVC
where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo Warning: MSVC compiler not found in PATH.
    echo Please run this script from a Visual Studio Developer Command Prompt,
    echo or install Visual Studio with C++ support.
    echo.
    echo You can also try using MinGW-w64 instead.
    exit /b 1
)
echo [OK] MSVC compiler found

REM Check for vcpkg (recommended for dependencies)
if not defined VCPKG_ROOT (
    echo.
    echo Note: VCPKG_ROOT is not set.
    echo For easier dependency management, consider installing vcpkg:
    echo   git clone https://github.com/Microsoft/vcpkg.git
    echo   cd vcpkg ^&^& bootstrap-vcpkg.bat
    echo   vcpkg install sdl2:x64-windows qt6:x64-windows
    echo.
    echo Alternatively, manually install SDL2 and Qt and set CMAKE_PREFIX_PATH.
    echo.
    pause
)

REM Architecture detection
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    set ARCH=x64
) else if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set ARCH=ARM64
) else (
    set ARCH=%PROCESSOR_ARCHITECTURE%
)

echo.
echo Architecture: %ARCH%
echo.

REM Create build directory
if not exist build mkdir build
cd build

REM Configure with CMake
echo Configuring with CMake...
if defined VCPKG_ROOT (
    cmake .. -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
) else (
    cmake ..
)

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo CMake configuration failed. Please check that SDL2 and Qt are installed.
    echo.
    echo If using vcpkg, install dependencies:
    echo   vcpkg install sdl2:x64-windows qt6:x64-windows
    echo.
    exit /b 1
)

REM Build
echo.
echo Building ZeroPoint...
cmake --build . --config Release -j

if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

echo.
echo === Build Complete ===
echo Binaries are in: build\bin\Release\
echo.
echo Main executables:
echo   - bin\Release\zeropoint_qt.exe   (Qt GUI frontend)
echo   - bin\Release\zeropoint_sdl.exe  (SDL frontend)
echo.
echo Test/utility executables:
echo   - bin\Release\test_cpu.exe, test_ppu.exe, test_apu.exe, test_dma.exe
echo   - bin\Release\run_demo.exe, run_apu_demo.exe
echo.

pause
