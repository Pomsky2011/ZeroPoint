# Display Timing

## Screen Specifications

### Vertical Timing
- **Total scanlines**: 261
- **Visible lines**: 256
- **VBlank period**: 5 scanlines
  - Scanline 0: Preline
  - Scanlines 1-256: Visible scanlines
  - Scanlines 257-260: VBlank (4 scanlines)

### Horizontal Timing
- **Total pixels per scanline**: 340
- **Visible pixels**: 256
- **HBlank pixels**: 84 (pixels 256-339)

## Display Resolution

- **Active display**: 256x256 pixels
- **Total frame**: 340x261 pixels

## Color Formats

The display supports two color modes, selectable via the PPU's SETRENDMOD instruction:

### 16-bit RGBA (RGBA16)

**Default mode**. Uses 16 bits per pixel.

Format: `BBBBBGGGGGRRRRR-`
- Bits 15-11: Blue (5 bits, 32 levels)
- Bits 10-6: Green (5 bits, 32 levels)
- Bits 5-1: Red (5 bits, 32 levels)
- Bit 0: Alpha (1 bit, 0=transparent, 1=opaque)

**Total colors**: 32,768 (32 × 32 × 32)

**Memory usage**: 8 KiB framebuffer (8 banks, 16 scanlines buffered)

**Advantages**:
- Compact memory footprint
- Authentic retro color depth
- Fast processing
- 16 scanlines buffered (excellent for raster effects)

### 32-bit RGBA (RGBA32)

**Extended mode**. Uses 32 bits per pixel.

Format: `RRRRRRRRGGGGGGGGBBBBBBBBAAAAAAAA`
- Bits 31-24: Red (8 bits, 256 levels)
- Bits 23-16: Green (8 bits, 256 levels)
- Bits 15-8: Blue (8 bits, 256 levels)
- Bits 7-0: Alpha (8 bits, 256 levels)

**Total colors**: 16,777,216 (256 × 256 × 256)

**Memory usage**: 8 KiB framebuffer (8 banks, 8 scanlines buffered)

**Advantages**:
- Full color depth
- Smooth gradients
- Full alpha blending support
- 8 scanlines buffered (good for effects)

## Switching Render Modes

The PPU can switch between render modes using:

```assembly
SETRENDMOD 0    ; Switch to 16-bit mode
SETRENDMOD 1    ; Switch to 32-bit mode
```

When switching modes:
- The framebuffer is automatically converted
- 16→32: Colors are expanded (5-bit → 8-bit per channel)
- 32→16: Colors are downsampled (8-bit → 5-bit per channel)
- Alpha channel is converted appropriately (1-bit ↔ 8-bit)

## Color Conversion

### 16-bit to 32-bit

Each 5-bit color component is expanded to 8-bit using:
```
value_8bit = (value_5bit << 3) | (value_5bit >> 2)
```

This ensures that:
- 0x00 (5-bit) → 0x00 (8-bit)
- 0x1F (5-bit) → 0xFF (8-bit)

Alpha: 0 → 0x00, 1 → 0xFF

### 32-bit to 16-bit

Each 8-bit color component is reduced to 5-bit using:
```
value_5bit = value_8bit >> 3
```

Alpha: value > 127 → 1, otherwise → 0

## Video Output Coprocessor (VOC)

The VOC provides hardware control over the display system through 16 memory-mapped registers at **$00F0-$00FF** in PPU memory.

### VOC Register Map

#### $00F0: Render Mode Control (CRHVPLIW)
Bit field controlling core display behavior:
- **Bit 7 (C)**: Color depth
  - 0 = 16-bit BGR (5-5-5-1)
  - 1 = 32-bit RGBA (8-8-8-8)
- **Bit 6 (R)**: Rolling buffer mode
  - 0 = Block mode (16/8 scanlines together based on color depth)
  - 1 = Single scanline mode (one scanline at a time)
- **Bit 5 (H)**: H-Blank interrupt enable
  - 0 = Disabled
  - 1 = Trigger interrupt at start of H-Blank
- **Bit 4 (V)**: V-Blank interrupt enable
  - 0 = Disabled
  - 1 = Trigger interrupt at start of V-Blank
- **Bit 3 (P)**: Palette mode
  - 0 = 16 colors
  - 1 = 256 colors
- **Bit 2 (L)**: Reserved (future use)
- **Bit 1 (I)**: Reserved (future use)
- **Bit 0 (W)**: Reset switch
  - Writing 1 resets PPU state (registers, execution pointer) but preserves VRAM
  - Automatically clears after reset completes

#### $00F1-$00F2: Palette Address
16-bit pointer to palette data in PPU memory:
- **$00F1**: Low byte (LSB)
- **$00F2**: High byte (MSB)

Used by palette load operations to determine where palette data is located.

#### $00F3-$00FA: Framebuffer Bank Order
8 bytes controlling the order of framebuffer banks:
- Each byte represents one bank (0-7)
- Default order: `00 01 02 03 04 05 06 07`
- Custom order example: `01 23 45 67 89 AB CD EF`
- **Only writable when auto-roll is disabled** ($00FB = 0)
- Allows advanced raster effects and custom scanline ordering

#### $00FB: Framebuffer Auto-Roll Toggle
Controls automatic bank rotation:
- **0**: Manual mode - banks don't rotate automatically, $00F3-$00FA can be written
- **1**: Auto mode (default) - banks rotate each H-Blank, $00F3-$00FA is read-only

#### $00FC-$00FF: Tile Translucency Settings
32-bit register controlling tile transparency and blending (4 bytes, little-endian):

Format: `0xTTTTPHMM` (stored as 4 bytes: MM, PH, TT, TT)
- **Bits 31-4 (T)**: Translucency values for 4 tiles (4 bits each, tiles N-3 to N)
  - Each 4-bit value: 0=fully transparent, 15=fully opaque
- **Bit 3 (P)**: Push flag - write 1 to apply settings immediately
- **Bit 2 (H)**: PPU Halt switch
  - Once set to 1, PPU halts until reset switch ($00F0 bit 0) is triggered
  - Sticky flag - doesn't auto-clear
- **Bits 1-0 (M)**: Blending mode for previous 4 tiles
  - 00: Multiply (darken)
  - 01: Average (50/50 blend)
  - 10: Subtract (color subtraction)
  - 11: Add (lighten/additive)

**Important**: In 32-bit RGBA mode, this register becomes **read-only** since alpha channel handles transparency directly.

### VOC Usage Examples

#### Switching Color Modes
```asm
; Switch to 32-bit RGBA mode
TARREG 2, LSB, DP
SETBYTE 2, 0xF0
MOV R1 (DP)           ; Read current mode
TARREG 2, LSB, R1
SETBYTE 2, 0x80       ; Set bit 7 (C)
MOVDP R1              ; Write back
```

#### Enabling V-Blank Interrupt
```asm
; Enable V-Blank interrupt (bit 4)
TARREG 2, LSB, DP
SETBYTE 2, 0xF0
MOV R1 (DP)
TARREG 2, LSB, R1
SETBYTE 2, 0x10       ; Set bit 4 (V)
MOVDP R1
```

#### Manual Bank Ordering
```asm
; Disable auto-roll
TARREG 2, LSB, DP
SETBYTE 2, 0xFB
CLR R1
MOVDP R1              ; $00FB = 0

; Set custom bank order (reverse)
TARREG 2, LSB, DP
SETBYTE 2, 0xF3
TARREG 2, LSB, R1
SETBYTE 2, 0x07
MOVDP R1              ; Bank 0 = physical bank 7
; ... continue for other banks
```

## Rolling Framebuffer Architecture

The display uses a **bank-based rolling buffer** instead of a full-frame buffer:

### Design
- **8 Banks**: Memory organized as 8 banks of 1 KiB each (8 KiB total)
- **Bank Layout**:
  - **RGBA16 mode**: 2 scanlines per bank (512 bytes each) = 16 scanlines total
  - **RGBA32 mode**: 1 scanline per bank (1024 bytes each) = 8 scanlines total
- **Memory Map**: Directly accessible at **0xE000-0xFFFF** in PPU memory

### Operation
During each H-Blank (end of scanline), the display rolls banks to make room:

**RGBA32 mode (32-bit color):**
- Rolls 1 bank per H-Blank (one scanline advances per H-Blank; `slotsPerBank == 1` in `src/display.cpp`)
- Each bank holds 1 scanline (1024 bytes)
- Buffer window: 8 scanlines ahead

**RGBA16 mode (16-bit color):**
- Rolls 1 bank per H-Blank (every 2 scanlines)
- Each bank holds 2 scanlines (512 bytes each)
- Buffer window: 16 scanlines ahead

### Window Addressing Across Frame Boundaries

`windowStart` and the framebuffer contents are **not** reset every frame.
Both `windowStart` and the local scanline `y` (0-255, as every caller sees
it) are converted into a single ever-growing absolute row space via
`frameRowOffset` (`Display::getBufferBank()`, `src/display.cpp`) before the
window-membership check `[windowStart, windowStart + window)` runs;
`frameRowOffset` is bumped by `VISIBLE_SCANLINES` exactly once per frame, at
the transition into the vblank tail (scanline 257) — deliberately *before*
the raster position wraps back to scanline 0 below it. `y % window` is
unaffected either way, since `frameRowOffset` is always a multiple of both
possible window sizes (8, 16).

This exists so that a `setPixel`/`setPixel32(x, y=0..)` call made during the
vblank tail (after the last visible scanline, before the next frame's
scanline 1) lands in the *next* frame's row space rather than the
just-finished one, and so the window keeps sliding continuously across the
frame boundary instead of snapping `windowStart` back to 0. A hard reset
there used to erase the same one-band lead every other row in the frame
gets, which meant scanline 0 of each new frame started with less write
lookahead than every other row — this is what caused the top rows of a
frame to be unreliable for time-critical writes (e.g. the boot ROM splash's
vblank-tail dispatch, see `docs/ppu.md`). `windowStart` and
`frameRowOffset` are `int64_t` so they never meaningfully overflow across a
long-running session; both are still reset to 0 on `setRenderMode()` (mode
switches still clear the framebuffer — see Limitations below).

### Memory Access
- **Read/Write**: Use MOV/MOVDP instructions to access framebuffer bytes
- **Address Range**: 0xE000-0xFFFF (always 8 KiB regardless of mode)
- **Bank Size**: 1024 bytes per bank
- **Byte Order**: Little-endian (matches native format)

### Benefits
- **Low VRAM Usage**: 8 KiB total instead of 256 KiB
- **Cache-Friendly**: Small buffer fits in CPU cache
- **Scanline-Based**: Natural fit for raster effects
- **Mode-Aware**: Automatically adjusts rolling rate based on pixel size

### Limitations
- **Write Window**:
  - RGBA16: Can only reliably write to next 16 scanlines
  - RGBA32: Can only reliably write to next 8 scanlines
- **No Random Access**: Cannot read pixels from arbitrary Y coordinates outside window
- **Data Loss**: Writes outside the buffer window are forgotten
- **Mode Switching**: Clears framebuffer when switching between 16-bit and 32-bit modes
