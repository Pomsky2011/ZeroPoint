# ZeroPoint Serial Debug Protocol

## Overview

The ZeroPoint debug interface uses **Player Port 2** as a serial communication channel between the emulator and an external debug monitor. The boot ROM detects dev mode and enters a debug loop, allowing real-time inspection and control.

## Architecture

### Memory Map
- **Boot ROM**: Bank $FF (64 KB, read-only)
- **Player Ports**: $D80030-$D8003F (16 bytes I/O)
  - **Port 1** ($D80030-$D80037): Player 1 controller
  - **Port 2** ($D80038-$D8003F): Player 2 / Debug serial interface

### Boot ROM Behavior
1. CPU resets to RESET vector at $00FFFC-FFFD
2. Boot ROM checks dev mode flag (I/O register $D80048)
3. **Dev Mode (flag = 1)**: Enter debug loop, await commands on Port 2
4. **Normal Mode (flag = 0)**: Jump to ROM entry point from header

## Player Port 2 Registers (Debug Mode)

| Address   | Name       | Access | Description                          |
|-----------|------------|--------|--------------------------------------|
| $D80038   | P2_STATUS  | R      | Status flags (bit 0: data ready)     |
| $D80039   | P2_DATA    | R/W    | Data byte (read command, write response) |
| $D8003A   | P2_CTRL    | W      | Control (bit 0: clear buffer)        |
| $D8003B-F | Reserved   | -      | Future expansion                     |

### P2_STATUS Flags
- **Bit 0 (RX_READY)**: 1 = command byte available to read
- **Bit 1 (TX_EMPTY)**: 1 = transmit buffer empty, can write
- **Bits 2-7**: Reserved

## Debug Protocol Commands

All commands are **single-byte** with optional parameters. Responses include status byte + data.

### Command Format
```
[CMD] [PARAM1] [PARAM2] ... [PARAMN]
```

### Response Format
```
[STATUS] [DATA1] [DATA2] ... [DATAN]
```

**Status Codes:**
- `0x00` - OK
- `0x01` - Error: Invalid command
- `0x02` - Error: Invalid address
- `0x03` - Error: Invalid parameter

## Command Set

### 0x01: Read CPU Register
**Format:** `01 [REG]`
- **REG**: Register selector
  - `0x00` = A (16-bit, LSB first)
  - `0x01` = X (16-bit)
  - `0x02` = Y (16-bit)
  - `0x03` = SP (16-bit)
  - `0x04` = D (16-bit)
  - `0x05` = PC (16-bit)
  - `0x06` = PB (8-bit)
  - `0x07` = DB (8-bit)
  - `0x08` = P (flags, 8-bit)

**Response:** `[STATUS] [DATA_LO] [DATA_HI]`

**Example:** Read A register
```
Send: 01 00
Recv: 00 42 00  (A = 0x0042)
```

### 0x02: Write CPU Register
**Format:** `02 [REG] [DATA_LO] [DATA_HI]`

**Response:** `[STATUS]`

### 0x03: Read Memory
**Format:** `03 [ADDR_LO] [ADDR_MID] [ADDR_HI] [LEN]`
- **ADDR**: 24-bit address (little-endian)
- **LEN**: Number of bytes to read (1-255)

**Response:** `[STATUS] [BYTE1] [BYTE2] ... [BYTEN]`

**Example:** Read 4 bytes from $800000
```
Send: 03 00 00 80 04
Recv: 00 AA BB CC DD
```

### 0x04: Write Memory
**Format:** `04 [ADDR_LO] [ADDR_MID] [ADDR_HI] [LEN] [BYTE1] ... [BYTEN]`

**Response:** `[STATUS]`

### 0x05: Continue Execution
**Format:** `05`
- Resumes execution from current PC
- Boot ROM exits debug loop, jumps to game code
- Can re-enter via breakpoint or interrupt

**Response:** `[STATUS]`

### 0x06: Step Instruction
**Format:** `06 [COUNT]`
- **COUNT**: Number of CPU instructions to execute (1-255)
- Returns after executing COUNT instructions

**Response:** `[STATUS] [PC_LO] [PC_HI] [PB]`
- Returns new PC after step

### 0x07: Set Breakpoint
**Format:** `07 [ADDR_LO] [ADDR_MID] [ADDR_HI]`
- Sets CPU breakpoint at 24-bit address
- Max 16 breakpoints supported

**Response:** `[STATUS] [BP_ID]`
- **BP_ID**: Breakpoint slot (0-15)

### 0x08: Clear Breakpoint
**Format:** `08 [BP_ID]`

**Response:** `[STATUS]`

### 0x09: Get All CPU State
**Format:** `09`
- Returns complete CPU state dump

**Response:** `[STATUS] [A_LO] [A_HI] [X_LO] [X_HI] [Y_LO] [Y_HI] [SP_LO] [SP_HI] [D_LO] [D_HI] [PC_LO] [PC_HI] [PB] [DB] [P]`
- Total: 1 status + 15 data bytes

### 0x0A: Read PPU Register
**Format:** `0A [REG]`
- **REG**: PPU register (0-63)

**Response:** `[STATUS] [DATA_LO] [DATA_HI]`

### 0x0B: Write PPU Register
**Format:** `0B [REG] [DATA_LO] [DATA_HI]`

**Response:** `[STATUS]`

### 0x0C: Read PPU Memory
**Format:** `0C [ADDR_LO] [ADDR_HI] [LEN]`
- **ADDR**: 16-bit PPU memory address

**Response:** `[STATUS] [BYTE1] ... [BYTEN]`

### 0x0D: Read APU Register
**Format:** `0D [REG]`
- **REG**: APU register (0-255)

**Response:** `[STATUS] [DATA]`

### 0x0E: Read APU Memory
**Format:** `0E [ADDR_LO] [ADDR_HI] [LEN]`

**Response:** `[STATUS] [BYTE1] ... [BYTEN]`

### 0x10: Reset System
**Format:** `10`
- Soft reset (like pressing reset button)
- Re-enters boot ROM debug loop in dev mode

**Response:** `[STATUS]`

### 0x11: Get System Info
**Format:** `11`

**Response:** `[STATUS] [MASTER_CYCLE_0] [MASTER_CYCLE_1] [MASTER_CYCLE_2] [MASTER_CYCLE_3] [CPU_STATE] [PPU_STATE] [APU_STATE] [DMA_ACTIVE]`
- **MASTER_CYCLE**: 32-bit cycle count (little-endian)
- **CPU_STATE**: 0=Running, 1=Halted, 2=Waiting
- **PPU_STATE**: PPU state enum
- **APU_STATE**: APU state enum
- **DMA_ACTIVE**: Number of active DMA channels

## Boot ROM Debug Loop

```asm
; Boot ROM entry point ($FF0000)
boot_rom_start:
    SEI                   ; Disable interrupts
    CLC
    XCE                   ; Native mode
    REP #$30              ; 16-bit A, X, Y

    ; Check dev mode flag
    LDA $D80048           ; DEV_MODE register
    AND #$01
    BEQ normal_boot       ; If 0, normal boot

    ; Dev mode - enter debug loop
debug_loop:
    LDA $D80038           ; Read P2_STATUS
    AND #$01              ; Check RX_READY
    BEQ debug_loop        ; Wait for command

    LDA $D80039           ; Read command byte
    JSR process_command   ; Handle command
    BRA debug_loop        ; Loop

normal_boot:
    ; Jump to ROM entry point from ROM header
    LDA $00FFFE           ; Read entry point (24-bit)
    ; ... setup and jump
    JML entry_point
```

## External Debug Monitor

The debug monitor is a separate program (Python/C++) that:
1. Communicates with emulator via socket/pipe
2. Sends debug commands
3. Parses responses
4. Provides CLI or GUI interface

### Example Python Monitor
```python
import socket

class ZeroPointDebugger:
    def __init__(self, host='localhost', port=6502):
        self.sock = socket.socket()
        self.sock.connect((host, port))

    def read_register(self, reg):
        """Read CPU register (0=A, 1=X, etc.)"""
        self.sock.send(bytes([0x01, reg]))
        resp = self.sock.recv(3)
        if resp[0] != 0x00:
            raise Exception(f"Error: {resp[0]}")
        return resp[1] | (resp[2] << 8)

    def read_memory(self, addr, length):
        """Read memory at 24-bit address"""
        cmd = bytes([0x03, addr & 0xFF, (addr >> 8) & 0xFF,
                     (addr >> 16) & 0xFF, length])
        self.sock.send(cmd)
        resp = self.sock.recv(1 + length)
        if resp[0] != 0x00:
            raise Exception(f"Error: {resp[0]}")
        return resp[1:]

    def set_breakpoint(self, addr):
        """Set breakpoint at 24-bit address"""
        cmd = bytes([0x07, addr & 0xFF, (addr >> 8) & 0xFF,
                     (addr >> 16) & 0xFF])
        self.sock.send(cmd)
        resp = self.sock.recv(2)
        return resp[1]  # Breakpoint ID
```

## Emulator Implementation

### System Class Extensions
```cpp
class System {
    // ...existing...

    bool devMode;
    DebugServer* debugServer;

    void setDevMode(bool enabled);
    void handleDebugCommand(uint8_t cmd, const uint8_t* params);
};
```

### Debug Server (Socket Interface)
The emulator runs a TCP server on port 6502 when dev mode is enabled:
- Accepts debug monitor connections
- Translates socket I/O to/from P2_DATA register
- Handles protocol framing

## Usage

### Starting Emulator in Dev Mode
```bash
./bin/zeropoint_qt --dev-mode --debug-port 6502 game.zpb
```

### External Monitor Connection
```bash
python zpdb.py localhost:6502
(zpdb) break $800100
Breakpoint 0 set at $800100
(zpdb) continue
Running...
Breakpoint 0 hit at $800100
(zpdb) info registers
A:  $0042  X: $0000  Y: $0000  SP: $01FF
PC: $800100  PB: $80  DB: $00  P: --MX-I--
(zpdb) x/16 $800000
800000: EA EA EA EA 4C 00 80 A9 42 00 85 00 60 00 00 00
```

## Future Extensions

- Watchpoints (break on memory write)
- Conditional breakpoints
- Performance profiling
- Memory usage maps
- Trace logging
