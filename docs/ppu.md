# PPU (Picture Processing Unit)

## Overview

The ZeroPoint PPU is a microcode-based graphics processor with its own instruction set, running at 64 MHz. It executes 1 instruction per cycle (64 million instructions per second).

## Architecture

### Registers
- **64 registers**, all 16-bit
- **Register 63 (0x3F)**: DP (Data Pointer)
- **Register 62 (0x3E)**: PC (Program Counter) - used for jumps, not execution pointer

### Memory
- **64 KiB** (65536 bytes) shared with ROM
- Contains both microcode and data

### Execution
- **Clock**: 64 MHz (master clock, executes every cycle)
- **Instruction time**: 1 cycle per instruction
- **Instruction format**: 16 bits (4-bit opcode + 12-bit operand)

### System Clock Synchronization

The PPU runs at 64 MHz, synchronized to the master clock. It executes every master cycle:

- **Master clock**: 64 MHz (PPU pixel clock)
- **PPU clock**: 64 MHz (every master cycle)
- **Execution pattern**: Executes on cycles 0-15 (every cycle)

Other system components run at submultiples of the master clock:
- **DMA**: 32 MHz (every 2 cycles)
- **CPU**: 16 MHz (every 4 cycles)
- **APU**: 4 MHz (every 16 cycles)

## Initialization Sequence

1. **Wait for Interrupts**: PPU expects first two values to be VBlank and HBlank interrupt jump addresses
2. **Boot ROM**: Runs boot animation displaying logo while CPU loads microcode
3. **Wait for Start**: Stalls until CPU sends start interrupt
4. **Execute Microcode**: Begins normal operation

## Instruction Set

### Format

```
16-bit instruction:
[15:12] - 4-bit opcode
[11:0]  - 12-bit operand
```

### Basic Instructions

| Opcode | Mnemonic | Format | Description |
|--------|----------|--------|-------------|
| 0x0 | DEFCALL | `0000 0000 XXXXXXXX` | Define callable function at address XX |
| 0x1 | ENDDEFCALL | `0001 000000000000` | End function definition |
| 0x2 | SWAPREG | `0010 XXXXXX YYYYYY` | Swap registers X and Y |
| 0x3 | CLR | `0011 000000 XXXXXX` | Clear register X |
| 0x4 | CMP | `0100 XXXXXX YYYYYY` | Compare X and Y (sets flags) |
| 0x5 | CLRF | `0101 00000000000` | Clear comparison flags |
| 0x6 | JMZ/JMG | `0110 B 00000000000` | Jump if zero (B=0) or greater (B=1) to (PC) |
| 0x7 | JNZ/JNG | `0111 B 00000000000` | Jump if not zero (B=0) or not greater (B=1) to (PC) |
| 0x8 | INC | `1000 0 00000 XXXXXX` | Increment register X |
| 0x9 | DEC | `1001 000000 XXXXXX` | Decrement register X |
| 0xA | ADD | `1010 XXXXXX YYYYYY` | X = X + Y |
| 0xB | SUB | `1011 XXXXXX YYYYYY` | X = X - Y |
| 0xC | MUL | `1100 XXXXXX YYYYYY` | X = X × Y |
| 0xD | INTDIV | `1101 XXXXXX YYYYYY` | X = X / Y (integer division) |
| 0xE | HALT | `1110 000000000000` | Stop execution |
| 0xF | PRESET | `1111 ZZZZ WWWWWWWW` | Extended instructions (see below) |

### Comparison Flags

- **Zero**: Set when comparison result is zero
- **Greater**: Set when left operand > right operand

### Conditional Jumps

- **JMZ**: Jump if zero flag set
- **JMG**: Jump if greater flag set (also JNL - jump if not less)
- **JNZ**: Jump if zero flag clear
- **JNG**: Jump if greater flag clear (also JML - jump if less)

## Preset F Instructions (0xF)

Extended graphics and control instructions.

| Sub-op | Mnemonic | Format | Description |
|--------|----------|--------|-------------|
| 0x0 | SETPOS | `0000 XXXX YYYY` | Set position using 4-bit reg IDs from banks |
| 0x1 | SETTILE | `0001 WWWWWWWW` | Set tile ID to W |
| 0x2 | SETDP | `0010 XXXXXX 00` | Set DP from register X |
| 0x3 | MOV(DP) | `0011 XXXXXX 00` | Move register X to address (DP) |
| 0x4 | SETRENDMOD | `0100 B 0000000` | Set render mode: B=0→16-bit RGBA, B=1→32-bit RGBA |
| 0x5 | PALETTE16 | `0101 00000000` | Load 16-color palette from (DP) (32/64 bytes) |
| 0x6 | PALETTE256 | `0110 00000000` | Load 256-color palette from (DP) (512/1024 bytes) |
| 0x7 | JMR | `0111 00000000` | Jump relative to (PC) |
| 0x8 | MOV | `1000 XXXXXX 00` | Move from address (DP) to register X |
| 0x9 | SETREGBANK | `1001 ZZ WW 0000` | Set register banks: Z for X, W for Y |
| 0xA | CLRTILE | `1010 00000000` | Clear current tile |
| 0xB | CLRPALETTE | `1011 00000000` | Clear palette |
| 0xC | STRTDEFTILE | `1100 00000000` | Start defining tile |
| 0xD | ENDDEFTILE | `1101 00000000` | End defining tile |
| 0xE | CALL | `1110 XXXXXXXX` | Call function at address X |
| 0xF | GBLS | `1111 XXXXXX 00` | Get blank status into register X |

### GBLS - Get Blank Status

Returns blank status in specified register:
- **0**: VBlank active
- **1**: HBlank active
- **65535**: Neither (active rendering)

### Register Banks

For SETPOS instruction with 4-bit register IDs:
- Bank select via SETREGBANK (bits 0-1 for each bank)
- Full register ID = (bank << 4) | 4-bit ID
- Allows access to all 64 registers with 4-bit encoding

## Example Programs

### Simple Counter

```
; Count from 0 to 10 in register 0
; Register 1 = target (10)
; Register 62 (PC) = loop address

0x3000  ; CLR R0 (clear register 0)
0x3001  ; CLR R1
0x800A  ; INC R1 ... (repeat to set R1 = 10)

; Loop:
0x4001  ; CMP R0, R1 (compare counter to 10)
0x7800  ; JNG (if R0 < R1, jump to PC)
0xE000  ; HALT
```

### Memory Copy

```
; Copy 16 bits from address in DP to register 5

0xF205  ; SETDP R5 (set DP from R5)
0xF805  ; MOV R5, (DP) (load from DP to R5)
```

## Timing

At 64 MHz with 1 cycle per instruction:

- **Instructions per second**: 64 million
- **Instructions per scanline** (at 261 scanlines/frame, ~60 Hz): ~4096
- **Instructions per frame**: ~1,066,666

This gives plenty of time for complex rendering operations.

## Integration with Display

The PPU should be synchronized with the display controller:
- VBlank and HBlank status can be queried via GBLS
- Interrupt addresses set during initialization
- PPU can execute different code paths during blanking periods

## Implementation Notes

### Not Yet Implemented
The following are placeholder operations in current implementation:
- Tile rendering system
- Palette management
- Call stack for function calls
- Boot ROM animation

### Future Enhancements
- Sprite rendering
- Background layers
- Hardware scrolling
- Alpha blending (32-bit mode)
