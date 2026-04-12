# Building ZeroPoint

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

### macOS with Homebrew

```bash
# Install dependencies
brew install cmake sdl2 qt6

# Create build directory
mkdir build_qt
cd build_qt

# Configure and build
cmake ..
make

# Run the emulator
./bin/zeropoint_qt
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies
sudo apt-get install cmake libsdl2-dev qt6-base-dev

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
│   ├── display.h     # Display system
│   ├── rom.h         # ROM loader
│   └── window.h      # SDL window (standalone)
├── src/              # Core source files
│   ├── display.cpp
│   ├── main.cpp      # SDL standalone entry point
│   ├── rom.cpp
│   └── window.cpp
├── qt/               # Qt frontend
│   ├── main.cpp
│   ├── mainwindow.h/cpp/ui
│   ├── configdialog.h/cpp/ui
│   └── emulatorwidget.h/cpp
├── tools/            # Development tools
│   └── make_test_rom.cpp
├── docs/             # Documentation
├── Makefile          # SDL build
└── CMakeLists.txt    # Qt/full build
```
