#!/bin/bash
# ZeroPoint Build Script for Linux (x86_64 & ARM64)

set -e

echo "=== ZeroPoint Linux Build Script ==="
echo ""

# Detect package manager
if command -v apt-get &> /dev/null; then
    PKG_MGR="apt-get"
    INSTALL_CMD="sudo apt-get install -y"
    SDL2_PKG="libsdl2-dev"
    QT_PKG="qt6-base-dev"
    CMAKE_PKG="cmake"
    BUILD_PKG="build-essential"
elif command -v dnf &> /dev/null; then
    PKG_MGR="dnf"
    INSTALL_CMD="sudo dnf install -y"
    SDL2_PKG="SDL2-devel"
    QT_PKG="qt6-qtbase-devel"
    CMAKE_PKG="cmake"
    BUILD_PKG="gcc-c++ make"
elif command -v pacman &> /dev/null; then
    PKG_MGR="pacman"
    INSTALL_CMD="sudo pacman -S --needed"
    SDL2_PKG="sdl2"
    QT_PKG="qt6-base"
    CMAKE_PKG="cmake"
    BUILD_PKG="base-devel"
else
    echo "Warning: Could not detect package manager."
    echo "Please install the following manually:"
    echo "  - SDL2 development files"
    echo "  - Qt6 (or Qt5) development files"
    echo "  - CMake"
    echo "  - C++ compiler (GCC/Clang)"
fi

# Check and install dependencies
if [ -n "$PKG_MGR" ]; then
    echo "Detected package manager: $PKG_MGR"
    echo "Checking dependencies..."

    if ! command -v cmake &> /dev/null; then
        echo "Installing CMake..."
        $INSTALL_CMD $CMAKE_PKG
    else
        echo "✓ CMake is installed"
    fi

    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        echo "Installing build tools..."
        $INSTALL_CMD $BUILD_PKG
    else
        echo "✓ Build tools are installed"
    fi

    # SDL2 check
    if ! pkg-config --exists sdl2 2>/dev/null; then
        echo "Installing SDL2..."
        $INSTALL_CMD $SDL2_PKG
    else
        echo "✓ SDL2 is installed"
    fi

    # Qt check
    if ! pkg-config --exists Qt6Core 2>/dev/null && ! pkg-config --exists Qt5Core 2>/dev/null; then
        echo "Installing Qt..."
        $INSTALL_CMD $QT_PKG || $INSTALL_CMD qt5-qtbase-devel || $INSTALL_CMD libqt5-qtbase-dev || true
    else
        echo "✓ Qt is installed"
    fi
fi

# Detect architecture
ARCH=$(uname -m)
echo ""
echo "Architecture: $ARCH"

# Build
echo ""
echo "Building ZeroPoint..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo ""
echo "=== Build Complete ==="
echo "Binaries are in: build/bin/"
echo ""
echo "Main executables:"
echo "  - bin/zeropoint_qt   (Qt GUI frontend)"
echo "  - bin/zeropoint_sdl  (SDL frontend)"
echo ""
echo "Test/utility executables:"
echo "  - bin/test_cpu, bin/test_ppu, bin/test_apu, bin/test_dma"
echo "  - bin/run_demo, bin/run_apu_demo"
