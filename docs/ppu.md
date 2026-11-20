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
| 0x6 | PALETTE256 | `0110 00000000` | Load 256-color palette from (DP): 256 16-bit BBGR entries (512 bytes) in 16-bit render mode, 256 RRGGBBAA entries (1024 bytes) in 32-bit mode |
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
- **No channel is free**: `TILEDRAW` waits: it re-executes every cycle until a channel frees (a stalled blitter queue),
  then dispatches as above. Before CORE_REVISION 7 it drew synchronously right there, ahead of the older blits still in
  flight, which put a later layer *under* an earlier one.

**Order**: blits retire (draw) in issue order whatever they cost: a blit's completion is held strictly after the one issued
before it, and completions that fall due together run in that order. So program order is z-order: a cheap clipped sprite
tile issued after a full background tile lands on top of it. Throughput settles at the channel limit by itself, one blit per
~33 cycles (264 / 8) once all eight are busy; a program needs no pacing code of its own. Test: `bin/test_blit_order`.

**Blit cost**: `CYC_TILE_BASE + TILE_PIXELS * (blended ? CYC_TILE_PX_BLEND :
CYC_TILE_PX)` = `8 + 64*4 = 264` cycles for an opaque tile, `8 + 64*6 = 392`
cycles for a blended one (`src/ppu.cpp` `CYC_*` constants). 8 channels gives
enough headroom for the ZPbootROM splash screen's per-H-blank dispatch
pattern (up to 8 `TILEDRAW`s per H-blank call) to stay entirely on the cheap
async path — with fewer channels, the excess dispatches would hit the
264-cycle-plus synchronous fallback and could blow the 340-cycle H-blank
budget.

**Clipping**: the draw position's x (`$0200-$0201`) is signed 16-bit, so a tile can start off the left edge
(`0xFFFC` = -4 draws columns 4-7 at x = 0-3); pixels off either side of the 256-pixel scanline are dropped, and a
tile wholly off an edge draws nothing. Before CORE_REVISION 4 the x was read as unsigned, so any x >= `$8000` (every
negative one) dropped the whole tile. y is an absolute row in the rolling-buffer space, not signed. Test:
`bin/test_tiledraw_clip`.

### Tile attributes

Four per-blit bits in the VOC's `$00FC` (bits 2-5). They take effect when `$00FD` bit 4 is written as 1 (the attribute
push; bit 3 is the blend mode's, and the two are independent, so changing a flip never re-latches the translucency
bytes), and each `TILEDRAW` snapshots the latched value into its channel. One `MOVDP` of `attrs | $1000` to `$00FC`
sets and pushes in a single word write. Unlike the rest of `$00FC-$00FF` they are writable in either color depth; they
reset to 0 and change no timing.

Why a push and not "read live": a program image DMA'd over the VOC window leaves arbitrary bytes in `$00FC`. The boot
ROM splash's image put `$30` there, which with live sampling was a vflip plus a row clip with an inverted window and
blanked every tile it drew. A stray byte now does nothing until a push, and the splash's `$00FD` is 0. Test:
`tools/test_splash.sh` (the real boot ROM's frames, byte for byte).

| Bit | Name | Effect |
|-----|------|--------|
| 2 | T | transparent: a pixel whose palette entry has alpha 0 (after `$00FC-$00FF` translucency) leaves the framebuffer alone, like the line blitter. With blending on it is skipped, not blended |
| 3 | X | mirror left-right |
| 4 | Y | mirror top-bottom |
| 5 | C | row clip: write only the screen rows in `[$0204, $0206)` (16-bit words, sampled at issue like the position) |

Flips change which tile pixel lands where, never where the tile lands: x/y and clipping are as unflipped. Any attribute
bit set (or a blend mode) takes the per-pixel blit path instead of the whole-row fast path, with the same cycle cost.
Test: `bin/test_tile_attrs`.

**Row clip (C)**: only destination rows `y + ty` inside `[$0204, $0206)` are written (an empty or inverted window writes
nothing); the clip is independent of flips and blending. The blit is charged only for the rows it writes,
`8 + rows * 8 * px_cost` (so 264 cycles for all 8 rows, as before). What it is for: the framebuffer is a 16-row
rolling window and a band of tiles is only writable for 8 scanlines, so a sprite tile at any y that straddles two
bands is issued once per band with that band as the window. A tile can then never overwrite rows another, later-drawn
tile already owns, so z-order between sprites stays exact (re-issuing it unclipped redraws it over a sprite that drew
after it), and the double issue costs the same channel time as one full blit. Test: `bin/test_tile_rowclip`.

**Palettes**: 4bpp tiles read the 16-entry palette (`PALETTE16`), 8bpp tiles the 256-entry one (`PALETTE256`) in either
render mode; the tile mode's bit 0 (16BBGR vs 32RGBA) no longer selects anything. Before CORE_REVISION 5, 8bpp tiles in
16-bit mode used only the first 16 entries and showed indices 16+ as gray, and `PALETTE256` always read 1024 bytes of
32-bit entries. `$00F0` bit 3 only picks the table for the single-pixel port's index at `$0104`.

### Line Blitter

A second async blit unit, for writing a horizontal run of pixels at an arbitrary (signed) x on one row: four more
channels in the same blit-channel array (`NUM_LINE_CHANNELS`, after the eight tile channels), advanced and completed
by the same bookkeeping, so it costs the scheduler nothing. It exists because a tile blit rewrites all 64 pixels of
its 8x8 block with no transparency, which can't express "row 105 only, shifted 37 pixels".

**Launch port** (plain PPU memory the launch reads, so `MOVDP` works; a program stores the words, then the launch):

| Address | Meaning |
|---------|---------|
| `$0340` | source: PPU address of the first of `count` 4bpp palette indices (high nibble first, like a tile row) |
| `$0342` | destination x, signed 16-bit (a run may start off the left edge) |
| `$0344` | destination y |
| `$0346` | count, in pixels |
| `$0348` | write = launch (the low byte of the word `MOVDP` stores) |
| `$034A` | read: bits 0-3 = line channel busy; bit 7 = a launch was dropped (sticky). Any write clears bit 7 |

**Behaviour.** The port is snapshotted at launch into a free line channel; the pixels are written when the channel
completes, `CYC_LINE_BASE + count * CYC_LINE_PX` cycles later (8 + 1 per pixel, constants at the top of
`src/ppu.cpp`), reading the source and the live palette at completion like a tile blit. A palette entry with alpha 0
is transparent (the pixel is left alone), which is how a run gets ragged edges. Pixels off the scanline and rows
outside the rolling window are dropped by the same checks the tile blitter uses. If all four line channels are busy
the launch is **dropped** and the overrun bit is set (a write can't stall the instruction that made it). Both the
launch and the channel state are in `stateHash()`.

The boot ROM splash uses it for the ZEROPOINT logo: one launch per scanline from the hblank handler, one row ahead
(`ZPbootROM/ppu/code/gen_mosaic.py`, `emit_logo_section`). `test_line_blit` covers the unit.

### Tile-Row Queue (write-ahead)

`TILEDRAW` makes the program issue every tile itself (~33 cycles of PPU time each, and each must land inside its
rows' ~9-scanline window). The tile-row queue moves that to hardware: a program queues a whole *row of tilemap cells*
at a port, any time before the beam needs it, and the hardware reads the cells, holds each row until its screen rows
are safe to write, and dispatches the tiles onto the same eight blit channels. It exists for scrolling layers: a map
row is one launch (~40 PPU cycles) instead of 33+ tile issues. Test: `bin/test_row_queue`; the microcode built on it is
`ZPdevtools/ucode/layers` (`ZPdevtools/docs/ppu/layers.md`, test `bin/test_layers`).

**Cells** are read from PPU memory *when dispatched* (not at launch), 4 bytes each: word0 = tile ID in the low byte
(the high byte is ignored), word1 = `bank | attrs | $1000` with the bank in bits 0-1 and the tile attributes (T/H/V) in
bits 2-4, the same word `MOVDP`s to `$00FC` (so one register can feed `SETTILEBANK` and the attribute push). Keep a
queued row's cells stable until it has been dispatched.

**Port** (plain PPU memory the launch snapshots; words, little-endian; `MOVDP` to successive words, then `$0360`):

| Address | Meaning |
|---------|---------|
| `$0350` | SRC: PPU address of the first cell |
| `$0352` | COUNT: cells (1-128) |
| `$0354` | X: screen x of the first cell, signed (cell k is drawn at X + 8k) |
| `$0356` | Y: row of the tile tops within the frame, signed (a tile may start above row 0) |
| `$0358`, `$035A` | CLIP0, CLIP1: with FLAGS bit 0, only rows `[CLIP0, CLIP1)` are written (signed frame rows) |
| `$035C` | FLAGS: bit 0 CLIP, bit 1 FLIPX (cells walked in reverse and each mirrored), bit 2 FLIPY (each cell's V toggled), bit 3 WRAP (the cell index wraps inside a 64-cell, 256-byte-aligned row), bits 4-5 tile mode, bit 6 TRANSPARENT (every cell) |
| `$035E` | FRAME: the 16-bit frame number the rows belong to (see below) |
| `$0360` | write = launch (low byte); read = cells still pending |
| `$0362` | MODE: bit 0 = write-ahead mode (below) |
| `$0364` | STATUS: bit 0 = *late*, bit 1 = *overflow* (sticky); any write clears them |
| `$0366` | read: the hardware's own frame number (bumped at scanline 257) |

**Rows are absolute.** A row is `256 x frame + y`, and the launch names its frame at `$035E` (16-bit, placed relative to
the hardware's frame). A program counts its own frames, so a launch that blocks for queue space for a long time still
lands in the frame it was written for; it never has to guess where the beam is. A frame the beam has already passed is
simply late. Blend mode and translucency are snapshotted at launch like `TILEDRAW`'s.

**Dispatch.** One cell per 9 cycles at most (the cycle that reads it, then `TQ_DISPATCH_CYCLES` = 8 more; the cycle after a
launch that finds the queue empty), in queue order, onto a free tile channel, once the cell's last row is safe
to write: its slot must have been cleared of the row one window earlier (`Display::rowIsClear`). That is stricter than
"inside the window": in RGBA16 block mode slots clear in bank *pairs*, so an even row that has only just entered the
window shares a slot whose clear is still deferred, and a write there is wiped a scanline later. The queue is in order,
so a held entry holds everything behind it: queue rows in increasing order. Capacity is 128 *cells* across all queued
rows; a launch that would exceed it is dropped whole and sets *overflow* (poll `$0360` before launching).

**Retire order.** A queued blit only retires after the older in-flight blits it *overlaps* (rows and columns). Blits that
touch other pixels cannot change what each other draw, and strictly in-order retire cost ~25% of the channel capacity on
a fine scroll (a short clipped half stuck behind a long one). A `TILEDRAW` blit counts as overlapping everything and a
later `TILEDRAW` still retires after any queued blit, so mixing the two keeps `TILEDRAW`'s issue-order guarantee. A tile
costs `8 + rows x 8 x 4` channel-cycles (rows = the rows it writes), so a tile row split into two clipped halves costs one
tile plus 8. Eight channels retire one full tile per ~33 cycles: about 82 per 8-scanline band, whatever feeds them.

**Late.** A queued blit whose rows have left the window by the time it completes sets the sticky *late* bit instead of
tearing the frame silently. A program that must never lag keeps it clear (`test_layers` asserts that under saturation).

**Write-ahead mode** (`$0362` bit 0): the framebuffer window `$E000-$FFFF` and the queue's backing store `$C000-$DFFF` are
off the PPU's bus and read open bus (`$FF`), writes ignored. Blits still read the rolling buffer for blending. Off by
default, so existing programs are unaffected. The queue state is in `stateHash()` and reset clears it (`CORE_REVISION` 9).

**PPU time.** Blits and the queue run on the PPU's cycle count `clk` (absolute, counted only while the PPU is Running):
every blit has a `completeAt` on it, and the queue has the cycle it next looks at its head cell (`tqAt`: one past a
dispatch + 8, one past an empty cell, one past a launch that found the queue empty; a cell held on its rows waits for the
display to move, `tqHeld`). `evAt` is the cycle of the next event of either kind. On a cycle the blits that finish do so
first (`completeAt` order, channel order on a tie), then the queue looks, then the PPU's instruction (if it is at a
boundary) runs, so a dispatch sees the channels a completion just freed and an instruction sees both. `tick()`,
`tickMany()` and `runBatch()` all advance the same state through `advanceTo()`: a stretch with no event inside is one
compare, and nothing needs to be settled before an instruction touches a blit channel or the queue. Exact cycles are
checked by `bin/test_queue_timing`; stepped-vs-batched identity by `bin/fuzz_queue` and `bin/test_row_queue`. Emulation is
unchanged by this scheduling (it replaced a per-cycle one bit for bit: same `stateHash()`, same goldens).

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
`ZPdevtools/executables/ppuasm` to assemble real programs, and see
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
- **Peak issue rate if never stalled**: ~68 million instructions/second (fewer in practice — branches/MUL/DIV/palette loads/`CLRTILE`/`CLRPALETTE` all cost more than 1 cycle, and `TILEDRAW` stalls while all eight blit channels are busy — see the `CYC_*` constants in `src/ppu.cpp`)

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
- Nothing boot-ROM-related is outstanding on the PPU side: the boot ROM,
  its splash animation and signature verification are all implemented (see
  CLAUDE.md and `docs/zpb-format.md`)

Tile rendering (SETTILE/TILEDRAW/CLRTILE/SETTILEBANK), palette management
(PALETTE16/PALETTE256/CLRPALETTE), and the call stack (DEFCALL/CALL/RET) are
all implemented — the doc previously listed these as placeholders, which is
stale.

### Future Enhancements
- Alpha blending (32-bit mode)

Background layers, sprites and scrolling exist as microcode on the tile-row queue (see above and
`ZPdevtools/docs/ppu/layers.md`); there is no scroll register, the program feeds the queue.
