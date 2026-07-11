# I/O Register Mapping - ZeroPoint System

> **SUPERSEDED / ASPIRATIONAL**: This is a pre-implementation proposal — it
> ends with "Next Steps: decide on exact addresses." None of the concrete
> addresses below were adopted (base bank $00 → actual is $D8; PPU/APU
> windows $70/$60 → actual is $B0/$A0; multi-controller JOY1-4 design,
> programmable-reload timers, 16-bit interrupt vector registers, and a
> debug/profiler register block were never built). For the real, current
> register map, see **`docs/io-usage-guide.md`**. Kept as historical design
> intent only.

This document lists all I/O slots that need memory-mapped addresses for hardware communication between the DEF88186 CPU and other system components.

## Overview

I/O registers allow the CPU to control and communicate with:
- **PPU** (Picture Processing Unit) - Graphics control
- **APU** (Audio Processing Unit) - Sound control
- **DMA** (Direct Memory Access) - Fast memory transfers
- **Controllers** - User input
- **Timers** - System timing
- **System Control** - Reset, interrupts, etc.

## Memory Map Recommendation

Based on the DEF88186 memory map documentation, I/O registers should be placed in:
- **Bank $00, offsets $C000-$DFFF** (8 KB I/O space)

## I/O Slots Requiring Mapping

### 1. PPU Control Registers
**Purpose**: Control PPU execution and graphics rendering

| Register Name | Size | Description |
|---------------|------|-------------|
| PPUCTRL | 1 byte | PPU control (start, reset, enable interrupts) |
| PPUSTATUS | 1 byte | PPU status (running, halted, current state) |
| PPUPC | 2 bytes | PPU program counter (read/write) |
| PPUSP | 2 bytes | PPU stack pointer |
| PPUDP | 2 bytes | PPU data pointer |
| PPU_INT_VBLANK | 2 bytes | V-Blank interrupt address (R59) |
| PPU_INT_HBLANK | 2 bytes | H-Blank interrupt address (R60) |

**Suggested base**: `$C000` (16 bytes)

### 2. PPU Memory Upload
**Purpose**: Upload microcode, tiles, palettes to PPU memory

| Register Name | Size | Description |
|---------------|------|-------------|
| PPUMEM_ADDR | 2 bytes | PPU memory address pointer |
| PPUMEM_DATA | 1 byte | Write to upload data to PPU memory |
| PPUMEM_DMA_SRC | 3 bytes | DMA source address (24-bit) |
| PPUMEM_DMA_LEN | 2 bytes | DMA transfer length |
| PPUMEM_DMA_START | 1 byte | Trigger DMA transfer to PPU |

**Suggested base**: `$C010` (16 bytes)

### 3. APU Control Registers
**Purpose**: Control APU execution and audio playback

| Register Name | Size | Description |
|---------------|------|-------------|
| APUCTRL | 1 byte | APU control (start, reset, halt) |
| APUSTATUS | 1 byte | APU status (running, halted) |
| APUPC | 2 bytes | APU program counter |
| APUSP | 2 bytes | APU stack pointer |
| APUR0-R7 | 8 bytes | APU general purpose registers (optional direct access) |

**Suggested base**: `$C100` (16 bytes)

### 4. APU Memory Upload
**Purpose**: Upload audio programs and samples to APU memory

| Register Name | Size | Description |
|---------------|------|-------------|
| APUMEM_ADDR | 2 bytes | APU memory address pointer |
| APUMEM_DATA | 1 byte | Write to upload data to APU memory |
| APUMEM_DMA_SRC | 3 bytes | DMA source address (24-bit) |
| APUMEM_DMA_LEN | 2 bytes | DMA transfer length |
| APUMEM_DMA_START | 1 byte | Trigger DMA transfer to APU |
| APU_BANK | 1 byte | APU ROM bank select (for 448 KB AROM) |

**Suggested base**: `$C110` (16 bytes)

### 5. MMP Audio Control
**Purpose**: Control Music Mixing Processor within APU

| Register Name | Size | Description |
|---------------|------|-------------|
| MMP_MASTER_VOL | 1 byte | Master volume (0-255) |
| MMP_CHANNEL_EN | 4 bytes | Channel enable bits (32 channels) |
| MMP_SAMPLE_RATE | 2 bytes | Sample rate control (if adjustable) |
| MMP_STATUS | 1 byte | MMP status (active channels, buffer status) |

**Suggested base**: `$C120` (16 bytes)

**Note**: Individual channel control (pitch, volume, sample address) is memory-mapped within APU memory ($0000-$00FF), accessed via APU window at bank $60.

### 6. Controller/Input Registers
**Purpose**: Read user input from controllers

| Register Name | Size | Description |
|---------------|------|-------------|
| JOY1 | 2 bytes | Controller 1 button state |
| JOY2 | 2 bytes | Controller 2 button state |
| JOY3 | 2 bytes | Controller 3 button state (optional) |
| JOY4 | 2 bytes | Controller 4 button state (optional) |
| JOYCTRL | 1 byte | Controller strobe/latch control |

**Suggested base**: `$C200` (16 bytes)

### 7. Timer Registers
**Purpose**: System timing and delays

| Register Name | Size | Description |
|---------------|------|-------------|
| TIMER0_COUNT | 2 bytes | Timer 0 counter |
| TIMER0_RELOAD | 2 bytes | Timer 0 reload value |
| TIMER0_CTRL | 1 byte | Timer 0 control (enable, mode) |
| TIMER1_COUNT | 2 bytes | Timer 1 counter |
| TIMER1_RELOAD | 2 bytes | Timer 1 reload value |
| TIMER1_CTRL | 1 byte | Timer 1 control |

**Suggested base**: `$C300` (16 bytes)

### 8. DMA Controller
**Purpose**: Fast memory-to-memory transfers

| Register Name | Size | Description |
|---------------|------|-------------|
| DMA_MODE | 1 byte | DMA mode (transfer, fill, copy) |
| DMA_SRC_ADDR | 3 bytes | Source address (24-bit) |
| DMA_DST_ADDR | 3 bytes | Destination address (24-bit) |
| DMA_LENGTH | 2 bytes | Transfer length in bytes |
| DMA_CTRL | 1 byte | DMA control (start, stop, pause) |
| DMA_STATUS | 1 byte | DMA status (busy, done, error) |

**Suggested base**: `$C400` (16 bytes)

### 9. Interrupt Control
**Purpose**: Manage system interrupts

| Register Name | Size | Description |
|---------------|------|-------------|
| INT_ENABLE | 2 bytes | Interrupt enable flags |
| INT_STATUS | 2 bytes | Interrupt status/pending flags |
| INT_ACKNOWLEDGE | 2 bytes | Write to acknowledge interrupts |
| IRQ_VECTOR | 2 bytes | IRQ vector address |
| NMI_VECTOR | 2 bytes | NMI vector address |

**Suggested base**: `$C500` (16 bytes)

Interrupt types:
- Bit 0: V-Blank (PPU)
- Bit 1: H-Blank (PPU)
- Bit 2: Timer 0
- Bit 3: Timer 1
- Bit 4: Controller
- Bit 5: APU (audio complete)
- Bit 6: DMA complete
- Bit 7: External/User

### 10. System Control
**Purpose**: System-wide control and status

| Register Name | Size | Description |
|---------------|------|-------------|
| SYS_RESET | 1 byte | Write to reset components (bitmask) |
| SYS_STATUS | 1 byte | System status (power, clock speed) |
| SYS_VERSION | 1 byte | Hardware version ID |
| SYS_CONFIG | 1 byte | System configuration flags |

**Suggested base**: `$C600` (16 bytes)

### 11. Debug/Development Registers (Optional)
**Purpose**: Debugging and development tools

| Register Name | Size | Description |
|---------------|------|-------------|
| DEBUG_BREAK | 1 byte | Trigger debugger breakpoint |
| DEBUG_LOG | 1 byte | Write to debug log output |
| DEBUG_WATCH | 3 bytes | Set memory watch address |
| PROFILER_START | 1 byte | Start profiler |
| PROFILER_STOP | 1 byte | Stop profiler |

**Suggested base**: `$C700` (16 bytes)

### 12. Reserved for Future Expansion
**Size**: `$C800-$DFFF` (6 KB)

## Memory Map Summary

```
Bank $00 I/O Registers ($C000-$DFFF):
├─ $C000-$C00F  (16 B)  - PPU Control Registers
├─ $C010-$C01F  (16 B)  - PPU Memory Upload
├─ $C100-$C10F  (16 B)  - APU Control Registers
├─ $C110-$C11F  (16 B)  - APU Memory Upload
├─ $C120-$C12F  (16 B)  - MMP Audio Control
├─ $C200-$C20F  (16 B)  - Controller/Input
├─ $C300-$C30F  (16 B)  - Timer Registers
├─ $C400-$C40F  (16 B)  - DMA Controller
├─ $C500-$C50F  (16 B)  - Interrupt Control
├─ $C600-$C60F  (16 B)  - System Control
├─ $C700-$C70F  (16 B)  - Debug Registers (optional)
└─ $C800-$DFFF  (6 KB)  - Reserved
```

## Implementation Notes

1. **Register Implementation**: Use `CPU::registerIORegion()` for each block
2. **Read/Write Callbacks**: Provide lambdas or function pointers for each region
3. **Atomicity**: Some multi-byte registers may need special handling
4. **Write-Only vs Read-Write**: Some registers are write-only (control), others are read-only (status)
5. **Side Effects**: Writes to control registers may trigger actions (DMA start, PPU reset, etc.)

## Example Usage

```cpp
// Setup PPU control registers
cpu.registerIORegion(0xC000, 16,
    [&](uint16_t offset) -> uint8_t {
        // Read PPU register at offset 0-15
        switch(offset) {
            case 0: return ppuStatus;
            case 2: return ppu.getPC() & 0xFF;
            case 3: return (ppu.getPC() >> 8) & 0xFF;
            // ... etc
        }
        return 0xFF;
    },
    [&](uint16_t offset, uint8_t value) {
        // Write PPU register at offset 0-15
        switch(offset) {
            case 0: // PPUCTRL
                if (value & 0x01) ppu.start();
                if (value & 0x02) ppu.reset();
                break;
            // ... etc
        }
    }
);

// Map PPU memory window at bank $70
cpu.setPPU(&ppu);
cpu.mapPPUWindow(0x70);

// Map APU memory window at bank $60
cpu.setAPU(&apu);
cpu.mapAPUWindow(0x60);
```

## Next Steps

1. Decide on exact addresses for each I/O region
2. Implement register read/write handlers for each component
3. Test communication between CPU and PPU/APU
4. Add DMA controller implementation
5. Implement interrupt routing
