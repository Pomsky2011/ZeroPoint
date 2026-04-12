# ZeroPoint Build Guide

Complete guide for building the ZeroPoint emulator and development tools on all supported platforms.

## Quick Start

### Build Everything (Recommended)

```bash
# Clone the repository (if not already done)
cd /path/to/ZeroPoint

# Build emulator AND all dev tools in one command
./build-all.sh
```

This will:
1. Build the ZeroPoint emulator (Qt, SDL, all tests)
2. Build all ZPdevtools (assemblers, ROM builder, disassemblers, utilities)

### Build Only Emulator

**Linux:**
```bash
./build-linux.sh
```

**Windows:**
```cmd
build-windows.bat
```

### Build Only Dev Tools

```bash
cd ../ZPdevtools
./build-native.sh
```

**For MS-DOS 4.01+ (Turbo C / Microsoft C):**
```cmd
cd ZPdevtools
build-dos.bat
```

## Platform-Specific Instructions

### Linux (x86_64 & ARM64)

**Requirements:**
- Linux kernel 4.0+ (any modern distro)
- GCC or Clang
- CMake 3.16+
- SDL2 and Qt6 development packages

**Debian/Ubuntu:**
```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev qt6-base-dev
./build-linux.sh
```

**Fedora:**
```bash
sudo dnf install gcc-c++ make cmake SDL2-devel qt6-qtbase-devel
./build-linux.sh
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake sdl2 qt6-base
./build-linux.sh
```

**Output:**
- `build/bin/zeropoint_qt` - Qt frontend
- `build/bin/zeropoint_sdl` - SDL frontend
- `build/bin/test_*` - Test suites
- `../ZPdevtools/ppuasm`, `apuasm`, `cpuasm`, etc.

### Windows (x64 & ARM64)

**Requirements:**
- Windows 10/11
- Visual Studio 2019+ with C++ Desktop Development workload
- CMake 3.16+
- vcpkg (recommended for dependencies)

**Install vcpkg:**
```cmd
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install sdl2:x64-windows qt6:x64-windows
set VCPKG_ROOT=C:\path\to\vcpkg
```

**Build:**
```cmd
REM Run from Developer Command Prompt for VS
build-windows.bat
```

**Output:**
- `build\bin\Release\zeropoint_qt.exe` - Qt frontend
- `build\bin\Release\zeropoint_sdl.exe` - SDL frontend
- `build\bin\Release\test_*.exe` - Test suites
- `..\ZPdevtools\ppuasm.exe`, `apuasm.exe`, etc. (if using MSYS/MinGW)

### MS-DOS 4.01+ (Turbo C / Microsoft C)

**Requirements:**
- MS-DOS 4.01 or higher
- 80286 CPU (or better)
- 2 MB RAM minimum
- 100 MB hard drive space
- Turbo C 2.0 OR Microsoft C 6.0

**Build dev tools only (emulator cannot run on DOS):**
```cmd
cd ZPdevtools
build-dos.bat
```

This builds **ALL** dev tools as C89-compliant DOS executables:
- Assemblers: `CPUASM.EXE`, `APUASM.EXE`, `PPUASM.EXE`
- Disassemblers: `CPUDISASM.EXE`, `APUDISASM.EXE`, `PPUDISASM.EXE`
- ROM Tools: `ROMBUILDER.EXE`, `ROMINSPECT.EXE`
- Utilities: `HEXVIEW.EXE`, `WAV2MMP.EXE`
- C Compiler: `DEF88186CC.EXE`

**Output:**
- All executables in `BIN\` directory

**Note:** The ZeroPoint emulator requires a modern OS and cannot run on MS-DOS. However, **all development tools** can run on DOS, allowing you to develop ZeroPoint games on 1990s hardware!

## Build Scripts Reference

### Emulator Build Scripts

| Script | Platform | Description |
|--------|----------|-------------|
| `build-all.sh` | Linux | Master script - builds emulator + dev tools |
| `build-linux.sh` | Linux | Emulator only (x86_64 & ARM64) |
| `build-windows.bat` | Windows | Emulator only (x64 & ARM64) |

### Dev Tools Build Scripts

| Script | Platform | Location | Description |
|--------|----------|----------|-------------|
| `build-native.sh` | Linux | `ZPdevtools/` | All dev tools (native) |
| `build-dos.bat` | MS-DOS | `ZPdevtools/` | All dev tools (DOS) |
| `Makefile` | Linux | `ZPdevtools/` | GNU Make (native) |
| `Makefile.dos` | MS-DOS | `ZPdevtools/` | Turbo Make / NMAKE |

## Manual Build (Advanced)

### Emulator

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

### Dev Tools

```bash
cd ZPdevtools
make -j$(nproc)
```

## Troubleshooting

### Linux: "Qt6 not found"
```bash
# Try Qt5 instead
sudo apt install qt5-default    # Debian/Ubuntu
```

### Windows: "CMake cannot find SDL2"
```cmd
# Install with vcpkg
vcpkg install sdl2:x64-windows qt6:x64-windows
set VCPKG_ROOT=C:\path\to\vcpkg
```

### DOS: "Compiler not found"
- Install Turbo C 2.0 to `C:\TC\`
- OR install Microsoft C 6.0 to `C:\MSC\`
- Edit `build-dos.bat` if installed to different location

### Build Fails with Memory Error
- Linux: Check `ulimit -m` and increase if needed
- DOS: Ensure 2 MB RAM minimum (use `MEM` command to check)

## Testing Your Build

### Emulator
```bash
cd build/bin
./test_cpu          # Test CPU interpreter
./test_ppu          # Test PPU microcode
./test_apu          # Test APU
./test_dma          # Test DMA controller
```

### Dev Tools
```bash
cd ZPdevtools
./ppuasm examples/ppu/simple_pixel_test.asm test.bin
./cpuasm examples/cpu/hello.asm test.bin
./rombuilder -cpu cpu.bin -ppu ppu.bin -o test.rom
```

## See Also

- `CLAUDE.md` - System architecture overview
- `README.md` - Quick start and features
- `TODO.md` - Development status
- `ZPdevtools/README.md` - Dev tools documentation
- `ZPdevtools/README_DOS.txt` - DOS build instructions
- `ZPdevtools/C89_PORTING_GUIDE.txt` - C89 compatibility guide
