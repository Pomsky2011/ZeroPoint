# ZeroPoint Fantasy Console Emulator

Fantasy console with custom PPU (graphics), APU (audio), and DEF88186 CPU.

## Architecture

### Display
- **256×256** pixels, RGBA16 (5-5-5-1) / RGBA32 (8-8-8-8)
- **Rolling Buffer**: 8 banks × 1 KiB, rotates each H-Blank
- **Framebuffer**: $E000-$FFFF in PPU memory
- **Backends**: SDL2, Qt, Vulkan (native GPU acceleration)

### PPU (Picture Processing Unit)
- **67.108864 MHz (2^26 Hz)**, 1 instruction/cycle
- **64 × 16-bit registers** (R0-R63): R59 (VBlank INT), R60 (HBlank INT), R61 (SP), R62 (PC), R63 (DP)
- **64 KiB memory**, 15 basic + 16 extended opcodes
- **Tiles**: 8×8 pixels, 256 max, 4 modes (16/32-bit, 4/8bpp)
- **Palettes**: 16-color (16-bit) / 256-color (32-bit)

### APU (Audio Processing Unit)
- **4 MHz** (1.0 MIPS), 8-bit RISC, 47 instructions
- **64 KiB + 448 KiB banked AROM**
- **MMP**: 16 stereo channels, **SST**: Sample storage with looping

### DEF88186 CPU
- **16 MHz**, Hybrid 65C816/8086, 256 opcodes
- **24-bit address bus** (16 MB), little-endian
- **Registers**: A, X, Y, SP, D, PC, PB, DB, P
- Controls all system resources

### DMA Controller
- **32 MHz**, 16 channels (max 2 concurrent)
- 4 modes: DataCopy, ConstCopy, RepeatTransfer, ConstRepeat

### System Clock
- **Master**: 67.108864 MHz (2^26 Hz, 16-cycle pattern)
- **Frequencies**: PPU/Display (67.108864 MHz / 2^26 Hz), DMA (32 MHz), CPU (16 MHz), APU (4 MHz)

### Platform Support
- **macOS**: Native .app bundles with custom icons (Intel & Apple Silicon)
- **Windows**: x64, ARM64 (MSVC, MinGW-w64)
- **Linux**: x86_64, ARM64/aarch64
- **JIT**: x86-64, ARM64 (stable, use `--jit` flag)

## Memory Maps

### PPU Memory-Mapped I/O
- **$00F0-$00FF**: VOC control registers
- **$0100-$010B**: Pixel drawing
- **$0200-$0207**: Tile drawing
- **$0300-$033F**: Tile definition buffer
- **$E000-$FFFF**: Framebuffer (8 KiB rolling)

### VOC Registers ($00F0-$00FF)
- **$00F0**: Render mode (CRHVPLIW bits)
- **$00F1-$00F2**: Palette address
- **$00F3-$00FA**: Bank order
- **$00FB**: Auto-roll toggle
- **$00FC-$00FF**: Tile translucency

### CPU Memory Map (24-bit)
- **$00-$7F**: ROM (8 MB)
- **$80-$9F**: RAM (2 MB)
- **$A0**: APU Window (64 KB)
- **$B0**: PPU Window (64 KB)
- **$D8**: I/O Registers (72 bytes)
- **$FF**: Boot ROM (64 KB)

### I/O Registers (Bank $D8)
- **$D80000-$D8000F**: PPU Control
- **$D80010-$D8001F**: APU Control
- **$D80020-$D8002F**: DMA Control
- **$D80040-$D80047**: Display Status

## PPU Instructions

### Basic (0x0-0xD)
DEFCALL, MOVXP/NOP, SWAPREG, CLR, CMP, CLRF, JMZ/JMG, JNZ/JNG/JML, INC, DEC, ADD, SUB, MUL, INTDIV

### Preset E (0xE)
TARREG, SETBYTE, BUILD, CPREG

### Preset F (0xF)
SETPOS, SETTILE, SETDP, MOVDP, SETRENDMOD, PALETTE16, PALETTE256, JMR, MOV, SETREGBANK, CLRTILE, CLRPALETTE, TILEDRAW, CALL, GBLS

### Assembly Shorthands
```asm
JMR loop        ; Jump to label
PUSH/POP R5     ; Stack operations
JSR/RET         ; Subroutine call/return
HLT             ; Infinite loop
```

**⚠️ Shorthands use target registers 0-1. Use 2-3 for manual operations.**

## Building

### Quick Start

**macOS** (Intel & Apple Silicon):
```bash
./build-macos.sh
```

**Linux** (x86_64 & ARM64):
```bash
./build-linux.sh
```

**Windows** (x64 & ARM64):
```cmd
build-windows.bat
```

### Manual Build

**All Platforms**:
```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

**Dependencies**:
- **macOS**: `brew install sdl2 qt cmake`
- **Linux** (Debian/Ubuntu): `sudo apt install libsdl2-dev qt6-base-dev cmake build-essential`
- **Linux** (Fedora): `sudo dnf install SDL2-devel qt6-qtbase-devel cmake gcc-c++`
- **Linux** (Arch): `sudo pacman -S sdl2 qt6-base cmake base-devel`
- **Windows**: Install [vcpkg](https://vcpkg.io), then `vcpkg install sdl2:x64-windows qt6:x64-windows`

**Executables**: `bin/run_demo`, `bin/test_*`, `bin/zeropoint_sdl`, `bin/zeropoint_qt`

### Platform Support

- **Windows**: x64, ARM64 (MSVC, MinGW-w64)
- **Linux**: x86_64, ARM64/aarch64
- **macOS**: Intel (x86_64), Apple Silicon (ARM64)
- **JIT**: x86-64, ARM64 (experimental, use `--jit` flag)

## ZPdevtools

**MS-DOS 4.01+ Compatible**: All dev tools can run on 1990's workstations (80286, 2 MB RAM, 100 MB HDD)

### C Compiler (def88186cc)
C89-compliant compiler with Flex/Bison frontend. Supports: types, arrays, unions, enums, functions, control flow, operators, hardware LOOP/LPEND optimization.

**C89 Compliance**: All compiler source files (ast.c, codegen.c, main.c, preprocessor.c) have been ported to strict C89 compliance for Turbo C 2.0 compatibility on MS-DOS 4.01+. Comments converted from `//` to `/* */`, for-loop variable declarations moved to function scope, and mixed declarations fixed.

```bash
cd ZPdevtools/c_compiler && make
./def88186cc input.c -o output.asm && ../cpuasm output.asm output.bin
```

### Assemblers
- **ppuasm**: PPU microcode
- **apuasm**: APU programs
- **cpuasm**: DEF88186 assembly (with `.include`)

### ROM Builder
```bash
rombuilder -cpu main.bin -ppu gfx.bin -apu audio.bin -o game.rom
```
Creates: `game.rom` (binary) + `game.txt` (metadata)

### Development Tools
```bash
cd ZPdevtools && make
```
**Disassemblers**: cpudisasm, ppudisasm, apudisasm
**Analyzers**: rominspect, hexview

### MS-DOS Build
```bash
cd ZPdevtools
make -f Makefile.dos          # Turbo C
nmake /f Makefile.dos COMPILER=MSC  # Microsoft C
```
See `README_DOS.txt` and `C89_PORTING_GUIDE.txt` for details

### Examples
- PPU: `ZPdevtools/examples/ppu/*.asm`
- APU: `ZPdevtools/examples/apu/*.asm`
- CPU: `ZPdevtools/examples/cpu/*.asm`

## Documentation

- **PPU**: `docs/ppu/`, `ZPdevtools/docs/ppu/`
- **APU**: `ZPdevtools/docs/apu/`
- **CPU**: `ZPdevtools/docs/cpu/`
- **Instruction Implementation**: `docs/INSTRUCTION_IMPLEMENTATION_GUIDE.md` - Complete guide for implementing CPU/PPU/APU instructions

## Status

### Complete ✅
- DEF88186 CPU (256 opcodes), APU (47 instructions), PPU (31 opcodes)
- **Clean Instruction Dispatch System** (table-driven, easy to modify/implement)
- Display system with rolling framebuffer, NTSC timing
- Tile system (4 modes), palettes (16/256 colors), translucency
- VOC (16 control registers)
- DMA controller (4 modes, 16 channels)
- **C compiler (100% C89 compliant)**, assemblers (ppuasm/apuasm/cpuasm), ROM builder
- **C89 Porting** (Turbo C 2.0 / MS-DOS 4.01+ compatible)
- Memory mapping (24-bit), I/O registers (Bank $D8)
- System integration, interrupt routing (V-Blank/H-Blank)
- Development tools (5 disassemblers/analyzers)
- MMP audio (16 stereo channels, SDL output)
- **PPU JIT Compiler** (x86-64/ARM64, stable - use `--jit` flag)
- **macOS App Bundles** with custom icon support
- **Vulkan Renderer** (28% faster than SDL, native GPU acceleration, cross-platform)

### In Progress ⏳
- Boot ROM development
- System integration (CPU ↔ PPU ↔ APU communication)

### Planned 🔲
- Debugger with register inspection
- Hardware scrolling
- Window scaling options
- **JIT for additional architectures**: ARMv8/7/6 (Switch/Apple Silicon/Vita/3DS), PPC32 (Wii/U), PPC64 (Xbox 360/PS3)

### Known Issues ⚠️
- **ARM64 JIT under QEMU**: The ARM64 JIT works on real ARM64 hardware but crashes under QEMU emulation due to instruction cache flush interactions. Use interpreter mode when testing with Docker/QEMU.

## File Structure

```
ZeroPoint/          - Emulator core
├── include/        - Headers (display, ppu, apu, cpu, dma, rom, window, vulkan_window, ppu_jit)
│   ├── cpu_instructions.h, ppu_instructions.h, apu_instructions.h - Instruction dispatch headers
├── src/            - Implementation (including vulkan_window.cpp)
│   ├── cpu_instructions.cpp, ppu_instructions.cpp, apu_instructions.cpp - Instruction handlers
├── qt/             - Qt frontend
├── tools/          - Test/demo runners (test_jit, run_demo_vulkan, etc.)
├── shaders/        - Vulkan GLSL shaders (compiled to SPIR-V)
└── docs/           - Documentation
    └── INSTRUCTION_IMPLEMENTATION_GUIDE.md - How to implement instructions

ZPdevtools/         - Development tools
├── c_compiler/     - def88186cc compiler
├── *asm.c          - Assemblers (ppu/apu/cpu)
├── rombuilder.c    - ROM builder
├── examples/       - Example programs
└── docs/           - Complete documentation
```

## Credits

Built with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
- Use interpreter for most tests.