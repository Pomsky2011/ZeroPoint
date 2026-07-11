# I/O Registers Found in ZeroPoint Codebase

This document lists ALL I/O registers that currently exist in the ZeroPoint hardware implementations and need CPU memory-mapped addresses assigned.

> **Note**: This is a pre-implementation planning doc — the addresses were
> assigned differently than proposed here (bank $D8, not $70/$60), and
> controller input, timers, and interrupts (marked "NOT FOUND" below) were
> all subsequently implemented. **`docs/io-usage-guide.md` reflects the
> actual, current register map** — treat this file as historical context for
> *why* the registers exist, not where they live.

---

## 1. PPU (Picture Processing Unit)
**File**: `include/ppu.h`

### Control Methods (Need Write Access)
- `start()` - Start PPU execution
- `reset()` - Reset PPU state
- `setBlankStatus(vblank, hblank)` - Set blank flags (from Display)

### Status/State (Need Read Access)
- `getState()` → PPUState (WaitingForStart, Running, Halted)
- `getExecutionPointer()` → uint16_t (current PC)
- `getRegister(reg)` → uint16_t (read any of 64 registers)

### Special Registers (Direct CPU Access)
- **REG_PC (R62)** - Program Counter (16-bit)
- **REG_SP (R61)** - Stack Pointer (16-bit)
- **REG_DP (R63)** - Data Pointer (16-bit)
- **REG_VBLANK_INT (R59)** - V-Blank Interrupt Address (16-bit)
- **REG_HBLANK_INT (R60)** - H-Blank Interrupt Address (16-bit)

### Memory Access
- `readMemory(address)` → uint8_t (read PPU's 64 KiB memory)
- `writeMemory(address, value)` — now implemented (`include/ppu.h`), wired
  into `CPU::writeMapped`; the "no write method yet" note below is stale

### VOC Registers (Memory-Mapped at $00F0-$00FF in PPU memory)
**Struct**: `VOCRegisters` (ppu.h:182-189)

| Offset | Name | Type | Description |
|--------|------|------|-------------|
| $00F0 | renderModeControl | uint8_t | CRHVPLIW flags |
| $00F1 | paletteAddrLow | uint8_t | Palette address LSB |
| $00F2 | paletteAddrHigh | uint8_t | Palette address MSB |
| $00F3-$00FA | bankOrder[8] | uint8_t[8] | Framebuffer bank order |
| $00FB | autoRollToggle | uint8_t | Auto-roll toggle |
| $00FC-$00FF | translucency[4] | uint8_t[4] | Tile translucency (32-bit) |

**Bit Masks for $00F0**:
- Bit 7 (0x80): VOC_COLOR_DEPTH - 0=16-bit, 1=32-bit
- Bit 6 (0x40): VOC_ROLLING_MODE - 0=block, 1=single scanline
- Bit 5 (0x20): VOC_HBLANK_INT - H-blank interrupt enable
- Bit 4 (0x10): VOC_VBLANK_INT - V-blank interrupt enable
- Bit 3 (0x08): VOC_PALETTE_MODE - 0=16 color, 1=256 color
- Bit 0 (0x01): VOC_RESET - Reset switch

**Access Method**: These are already memory-mapped within PPU's 64 KiB space. CPU accesses them via bank **$B0** window (not $70 — that address was never adopted).

---

## 2. APU (Audio Processing Unit)
**File**: `include/apu.h`

### Control Methods (Need Write Access)
- `reset()` - Reset APU state
- `setPC(value)` - Set program counter
- `setRegister(reg, value)` - Set any register
- `setROMBank(bank)` - Set ROM bank (0-17)
- `setIOBank(bank)` - Set I/O bank
- `setHalted(value)` - Halt/resume APU

### Status/State (Need Read Access)
- `getPC()` → uint16_t - Get program counter
- `getRegister(reg)` → uint8_t - Get register value
- `getCycleCount()` → uint64_t - Cycle counter
- `getInstructionCount()` → uint64_t - Instruction counter
- `isHalted()` → bool - Halted state

### Memory Access (Already Implemented)
- `readByte(address)` → uint8_t - Read APU memory
- `writeByte(address, value)` - Write APU memory
- `readWord(address)` → uint16_t - Read 16-bit word
- `writeWord(address, value)` - Write 16-bit word
- `loadROM(data, size, address)` - Load program into APU memory

### Special Registers (CPU Access)
Only `pc`, `sp`, `romBank`, and `ioBank` actually have I/O register access
(`src/cpu.cpp` APU Control block). `rp`/`dp`/`db`/`bf` have no I/O path at
all — CPU code cannot read or write them.
- **pc** (uint16_t) - Program counter
- **sp** (uint16_t) - Stack pointer
- **romBank** (uint16_t) - Current ROM bank (0-17)
- **ioBank** (uint16_t) - Current I/O bank
- **rp/dp/db/bf** - exist as APU state but are not CPU-accessible via I/O

### DEF88186 Communication Interface
**Methods**: `readDEF88186Input()`, `writeDEF88186Input()`, `readDEF88186Output()`, `writeDEF88186Output()`

**Memory Map Locations** (apu.h:122-126):
- `DEF88186_INPUT_BASE` = $1000 (CPU → APU, 2 KB)
- `DEF88186_OUTPUT_BASE` = $1800 (APU → CPU, 2 KB)

### MMP (Music Mixing Processor) Control
**Methods**:
- `updateMMP()` - Update audio mixing
- `getMixedSampleLeft()` → int16_t - Get left channel output
- `getMixedSampleRight()` → int16_t - Get right channel output
- `resetMMP()` - Reset MMP state

**MMP Memory-Mapped Registers** (in APU memory):
- Base: `MMP_BASE` = $0000
- Size: `MMP_SIZE` = $0100 (256 bytes)

**16 Channels with pan** (each `MMPChannel` has):
- `pitch` (uint16_t) - Playback rate (0x1000 = 1.0×)
- `volume` (uint8_t) - 0 = silent, 255 = max
- `pan` (uint8_t) - 0 = full L, 128 = center, 255 = full R
- `reserved0` (uint8_t) - Reserved per-channel flags
- `stlAddress` (uint16_t) - STL entry pointer (0 = off)
- `samplePosition` (uint32_t) - Fixed-point position (upper bits = index, lower 12 = fraction)
- `active` (bool) - Channel active flag

**Register Map** ($0000-$00FF):
| Range       | Size  | Description                          |
|-------------|-------|--------------------------------------|
| $0000-$001F | 32 B  | Pitch (ch0-15, 2 bytes each)         |
| $0020-$002F | 16 B  | Volume (ch0-15, 1 byte each)         |
| $0030-$003F | 16 B  | Pan (ch0-15, 1 byte each)            |
| $0040-$004F | 16 B  | Reserved0 (ch0-15, per-channel)      |
| $0050-$006F | 32 B  | STL address (ch0-15, 2 bytes each)   |
| $0070-$007F | 16 B  | Reserved1 (global)                   |
| $0080-$00FF | 128 B | Reserved2                            |

**Access Method**: MMP registers are memory-mapped at $0000-$00FF in APU memory, accessible via bank **$A0** window (not $60 — that address was never adopted).

---

## 3. DMA Controller
**File**: `include/dma.h`

### Control Methods (Need Write Access)
- `queueDMA(config[9])` → bool - Queue a DMA transfer
- `triggerInterrupt()` - Pause all DMA
- `clearInterrupt()` - Resume DMA
- `reset()` - Reset all channels
- `setMemoryCallbacks(read, write)` - Set memory access functions

### Status (Need Read Access)
- `isChannelBusy(channel)` → bool - Check if channel is busy
- `getChannelState(channel)` → DMAState - Get channel state
- `getQueuedCount()` → size_t - Pending transfers
- `getActiveChannelCount()` → int - Active channels
- `isInterruptActive()` → bool - Interrupt status

### DMA Configuration (9 bytes)
**Struct**: `DMAConfig` (dma.h:19-40)

| Byte | Name | Description |
|------|------|-------------|
| 0 | mode | SS XX YYYY (size bits, mode, channel) |
| 1-3 | sourceAddr | 24-bit source address |
| 4-6 | targetAddr | 24-bit target address |
| 7 | sizeMultiplier | Size multiplier |
| 8 | interrupt | Interrupt/trigger byte |

**DMA Modes** (dma.h:11-16):
- 0b00: DataCopy (3 cycles/byte)
- 0b01: ConstCopy (1 cycle/byte)
- 0b10: RepeatTransfer (3 cycles/byte)
- 0b11: ConstRepeat (2 cycles/byte)

**DMA States** (dma.h:43-49):
- Idle
- Configuring (8 cycles)
- Initializing (mode-specific)
- Transferring
- Complete

**Channels**: 16 channels (0-15), max 2 active simultaneously

---

## 4. Display Controller
**File**: `include/display.h`

### Control Methods (Need Write Access)
- `setRenderMode(mode)` - Set RGBA16 or RGBA32
- `setPixel(x, y, color)` - Set 16-bit pixel
- `setPixel32(x, y, color)` - Set 32-bit pixel

### Status (Need Read Access)
- `getCurrentScanline()` → int (0-260)
- `getCurrentPixel()` → int (0-339)
- `isVisibleArea()` → bool
- `isVBlank()` → bool
- `isHBlank()` → bool
- `getRenderMode()` → RenderMode (RGBA16/RGBA32)
- `getCurrentColor()` → Color32
- `getCurrentColor16()` → Color16
- `getPixel(x, y)` → Color32

### Framebuffer Access
- `getFramebuffer16()` → Color16* (16-bit mode)
- `getFramebuffer32()` → Color32* (32-bit mode)
- `getFramebufferSize()` → size_t (8192 bytes)
- `getBufferBank(y, offsetInBank)` → int

**Note**: Display is controlled by PPU. CPU access likely through PPU control registers.

---

## 5. System (No dedicated registers found)
**Files checked**: cpu.h, window.h, rom.h

### CPU Control (CPU self-management, not I/O mapped)
- `reset()` - Reset CPU
- `step()` / `run(cycles)` - Execute instructions
- Register setters (A, X, Y, SP, D, PC, PB, DB, flags)

### Window (SDL frontend, not hardware)
- No hardware registers needed

### ROM (File loader, not hardware)
- No hardware registers needed

---

## 6. Controller/Input — now implemented
**Status**: `PlayerInput` (P1 direction/control/buttons) exists in
`include/cpu.h`, fed from SDL keyboard via `Window::getPlayerInput`
(`src/window.cpp`), exposed at `$D80030-$D80032`. A single controller only —
no multi-controller/strobe design as originally proposed.

---

## 7. Timers — now implemented
**Status**: 8 hardware timers (V/H/S/Q/E/T/A/R fixed-period flags) exist in
`System` (`include/system.h`/`src/system.cpp`), exposed at
`$D80050-$D80052`. No programmable reload/countdown timers, if that's what
was originally envisioned — just fixed periods.

---

## 8. Interrupts — now implemented as a dedicated controller
**Status**: `InterruptController` (`include/interrupt_controller.h`) latches
and routes 4 sources (V-Blank/H-Blank/Timer/DMA-reserved) to the CPU's
single IRQ line, exposed at `$D80058-$D80059`. Simpler than the originally
proposed 8-source design with vector registers — vectors remain fixed CPU
memory, not I/O.

---

## Summary of Registers Needing CPU I/O Mapping

### PPU Control Block (needs 16+ bytes)
- PPUCTRL (1 byte) - start/reset/halt flags
- PPUSTATUS (1 byte) - state flags
- PPUPC (2 bytes) - program counter read/write
- PPUSP (2 bytes) - stack pointer
- PPUDP (2 bytes) - data pointer
- PPUREG_ADDR (1 byte) - register select (0-63)
- PPUREG_DATA (2 bytes) - register read/write
- PPU_VBLANK_INT (2 bytes) - R59
- PPU_HBLANK_INT (2 bytes) - R60

### APU Control Block (needs 16+ bytes)
- APUCTRL (1 byte) - reset/halt flags
- APUSTATUS (1 byte) - status flags
- APUPC (2 bytes) - program counter read/write
- APUSP (2 bytes) - stack pointer
- APU_ROMBANK (1 byte) - ROM bank select
- APU_IOBANK (1 byte) - I/O bank select
- APUREG_ADDR (1 byte) - register select (0-255) — addresses exist at
  $D80018/$D80019 but the read handler always returns 0 and the write
  handler has no case for them at all; this is a non-functional stub, unlike
  PPUREG_ADDR/DATA above which genuinely works
- APUREG_DATA (1 byte) - register read/write (same stub caveat)

### DMA Control Block (needs 16+ bytes)
- DMA_CONFIG (9 bytes) - Write 9-byte config to queue transfer
- DMA_STATUS (1 byte) - Read queue count, active channels
- DMA_INTERRUPT (1 byte) - Trigger/clear interrupt
- DMA_CHANNEL_STATUS (1 byte) - Write channel# to select, read state

### Display Status (read-only, 8 bytes)
- DISP_SCANLINE (2 bytes) - Current scanline
- DISP_PIXEL (2 bytes) - Current pixel
- DISP_STATUS (1 byte) - VBlank, HBlank, visible flags
- DISP_MODE (1 byte) - Render mode (16/32-bit)

---

## Memory Windows Already Supported
- **Bank $B0**: PPU memory window (64 KB) — not $70, see note at top of this doc
- **Bank $A0**: APU memory window (64 KB) — not $60

These use `CPU::mapPPUWindow()` and `CPU::mapAPUWindow()` - already implemented!

---

## Next Steps
**YOU need to assign addresses** for:
1. PPU Control Block
2. APU Control Block
3. DMA Control Block
4. Display Status Block

Recommended base: Bank $00, offsets $C000-$DFFF (8 KB I/O space)
