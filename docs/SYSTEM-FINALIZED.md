# ZeroPoint Basic System - FINALIZED

This document summarizes what was implemented to finalize the basic ZeroPoint system.

> **Note**: This document predates the interrupt controller, hardware timers, and
> DMA I/O integration (see `CLAUDE.md` "Status" section for current state). Some
> "remaining work" items below are now done; see inline corrections.

---

## What Was Implemented (Emulation Side)

All emulation-side features have been implemented and are ready to use.

### 1. PPU Memory Write Access ✅

**File**: `include/ppu.h:127-129`
```cpp
void writeMemory(uint16_t address, uint8_t value) {
    memory[address] = value;
}
```

- CPU can now write to PPU memory via bank $B0 window
- Enables CPU to upload microcode, tiles, palettes
- Fully integrated with CPU memory mapping (cpu.cpp:1752)

### 2. PPU Register Write Access ✅

**File**: `include/ppu.h:132-134`
```cpp
void setRegister(uint8_t reg, uint16_t value) {
    if (reg < 64) registers[reg] = value;
}
```

- CPU can now write to any of PPU's 64 registers
- Enables control of PC, SP, DP, interrupt vectors
- Ready for I/O integration (not yet wired to I/O registers)

### 3. APU Stack Pointer Access ✅

**File**: `include/apu.h:33-34`
```cpp
uint16_t getSP() const { return sp; }
void setSP(uint16_t value) { sp = value; }
```

- CPU can now read/write APU stack pointer
- Fully integrated with I/O registers at $D80014-$D80015
- I/O handlers updated (cpu.cpp:1956-2016)

### 4. System Integration Class ✅

**Files**: `include/system.h`, `src/system.cpp`

Complete system integration with:
- **Unified construction**: Creates and wires all components
- **ROM loading**: `loadROM(filename)` - loads ZPB files
- **Memory setup**: Auto-configures RAM banks, windows, I/O
- **Synchronized execution**: `step()`, `run(cycles)`
- **Component access**: Getters for CPU, PPU, APU, Display
- **Interrupt detection**: V-Blank/H-Blank edge detection, routed through the
  interrupt controller ($D80058-$D8005F) to the CPU's single IRQ line ✅ DONE

**Features**:
```cpp
System system;
system.loadROM("game.zpb");
system.run(1000000);  // Run 1M cycles

// Access components
system.getCPU().getA();
system.getPPU().getState();
system.getDisplay().isVBlank();
```

**Clock synchronization**:
- Master clock: 64 MHz (PPU pixel clock)
- Integer-based: Deterministic 16-cycle pattern (no fractional accumulators)
- PPU: 64 MHz (every cycle)
- DMA: 32 MHz (every 2 cycles: 1, 3, 5, 7, 9, 11, 13, 15)
- CPU: 16 MHz (every 4 cycles: 3, 7, 11, 15)
- APU: 4 MHz (every 16 cycles: 15)
- Display: 64 MHz (every cycle)

### 5. Interrupt Routing ✅

**Files**: `include/cpu.h:71-73`, `src/cpu.cpp:2082-2184`, `src/system.cpp:150-168`

Complete interrupt routing from Display → CPU:
- **`triggerIRQ()`**: Maskable interrupt
  - Checks I flag (returns if I=1)
  - Pushes PB, PC, P to stack
  - Sets I flag, clears D flag
  - Reads vector from $00:FFFE-FFFF
  - Jumps to handler
- **`triggerNMI()`**: Non-maskable interrupt
  - Cannot be masked (ignores I flag)
  - Pushes PB, PC, P to stack
  - Does NOT modify I flag
  - Clears D flag
  - Reads vector from $00:FFFA-FFFB
  - Jumps to handler

**System integration**:
```cpp
void System::checkInterrupts() {
    // Detect V-Blank rising edge → trigger IRQ
    if (vblankIRQEnabled && currentVBlank && !lastVBlank) {
        cpu.triggerIRQ();
    }

    // Detect H-Blank rising edge → trigger IRQ
    if (hblankIRQEnabled && currentHBlank && !lastHBlank) {
        cpu.triggerIRQ();
    }
}
```

**Usage in assembly**:
```asm
; Set up IRQ vector
.org $00FFFE
    .word irq_handler

irq_handler:
    ; Save registers
    REP #$30
    PHA
    PHX
    PHY

    ; Handle V-Blank
    JSR update_graphics

    ; Restore and return
    PLY
    PLX
    PLA
    RTI
```

### 6. Compilation ✅

All code compiles successfully:
- Added `src/system.cpp` to build
- Interrupt routing methods compile without errors
- No errors, only minor warnings in APU/PPU (tautological comparisons)
- CMakeLists automatically detected new files

---

## What Still Needs Implementation

These are documented in **`MISSING-IMPLEMENTATIONS.md`** for your decision.

### High Priority

**Interrupt Vectors** (HIGH priority)
- Need confirmation of vector locations
- Standard 65816: $FFFA (NMI), $FFFC (RESET), $FFFE (IRQ/BRK)
- DEF88186 additions: $FFF4 (COP), $FFF8 (ABORT)
- **Decision needed**: Confirm these match your spec

### Medium Priority

**Boot ROM** (YOU handle this)
- Location: Bank $FF
- Features: Signature verification, initialization
- You mentioned providing signing key for devs
- **Status**: Your responsibility

### Low Priority

**Controller Input**
- I/O space reserved at $C200
- **Decision needed**: Controller format, button mapping

**Timers** ✅ DONE
- 8 hardware timers implemented at $D80050-$D80052 (TIMER_CONTROL/STATUS/INT_ENABLE)

**DMA Integration** ✅ DONE
- DMA controller is wired to I/O; $D80020-$D8002F is a live 9-byte config
  buffer (see `src/cpu.cpp` `registerIORegion(IO_BANK, 0x0020, ...)`), not a
  placeholder

---

## Usage Example

### Simple System Usage

```cpp
#include "system.h"

int main() {
    ZeroPoint::System system;

    // Load ROM
    if (!system.loadROM("mygame.zpb")) {
        return 1;
    }

    // Run for 1 million cycles
    system.run(1000000);

    // Check results
    std::cout << "CPU cycles: " << system.getCPU().getCycleCount() << "\n";
    std::cout << "PPU state: " << (int)system.getPPU().getState() << "\n";

    return 0;
}
```

### Manual Component Setup (if not using System class)

```cpp
ZeroPoint::CPU cpu;
ZeroPoint::PPU ppu;
APU apu;
ZeroPoint::Display display;

// Wire components
cpu.setPPU(&ppu);
cpu.setAPU(&apu);
cpu.setDisplay(&display);
ppu.setDisplay(&display);

// Setup memory
cpu.allocateRAM(0x80, 32);      // Work RAM
cpu.mapPPUWindow(0xB0);         // PPU window
cpu.mapAPUWindow(0xA0);         // APU window
cpu.setupIORegisters();         // I/O at bank $D8

// Load ROM manually
cpu.loadROM(romData, romSize, 0x00);
cpu.setPC(0x8000);
cpu.setPB(0x00);

// Run
cpu.run(1000000);
```

---

## Summary of Changes

### Files Modified

| File | Changes |
|------|---------|
| `include/ppu.h` | Added `writeMemory()`, `setRegister()` |
| `include/apu.h` | Added `getSP()`, `setSP()` |
| `src/cpu.cpp` | Updated PPU write mapping, APU SP I/O handlers |
| `include/system.h` | **NEW** - System integration class |
| `src/system.cpp` | **NEW** - System implementation |

### New Features

- ✅ CPU ↔ PPU memory communication (bank $B0)
- ✅ CPU ↔ APU memory communication (bank $A0)
- ✅ Full I/O register support ($D8:0000-0047)
- ✅ Unified system class for easy integration
- ✅ Synchronized multi-component execution

### Compilation Status

```
✅ All code compiles successfully
✅ No errors
⚠️  Minor warnings (tautological comparisons in APU/PPU)
✅ System class integrated into build
```

---

## What's Next

### For You to Decide

1. **Boot ROM** - still not provided (bank $FF is unmapped); RSA/SHA-256
   verification math spec now written in `ZPbootROM/def88186/rsa.def`
2. Controller format and timer specs were decided and implemented — a
   single-controller design at $D80030-$D80032 (not $C200) and 8 fixed-period
   hardware timers at $D80050-$D80057 (not $C300, and not programmable
   reload timers as this doc originally floated)

### Future Enhancements (Low Priority)

- Register select mechanism: PPUREG_ADDR/DATA is done (`src/cpu.cpp`
  ~1743-1805); **APUREG_ADDR/DATA is still a non-functional stub** (always
  reads 0, writes are no-ops)
- DMA integration — done, see `docs/dma.md`
- Hardware scrolling — still missing
- Reverb/echo effects — still missing
- Debugger tools — still missing
- Optimizations

---

## Testing the System

### Recommended Test Order

1. **Test PPU memory writes** - Upload microcode via CPU
   ```asm
   LDA #$B0
   SDB              ; Switch to PPU bank
   LDA #$42
   STA $0000        ; Write to PPU memory
   ```

2. **Test APU memory writes** - Upload APU program via CPU
   ```asm
   LDA #$A0
   SDB              ; Switch to APU bank
   LDA #$00
   STA $8000        ; Write to APU BIOS area
   ```

3. **Test I/O registers** - Control PPU/APU via $D8
   ```asm
   LDA #$01
   STA $D80000      ; Start PPU
   LDA $D80001      ; Read PPU status
   ```

4. **Test System class** - Load and run a ROM
   ```cpp
   System system;
   system.loadROM("test.zpb");
   system.run(10000);
   ```

5. **Test interrupts** (when routing added) - V-Blank handler
   ```asm
   ; Set IRQ vector to vblank_handler
   ; Enable V-Blank interrupts in PPU
   ; Wait for interrupt
   ```

---

## System is Now BASIC-COMPLETE

The ZeroPoint system now has:
- ✅ Complete CPU with all 256 opcodes
- ✅ Complete PPU with graphics rendering
- ✅ Complete APU with audio mixing
- ✅ Memory mapping with ROM/RAM/Windows/I/O
- ✅ System integration class
- ✅ All emulation-side features

**Missing only**:
- Boot ROM (your responsibility)
- Interrupt routing (simple addition)
- Controller/Timer hardware (low priority)

**The basic system is functional and ready for testing!**
