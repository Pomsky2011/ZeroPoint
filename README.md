# ZeroPoint

A fantasy console emulator featuring a tile-based PPU with microcode programming and an 8-bit audio processor.

## Overview

ZeroPoint is a fantasy console with custom programmable graphics (PPU) and audio (APU) processors. Unlike traditional fixed-function hardware, both processors run custom instruction sets that give developers full control over rendering and sound.

## Features

### PPU (Picture Processing Unit)
- **Microcode-based graphics processor** - Program your own rendering pipeline
- **64 MHz execution**, 1 instruction per cycle
- **47 instructions** (4-bit opcode + extended instruction sets)
- **256×256 display** with dual color modes (16-bit/32-bit RGBA)
- **Rolling framebuffer** - 8 banks × 1 KiB with H-Blank rotation
- **Tile system** - 8×8 pixel tiles, 256 tiles max, DIY placement
- **Interrupts** - V-Blank and H-Blank with automatic stack management
- **64 × 16-bit registers** with special registers (PC, DP, SP)

### APU (Audio Processing Unit)
- **8-bit RISC processor** @ 4.2 MHz, 4 cycles per instruction
- **47 instructions** (5-bit opcode + 11-bit operands)
- **16-bit stack** with push/pop operations and function calls
- **Up to 256 × 8-bit registers** + special registers (PC, SP, RP, DP, DB, BF)
- **MMP** - Music Mixing Processor for 16 stereo channels
- **SST** - Sample Storage System with looping and effects
- **64 KiB addressable memory** + 448 KiB banked ROM

### Development Tools
- **zpasm** - PPU microcode assembler
- **apuasm** - APU assembly language compiler
- Two frontends:
  - **SDL**: Standalone emulator with simple display
  - **Qt**: Full GUI with ROM loading and configuration

## Building

Requires CMake 3.16+, C++17 compiler, SDL2, and Qt5/Qt6.

```bash
mkdir build && cd build
cmake ..
make -j4
```

### Executables
- `bin/zeropoint_sdl` - SDL frontend
- `bin/zeropoint_qt` - Qt frontend
- `bin/run_demo <demo.bin>` - Run PPU demo with SDL window
- `bin/test_demo <demo.bin>` - Run PPU demo headless (testing)
- `bin/test_apu <program.bin>` - Run APU program headless
- `bin/run_apu_demo <program.bin>` - Run APU program with audio output

## Creating Demos

### PPU Assembly
Located in `/Users/alexanderwhite/Documents/Code/ZPdevtools`

```bash
cd ../ZPdevtools
gcc -o zpasm zpasm.c
./zpasm examples/ppu/simple_pixel_test.asm output.bin
cd ../ZeroPoint/build
./bin/run_demo ../ZPdevtools/output.bin
```

### APU Assembly
```bash
cd ../ZPdevtools
gcc -o apuasm apuasm.c
./apuasm examples/apu/hello.asm
cd ../ZeroPoint/build
./bin/test_apu ../ZPdevtools/examples/apu/hello.bin 10000
```

## Documentation

### PPU
- `CLAUDE.md` - Complete architecture overview
- `docs/display.md` - Display system and color modes
- `ZPdevtools/docs/ppu/ucode.txt` - Complete instruction reference (2000+ lines)
- `ZPdevtools/docs/ppu/preset-e-and-shorthands.txt` - Preset E instructions

### APU
- `ZPdevtools/docs/apu/README.txt` - APU documentation index
- `ZPdevtools/docs/apu/instruction-set.txt` - All 47 instructions
- `ZPdevtools/docs/apu/programming-guide.txt` - Complete programming examples
- `ZPdevtools/docs/apu/mmp.txt` - Music Mixing Processor
- `ZPdevtools/docs/apu/sst.txt` - Sample Storage System

## Status

**PPU**: Display system, tile system, interrupts, and microcode execution implemented. Loop bug blocking complex demos.

**APU**: Full instruction set implemented including stack operations, function calls, and memory operations. MMP audio mixing not yet implemented.

## License

To be determined.
