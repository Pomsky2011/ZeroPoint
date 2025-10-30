#!/bin/bash
# ZeroPoint Rebuild Script
# Builds all isolated components and full emulator

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build_qt"

echo "=== ZeroPoint Rebuild Script ==="
echo ""

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    echo "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure CMake
echo "Configuring CMake..."
cmake .. || { echo "CMake configuration failed!"; exit 1; }
echo ""

# Build isolated CPU (console access)
echo "=== Building Isolated CPU (console) ==="
cmake --build . --target test_cpu -j$(sysctl -n hw.ncpu) || { echo "CPU build failed!"; exit 1; }
echo "✓ CPU test built: bin/test_cpu"
echo ""

# Build isolated APU (audio window)
echo "=== Building Isolated APU (audio) ==="
cmake --build . --target test_apu -j$(sysctl -n hw.ncpu) || { echo "APU build failed!"; exit 1; }
cmake --build . --target run_apu_demo -j$(sysctl -n hw.ncpu) || { echo "APU demo build failed!"; exit 1; }
echo "✓ APU test built: bin/test_apu"
echo "✓ APU demo built: bin/run_apu_demo"
echo ""

# Build isolated PPU (screen access)
echo "=== Building Isolated PPU (screen) ==="
cmake --build . --target run_demo -j$(sysctl -n hw.ncpu) || { echo "PPU build failed!"; exit 1; }
cmake --build . --target test_ppu -j$(sysctl -n hw.ncpu) || { echo "PPU test build failed!"; exit 1; }
echo "✓ PPU demo built: bin/run_demo"
echo "✓ PPU test built: bin/test_ppu"
echo ""

# Build DMA test
echo "=== Building DMA Test ==="
cmake --build . --target test_dma -j$(sysctl -n hw.ncpu) || { echo "DMA build failed!"; exit 1; }
echo "✓ DMA test built: bin/test_dma"
echo ""

# Build full emulators
echo "=== Building Full Emulator (SDL) ==="
cmake --build . --target zeropoint_sdl -j$(sysctl -n hw.ncpu) || { echo "SDL emulator build failed!"; exit 1; }
echo "✓ SDL emulator built: bin/zeropoint_sdl"
echo ""

echo "=== Building Full Emulator (Qt) ==="
cmake --build . --target zeropoint_qt -j$(sysctl -n hw.ncpu) || { echo "Qt emulator build failed!"; exit 1; }
echo "✓ Qt emulator built: bin/zeropoint_qt"
echo ""

echo "=== Build Complete ==="
echo ""
echo "Entry Points (Full Emulator):"
echo "  - APU: $8000 (apu_bios.bin)"
echo "  - PPU: $0000 (loaded by CPU)"
echo "  - CPU: $E00000 (zp_boot.rom)"
echo ""
echo "Usage Examples:"
echo "  Isolated CPU:  ./bin/test_cpu"
echo "  Isolated APU:  ./bin/run_apu_demo <audio.bin>"
echo "  Isolated PPU:  ./bin/run_demo <graphics.bin>"
echo "  Full Emulator: ./bin/zeropoint_sdl <rom.zpb>"
echo "                 ./bin/zeropoint_qt <rom.zpb>"
echo ""
