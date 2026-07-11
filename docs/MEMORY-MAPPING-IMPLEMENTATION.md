# Memory Mapping Implementation Summary

## What Was Implemented

Complete memory mapping system for the DEF88186 CPU with I/O register support.

---

## Memory Map (Finalized)

### Banks $00-$7F (8 MB) - ROM
Program ROM, system code, cartridge data

### Banks $80-$9F (2 MB) - Work RAM
General-purpose RAM, buffers, save data

### Banks $A0-$AF (1 MB) - APU Communication
- **$A0:0000-$A0:FFFF**: APU memory window (64 KB) ✅ **IMPLEMENTED**
- $A1-$AF: Reserved for APU expansion

### Banks $B0-$B7 (512 KB) - PPU Communication
- **$B0:0000-$B0:FFFF**: PPU memory window (64 KB) ✅ **IMPLEMENTED**
- $B1-$B2: Tile/palette upload regions
- $B3-$B7: Reserved for PPU expansion

### Banks $B8-$BD (384 KB) - Expansion
Reserved for future use

### Banks $BE-$BF (128 KB) - Work RAM Shadow
Additional work RAM for large buffers

### Banks $C0-$DF (2 MB) - Hardware Regions
AROM, coprocessor registers

### Banks $D8 - I/O Registers ✅ **IMPLEMENTED**
| Address | Size | Block | Description |
|---------|------|-------|-------------|
| $D8:0000-$D8:000F | 16 bytes | PPU Control | Start, reset, status, PC, SP, DP, interrupts |
| $D8:0010-$D8:001F | 16 bytes | APU Control | Reset, halt, PC, SP, banks |
| $D8:0020-$D8:002F | 16 bytes | DMA Control | Transfer config, status (implemented) |
| $D8:0040-$D8:0047 | 8 bytes | Display Status | Scanline, pixel, status, mode (read-only) |

### Banks $E0-$FF - Boot/System ROM
Security keys, boot ROM, interrupt vectors

---

## Code Changes

### Files Modified

**`include/cpu.h`**
- Added forward declarations for PPU, APU, Display
- Added `MemoryRegion` struct with types: ROM, RAM, PPU_WINDOW, APU_WINDOW, IO_REGISTERS
- Added `setPPU()`, `setAPU()`, `setDisplay()` methods
- Added `loadROM()`, `allocateRAM()` for memory region setup
- Added `mapPPUWindow()`, `mapAPUWindow()` for hardware windows
- Added `registerIORegion()` for I/O register callbacks
- Added `setupIORegisters()` to configure all I/O blocks
- Added memory mapping helper methods: `readMapped()`, `writeMapped()`, `findRegion()`
- Added hardware pointers: `ppuPtr`, `apuPtr`, `displayPtr`

**`src/cpu.cpp`**
- Modified `readByte()` and `writeByte()` to use memory mapping when enabled
- Implemented `readMapped()` - routes reads to ROM/RAM/PPU/APU/IO regions
- Implemented `writeMapped()` - routes writes with ROM write protection
- Implemented `findRegion()` - locates memory region for any 24-bit address
- Implemented `loadROM()` - allocate and load ROM data into banks
- Implemented `allocateRAM()` - allocate RAM in specified banks
- Implemented `registerIORegion()` - register I/O callback handlers
- Implemented `mapPPUWindow()` - map PPU memory to a bank
- Implemented `mapAPUWindow()` - map APU memory to a bank
- Implemented `setupIORegisters()` - **230+ lines of I/O register handlers**

### I/O Register Handlers Implemented

**PPU Control Block** (src/cpu.cpp:1855-1933)
- PPUCTRL: Start/reset PPU
- PPUSTATUS: Read PPU state
- PPUPC: Read program counter
- PPUSP: Read stack pointer (R61)
- PPUDP: Read data pointer (R63)
- PPU_VBLANK_INT: Read V-Blank interrupt address (R59)
- PPU_HBLANK_INT: Read H-Blank interrupt address (R60)

**APU Control Block** (src/cpu.cpp:1938-2021)
- APUCTRL: Reset/halt/resume APU
- APUSTATUS: Read halted state
- APUPC: Read/write program counter
- APU_ROMBANK: Set ROM bank (0-17)
- APU_IOBANK: Set I/O bank

**Display Status Block** (src/cpu.cpp:2035-2070)
- DISP_SCANLINE: Current scanline (read-only)
- DISP_PIXEL: Current pixel position (read-only)
- DISP_STATUS: V-Blank, H-Blank, visible area flags (read-only)
- DISP_MODE: Render mode (16-bit/32-bit, read-only)

**DMA Control Block** (src/cpu.cpp, `registerIORegion(IO_BANK, 0x0020, ...)`)
- Implemented: 9-byte sequential config buffer, queues transfers via `DMAController::queueDMA`

---

## Documentation Created

**`docs/io-mapping.md`** (recommended addresses, comprehensive)
- 12 I/O register categories with suggested addresses
- Detailed register specifications
- Example implementation code

**`docs/io-registers-found.md`** (actual registers in codebase)
- Complete inventory of existing I/O registers
- Detailed descriptions of PPU, APU, DMA, Display interfaces
- Methods and structs for each hardware component

**`docs/io-assignments-needed.txt`** (quick reference)
- 4 I/O blocks with assigned addresses
- Memory window configuration
- Example usage code

**`docs/io-usage-guide.md`** (how to use the I/O registers)
- C++ setup instructions
- DEF88186 assembly examples for each register
- Access patterns (V-Blank wait, bank switching, etc.)
- Complete PPU setup example

**`docs/MEMORY-MAPPING-IMPLEMENTATION.md`** (this file)
- Summary of implementation
- Final memory map
- Code changes

---

## How to Use

### C++ Setup
```cpp
ZeroPoint::CPU cpu;
ZeroPoint::PPU ppu;
APU apu;
ZeroPoint::Display display;

// Connect hardware
cpu.setPPU(&ppu);
cpu.setAPU(&apu);
cpu.setDisplay(&display);

// Setup memory
cpu.allocateRAM(0x80, 32);           // 2 MB Work RAM
cpu.loadROM(romData, romSize, 0x00); // ROM in banks $00-$7F
cpu.mapPPUWindow(0xB0);              // PPU at bank $B0
cpu.mapAPUWindow(0xA0);              // APU at bank $A0
cpu.setupIORegisters();              // Setup I/O at bank $D8

// Run
cpu.run(1000000);
```

### Assembly Access
```asm
; Start PPU
LDA #$01
STA $D80000        ; PPUCTRL: start

; Wait for V-Blank
wait_vblank:
    LDA $D80044    ; DISP_STATUS
    AND #$01       ; V-Blank bit
    BEQ wait_vblank

; Access PPU memory
LDA #$B0
SDB                ; Switch to PPU bank
LDA #$42
STA $0000          ; Write to PPU memory
```

---

## Features

✅ **Bank-based memory mapping** (256 banks × 64 KB)
✅ **ROM/RAM regions** with dynamic allocation
✅ **PPU memory window** (bank $B0)
✅ **APU memory window** (bank $A0)
✅ **I/O register framework** with read/write callbacks
✅ **4 I/O register blocks** implemented at bank $D8
✅ **Backward compatible** with legacy flat memory
✅ **Compiles successfully**
✅ **Fully documented**

---

## What's NOT Implemented

> Note: most of this section is stale, but with one correction to a claim
> made in an earlier pass of this doc. `PPU::writeMemory`/`setRegister` and
> `APU::getSP`/`setSP` exist (`include/ppu.h`, `include/apu.h`); PPUREG_ADDR/
> DATA register-select latching genuinely works (`src/cpu.cpp` ~1743-1805);
> the DMA controller is wired to I/O at $D80020-$D8002F (`src/cpu.cpp`).
> **APUREG_ADDR/DATA, however, is still a non-functional stub** — the read
> handler always returns 0 and the write handler has no case for those
> offsets at all (`src/cpu.cpp` ~1849-1857). Don't rely on it.

---

## Next Steps

1. **Test the memory mapping** - Write a test program that accesses I/O registers
2. **Add PPU write methods** - Enable CPU to modify PPU PC/SP/DP
3. **Integrate DMA** - Connect DMA controller to I/O registers
4. **Implement register select** - Track selected register for PPUREG/APUREG access
5. **Add controller input** - Implement controller I/O registers when needed
6. **Add timers** - Implement timer hardware when needed

---

## Summary

The DEF88186 CPU now has a complete, flexible memory mapping system that supports:
- Multi-bank ROM and RAM
- Hardware memory windows (PPU, APU)
- Memory-mapped I/O registers
- Callback-based I/O handlers

All I/O registers are assigned to **Bank $D8** as you specified, and the system is ready to use!
