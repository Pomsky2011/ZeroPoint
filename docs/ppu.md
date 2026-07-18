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

A Boot ROM exists at CPU bank $E0 (read-only, mapped at construction — see
CLAUDE.md's CPU Memory Map); hardware reset lands the CPU there instead of
bank $00 whenever one is loaded, and the default stub hands off to the
cartridge entry point. The `ZPbootROM` project's splash screen drives the PPU
directly during that handoff, including the tile-banking (`SETTILEBANK`) and
concurrent TILEDRAW-blitter features described below — see
`docs/zpb-format.md` for the signature-verification side of boot. Standalone
test/demo programs (`ZPdevtools/examples/ppu/*.asm`) that don't go through a
boot ROM still set up VBlank/HBlank interrupt registers (R59/R60) and start
execution directly via the PPUCTRL I/O register.

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
| 0xC | TILEDRAW | `1100 00000000` | Dispatch a draw of the current tile (SETTILE/SETTILEBANK) at the current position (SETPOS) — see "Concurrent TILEDRAW Blitter" below |
| 0xD | SETTILEBANK | `1101 XXXXXX --` | Select the active tile bank from register X (`registers[X] % MAX_TILE_BANKS`); same operand shape as SETDP |
| 0xE | CALL | `1110 XXXXXXXX` | Jump through call-table slot X (populated earlier by DEFCALL), pushing a return address |
| 0xF | GBLS | `1111 XXXXXX 00` | Get blank status into register X |

Note: there is no tile-definition instruction pair (no STRTDEFTILE/ENDDEFTILE)
— tile pixel data is written directly to the `$0300-$033F` memory-mapped
tile-definition buffer, 64 bytes per tile, targeting
`tileStorage[currentTileBank][currentTileId]` (`src/ppu.cpp`).

### Tile Banking (SETTILEBANK)

Tile storage is banked: `MAX_TILE_BANKS = 4` banks × `MAX_TILES = 256` tiles
each (1024 tiles total), `tileStorage[bank][tileId][byte]` in `include/ppu.h`.
`SETTILEBANK` selects which bank subsequent tile-definition writes and
`SETTILE`'s 8-bit tile ID index into — it's a bank-select register, not a
wider tile-ID field; `SETTILE`'s own encoding is unchanged. `TILEDRAW`
snapshots the active bank (along with tile ID, mode, position, blend mode,
and translucency) into its dispatched blit channel at issue time, not at
completion time, so a `SETTILEBANK` right after issuing a `TILEDRAW` doesn't
affect the tile already in flight. `CLRTILE` clears all banks; reset (VOC
reset or `PPU::reset()`) zeroes `currentTileBank` back to 0.

### Concurrent TILEDRAW Blitter

`TILEDRAW` doesn't draw synchronously by default. On issue, the PPU looks
for a free slot among `NUM_BLIT_CHANNELS` (8) background blit channels
(`include/ppu.h`/`src/ppu.cpp`):

- **A channel is free**: the draw's parameters (position, tile bank/ID/mode,
  blend mode, translucency) are snapshotted into the channel, the channel is
  marked busy with `cyclesRemaining` set to the full blit cost (below), and
  `TILEDRAW` itself only costs the issuing PPU `CYC_TILE_BASE` (8) cycles —
  it can move on to its next instruction, including dispatching another
  `TILEDRAW` to a different channel, immediately. Busy channels are advanced
  every cycle regardless of what the PPU itself is doing (including while
  stalled on something else), and the pixel write happens when a channel's
  `cyclesRemaining` reaches 0.
- **No channel is free**: `TILEDRAW` falls back to drawing synchronously
  inline, paying the full blit cost as its own instruction stall — this is
  the same behavior the PPU always had before concurrent dispatch existed,
  and is strictly a worse-case-only path.

**Blit cost**: `CYC_TILE_BASE + TILE_PIXELS * (blended ? CYC_TILE_PX_BLEND :
CYC_TILE_PX)` = `8 + 64*4 = 264` cycles for an opaque tile, `8 + 64*6 = 392`
cycles for a blended one (`src/ppu.cpp` `CYC_*` constants). 8 channels gives
enough headroom for the ZPbootROM splash screen's per-H-blank dispatch
pattern (up to 8 `TILEDRAW`s per H-blank call) to stay entirely on the cheap
async path — with fewer channels, the excess dispatches would hit the
264-cycle-plus synchronous fallback and could blow the 340-cycle H-blank
budget.

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

68.011355 MHz is the *nominal* clock the CPU/DMA/APU clocks and timer periods
are derived from (master/4, master/2, master/16), not a literal free-running
wall-clock rate: the Display and PPU are ticked together, cycle-for-cycle, by
`System::tickComponents()`, and `System::stepFrame()` runs exactly
`CYCLES_PER_FRAME` master cycles per call before the frontend sleeps to a
~60 Hz real-time target (`src/main.cpp`) — so in real time the PPU executes
roughly `CYCLES_PER_FRAME` cycles per 1/60 s, not 68 million cycles every
real second.

Within that budget, cycles per scanline/frame are exact and raster-derived,
not a clock/60 Hz approximation:

- **Cycles per scanline**: 340 (`TOTAL_PIXELS_PER_LINE`, `include/display.h`) — a hard ceiling on how much work (including `TILEDRAW` dispatches, see "Concurrent TILEDRAW Blitter" above) fits between two H-Blank edges
- **Cycles per frame**: 88,740 (340 × 261 `TOTAL_SCANLINES` = `System::CYCLES_PER_FRAME`)
- **Peak issue rate if never stalled**: ~68 million instructions/second (fewer in practice — branches/MUL/DIV/palette loads/`CLRTILE`/`CLRPALETTE` all cost more than 1 cycle, and `TILEDRAW`'s synchronous fallback costs far more — see the `CYC_*` constants in `src/ppu.cpp`)

An H-Blank ISR's real deadline is the full 340-cycle scanline (the interrupt
is edge-triggered once per scanline, not tied to the 84-pixel physical
blanking window alone) — see `ZPdevtools/docs/ppu/ucode.txt` section 8.3 for
why this specifically motivated the concurrent TILEDRAW blitter's 8-channel
design.

## Integration with Display

The PPU should be synchronized with the display controller:
- VBlank and HBlank status can be queried via GBLS
- Interrupt addresses set during initialization
- PPU can execute different code paths during blanking periods

## Implementation Notes

### Not Yet Implemented
- Boot ROM signature verification is still in progress (the boot ROM itself,
  including its splash animation, is implemented — see CLAUDE.md's "In
  Progress" section and `docs/zpb-format.md`)

Tile rendering (SETTILE/TILEDRAW/CLRTILE/SETTILEBANK), palette management
(PALETTE16/PALETTE256/CLRPALETTE), and the call stack (DEFCALL/CALL/RET) are
all implemented — the doc previously listed these as placeholders, which is
stale.

### Future Enhancements
- Sprite rendering
- Background layers
- Hardware scrolling
- Alpha blending (32-bit mode)
