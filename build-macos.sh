#!/bin/bash
# ZeroPoint Build Script for macOS (Intel & Apple Silicon)

set -e

echo "=== ZeroPoint macOS Build Script ==="
echo ""

# Check for Homebrew
if ! command -v brew &> /dev/null; then
    echo "Error: Homebrew is not installed."
    echo "Please install Homebrew from https://brew.sh/"
    exit 1
fi

# Check for required dependencies
echo "Checking dependencies..."

if ! brew list sdl2 &> /dev/null; then
    echo "Installing SDL2..."
    brew install sdl2
else
    echo "✓ SDL2 is installed"
fi

if ! brew list qt &> /dev/null && ! brew list qt@6 &> /dev/null && ! brew list qt@5 &> /dev/null; then
    echo "Installing Qt..."
    brew install qt
else
    echo "✓ Qt is installed"
fi

if ! command -v cmake &> /dev/null; then
    echo "Installing CMake..."
    brew install cmake
else
    echo "✓ CMake is installed"
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
make -j$(sysctl -n hw.ncpu)

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
