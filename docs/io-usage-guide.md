# I/O Register Usage Guide - ZeroPoint System

This guide shows how to set up and use the CPU's memory-mapped I/O registers.

## Memory Map (Finalized)

### Bank $D8 - I/O Registers
All hardware control registers are located in **Bank $D8**:

| Address Range | Size | Block | Access |
|---------------|------|-------|--------|
| $D8:0000-$D8:000F | 16 bytes | PPU Control | Read/Write |
| $D8:0010-$D8:001F | 16 bytes | APU Control | Read/Write |
| $D8:0020-$D8:002F | 16 bytes | DMA Control | Read/Write |
| $D8:0040-$D8:0047 | 8 bytes | Display Status | Read-Only |

### Memory Windows
- **Bank $B0**: PPU Memory Window (64 KB)
- **Bank $A0**: APU Memory Window (64 KB)

---

## Setup (C++ Side)

```cpp
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "display.h"

// Create hardware instances
ZeroPoint::CPU cpu;
ZeroPoint::PPU ppu;
APU apu;
ZeroPoint::Display display;

// Connect hardware to CPU
cpu.setPPU(&ppu);
cpu.setAPU(&apu);
cpu.setDisplay(&display);

// Setup memory map
cpu.allocateRAM(0x80, 32);          // Banks $80-$9F: 2 MB Work RAM
cpu.loadROM(romData, romSize, 0x00); // Banks $00-$7F: ROM

// Map hardware windows
cpu.mapPPUWindow(0xB0);              // Bank $B0 → PPU memory
cpu.mapAPUWindow(0xA0);              // Bank $A0 → APU memory

// Setup I/O registers at Bank $D8
cpu.setupIORegisters();

// Ready to run!
cpu.setPC(entryPoint);
cpu.setPB(0x00);
cpu.run(1000000);
```

---

## Using I/O Registers (DEF88186 Assembly)

### 1. PPU Control Block ($D8:0000-$D8:000F)

```asm
; Start the PPU
LDA #$01
STA $D80000        ; PPUCTRL: bit 0 = start

; Check PPU status
LDA $D80001        ; PPUSTATUS: 0=waiting, 1=running, 2=halted

; Read PPU program counter
LDA $D80002        ; PPUPC low byte
LDX $D80003        ; PPUPC high byte

; Read stack pointer
LDA $D80004        ; PPUSP low
LDX $D80005        ; PPUSP high

; Read data pointer
LDA $D80006        ; PPUDP low
LDX $D80007        ; PPUDP high

; Read V-Blank interrupt address (R59)
LDA $D8000B        ; Low byte
LDX $D8000C        ; High byte

; Read H-Blank interrupt address (R60)
LDA $D8000D        ; Low byte
LDX $D8000E        ; High byte

; Reset PPU
LDA #$02
STA $D80000        ; PPUCTRL: bit 1 = reset
```

### 2. APU Control Block ($D8:0010-$D8:001F)

```asm
; Reset APU
LDA #$01
STA $D80010        ; APUCTRL: bit 0 = reset

; Halt APU
LDA #$02
STA $D80010        ; APUCTRL: bit 1 = halt

; Resume APU
LDA #$04
STA $D80010        ; APUCTRL: bit 2 = resume

; Check if APU is halted
LDA $D80011        ; APUSTATUS: 0=running, 1=halted

; Read APU program counter
LDA $D80012        ; APUPC low
LDX $D80013        ; APUPC high

; Write APU program counter
LDA #$00
STA $D80012        ; Set PC = $1234
LDA #$12
STA $D80013

; Set APU ROM bank (0-17)
LDA #$05
STA $D80016        ; APU_ROMBANK

; Set APU I/O bank
LDA #$00
STA $D80017        ; APU_IOBANK
```

### 3. Display Status Block ($D8:0040-$D8:0047, Read-Only)

```asm
; Read current scanline (0-260)
LDA $D80040        ; Low byte
LDX $D80041        ; High byte

; Read current pixel (0-339)
LDA $D80042        ; Low byte
LDX $D80043        ; High byte

; Check display status
LDA $D80044        ; DISP_STATUS
AND #$01           ; Bit 0: V-Blank
BNE in_vblank
AND #$02           ; Bit 1: H-Blank
BNE in_hblank
AND #$04           ; Bit 2: Visible area
BNE in_visible

; Check render mode
LDA $D80045        ; DISP_MODE: 0=16-bit, 1=32-bit
CMP #$01
BEQ is_32bit
```

### 4. Direct PPU Memory Access (Bank $B0)

Instead of using I/O registers, you can directly access PPU's 64 KB memory:

```asm
; Set data bank to PPU window
LDA #$B0
SDB                ; DB = $B0

; Write to PPU memory at $B0:0000
LDA #$42
STA $0000          ; Writes to PPU memory offset $0000

; Write PPU microcode
LDX #$0000
load_loop:
    LDA ppu_code,X
    STA $0000,X    ; Write to PPU memory
    INX
    CPX #$0100
    BNE load_loop

; Access VOC registers (PPU memory $00F0-$00FF)
LDA #$80           ; 32-bit color mode
STA $00F0          ; VOC renderModeControl
```

### 5. Direct APU Memory Access (Bank $A0)

```asm
; Set data bank to APU window
LDA #$A0
SDB                ; DB = $A0

; Write to APU memory
LDA #$00
STA $1000          ; Writes to APU memory at $1000

; Upload APU program
LDX #$0000
upload:
    LDA apu_code,X
    STA $8000,X    ; Write to APU BIOS area
    INX
    CPX #apu_code_size
    BNE upload

; Write to MMP registers (APU memory $0000-$00FF)
; Channel 0 pitch
LDA #$00
STA $0000          ; Pitch low byte
LDA #$10
STA $0001          ; Pitch high byte ($1000 = 1.0x speed)

; Channel 0 volume
LDA #$FF
STA $0002          ; Max volume
```

---

## Access Patterns

### Wait for V-Blank

```asm
wait_vblank:
    LDA $D80044    ; DISP_STATUS
    AND #$01       ; Check V-Blank bit
    BEQ wait_vblank
    ; Now in V-Blank
```

### Synchronize with PPU

```asm
; Wait for PPU to be running
wait_ppu_running:
    LDA $D80001    ; PPUSTATUS
    CMP #$01       ; 1 = running
    BNE wait_ppu_running
```

### Upload Data During V-Blank

```asm
    ; Wait for V-Blank
wait_vb:
    LDA $D80044
    AND #$01
    BEQ wait_vb

    ; Switch to PPU bank
    LDA #$B0
    SDB

    ; Upload tile data
    LDX #$0000
upload_tiles:
    LDA tile_data,X
    STA $0300,X    ; PPU tile buffer at $0300
    INX
    CPX #64        ; 64 bytes per tile
    BNE upload_tiles

    ; Back to ROM
    LDA #$00
    SDB
```

---

## Important Notes

### Bank Switching
- Always save and restore the Data Bank (DB) when accessing memory windows
- Use `SDB` instruction to set DB (DEF88186 extension)
- Example:
  ```asm
  PHB             ; Save current DB
  LDA #$B0
  SDB             ; Switch to PPU window
  ; ... access PPU memory ...
  PLB             ; Restore original DB
  ```

### Read-Modify-Write
- Some I/O registers are write-only (return 0 when read)
- Some I/O registers are read-only (ignore writes)
- Check the access column in the register tables above

### Multi-Byte Registers
- Always write low byte first, then high byte
- Read in any order (values are atomic)

### Interrupt Safety
- Disable interrupts (SEI) when accessing multi-byte registers
- Re-enable after (CLI)

---

## Example: Complete PPU Setup

```asm
; Setup and start PPU
setup_ppu:
    ; Reset PPU
    LDA #$02
    STA $D80000

    ; Switch to PPU memory window
    LDA #$B0
    SDB

    ; Upload microcode to PPU memory
    LDX #$0000
upload_microcode:
    LDA ppu_program,X
    STA $0000,X
    INX
    CPX #ppu_program_size
    BNE upload_microcode

    ; Set V-Blank interrupt handler
    LDA #<vblank_handler
    STA $00F0      ; Assume handler address in VOC or via registers
    LDA #>vblank_handler
    STA $00F1

    ; Switch back to ROM
    LDA #$00
    SDB

    ; Start PPU
    LDA #$01
    STA $D80000    ; PPUCTRL: start

    RTS
```

---

## Summary

All I/O registers are now implemented at **Bank $D8** as you specified:
- ✅ PPU Control: $D80000-$D8000F
- ✅ APU Control: $D80010-$D8001F
- ✅ DMA Control: $D80020-$D8002F (placeholder)
- ✅ Display Status: $D80040-$D80047

Memory windows work via:
- ✅ Bank $B0 for PPU (64 KB)
- ✅ Bank $A0 for APU (64 KB)

Setup function `CPU::setupIORegisters()` must be called after setting PPU, APU, and Display pointers.
