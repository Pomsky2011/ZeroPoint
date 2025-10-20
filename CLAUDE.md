# ZeroPoint Fantasy Console Emulator

A fantasy console emulator featuring a tile-based PPU (Picture Processing Unit) with a custom microcode instruction set.

## Architecture

### Display System
- **Resolution**: 256×256 pixels
- **Color Modes**:
  - RGBA16: 5-5-5-1 bits (32K colors + 1-bit alpha), 16 scanlines buffered
  - RGBA32: 8-8-8-8 bits (16.7M colors + full alpha), 8 scanlines buffered
- **Rolling Buffer**: 8 banks × 1 KiB = 8 KiB total
  - 16-bit mode: 2 scanlines per bank
  - 32-bit mode: 1 scanline per bank
- **H-Blank Rolling**: Banks rotate each H-Blank (2 banks in 32-bit, 1 bank in 16-bit)
- **Memory-Mapped**: Framebuffer accessible at $E000-$FFFF in PPU memory
- **Backends**: SDL2 window and Qt widget renderer

### PPU (Picture Processing Unit)
- **Clock**: 64 MHz (64,000,000 Hz)
- **Execution**: 1 instruction per cycle
- **Registers**: 64 × 16-bit general purpose
  - R59: V-Blank Interrupt Address (jump here on V-Blank if non-zero)
  - R60: H-Blank Interrupt Address (jump here on H-Blank if non-zero)
  - R61 (SP): Stack Pointer (conventional)
  - R62 (PC): Program Counter for jumps
  - R63 (DP): Data Pointer for memory access
- **Memory**: 64 KiB unified (shared with ROM)
- **Instruction Set**: 15 basic opcodes + 16 Preset F extended instructions
- **Instruction Format**: 16-bit big-endian (4-bit opcode + 12-bit operand)

### Tile System
- **Tiles**: 8×8 pixels, 64 bytes each
- **Storage**: 256 tiles maximum
- **Definition**: Dynamic via STRTDEFTILE/ENDDEFTILE instructions
- **Placement**: Unbound - place at any pixel coordinate
- **Overlap**: Fully supported, tiles can draw over each other
- **No Layers**: No automatic sprite/background system - fully DIY

### APU (Audio Processing Unit)
- **Architecture**: 8-bit RISC processor
- **Clock**: 4.2 MHz (4,200,000 Hz)
- **CPI**: 4 cycles per instruction
- **Performance**: 1.048 MIPS (1,050,000 instructions/second)
- **Instruction Set**: 41 instructions (5-bit opcode + 11-bit operands)
- **Instruction Format**: 16-bit big-endian
- **Memory**: 64 KiB addressable + 448 KiB banked AROM
- **Registers**: Up to 256 8-bit general purpose + special registers (PC, RP, DP, DB, BF)
- **MMP**: Hardware mixer for 16 stereo channels (32 mono)
- **SST**: Sample storage system with looping and dynamic range clamping
- **Effects**: Per-channel reverb, echo, Gaussian interpolation
- **DEF88186**: Bidirectional CPU communication interface

## Memory Map

### Memory-Mapped I/O

#### Pixel Drawing (0x0100-0x010B)
- `0x0100-0x0101`: X position (16-bit little-endian)
- `0x0102-0x0103`: Y position (16-bit little-endian)
- `0x0104-0x0105`: Red component (16-bit, low byte used)
- `0x0106-0x0107`: Green component (16-bit, low byte used)
- `0x0108-0x0109`: Blue component (16-bit, low byte used)
- `0x010A-0x010B`: Alpha component (16-bit, low byte used)
  - Writing to 0x010A triggers pixel draw

#### Tile Drawing (0x0200-0x0207)
- `0x0200-0x0201`: X position (16-bit)
- `0x0202-0x0203`: Y position (16-bit)
- `0x0204-0x0205`: Tile ID (16-bit, low byte used)
- `0x0206-0x0207`: Trigger (writing to 0x0206 draws tile)

#### Tile Definition Buffer (0x0300-0x033F)
- 64-byte region for writing tile data
- Active between STRTDEFTILE and ENDDEFTILE
- Each byte represents one pixel's grayscale value

#### Framebuffer (0xE000-0xFFFF)
- **8 Banks × 1 KiB**: Organized as 8 memory banks of 1 KiB each (8 KiB total)
- **RGBA16 mode**: 16 scanlines (2 scanlines per bank, 512 bytes each)
- **RGBA32 mode**: 8 scanlines (1 scanline per bank, 1024 bytes each)
- **Direct Access**: Can read/write framebuffer bytes directly via MOV/MOVDP
- **H-Blank Rolling**: Each H-Blank, banks roll to make room for new scanlines:
  - 32-bit mode: Roll 2 banks per H-Blank
  - 16-bit mode: Roll 1 bank per H-Blank (every 2 scanlines)
- **Write Anywhere**: setPixel can write to any Y coordinate, but data outside the buffer window is forgotten

## Interrupts

The PPU supports V-Blank and H-Blank interrupts via special registers:

### Interrupt Registers
- **R59 (V-Blank)**: Set to the address of your V-Blank interrupt handler
- **R60 (H-Blank)**: Set to the address of your H-Blank interrupt handler

### Behavior
- If R59 is non-zero during V-Blank, the PPU:
  1. Pushes the current execution pointer onto the stack (using R61/SP)
  2. Jumps to the address in R59
- If R60 is non-zero during H-Blank, the PPU:
  1. Pushes the current execution pointer onto the stack (using R61/SP)
  2. Jumps to the address in R60
- Set to 0 to disable the corresponding interrupt
- Interrupts check once per cycle before instruction execution
- **Use RET to return from interrupt handlers**

### Example
```asm
; Initialize stack pointer (grows downward)
TARREG 2, LSB, SP
TARREG 3, MSB, SP
SETBYTE 2, 0xFF
SETBYTE 3, 0xFF      ; SP = 0xFFFF (top of memory)

; Set up V-Blank handler
TARREG 2, LSB, R59
TARREG 3, MSB, R59
SETBYTE 2, vblank_handler
SETBYTE 3, vblank_handler

; Main loop
main:
    ; Your game logic here
    JMR main

vblank_handler:
    ; Update graphics during V-Blank
    ; ... your code ...

    ; Return from interrupt (pops return address from stack)
    RET
```

## PPU Instruction Set

### Basic Instructions (Opcodes 0x0-0xD)

| Opcode | Mnemonic | Operands | Description |
|--------|----------|----------|-------------|
| 0x0 | DEFCALL | X, Y | Define callable: address in RX, ID in RY.LSB |
| 0x1 | ENDDEFCALL | X | End call definition for ID in RX.LSB |
| 0x2 | SWAPREG | X, Y | Swap registers X and Y |
| 0x3 | CLR | X | Clear register X (set to 0) |
| 0x4 | CMP | X, Y | Compare X and Y, set flags |
| 0x5 | CLRF | - | Clear comparison flags |
| 0x6 | JMZ/JMG | - | Jump to (PC) if zero/greater |
| 0x7 | JNZ/JNG/JML | - | Jump to (PC) if not zero/not greater |
| 0x8 | INC | X | Increment register X |
| 0x9 | DEC | X | Decrement register X |
| 0xA | ADD | X, Y | X = X + Y |
| 0xB | SUB | X, Y | X = X - Y |
| 0xC | MUL | X, Y | X = X * Y |
| 0xD | INTDIV | X, Y | X = X / Y (integer division) |

### Preset E Instructions (Opcode 0xE)

**⚠️ WARNING**: If you use assembler shorthands (JMR label, PUSH, POP, JSR, RET, HLT), **target registers 0 and 1 will be overwritten**. You can use them manually, but they'll get corrupted the moment you use any shorthand. Stick to target registers 2 and 3 for reliable code.

| Subop | Mnemonic | Operands | Description |
|-------|----------|----------|-------------|
| 0x0 | TARREG | T, Y, X | Set target register T (0-3) to point to register X, byte Y (LSB/MSB) |
| 0x1 | SETBYTE | T, imm8 | Set byte in target register T to 8-bit immediate value |
| 0x2 | BUILD | T1, T2, X | Build register X from target registers (T1=MSB, T2=LSB) |
| 0x3 | CPREG | X, Y | Copy register X to register Y (uses register banks) |

**Target Registers**: 4 staging registers (0-3) that can point to any of the 64 main registers and specify which byte (LSB or MSB) to operate on. Used for building 16-bit constants.

**Example - Load 0x1234 into R5:**
```asm
TARREG 0, LSB, R5    ; Target 0 → R5's low byte
TARREG 1, MSB, R5    ; Target 1 → R5's high byte
SETBYTE 0, 0x34      ; R5[7:0] = 0x34
SETBYTE 1, 0x12      ; R5[15:8] = 0x12
; R5 now contains 0x1234
```

### Preset F Instructions (Opcode 0xF)

| Subop | Mnemonic | Operands | Description |
|-------|----------|----------|-------------|
| 0x0 | SETPOS | X, Y | Set position (4-bit regs from banks) |
| 0x1 | SETTILE | X, mode | Set tile from register X, in mode (0-3) |
| 0x2 | SETDP | X | Set DP = register X |
| 0x3 | MOVDP | X | Write register X to (DP), (DP+1) |
| 0x4 | SETRENDMOD | mode | Set render mode (0=16-bit, 1=32-bit) |
| 0x5 | PALETTE16 | - | Load 16-color palette from (DP) |
| 0x6 | PALETTE256 | - | Load 256-color palette from (DP) |
| 0x7 | JMR | - | Jump relative to PC |
| 0x8 | MOV | X | Read (DP), (DP+1) into register X |
| 0x9 | SETREGBANK | X, Y | Set register banks for SETPOS/CPREG |
| 0xA | CLRTILE | - | Clear all tile storage |
| 0xB | CLRPALETTE | - | Clear palette |
| 0xC | TILEDRAW | - | Draw tile at position (from 0x0200) |
| 0xD | (Reserved) | - | Reserved for future use |
| 0xE | CALL | addr | Call function (not fully implemented) |
| 0xF | GBLS | X | Get blank status into register X |

**Tile Modes (for SETTILE):**
- 0: 16BBGR 4bpp (16 colors, 4 bits per pixel)
- 1: 32RGBA 4bpp (16 colors, 4 bits per pixel, full alpha)
- 2: 16BBGR 8bpp (256 colors, 8 bits per pixel)
- 3: 32RGBA 8bpp (256 colors, 8 bits per pixel, full alpha)

## Assembly Language (zpasm)

### Register Syntax
- `R0` through `R63`: General purpose registers
- `PC`: Program counter (R62)
- `DP`: Data pointer (R63)
- `SP`: Stack pointer (R61)

### Immediate Values
- Decimal: `42`, `255`
- Hexadecimal: `0xFF`, `0x100`
- Binary: `0b11111111`

### Labels
```asm
loop:
    INC R0
    CMP R0, R1
    JNG        ; Jumps to (PC), must set PC to loop address first
```

### Comments
```asm
; This is a comment
CLR R0    ; Clear R0
```

### Assembler Shorthands

**⚠️ IMPORTANT**: Shorthands **overwrite target registers 0 and 1**. You can use them manually, but they'll get stomped on whenever you use a shorthand. For reliable code, stick to target registers 2 and 3.

#### Label-Based Jumps
```asm
; Instead of manually loading PC:
JMR loop        ; Automatically expands to TARREG + SETBYTE + JMR
JMZ loop        ; Jump if zero to loop
JNZ loop        ; Jump if not zero
JNG loop        ; Jump if not greater (less or equal)
JMG loop        ; Jump if greater
```

**Expansion:**
```asm
JMR loop  →  TARREG 0, LSB, PC
             TARREG 1, MSB, PC
             SETBYTE 0, loop        ; Assembler splits address automatically
             SETBYTE 1, loop
             JMR
```

#### Stack Operations
```asm
PUSH R5         ; Push register onto stack
POP R5          ; Pop from stack into register
JSR function    ; Jump to subroutine (push PC, then jump)
RET             ; Return from subroutine (pop PC, then jump)
```

**Expansion:**
```asm
PUSH R5  →  SETDP SP
            INC SP
            INC SP
            MOVDP R5

POP R5   →  SETDP SP
            DEC SP
            DEC SP
            MOV R5

RET      →  POP PC
            JMR
```

#### HLT Shorthand
```asm
HLT             ; Halt execution (infinite loop)
```

**Expansion:**
```asm
HLT  →  @halt_0:
        TARREG 0, LSB, PC
        TARREG 1, MSB, PC
        SETBYTE 0, @halt_0
        SETBYTE 1, @halt_0
        JMR
```

#### Variable Aliases
```asm
; Define aliases for constants or registers
$0x0100 = IO_BASE
R10 = TEMP_REG

; Then use them:
SETBYTE 0, IO_BASE    ; Assembler replaces with 0x0100
CLR TEMP_REG          ; Assembler replaces with R10
```

**Syntax:** `LEFT = RIGHT` means "replace all instances of RIGHT with LEFT"

### Limitations
- **MOVDP/MOV are 16-bit**: Writes/reads 2 bytes at a time (little-endian)
- **CALL instruction**: Not fully implemented yet
- **Target register reservation**: If using shorthands, target registers 0 and 1 are reserved

### Building Constants

**New Method (Preset E):**
```asm
; Load 0x1234 into R5 (4 instructions)
TARREG 0, LSB, R5
TARREG 1, MSB, R5
SETBYTE 0, 0x34
SETBYTE 1, 0x12

; Load 256 into R7 (4 instructions)
TARREG 0, LSB, R7
TARREG 1, MSB, R7
SETBYTE 0, 0x00
SETBYTE 1, 0x01

; Using target register 2 to preserve 0 and 1 for shorthands
TARREG 2, LSB, R8
TARREG 3, MSB, R8
SETBYTE 2, 0xFF
SETBYTE 3, 0x00  ; R8 = 0x00FF = 255
```

**Old Method (Still works, but verbose):**
```asm
; Build 16 using multiplication (5 instructions)
CLR R14
INC R14
INC R14       ; R14 = 2
MUL R14, R14  ; R14 = 4
MUL R14, R14  ; R14 = 16

; Build 256 (7 instructions)
CLR R7
ADD R7, R14
MUL R7, R14   ; R7 = 256
```

**⚠️ Best Practice:** Use target registers 2 and 3 for manual constant loading to avoid having your values corrupted by shorthands.

## Creating Demos

### Pixel Drawing Example (New Method)
```asm
; Define aliases for clarity
$0x0100 = IO_PIXEL_BASE
R20 = X_POS
R21 = Y_POS
R22 = RED
R23 = GREEN
R24 = BLUE
R25 = ALPHA

init:
    SETRENDMOD 1    ; Enable 32-bit color mode

    ; Load DP = 0x0100 using target registers 2 and 3
    ; (Preserving 0 and 1 for HLT shorthand)
    TARREG 2, LSB, DP
    TARREG 3, MSB, DP
    SETBYTE 2, 0x00
    SETBYTE 3, 0x01  ; DP = 0x0100

    ; Load X = 10
    TARREG 2, LSB, X_POS
    SETBYTE 2, 10

    ; Load Y = 10
    TARREG 2, LSB, Y_POS
    SETBYTE 2, 10

    ; Load RED = 255
    TARREG 2, LSB, RED
    SETBYTE 2, 255

    ; Load GREEN = 128
    TARREG 2, LSB, GREEN
    SETBYTE 2, 128

    ; Load BLUE = 64
    TARREG 2, LSB, BLUE
    SETBYTE 2, 64

    ; Load ALPHA = 255
    TARREG 2, LSB, ALPHA
    SETBYTE 2, 255

    ; Write to memory-mapped I/O
    MOVDP X_POS     ; 0x0100-0x0101: X
    INC DP
    INC DP
    MOVDP Y_POS     ; 0x0102-0x0103: Y
    INC DP
    INC DP
    MOVDP RED       ; 0x0104-0x0105: R
    INC DP
    INC DP
    MOVDP GREEN     ; 0x0106-0x0107: G
    INC DP
    INC DP
    MOVDP BLUE      ; 0x0108-0x0109: B
    INC DP
    INC DP
    MOVDP ALPHA     ; 0x010A-0x010B: A (triggers draw)

    HLT             ; Uses shorthand (needs target registers 0 and 1)
```

### Tile Drawing Example (New Method)
```asm
$0x0200 = TILE_POS_BASE
$0x0300 = TILE_DATA_BASE
R20 = TILE_X
R21 = TILE_Y
R30 = TILE_ID

init:
    SETRENDMOD 1

    ; Load tile ID = 1
    TARREG 2, LSB, TILE_ID
    SETBYTE 2, 1

    ; Set current tile to ID 1, mode 3 (32RGBA 8bpp)
    SETTILE TILE_ID, 3

    ; Load DP = 0x0300 (tile data buffer)
    TARREG 2, LSB, DP
    TARREG 3, MSB, DP
    SETBYTE 2, 0x00
    SETBYTE 3, 0x03  ; DP = 0x0300

    ; Fill tile with grayscale gradient (64 bytes)
    TARREG 2, LSB, R10
    SETBYTE 2, 0     ; R10 = counter

fill_loop:
    MOVDP R10        ; Write pixel value
    INC DP
    INC DP
    INC R10

    ; Check if done (64 pixels)
    TARREG 2, LSB, R11
    SETBYTE 2, 64
    CMP R10, R11
    JNG fill_loop    ; Loop if R10 < 64

    ; Now draw the tile at position (64, 64)
    ; Load X = 64, Y = 64
    TARREG 2, LSB, TILE_X
    SETBYTE 2, 64
    TARREG 2, LSB, TILE_Y
    SETBYTE 2, 64

    ; Set DP to tile position region
    TARREG 2, LSB, DP
    TARREG 3, MSB, DP
    SETBYTE 2, 0x00
    SETBYTE 3, 0x02  ; DP = 0x0200

    ; Write position
    MOVDP TILE_X     ; 0x0200-0x0201: X
    INC DP
    INC DP
    MOVDP TILE_Y     ; 0x0202-0x0203: Y

    ; Draw the tile
    TILEDRAW

    HLT
```

## Building

### Dependencies
- CMake 3.16+
- C++17 compiler
- SDL2
- Qt5 or Qt6

### Build Commands
```bash
cd ZeroPoint
mkdir build
cd build
cmake ..
make
```

### Executables

#### Emulators
- `bin/zeropoint_sdl` - SDL frontend
- `bin/zeropoint_qt` - Qt frontend

#### PPU Tools
- `bin/run_demo <demo.bin>` - Run PPU demo with SDL window
- `bin/test_demo <demo.bin>` - Run PPU demo headless (testing)
- `bin/test_ppu` - PPU test
- `bin/make_test_rom` - ROM creation tool
- `bin/test_dma` - DMA controller test

#### APU Tools
- `bin/test_apu <program.bin> [max_cycles]` - Run APU program headless
- `bin/run_apu_demo <program.bin>` - Run APU program with audio output

## ZPdevtools (Assemblers)

Located in `/Users/alexanderwhite/Documents/Code/ZPdevtools`

### PPU Assembler (zpasm)

#### Building
```bash
gcc -o zpasm zpasm.c
```

#### Usage
```bash
./zpasm input.asm output.bin
```

#### PPU Example Programs
- `examples/ppu/simple_pixel_test.asm` - Draw single pixel
- `examples/ppu/tile_simple_demo.asm` - Draw 8×8 tile
- `examples/ppu/gradient_demo_expanded.asm` - Gradient pattern
- `examples/ppu/gradient_demo_grid.asm` - 8×8 grid of pixels

### APU Assembler (apuasm)

#### Building
```bash
gcc -o apuasm apuasm.c
```

#### Usage
```bash
./apuasm input.asm [output.bin]
```

If no output file is specified, uses input filename with `.bin` extension.

#### APU Example Programs
- `examples/apu/hello.asm` - Simple register operations (X=42, Y=100, R2=X+Y)
- `examples/apu/counter.asm` - Hardware loop counting from 0 to 10
- `examples/apu/tone_gen.asm` - Writes sample data to ARAM

#### Assemble and Run
```bash
./apuasm examples/apu/hello.asm
cd ../ZeroPoint/build
./bin/test_apu ../ZPdevtools/examples/apu/hello.bin 10000
```

## Known Issues & Limitations

1. **⚠️ CRITICAL BUG - Loops Broken**: Loop counters get stuck at value 1 - infinite loops occur even in simple increment/compare/jump patterns. Investigating zpasm label encoding or jump instruction implementation.
2. **No Immediate Values**: Every constant must be constructed step-by-step
3. **MOVDP Alignment**: Writes 16-bit values, must account for word alignment in I/O
4. **Small Tiles**: 8×8 tiles appear small on 256×256 screen (consider scaling window)
5. **No Palette System**: Palette instructions defined but not implemented
6. **Grayscale Tiles**: Current tile system treats each byte as grayscale (R=G=B)
7. **HLT is not HALT**: HLT shorthand expands to a 6-instruction infinite loop, not a real halt opcode

## Development Notes

### Recent Bugs Fixed (2025-10-19)
- **Interrupt stack push order**: Was writing to memory before decrementing SP, causing buffer overflow. Fixed to decrement SP first.
- **HLT detection**: Added automatic detection of HLT pattern (repeating PC sequences of 5-8 instructions) to run_demo and test_demo tools
- **MOVDP/MOV register encoding**: Had `| 0x04` and `| 0x08` offset causing registers to be decoded +1
- **Memory-mapped I/O alignment**: Had to use word-aligned (2-byte) regions for MOVDP
- **Color conversion**: Accurate 5→8 bit expansion using `(x << 3) | (x >> 2)`
- **Tile buffer**: Needed separate definition buffer vs. storage to prevent corruption

### Performance
- **PPU**: 64M instructions/second theoretical
- **Frame budget**: ~1.1M instructions @ 60Hz
- **Pixel drawing**: ~20 instructions per pixel via memory-mapped I/O
- **Tile drawing**: More efficient than individual pixels for 8×8 regions

## Future Enhancements

### PPU
- [ ] Palette system implementation
- [ ] DMA-based tile copying
- [ ] Indexed color tile format (reduce from 64 to 64 palette indices)
- [ ] Hardware scrolling
- [ ] Sprite system (optional layer over DIY tiles)
- [ ] Window scaling options (2×, 4×, 8×)

### APU (Audio Processing Unit)
- [x] APU architecture documentation (8-bit RISC @ 4.2 MHz)
- [x] MMP (Music Mixing Processor) specification
- [x] SST (Sample Storage System) format
- [x] APU implementation (hardware emulation)
- [x] APU assembler (apuasm)
- [x] Audio output integration with SDL
- [ ] Full MMP audio mixing implementation
- [ ] Sample library and tools
- [ ] APU integration with main emulator

### Development Tools
- [ ] Debugger with register inspection
- [ ] Better assembler with macros and immediate value support
- [ ] Disassembler for both PPU and APU

## File Structure

```
ZeroPoint/
├── include/
│   ├── display.h       - Display framebuffer manager
│   ├── ppu.h          - PPU microcode processor
│   ├── rom.h          - ROM loading
│   ├── dma.h          - DMA controller
│   └── window.h       - SDL window wrapper
├── src/
│   ├── display.cpp
│   ├── ppu.cpp
│   ├── rom.cpp
│   ├── dma.cpp
│   ├── window.cpp
│   └── main.cpp       - SDL frontend
├── qt/
│   ├── main.cpp
│   ├── mainwindow.cpp
│   ├── emulatorwidget.cpp
│   └── configdialog.cpp
├── tools/
│   ├── run_demo.cpp   - SDL demo runner
│   ├── test_demo.cpp  - Headless demo tester
│   ├── test_ppu.cpp
│   ├── test_dma.cpp
│   └── make_test_rom.cpp
├── docs/
│   ├── display.md
│   └── ppu/
│       └── ucode.txt  - Complete instruction reference
└── CMakeLists.txt
```

## Documentation

### PPU Documentation
- `docs/display.md` - Display system architecture and color modes
- `docs/ppu/ucode.txt` - Complete PPU microcode reference (2000+ lines)
- `ZPdevtools/README.md` - PPU assembler documentation
- `ZPdevtools/docs/ppu/ucode.txt` - Assembly language reference
- `ZPdevtools/docs/ppu/preset-e-and-shorthands.txt` - Preset E and assembler shorthands

### APU Documentation (New!)
- `ZPdevtools/docs/apu/README.txt` - APU documentation index
- `ZPdevtools/docs/apu/overview.txt` - Architecture overview (8-bit RISC @ 4.2 MHz)
- `ZPdevtools/docs/apu/instruction-set.txt` - All 41 APU instructions
- `ZPdevtools/docs/apu/memory-map.txt` - Memory layout and banking
- `ZPdevtools/docs/apu/registers.txt` - Register reference (X, Y, PC, RP, DP, DB, BF)
- `ZPdevtools/docs/apu/sst.txt` - Sample Storage System format
- `ZPdevtools/docs/apu/mmp.txt` - Music Mixing Processor (16 stereo channels)
- `ZPdevtools/docs/apu/programming-guide.txt` - Complete programming examples

## License

See project LICENSE file.

## Credits

Built with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
