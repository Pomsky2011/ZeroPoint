# ZeroPoint Fantasy Console Emulator

A fantasy console featuring custom PPU (graphics), APU (audio), and DEF88186 CPU (main controller).

## Architecture Overview

### Display System
- **Resolution**: 256×256 pixels
- **Color Modes**: RGBA16 (5-5-5-1, 16 scanlines) / RGBA32 (8-8-8-8, 8 scanlines)
- **Rolling Buffer**: 8 banks × 1 KiB, rotates each H-Blank
- **Framebuffer**: Memory-mapped at $E000-$FFFF in PPU memory
- **Backends**: SDL2 and Qt

### PPU (Picture Processing Unit)
- **Clock**: 64 MHz, 1 instruction/cycle
- **Registers**: 64 × 16-bit (R0-R63)
  - R59: V-Blank interrupt address
  - R60: H-Blank interrupt address
  - R61 (SP): Stack pointer
  - R62 (PC): Program counter
  - R63 (DP): Data pointer
- **Memory**: 64 KiB unified
- **Instruction Set**: 15 basic + 16 Preset F extended opcodes
- **Format**: 16-bit big-endian (4-bit opcode + 12-bit operand)

### Tile System
- **8×8 pixels**, 64 bytes each, 256 tiles max
- Unbound placement at any pixel coordinate
- No automatic layering - fully DIY
- **4 tile modes**: 16BBGR 4bpp, 32RGBA 4bpp, 16BBGR 8bpp, 32RGBA 8bpp
- **Palette system**: 16-color (16-bit) and 256-color (32-bit) palettes
- **Translucency**: Per-tile opacity (0-15) with 4 blending modes
  - Multiply (darken), Average (50/50), Subtract, Add (lighten)

### APU (Audio Processing Unit)
- **Clock**: 4 MHz (ticks every 16 master cycles), 4 CPI (1.0 MIPS)
- **8-bit RISC architecture**
- **47 instructions** (5-bit opcode + 11-bit operands)
- **Memory**: 64 KiB + 448 KiB banked AROM
- **Registers**: Up to 256 8-bit + special (PC, RP, DP, DB, BF, SP)
- **Stack operations**: PUX, PUY, POX, POY, RET
- **Functions**: CFN/CCF with auto return address
- **MMP**: 16 stereo channels (32 mono)
- **SST**: Sample storage with looping

### DEF88186 Main CPU
- **Clock**: 16 MHz (ticks every 4 master cycles)
- **Hybrid 65C816/8086 16-bit** (system master)
- **256 opcodes**: 65816 base + 8086-inspired extensions
- **Address Bus**: 24-bit (16 MB via banking)
- **Endianness**: Little-endian
- **Registers**: A (16/8-bit), X, Y, SP, D, PC, PB, DB, P (NV-MXDIZC)
- **65816 Features**: Full addressing modes, 24-bit long addressing, stack-relative, block moves, BCD
- **8086 Extensions**: LOOP/LPEND, MUL, DIV, XCHG, SHL/SHR, SDB, CALL/RET
- **Role**: Controls all system resources (PPU, APU, DMA, memory, I/O)

### DMA Controller
- **Clock**: 32 MHz (ticks every 2 master cycles)
- **16 independent channels**, max 2 active simultaneously
- **4 transfer modes**: DataCopy (3 cyc/byte), ConstCopy (1 cyc/byte), RepeatTransfer (3 cyc/byte), ConstRepeat (2 cyc/byte)
- **Configurable**: 9-byte configuration per transfer
- **Interrupt handling**: Pauses all DMA during interrupts

### System Clock Synchronization
- **Master Clock**: 64 MHz (PPU pixel clock)
- **Integer-based**: Deterministic 16-cycle pattern (no fractional accumulators)

**Execution Pattern** (repeats every 16 master cycles):
- **Cycle 0**: PPU, Display
- **Cycle 1**: PPU, DMA, Display
- **Cycle 2**: PPU, Display
- **Cycle 3**: PPU, CPU, DMA, Display
- **Cycle 4**: PPU, Display
- **Cycle 5**: PPU, DMA, Display
- **Cycle 6**: PPU, Display
- **Cycle 7**: PPU, CPU, DMA, Display
- **Cycle 8**: PPU, Display
- **Cycle 9**: PPU, DMA, Display
- **Cycle 10**: PPU, Display
- **Cycle 11**: PPU, CPU, DMA, Display
- **Cycle 12**: PPU, Display
- **Cycle 13**: PPU, DMA, Display
- **Cycle 14**: PPU, Display
- **Cycle 15**: PPU, CPU, DMA, APU, Display

**Effective Frequencies**:
- **PPU**: 64 MHz (every cycle)
- **Display**: 64 MHz (every cycle)
- **DMA**: 32 MHz (every 2 cycles: 1, 3, 5, 7, 9, 11, 13, 15)
- **CPU**: 16 MHz (every 4 cycles: 3, 7, 11, 15)
- **APU**: 4 MHz (every 16 cycles: 15)

## Memory Map (PPU)

### Memory-Mapped I/O
- **0x00F0-0x00FF**: Video Output Coprocessor (VOC) control registers
- **0x0100-0x010B**: Pixel drawing (X, Y, R, G, B, A - write to 0x010A triggers)
- **0x0200-0x0207**: Tile drawing (X, Y, tile ID - write to 0x0206 triggers)
- **0x0300-0x033F**: Tile definition buffer (64 bytes, grayscale values)
- **0xE000-0xFFFF**: Framebuffer (8 KiB, 8 banks rolling on H-Blank)

### Video Output Coprocessor (VOC) Registers

The VOC manages display control through 16 bytes at **$00F0-$00FF**:

#### $00F0: Render Mode Control (CRHVPLIW)
- **C** (bit 7): Color depth (0=16-bit BGR, 1=32-bit RGBA)
- **R** (bit 6): Rolling toggle (0=block scanlines, 1=one scanline at a time)
- **H** (bit 5): H-Blank interrupt enable
- **V** (bit 4): V-Blank interrupt enable
- **P** (bit 3): Palette mode (0=16 colors, 1=256 colors)
- **L** (bit 2): Reserved
- **I** (bit 1): Reserved
- **W** (bit 0): Reset switch (clears PPU state except VRAM)

#### $00F1-$00F2: Palette Address
- 16-bit pointer to palette data (LSB, MSB)

#### $00F3-$00FA: Framebuffer Bank Order
- Controls manual ordering of 8 framebuffer banks
- Example: `01 23 45 67 89 AB CD EF`
- Only writable when auto-roll ($00FB) is disabled

#### $00FB: Framebuffer Auto-Roll Toggle
- **0**: Manual bank control (write to $00F3-$00FA enabled)
- **1**: Automatic bank rotation on H-Blank (default)

#### $00FC-$00FF: Tile Translucency Settings
Format: `0xTTTTPHMM`
- **T** (bits 15-4): Translucency values for previous 4 tiles (4 bits each)
- **P** (bit 3): Push settings now
- **H** (bit 2): PPU halt switch (sticky until reset)
- **M** (bits 1-0): Blending mode for previous 4 tiles
  - 00: Multiply
  - 01: Average
  - 10: Subtract
  - 11: Add

**Note**: Translucency is read-only in 32-bit RGBA mode

## PPU Interrupts

Set R59 (V-Blank) or R60 (H-Blank) to handler address (non-zero enables):
- Pushes current execution pointer to stack
- Jumps to handler address
- Use RET to return from handler

```asm
TARREG 2, LSB, SP
TARREG 3, MSB, SP
SETBYTE 2, 0xFF
SETBYTE 3, 0xFF      ; SP = 0xFFFF

TARREG 2, LSB, R59
TARREG 3, MSB, R59
SETBYTE 2, vblank_handler
SETBYTE 3, vblank_handler
```

## PPU Instruction Set

### Basic (0x0-0xD)
DEFCALL, MOVXP/NOP, SWAPREG, CLR, CMP, CLRF, JMZ/JMG, JNZ/JNG/JML, INC, DEC, ADD, SUB, MUL, INTDIV

### Preset E (0xE) - Target Registers
- **TARREG** T, Y, X: Point target register T (0-3) to register X byte Y (LSB/MSB)
- **SETBYTE** T, imm8: Set byte in target T to immediate
- **BUILD** T1, T2, X: Build register X from targets (T1=MSB, T2=LSB)
- **CPREG** X, Y: Copy register X to Y

**⚠️ WARNING**: Assembler shorthands (JMR label, PUSH, POP, JSR, RET, HLT) overwrite target registers 0 and 1. Use registers 2 and 3 for manual operations.

### Preset F (0xF) - Extended
SETPOS, SETTILE, SETDP, MOVDP, SETRENDMOD, PALETTE16, PALETTE256, JMR, MOV, SETREGBANK, CLRTILE, CLRPALETTE, TILEDRAW, (Reserved), CALL, GBLS

## PPU Assembly (ppuasm)

### Shorthands (use target registers 0-1)
```asm
JMR loop        ; Jump to label (expands to TARREG+SETBYTE+JMR)
JMZ/JNZ/JNG/JMG loop
PUSH R5         ; Push to stack
POP R5          ; Pop from stack
JSR func        ; Jump to subroutine
RET             ; Return (pop PC and jump)
HLT             ; Infinite loop
```

### Constants (use target registers 2-3 to avoid conflicts)
```asm
TARREG 2, LSB, R5
TARREG 3, MSB, R5
SETBYTE 2, 0x34
SETBYTE 3, 0x12  ; R5 = 0x1234
```

### Aliases
```asm
$0x0100 = IO_BASE
R10 = TEMP_REG
```

## Building

```bash
cd ZeroPoint/build_qt  # or mkdir build && cd build
cmake ..
make
```

### Executables
- **PPU**: bin/run_demo, bin/test_demo, bin/test_ppu, bin/make_test_rom, bin/test_dma
- **APU**: bin/test_apu, bin/run_apu_demo
- **CPU**: bin/test_cpu
- **Emulators**: bin/zeropoint_sdl, bin/zeropoint_qt

## ZPdevtools (`/Users/alexanderwhite/Documents/Code/ZPdevtools`)

### C Compiler (def88186cc)
Complete C89-compliant C-to-DEF88186 compiler with Flex/Bison frontend.

**Features**: int/char/void/short/long, unsigned/signed, const/volatile, static/extern, arrays, unions, enums, typedef, functions, recursion, if/else, while/for/do-while, switch/case, goto/labels, operators, hardware LOOP/LPEND optimization

```bash
cd ZPdevtools/c_compiler && make
./def88186cc input.c -o output.asm
./cpuasm output.asm output.bin
```

### Assemblers
- **ppuasm** (PPU): `gcc -o ppuasm ppuasm.c`
- **apuasm** (APU): `gcc -o apuasm apuasm.c`
- **cpuasm** (CPU): `gcc -o cpuasm cpuasm.c -Wall`

### ROM Builder
Combines CPU, PPU, and APU binaries into a single ROM file:
```bash
gcc -o rombuilder rombuilder.c
rombuilder -cpu main.bin -ppu gfx.bin -apu audio.bin -o game.rom
```

Creates:
- `game.rom` - Headerless ROM file
- `game.txt` - Metadata with CPU entry point and memory layout

**Features:**
- Configurable base addresses for each component
- Custom CPU entry point specification
- Automatic metadata generation
- Memory layout visualization
- Multi-file support via assembler `.include` directives

### Example Programs
- **PPU**: `ZPdevtools/examples/ppu/*.asm`
- **APU**: `ZPdevtools/examples/apu/*.asm`
- **CPU**: `ZPdevtools/examples/cpu/*.asm`

### Development Tools
Complete debugging and analysis suite:
```bash
cd ZPdevtools && make  # Build all tools
```

**Disassemblers:**
- `cpudisasm` - Disassemble DEF88186 binaries
- `ppudisasm` - Disassemble PPU microcode
- `apudisasm` - Disassemble APU programs

**Analyzers:**
- `rominspect` - Analyze ZPB ROM files (verify, extract components)
- `hexview` - Smart hex viewer with statistics

See `ZPdevtools/DEV-TOOLS.md` for complete guide.

## System Integration

### Memory Map (24-bit addressing)
- **$00-$7F**: ROM (8 MB)
- **$80-$9F**: Work RAM (2 MB)
- **$A0**: APU Window (64 KB direct APU memory access)
- **$B0**: PPU Window (64 KB direct PPU memory access)
- **$BE-$BF**: Shadow RAM (128 KB)
- **$D8**: I/O Registers (72 bytes)
- **$FF**: Boot ROM (64 KB)

### I/O Registers (Bank $D8)
- **$D80000-$D8000F**: PPU Control (start, status, PC, SP, DP, interrupts)
- **$D80010-$D8001F**: APU Control (status, PC, SP, RP, DP, DB)
- **$D80020-$D8002F**: DMA Control (placeholder)
- **$D80040-$D80047**: Display Status (V-Blank, H-Blank, scanline, pixel)

### Interrupt System
**Vectors** (Bank $00):
- `$FFFA-FFFB`: NMI vector
- `$FFFC-FFFD`: RESET vector
- `$FFFE-FFFF`: IRQ vector

**Usage:**
```asm
; Set up IRQ vector
.org $00FFFE
    .word irq_handler

irq_handler:
    REP #$30
    PHA
    PHX
    PHY
    ; Handle V-Blank
    JSR update_graphics
    PLY
    PLX
    PLA
    RTI
```

**C++ System Class:**
```cpp
#include "system.h"

ZeroPoint::System system;
system.loadROM("game.zpb");
system.setVBlankIRQEnabled(true);
system.run(1000000);  // Run 1M cycles
```

## Documentation References

### PPU
- `docs/display.md`, `docs/ppu/ucode.txt`
- `ZPdevtools/docs/ppu/ucode.txt`, `preset-e-and-shorthands.txt`

### APU
- `ZPdevtools/docs/apu/`: README.txt, overview.txt, instruction-set.txt, memory-map.txt, registers.txt, sst.txt, mmp.txt, programming-guide.txt

### DEF88186 CPU
- `ZPdevtools/docs/cpu/`: README.txt, overview.txt, instruction-set.txt, addressing-modes.txt, flags.txt, programming-guide.txt, encoding.txt, interrupts.txt, memory-map.txt, conventions.txt, timing.txt, comparison.txt

## Known Issues

1. MOVDP/MOV are 16-bit word-aligned
2. HLT is 6-instruction infinite loop, not true halt

## Status

### Complete
- ✅ DEF88186 CPU: All 256 opcodes implemented (1560 lines)
- ✅ APU: Full implementation with stack ops and functions
- ✅ **MMP Audio**: 16 stereo channels with pitch/volume control, SDL output
- ✅ PPU: Core instruction set and rendering (loops fixed!)
- ✅ Tile system: 4bpp/8bpp modes with palette lookups and translucency
- ✅ Palette system: 16-color and 256-color palettes with load operations
- ✅ VOC: Video Output Coprocessor with 16 control registers
- ✅ C compiler: Full toolchain (Flex/Bison/AST/Codegen)
- ✅ Assemblers: ppuasm, apuasm, cpuasm (with `.include` support)
- ✅ ROM Builder: rombuilder (combines CPU/PPU/APU into single ROM)
- ✅ **Memory Mapping System**: 24-bit address space with ROM/RAM/PPU/APU/I/O regions
- ✅ **I/O Registers**: Bank $D8 with PPU, APU, DMA, and Display status blocks
- ✅ **System Integration**: Unified System class with synchronized execution
- ✅ **Interrupt Routing**: V-Blank/H-Blank IRQ/NMI support with proper 65816 sequences
- ✅ **Development Tools**: 5 disassemblers/analyzers (cpudisasm, ppudisasm, apudisasm, rominspect, hexview)
- ✅ **DMA Controller**: Full implementation with 4 transfer modes, 16 channels, interrupt handling
  - DataCopy (3 cyc/byte), ConstCopy (1 cyc/byte), RepeatTransfer (3 cyc/byte), ConstRepeat (2 cyc/byte)
  - Max 2 concurrent channels, automatic queueing
  - I/O registers at $D80020-$D8002F with status monitoring
  - Integrated with CPU memory system and interrupt handling
  - Comprehensive test suite (7 tests, all passing)

### In Progress
- ⏳ Boot ROM development

### Planned
- 🔲 Debugger with register inspection
- 🔲 Hardware scrolling
- 🔲 Window scaling options

## File Structure

```
ZeroPoint/
├── include/        - Headers (display, ppu, apu, cpu, rom, dma, window)
├── src/            - Implementation + main.cpp (SDL)
├── qt/             - Qt frontend
├── tools/          - Test/demo runners
└── docs/           - Documentation

ZPdevtools/
├── c_compiler/     - def88186cc (Flex/Bison/AST/Codegen)
├── ppuasm.c        - PPU assembler
├── apuasm.c        - APU assembler
├── cpuasm.c        - CPU assembler (with .include support)
├── rombuilder.c    - ROM builder tool
├── examples/       - Example programs (ppu, apu, cpu)
└── docs/           - Complete documentation (ppu, apu, cpu)
```

## Credits

Built with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
