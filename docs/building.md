# Building ZeroPoint

> **Note**: This doc describes an older, minimal build path (root `Makefile`,
> hand-created `build_qt/` directory). **`BUILD.md` and the README's Quick
> Start are the current, maintained instructions** (`./build-linux.sh` /
> `build-windows.bat` + top-level `cmake`) — use those. This file is kept for
> the SDL-only `make` path, which still exists but isn't Vulkan-aware and
> doesn't build the Qt frontend or dev tools. macOS support was removed from
> the project (Linux/Windows only now); the Homebrew section below is stale.

## Prerequisites

### SDL Frontend
- C++17 compatible compiler (gcc, clang)
- SDL2 development libraries
- Make

### Qt Frontend
- C++17 compatible compiler
- CMake 3.16 or higher
- SDL2 development libraries
- Qt5 or Qt6 development libraries

## Building the SDL Frontend

The SDL frontend is the simple standalone emulator.

```bash
make
./build/zeropoint
```

## Building the Qt Frontend

The Qt frontend provides a full GUI with ROM loading and configuration.

### Linux (Ubuntu/Debian)

macOS is no longer supported (removed from the project); Linux and Windows
only. `CMakeLists.txt` requires Vulkan unconditionally (`find_package(Vulkan
REQUIRED)`) even for this SDL/Qt-only path — see `BUILD.md`.

```bash
# Install dependencies
sudo apt-get install cmake libsdl2-dev qt6-base-dev libvulkan-dev

# Create build directory
mkdir build_qt
cd build_qt

# Configure and build
cmake ..
make

# Run the emulator
./bin/zeropoint_qt
```

## Building Tools

The test ROM creation tool is built automatically with CMake:

```bash
cd build_qt
./bin/make_test_rom test.zpb
```

This creates a test ROM file that can be loaded in the emulator.

## Project Structure

```
ZeroPoint/
├── include/          # Header files
│   ├── display.h, rom.h, window.h, vulkan_window.h
│   ├── cpu.h, cpu_instructions.h
│   ├── ppu.h, ppu_instructions.h, ppu_jit.h
│   ├── apu.h, apu_instructions.h
│   ├── dma.h, system.h, interrupt_controller.h
├── src/              # Core source files
│   ├── display.cpp, rom.cpp, window.cpp, vulkan_window.cpp
│   ├── cpu.cpp, cpu_instructions.cpp
│   ├── ppu.cpp, ppu_jit.cpp
│   ├── apu.cpp
│   ├── dma.cpp, system.cpp, main.cpp
├── qt/               # Qt frontend
│   ├── main.cpp
│   ├── mainwindow.h/cpp/ui
│   ├── configdialog.h/cpp/ui
│   └── emulatorwidget.h/cpp
├── tools/            # Development tools & test runners
│   └── make_test_rom.cpp, run_demo*.cpp, test_*.cpp
├── shaders/          # Vulkan GLSL shaders (compiled to SPIR-V)
├── docs/             # Documentation
├── Makefile          # Minimal SDL-only build (see note at top of this file)
└── CMakeLists.txt    # Current, maintained build (Qt + SDL + Vulkan + tests)
```

(`ppu_instructions.cpp`/`apu_instructions.cpp` still exist as build targets
but are now empty stubs — PPU and APU opcode dispatch both live directly in
`src/ppu.cpp`/`src/apu.cpp`.)
