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
- **4.194304 MHz (2^22 Hz)** (1.0 MIPS), 8-bit RISC
- **~47 instruction mnemonics** decoded from a 5-bit opcode field (28 of the 32
  encodable opcode values are dispatched in `APU::executeInstruction`; many
  opcodes further sub-decode into 2-4 mnemonics off the operand bits — e.g.
  opcode 0x09 covers LDA/STA/STA$. The remaining 4 opcode values (0x1C-0x1F)
  halt on execution.)
- **256 8-bit general-purpose registers** (array size), but the ISA only
  reaches **R0-R127** in practice: ALU source operands are hardwired to a
  single bit selecting R0(X)/R1(Y) only, and destination-register fields are
  7 bits (max R127). R128-R255 are unreachable through any instruction.
- **Special registers**: PC (16-bit), SP (16-bit hardware stack), RP, DP, DB, BF, FLAGS (4-bit: Z/G/L/C)
- **64 KiB + 448 KiB banked AROM**
- **Instructions**: NOP, JMP, JNZ/JZ, SRP/SDP/SDB, NOR, AND, ADD, SUB, STA/STR, LDA/SCR, flags (SFR/CF/SF/STF), ZOR/ZOA, LST/LFN/BRT/BRP, ADC, SBC, BEQ/BNE, BLT/BGT, JMS, INC/DEC, BSP/RET/PUX/PUY/POX/POY/PUDP/PODP, MOV/EXC, CME/CMN/CMG/CML, CRB, XOR
  (JSR/JDP/JDPS were part of an earlier ISA draft and were never implemented; only JMS exists)
- **MMP**: 16 channels with pan (pitch/volume/pan per-channel), **SST**: Sample storage with looping

### DEF88186 CPU
- **16.777216 MHz (2^24 Hz)**, Hybrid 65C816/8086, 256 opcodes
- **24-bit address bus** (16 MB), little-endian
- **Registers**: A, X, Y, SP, D, PC, PB, DB, P
- Controls all system resources

### DMA Controller
- **33.554432 MHz (2^25 Hz)**, 16 channels (max 2 concurrent)
- 4 modes: DataCopy, ConstCopy, RepeatTransfer, ConstRepeat

### System Clock
- **Master**: 67.108864 MHz (2^26 Hz, 16-cycle pattern)
- **Frequencies**: PPU/Display (67.108864 MHz, 2^26 Hz, master/1), DMA (33.554432 MHz, 2^25 Hz, master/2), CPU (16.777216 MHz, 2^24 Hz, master/4), APU (4.194304 MHz, 2^22 Hz, master/16)

### Platform Support
- **Linux**: x86_64, ARM64/aarch64
- **Windows**: x64, ARM64 (MSVC, MinGW-w64)
- **JIT**: x86-64, ARM64 (stable, use `--jit` flag)

## Memory Maps

### PPU Memory-Mapped I/O
- **$00F0-$00FF**: VOC control registers
- **$0100-$010B**: Pixel drawing
- **$0200-$0203**: Tile drawing (X/Y position; `$0204-$0207` are plain PPU RAM, not wired to any handler)
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
- **$BE-$BF**: Shadow Work RAM (128 KB)
- **$D8**: I/O Registers (96 bytes, $D80000-$D8005F)
- **$FF**: Boot ROM (64 KB) — reserved but not yet mapped; nothing allocates
  or loads into this bank, so reads currently return open-bus `$FF`. See
  "In Progress" status below.

### I/O Registers (Bank $D8)
- **$D80000-$D8000F**: PPU Control
- **$D80010-$D8001F**: APU Control
- **$D80020-$D8002F**: DMA Control
- **$D80030-$D8003F**: Player Ports (P1 controller direction/buttons; P2 is a
  buffered serial port with RX/TX status bits and no consumer yet — see
  `docs/debug-protocol.md`, which is a design doc, not a shipped feature)
- **$D80040-$D80047**: Display Status
- **$D80048-$D8004F**: System Control (only a DEV_MODE flag byte; the other
  registers described in early planning docs were never built)
- **$D80050-$D80057**: Timer Control
- **$D80058-$D8005F**: Interrupt Controller

### Timer System ($D80050-$D80052)
8 independent hardware timers with IRQ support (bit pattern: `0bVHSQETAR`):
- **V (0x80)**: V-blank timer (~16.67ms, 60Hz)
- **H (0x40)**: H-blank timer (one scanline, ~244μs)
- **S (0x20)**: 1 second timer
- **Q (0x10)**: 1/4 second timer (250ms)
- **E (0x08)**: 1/8 second timer (125ms)
- **T (0x04)**: 1/1024 second timer (~977μs)
- **A (0x02)**: 16777/16777216 second timer (~1ms)
- **R (0x01)**: 60 V-blank timer (~1 second)

**Registers**:
- **$D80050** (TIMER_CONTROL): Enable/disable timers (write bit=1 to enable)
- **$D80051** (TIMER_STATUS): Timer status flags (read), write 1 to clear
- **$D80052** (TIMER_INT_ENABLE): Enable IRQ per timer (write bit=1 to enable interrupt)

**Usage**: Enable timer in CONTROL, enable interrupt in INT_ENABLE. When timer expires, status flag is set and CPU IRQ is triggered (if interrupt enabled). Clear status flags by writing 1s to STATUS register.

### Interrupt Controller ($D80058-$D80059)
All IRQ sources share the CPU's single maskable line. The controller latches which source fired so the ISR can discriminate them, and provides a top-level per-source mask (in addition to each peripheral's own enable).

**Sources** (bit): `V (0x01)` V-blank, `H (0x02)` H-blank, `T (0x04)` any Timer (read TIMER_STATUS for which), `D (0x08)` DMA-complete (reserved).

**Registers**:
- **$D80058** (IRQ_STATUS): Read = latched pending sources. Write 1 to a bit to acknowledge/clear it. Clearing all TIMER_STATUS bits also clears the aggregate Timer bit.
- **$D80059** (IRQ_ENABLE): Top-level per-source mask (default 0xFF = transparent). A source must be enabled here *and* at its peripheral to assert the CPU line; pending bits are recorded regardless.

**Usage**: In the ISR, read IRQ_STATUS to find the source, dispatch, then write it back to acknowledge before RTI.

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

**Build Everything (Emulator + Dev Tools):**
```bash
./build-all.sh              # Builds emulator AND all dev tools
```

**Build Emulator Only:**
```bash
./build-linux.sh            # Linux (x86_64 & ARM64)
build-windows.bat           # Windows (x64 & ARM64)
```

**Build Dev Tools Only:**
```bash
cd ../ZPdevtools
./build-native.sh           # Native build (Linux)
build-dos.bat               # MS-DOS build (Turbo C/MS C)
```

### Manual Build

**All Platforms**:
```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

**Dependencies**:
- **Linux** (Debian/Ubuntu): `sudo apt install libsdl2-dev qt6-base-dev cmake build-essential`
- **Linux** (Fedora): `sudo dnf install SDL2-devel qt6-qtbase-devel cmake gcc-c++`
- **Linux** (Arch): `sudo pacman -S sdl2 qt6-base cmake base-devel`
- **Windows**: Install [vcpkg](https://vcpkg.io), then `vcpkg install sdl2:x64-windows qt6:x64-windows`

**Executables**: `bin/run_demo`, `bin/test_*`, `bin/zeropoint_sdl`, `bin/zeropoint_qt`

### Platform Support

- **Linux**: x86_64, ARM64/aarch64
- **Windows**: x64, ARM64 (MSVC, MinGW-w64)
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

- **PPU**: `docs/ppu.md`, `ZPdevtools/docs/ppu/`
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
- **Hardware Timers** (8 independent timers with IRQ support: V-blank, H-blank, 1s, 1/4s, 1/8s, 1/1024s, ~1ms, 60-frame)
- **C compiler (100% C89 compliant)**, assemblers (ppuasm/apuasm/cpuasm), ROM builder
- **C89 Porting** (Turbo C 2.0 / MS-DOS 4.01+ compatible)
- Memory mapping (24-bit), I/O registers (Bank $D8)
- System integration, interrupt routing (V-Blank/H-Blank/Timers)
- Development tools (5 disassemblers/analyzers)
- MMP audio (16 channels with pan, SDL output)
- **PPU instruction timing model**: multi-cycle cost per instruction (branches, MUL/DIV, palette loads, the TILEDRAW blitter). Costs are tunable constants at the top of `src/ppu.cpp`; `PPU::tick()` stalls for `cost-1` cycles after issuing.
- **PPU batched executor** (`PPU::runBatch`): fast interpreter path that runs whole instructions and collapses stalls (1.2x-4x faster than per-cycle ticking, provably state-identical). The `--jit` flag drives this. NOTE: the "JIT" is this batched executor, **not** a native compiler — the `emit*` codegen only ever wrapped `tick()` in a native loop; real per-opcode codegen was scoped and deliberately not built.
- **Vulkan Renderer** (28% faster than SDL, native GPU acceleration, cross-platform)

### In Progress ⏳
- Boot ROM development
- System integration (CPU ↔ PPU ↔ APU communication)

### Planned 🔲
- Debugger with register inspection
- Hardware scrolling
- Window scaling options
- **JIT for additional architectures**: ARMv8/7/6 (Switch/Vita/3DS), PPC32 (Wii/U), PPC64 (Xbox 360/PS3)

### Known Issues ⚠️
- **ARM64 JIT under QEMU**: The ARM64 JIT works on real ARM64 hardware but crashes under QEMU emulation due to instruction cache flush interactions. Use interpreter mode when testing with Docker/QEMU.

## File Structure

```
ZeroPoint/          - Emulator core
├── include/        - Headers (display, ppu, apu, cpu, dma, rom, window, vulkan_window, ppu_jit, interrupt_controller)
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