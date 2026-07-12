# PPU (Picture Processing Unit)

## Overview

The ZeroPoint PPU is a microcode-based graphics processor with its own instruction set, running at 68.011355 MHz (the system's master clock, master/1). It executes 1 instruction per cycle, though some instructions (branches, MUL/DIV, palette loads, TILEDRAW) cost extra cycles — see `PPU::tick()` and the `CYC_*` constants in `src/ppu.cpp`.

## Architecture

### Registers
- **64 registers**, all 16-bit
- **Register 63 (0x3F)**: DP (Data Pointer)
- **Register 62 (0x3E)**: PC (Program Counter) - used for jumps, not execution pointer

### Memory
- **64 KiB** (65536 bytes) shared with ROM
- Contains both microcode and data

### Execution
- **Clock**: 68.011355 MHz (master clock, executes every cycle)
- **Instruction time**: 1 base cycle; some instructions stall for extra cycles (see `src/ppu.cpp` `CYC_*` constants)
- **Instruction format**: 16 bits (4-bit opcode + 12-bit operand)

### System Clock Synchronization

The PPU runs at the master clock rate, synchronized via `System::tickComponents()` (`src/system.cpp`):

- **Master clock**: 68.011355 MHz (NTSC colorburst 3.579545 MHz x19)
- **PPU clock**: 68.011355 MHz (every master cycle, master/1)
- **DMA**: 34.005678 MHz (master/2)
- **CPU**: 17.002839 MHz (master/4)
- **APU**: 4.250710 MHz (master/16)

## Initialization Sequence

There is currently no boot ROM (bank $FF is unmapped — see CLAUDE.md "In Progress"), so there is no fixed hardware initialization sequence yet. Test/demo programs (`ZPdevtools/examples/ppu/*.asm`) set up VBlank/HBlank interrupt registers (R59/R60) and start execution directly via the PPUCTRL I/O register.

## Instruction Set

### Format

```
16-bit instruction:
[15:12] - 4-bit opcode
[11:0]  - 12-bit operand
```

### Basic Instructions

Verified against `src/ppu.cpp` `PPU::executeInstruction()`. There is no HALT
opcode — the PPU only stops via VOC register `$00FD` bit 2 (see "Halting"
below). Opcode 0xE is PRESET_E (see "Preset E Instructions" below), not HALT.

| Opcode | Mnemonic | Format | Description |
|--------|----------|--------|-------------|
| 0x0 | DEFCALL | `0000 XXXXXX YYYYYY` | Register call-table slot `registers[X] & 0xFF` = entry address `registers[Y]` |
| 0x1 | MOVXP / NOP | `0001 D 00000 YYYYYY` | D=0: copy execution-pointer+2 into register Y (MOVXP); D=1: no-op |
| 0x2 | SWAPREG | `0010 XXXXXX YYYYYY` | Swap registers X and Y |
| 0x3 | CLR | `0011 000000 YYYYYY` | Clear register Y |
| 0x4 | CMP | `0100 XXXXXX YYYYYY` | Compare X and Y (sets flags) |
| 0x5 | CLRF | `0101 00000000000` | Clear comparison flags |
| 0x6 | JMZ/JMG | `0110 B 00000000000` | Jump if zero (B=0) or greater (B=1) to (PC, register 62) |
| 0x7 | JNZ/JNG | `0111 B 00000000000` | Jump if not zero (B=0) or not greater (B=1) to (PC) |
| 0x8 | INC | `1000 0 00000 YYYYYY` | Increment register Y |
| 0x9 | DEC | `1001 000000 YYYYYY` | Decrement register Y |
| 0xA | ADD | `1010 XXXXXX YYYYYY` | X = X + Y |
| 0xB | SUB | `1011 XXXXXX YYYYYY` | X = X - Y |
| 0xC | MUL | `1100 XXXXXX YYYYYY` | X = X × Y |
| 0xD | INTDIV | `1101 XXXXXX YYYYYY` | X = X / Y (integer division; divide-by-zero is a no-op) |
| 0xE | PRESET_E | `1110 SS ...` | Immediate/byte-building instructions — TARREG/SETBYTE/BUILD/CPREG (see below) |
| 0xF | PRESET_F | `1111 SSSS WWWWWWWW` | Extended instructions (see below) |

DEFCALL/CALL note: DEFCALL doesn't take a literal address — it registers an
8-bit call-table slot pointing at a register-supplied entry address. Preset
F's `CALL` (0xE) later jumps through that same slot, not to a literal
address (see "Preset F Instructions" below).

### Comparison Flags

- **Zero**: Set when comparison result is zero
- **Greater**: Set when left operand > right operand

### Conditional Jumps

- **JMZ**: Jump if zero flag set
- **JMG**: Jump if greater flag set (also JNL - jump if not less)
- **JNZ**: Jump if zero flag clear
- **JNG**: Jump if greater flag clear (also JML - jump if less)

## Preset E Instructions (0xE)

Immediate/byte-building instructions, decoded via `PPU::executePresetE()`
(`src/ppu.cpp`). Sub-opcode is bits 11-10 of the operand. These back the
`TARREG`/`SETBYTE`/`JSR`/`PUSH`/`POP`/`HLT` assembler shorthands — see
CLAUDE.md's "⚠️ Shorthands use target registers 0-1" warning.

| Sub-op | Mnemonic | Format | Description |
|--------|----------|--------|-------------|
| 00 | TARREG | `00 TT Y 0 XXXXXX` | Point target register T (0-3) at register X, byte select Y (0=LSB,1=MSB) |
| 01 | SETBYTE | `01 TT XXXXXXXX` | Write immediate byte X into the byte of the register target T points at |
| 10 | BUILD | `10 T1T1 T2T2 XXXXXX` | Build register X from target T1's byte (high) and target T2's byte (low) |
| 11 | CPREG | `11 00 XXXX YYYY` | Copy register X to register Y, both 4-bit IDs extended via SETREGBANK |

## Preset F Instructions (0xF)

Extended graphics and control instructions.

| Sub-op | Mnemonic | Format | Description |
|--------|----------|--------|-------------|
| 0x0 | SETPOS | `0000 XXXX YYYY` | Set tile-draw position from two 4-bit reg IDs extended via SETREGBANK; writes the same `$0200-$0203` cells TILEDRAW reads |
| 0x1 | SETTILE | `0001 XXXXXX YY` | Set current tile ID from register X (6 bits), tile mode from Y (0-3: 16bpp/32bpp × 4bpp/8bpp) |
| 0x2 | SETDP | `0010 XXXXXX 00` | Set DP from register X |
| 0x3 | MOV(DP) | `0011 XXXXXX 00` | Move register X to address (DP) |
| 0x4 | SETRENDMOD | `0100 B 0000000` | Set render mode: B=0→16-bit RGBA, B=1→32-bit RGBA |
| 0x5 | PALETTE16 | `0101 00000000` | Load 16-color palette from (DP) (32/64 bytes) |
| 0x6 | PALETTE256 | `0110 00000000` | Load 256-color palette from (DP) (512/1024 bytes) |
| 0x7 | JMR | `0111 00000000` | Jump relative to (PC) |
| 0x8 | MOV | `1000 XXXXXX 00` | Move from address (DP) to register X |
| 0x9 | SETREGBANK | `1001 00 ZZ WW 00` | Set register banks: Z (bits 5-4) for X, W (bits 3-2) for Y — used by SETPOS and CPREG |
| 0xA | CLRTILE | `1010 00000000` | Clear all tile storage |
| 0xB | CLRPALETTE | `1011 00000000` | Reset both palettes to default grayscale |
| 0xC | TILEDRAW | `1100 00000000` | Draw the current tile (SETTILE) at the current position (SETPOS), with palette lookup, translucency, and blending |
| 0xD | (reserved) | `1101 00000000` | No-op |
| 0xE | CALL | `1110 XXXXXXXX` | Jump through call-table slot X (populated earlier by DEFCALL), pushing a return address |
| 0xF | GBLS | `1111 XXXXXX 00` | Get blank status into register X |

Note: there is no tile-definition instruction pair (no STRTDEFTILE/ENDDEFTILE)
— tile pixel data is written directly to the `$0300-$033F` memory-mapped
tile-definition buffer, 64 bytes per tile.

### GBLS - Get Blank Status

Returns blank status in specified register:
- **0**: VBlank active
- **1**: HBlank active
- **65535**: Neither (active rendering)

### Halting

There is no HALT opcode. The PPU only enters `PPUState::Halted` via VOC
register `$00FD` bit 2 (see the VOC register table). The `HLT` assembler
shorthand builds a 6-instruction infinite loop, not a real halt — see
CLAUDE.md's "⚠️ Shorthands use target registers 0-1" note and
`ZPdevtools/TODO.md`'s "HLT is not HALT" known issue.

### Register Banks

For SETPOS and CPREG's 4-bit register IDs:
- Bank select via SETREGBANK sub-op 0x9 (bits 5-4 for the X bank, bits 3-2 for the Y bank)
- Full register ID = (bank << 4) | 4-bit ID
- Allows access to all 64 registers with 4-bit encoding

## Example Programs

These are conceptual sketches in assembler mnemonics, not raw hex — use
`ZPdevtools/ppuasm` to assemble real programs, and see
`ZPdevtools/examples/ppu/*.asm` for working examples.

### Simple Counter

```asm
CLR R0          ; clear counter
CLR R1
loop:
    INC R0
    CMP R0, R1
    JNG loop     ; loop while R0 <= R1
    JMR done
done:
```

### Memory Copy

```asm
; Copy 16 bits from address in R5 to register 6
SETDP R5        ; DP = R5
MOV R6          ; R6 = value at (DP)
```

## Timing

At 68.011355 MHz with (at minimum) 1 cycle per instruction:

- **Instructions per second**: ~68 million (fewer if branches/MUL/DIV/TILEDRAW/palette loads dominate — those cost extra cycles, see `src/ppu.cpp` `CYC_*` constants)
- **Instructions per scanline** (at 261 scanlines/frame, ~60 Hz): ~4344
- **Instructions per frame**: ~1,133,523

This gives plenty of time for complex rendering operations.

## Integration with Display

The PPU should be synchronized with the display controller:
- VBlank and HBlank status can be queried via GBLS
- Interrupt addresses set during initialization
- PPU can execute different code paths during blanking periods

## Implementation Notes

### Not Yet Implemented
- Boot ROM animation (no boot ROM exists at all yet — see CLAUDE.md)

Tile rendering (SETTILE/TILEDRAW/CLRTILE), palette management
(PALETTE16/PALETTE256/CLRPALETTE), and the call stack (DEFCALL/CALL/RET) are
all implemented — the doc previously listed these as placeholders, which is
stale.

### Future Enhancements
- Sprite rendering
- Background layers
- Hardware scrolling
- Alpha blending (32-bit mode)
