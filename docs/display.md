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

**Memory usage**: 128 KiB per framebuffer (256×256×2 bytes)

**Advantages**:
- Compact memory footprint
- Authentic retro color depth
- Fast processing

### 32-bit RGBA (RGBA32)

**Extended mode**. Uses 32 bits per pixel.

Format: `RRRRRRRRGGGGGGGGBBBBBBBBAAAAAAAA`
- Bits 31-24: Red (8 bits, 256 levels)
- Bits 23-16: Green (8 bits, 256 levels)
- Bits 15-8: Blue (8 bits, 256 levels)
- Bits 7-0: Alpha (8 bits, 256 levels)

**Total colors**: 16,777,216 (256 × 256 × 256)

**Memory usage**: 256 KiB per framebuffer (256×256×4 bytes)

**Advantages**:
- Full color depth
- Smooth gradients
- Full alpha blending support

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
