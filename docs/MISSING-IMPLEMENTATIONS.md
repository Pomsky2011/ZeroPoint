# Missing Implementations for Basic System

This document lists everything needed to finalize the basic ZeroPoint system.

---

## Category 1: Emulation-Side (Will be implemented automatically)

These are development/emulation features that will be added to the C++ codebase.

### PPU Missing Methods

**`writeMemory(uint16_t address, uint8_t value)`**
- **Needed for**: CPU writes to PPU memory via bank $B0 window
- **Current status**: PPU has `readMemory()` but no write equivalent
- **Impact**: CPU cannot upload microcode, tiles, or palettes to PPU
- **Priority**: HIGH - Required for CPU-PPU communication
- **File**: `include/ppu.h`, `src/ppu.cpp`

**`setRegister(uint8_t reg, uint16_t value)`**
- **Needed for**: CPU writes to PPU registers (PC, SP, DP) via I/O
- **Current status**: PPU has `getRegister()` but no setter
- **Impact**: CPU cannot control PPU execution state
- **Priority**: MEDIUM - Useful but not critical (can use loadMicrocode)
- **File**: `include/ppu.h`, `src/ppu.cpp`

### APU Missing Methods

**`getSP()`** and **`setSP(uint16_t value)`**
- **Needed for**: CPU reads/writes APU stack pointer via I/O registers
- **Current status**: APU has PC getters/setters but not SP
- **Impact**: I/O register $D80014-$D80015 (APUSP) returns 0
- **Priority**: LOW - Stack pointer rarely needs CPU access
- **File**: `include/apu.h`
- **Note**: APU's `sp` field exists (apu.h:146), just needs public accessors

### DMA Integration

**Connect DMA to CPU memory system**
- **Needed for**: Fast memory transfers via I/O registers at $D80020
- **Current status**: DMA controller exists but not connected
- **Impact**: DMA I/O registers at $D8:0020-$D8:002F are placeholders
- **Priority**: MEDIUM - Nice to have, not critical for basic system
- **File**: `src/cpu.cpp` (setupIORegisters, lines 2027-2030)
- **Requires**:
  - Add `DMAController* dmaPtr` to CPU
  - Implement DMA I/O handlers
  - Call `dma.tick()` during CPU execution
  - Set DMA memory callbacks to CPU's read/write

### System Integration Class

**Create unified `System` class**
- **Needed for**: Tie CPU, PPU, APU, Display together with synchronized execution
- **Current status**: No system-level integration (each tool does it manually)
- **Impact**: Every emulator/test needs to manually wire components
- **Priority**: HIGH - Critical for clean integration
- **File**: New `include/system.h`, `src/system.cpp`
- **Features**:
  - Constructor creates and wires CPU, PPU, APU, Display
  - `loadROM()` - loads ZPB ROM file
  - `reset()` - resets all components
  - `step()` - execute one master clock cycle
  - `run(cycles)` - execute N cycles
  - `tick()` - synchronized tick of all components
  - Handle interrupt routing (V-Blank, H-Blank → CPU IRQ)
  - Audio callback integration

### Interrupt Routing ✅ COMPLETE

**CPU interrupt system**
- **Needed for**: Route PPU V-Blank/H-Blank to CPU IRQ/NMI
- **Status**: ✅ IMPLEMENTED
- **Files**: `include/cpu.h:71-73`, `src/cpu.cpp:2082-2184`, `src/system.cpp:150-168`
- **Implementation**:
  - Added `triggerIRQ()` method - Maskable interrupt (respects I flag)
  - Added `triggerNMI()` method - Non-maskable interrupt (ignores I flag)
  - Reads IRQ vector from $00:FFFE-FFFF
  - Reads NMI vector from $00:FFFA-FFFB
  - Proper stack sequence: Push PB, PC (high, low), P to stack
  - IRQ sets I flag and clears D flag
  - NMI only clears D flag (I flag untouched)
  - System class detects V-Blank/H-Blank edges and triggers CPU IRQ
- **Result**: V-Blank and H-Blank interrupts now fully functional!

---

## Category 2: Missing Definitions (For you to decide)

These need design decisions or specifications.

### Boot ROM

**System boot sequence**
- **Needed for**: Initialize system on power-on/reset
- **Status**: NOT IMPLEMENTED (you mentioned this is your responsibility)
- **Location**: Bank $FF ($FF:0000-$FF:FFFF)
- **Features**:
  - Cartridge signature verification (private key system)
  - System initialization
  - Jump to cartridge entry point
- **Priority**: MEDIUM - Can test without it using direct PC setting
- **Note**: You said you'll provide the signing key for devs

### Interrupt Vector Table

**Define standard interrupt vectors**
- **Needed for**: CPU interrupt handling
- **Status**: Partially defined in cpu/interrupts.txt documentation
- **Standard 65816 vectors** (at end of bank $00):
  - $00:FFFA-$FFFB: NMI vector
  - $00:FFFC-$FFFD: RESET vector
  - $00:FFFE-$FFFF: IRQ/BRK vector
- **Additional DEF88186 vectors**:
  - $00:FFF4-$FFF5: COP vector
  - $00:FFF8-$FFF9: ABORT vector
- **Priority**: HIGH - Required for interrupts
- **Decision needed**: Confirm vector locations match documentation

### Controller Input Hardware

**Input device registers**
- **Needed for**: Read controller/keyboard input
- **Status**: NOT IMPLEMENTED (I/O space reserved at $C200)
- **Priority**: LOW - Not needed for basic graphics/audio
- **Decision needed**:
  - Controller format (NES-style? Modern gamepad?)
  - Number of controllers
  - Button mapping

### Timer Hardware

**System timers**
- **Needed for**: Timed interrupts, delays, profiling
- **Status**: NOT IMPLEMENTED (I/O space reserved at $C300)
- **Priority**: LOW - Can use V-Blank for timing
- **Decision needed**:
  - Timer resolution
  - Number of timers
  - Auto-reload vs one-shot

---

## Category 3: Nice-to-Have (Future enhancements)

### PPU Features

- **Register select mechanism** for PPUREG_ADDR/DATA ($D80008-$D8000A)
  - Currently I/O returns 0 (noted in cpu.cpp:1890-1897)
  - Would allow CPU to read/write any of 64 PPU registers

- **Hardware scrolling** (mentioned in TODO.md)
- **Window scaling** (2×, 4×, 8×) (mentioned in TODO.md)
- **Sprite system** (optional layer) (mentioned in TODO.md)

### APU Features

- **Register select mechanism** for APUREG_ADDR/DATA ($D80018-$D80019)
  - Currently I/O returns 0 (noted in cpu.cpp:1975-1978)
  - Would allow CPU to read/write any of 256 APU registers

- **Reverb/echo effects** (registers present, algorithms not implemented - TODO.md:94)
- **Gaussian interpolation** (currently nearest-neighbor - TODO.md:95)
- **Sample looping refinement** (basic framework present - TODO.md:96)

### CPU Features

- **Interrupt vectors** (proper IRQ/NMI) (TODO.md:459)
- **Memory banking implementation** (TODO.md:461)
- **Extended test suite** (TODO.md:462)

### Development Tools

- **Debugger** with PC tracing, register inspection (TODO.md:465, 471)
- **Disassemblers** for CPU/PPU/APU (TODO.md:493)
- **C preprocessor integration** (TODO.md:489)
- **Compiler optimizations** (TODO.md:490)

---

## Summary Table

| Item | Category | Priority | Who Implements |
|------|----------|----------|----------------|
| PPU writeMemory() | Emulation | HIGH | Auto |
| PPU setRegister() | Emulation | MEDIUM | Auto |
| APU getSP/setSP() | Emulation | LOW | Auto |
| DMA Integration | Emulation | MEDIUM | Auto |
| System Integration Class | Emulation | HIGH | Auto |
| Interrupt Routing | Emulation | MEDIUM | Auto |
| Boot ROM | Definition | MEDIUM | You |
| Interrupt Vectors | Definition | HIGH | You (confirm) |
| Controller Input | Definition | LOW | You |
| Timers | Definition | LOW | You |
| PPU Enhancements | Future | LOW | Future |
| APU Enhancements | Future | LOW | Future |
| Debug Tools | Future | MEDIUM | Future |

---

## Recommended Implementation Order

1. **System Integration Class** - Ties everything together cleanly
2. **PPU writeMemory()** - Enables CPU-to-PPU communication
3. **Interrupt Routing** - V-Blank/H-Blank to CPU
4. **Interrupt Vectors** - Confirm locations
5. **APU getSP/setSP()** - Complete I/O registers
6. **DMA Integration** - Fast transfers
7. **Boot ROM** - Final piece for complete system

After these are done, the basic system will be fully functional!

---

## Next Steps

**I will implement automatically**:
- ✅ PPU writeMemory()
- ✅ PPU setRegister()
- ✅ APU getSP/setSP()
- ✅ System integration class
- ✅ Interrupt routing

**You need to decide/provide**:
- Confirm interrupt vector locations
- Provide boot ROM (when ready)
- Decide on controller/timer specs (if needed)

**Future work** (low priority):
- DMA integration
- Register select mechanisms
- Enhanced audio/graphics features
- Debug tools
