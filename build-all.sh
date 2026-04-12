#!/bin/bash
# ZeroPoint Master Build Script
# Builds emulator AND all development tools

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZPDEVTOOLS_DIR="$SCRIPT_DIR/../ZPdevtools"

echo "========================================"
echo "  ZeroPoint Complete Build Script"
echo "========================================"
echo ""
echo "This script will build:"
echo "  1. ZeroPoint Emulator (Qt, SDL, all tests)"
echo "  2. ZPdevtools (assemblers, ROM tools, etc.)"
echo ""

# Detect OS
OS="$(uname -s)"
case "$OS" in
    Linux*)
        PLATFORM="Linux"
        NCPU=$(nproc)
        ;;
    CYGWIN*|MINGW*|MSYS*)
        PLATFORM="Windows"
        NCPU=$(nproc)
        ;;
    *)
        PLATFORM="Unknown"
        NCPU=4
        ;;
esac

echo "Platform: $PLATFORM"
echo "CPU cores: $NCPU"
echo ""

# ========================================
# Part 1: Build ZeroPoint Emulator
# ========================================
echo "========================================"
echo "  Part 1: Building ZeroPoint Emulator"
echo "========================================"
echo ""

# Use platform-specific build script
if [ "$PLATFORM" = "Linux" ]; then
    bash "$SCRIPT_DIR/build-linux.sh"
else
    echo "Building emulator..."
    mkdir -p build
    cd build
    cmake ..
    cmake --build . -j$NCPU
    cd ..
fi

echo ""
echo "✓ Emulator build complete"
echo ""

# ========================================
# Part 2: Build ZPdevtools
# ========================================
echo "========================================"
echo "  Part 2: Building ZPdevtools"
echo "========================================"
echo ""

if [ ! -d "$ZPDEVTOOLS_DIR" ]; then
    echo "Warning: ZPdevtools directory not found at: $ZPDEVTOOLS_DIR"
    echo "Skipping dev tools build."
    echo ""
else
    cd "$ZPDEVTOOLS_DIR"

    echo "Building native development tools..."
    make clean
    make -j$NCPU

    echo ""
    echo "✓ ZPdevtools build complete"
    echo ""

    # Show what was built
    echo "Built tools:"
    ls -1 ppuasm apuasm cpuasm rombuilder rominspect cpudisasm ppudisasm apudisasm wav2mmp hexview 2>/dev/null | sed 's/^/  - /' || echo "  (some tools may not have built)"
    echo ""

    cd "$SCRIPT_DIR"
fi

# ========================================
# Summary
# ========================================
echo "========================================"
echo "  Build Complete!"
echo "========================================"
echo ""
echo "Emulator binaries:"
echo "  build/bin/zeropoint_qt      - Qt GUI frontend"
echo "  build/bin/zeropoint_sdl     - SDL frontend"
echo "  build/bin/test_*            - Test suites"
echo "  build/bin/run_demo          - Demo runners"
echo ""
if [ -d "$ZPDEVTOOLS_DIR" ]; then
    echo "Development tools (in ../ZPdevtools/):"
    echo "  ppuasm, apuasm, cpuasm      - Assemblers"
    echo "  rombuilder, rominspect      - ROM tools"
    echo "  cpudisasm, ppudisasm, etc.  - Disassemblers"
    echo "  wav2mmp, hexview            - Utilities"
    echo ""
fi
echo "Quick start:"
echo "  cd build/bin"
echo "  ./zeropoint_qt              - Launch Qt frontend"
echo ""
echo "  cd ../ZPdevtools"
echo "  ./ppuasm examples/ppu/simple_pixel_test.asm output.bin"
echo "  cd ../ZeroPoint/build/bin"
echo "  ./run_demo ../../ZPdevtools/output.bin"
echo ""
