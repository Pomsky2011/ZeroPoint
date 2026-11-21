# Debug serial port and the dev unit

A UART between a running program and the host: the guest prints with `SER_DATA`, the host reads it from a terminal, file or
TCP socket, and the host can type back. It is **built** (core revision 11) and it is deterministic: everything the guest can
see is a function of the CPU cycle counter, and typed input is recorded in movies.

The port **exists on every unit** but is only *read* by a **dev unit**. A retail unit has the same three registers and they
behave exactly as they did before the dev unit existed (revision 10): the guest can write bytes into the transmit queue (it fills, 255 bytes, and is never
drained) and the receive queue is always empty. Nothing on the host side listens (no sink is ever called, host input is
refused), and nothing in the retail Boot ROM touches it or the dev-unit latch.

## The dev unit

A host setting, like `--no-verify`, and **never reachable from the guest**:

- `System::setDevUnit(true)`, or `zeropoint_sdl --dev-unit`. Set it before `powerOn()`; it survives `softReset()`/`hardReset()`.
- It turns the serial port into a UART (below).
- **Signature checks always pass.** The HLE boot skips verification (`--no-verify` is the same thing without the UART); the
  real **dev Boot ROM** (which also answers exceptions by asking the host for a handler image over this UART: docs/exceptions.md) reads `$D80048` bit 1 at the top of `main` and goes straight to the handoff instead of running
  `rsa_verify` (a dev unit boots in ~0.3 s of console time instead of tens of seconds). Trailer-v3 *chunk* verification (`COP #$FF`) is a
  separate gate and still checks the data it is asked about.
- **Two Boot ROMs** (`ZPbootROM/build.sh` makes both from one source): `bootrom.bin` is the **retail** image - signature
  verification is unconditional and the image contains no read of the latch at all, so no host setting can skip it (it is
  byte-for-byte what it was before the dev unit existed). `bootrom_dev.bin` is the **dev** image: identical, plus the latch check.
  The dev image on a retail unit verifies like the retail one; the retail image on a dev unit still verifies (the HLE boot
  has no such split: it follows the latch). `--boot ../ZPbootROM/build/bootrom_dev.bin --dev-unit` for the real thing.
  `test_serial` runs the whole 2x2 (image x unit) matrix.
- The guest can read it, never write it: bit 1 of `DEV_MODE` (`$D80048`) is 1 on a dev unit. Bit 0 is the older, guest-writable
  `DEV_MODE` byte that `System::setDevMode` sets and nothing acts on; the two are independent, so a program writing the
  register cannot turn verification off for the next boot.
- It is part of a movie's setup (header flag), so a recording replays on a dev unit.

## Registers (`$D8003C-$D8003E`)

| Address | Name | |
|---------|------|--|
| `$D8003C` | `SER_STATUS` | r: bit 0 **RX_READY** (a byte is waiting), bit 1 **TX_EMPTY** (the queue has room), bit 2 **OVERRUN** (an RX byte was dropped), bit 3 **TX_DONE** (everything queued has gone out). Bits 2-3 read 0 on a retail unit. |
| `$D8003D` | `SER_DATA` | r: next received byte (0 if none). w: queue a byte for transmit; dropped if the queue is full. |
| `$D8003E` | `SER_CTRL` | w: bit 0 clears both queues (and OVERRUN, and discards unsent bytes); bit 1 clears OVERRUN alone (dev unit). |

Both queues hold 255 bytes. The addresses are bank `$D8`, so assemble them as long operands (`lda $D8003C`).

## Timing (dev unit)

The transmit side models a 115200 baud 8N1 line on the 17.002839 MHz CPU clock: **one byte per 1476 CPU cycles**
(`CPU::SERIAL_CYCLES_PER_BYTE`). A byte written to an idle port is out 1476 cycles later; bytes queued behind it follow back to
back (the next starts when the previous finishes, not when the host got around to noticing). `TX_EMPTY` is "the queue is not
full" as it always was, so a program that wants backpressure polls it, and one that wants to stop only after everything has gone
out polls `TX_DONE`. A halted CPU stops the clock, so it stops the line.

The receive side has no baud limit of its own: a byte arrives at the cycle the host (or a movie) delivers it. If the queue is
full the byte is dropped and `OVERRUN` is set. Frontends feed the guest only while the queue has room
(`System::serialRxHasRoom()`), a few bytes per 60 Hz tick, so a file or a paste never overruns it.

**Why this is deterministic.** The guest-visible queue state is "every byte whose time has come has gone out", computed from the
cycle counter on every register access and every hash (`CPU::serialSettle`). When the host looks - every cycle, once a frame,
only at the end - changes nothing but how soon the sink hears about a byte. `test_serial` checks that two rigs observed at
different rates end in the same `stateHash`. On a retail unit none of this state exists: `stateHash` is bit-identical to
what it was without the dev unit (when the dev unit went in, at revision 11, the 24 `fuzz_determinism` goldens still matched; revision 13 regenerated them for the new raster).

## The host end

`System::setSerialSink(fn)` receives each transmitted byte; `System::serialReceive(byte)` sends one in. `SerialHost`
(`include/serial_host.h`, a separate small library: it needs threads and sockets) gives frontends the usual endpoints.
`zeropoint_sdl`:

| Flag | |
|------|--|
| `--dev-unit` | the dev unit (also needed for any `--serial-*`) |
| `--serial-out FILE\|-` | transmitted bytes go to a file or FIFO, or `-` for stdout. The default, if no output is named, is stdout. May be repeated. |
| `--serial-in FILE\|-` | received bytes come from stdin (`-`), a file (read once) or a FIFO (reopened when its writer goes). May be repeated. |
| `--serial-tcp PORT` | both directions on `127.0.0.1:PORT` (localhost only: whoever connects gets the guest's console). One client at a time; a new connection replaces the old. Try `nc 127.0.0.1 5000`. |

`--record`/`--play` work with all of it: while recording, typed bytes are logged as `serial` events at their exact master cycle
(`MovieSession::serialIn`); a replay takes no live input and produces the same output.

`zeropoint_qt` takes the same flags (`zeropoint_qt game.zpb --dev-unit --serial-tcp 5000`) and also has, in the Console menu, a
**Dev Unit** toggle (off at every launch: it turns signature checks off, so it is never remembered; it applies when a cartridge
is next loaded) and a **Serial Console** window: the guest's output scrolls by and a typed line goes to the guest on Enter
(optionally with a newline). The console and the `--serial-*` endpoints work together. Recording and replay behave as above
(`EmulatorWidget::serialSend` is the typed-text path, recorded like any host byte). Test: `bin/test_qt_serial`.

The Vulkan target (`run_demo_vulkan`) only plays PPU programs: there is no `System` behind it, so there is no serial port to wire.

## From a program

`ZPdevtools/examples/cpu/serial_echo.asm` prints a banner and echoes input. The minimum to print a byte:

```asm
tx:     lda $D8003C         ; SER_STATUS
        and #$02            ; TX_EMPTY
        beq tx
        lda #$41
        sta $D8003D         ; 'A'
```

`tools/test_serial.sh` runs that program on the real frontend, then records and replays the session.

## Design: a command protocol on top (not built)

Everything below is the original design for a monitor that would sit on this UART: a guest-side program (a library in ZPdevtools)
that answers register/memory/step/breakpoint commands from a host tool. None of it exists; the UART above is what it would use.
Nothing in the emulator or the Boot ROM enters a debug loop, there is no command processor and no port-6502 server (the
earlier draft's `--dev-mode`/`--debug-port` flags and a Boot ROM at bank `$FF` never existed).

### Commands

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

### Command set

#### 0x01: Read CPU Register
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

#### 0x02: Write CPU Register
**Format:** `02 [REG] [DATA_LO] [DATA_HI]`

**Response:** `[STATUS]`

#### 0x03: Read Memory
**Format:** `03 [ADDR_LO] [ADDR_MID] [ADDR_HI] [LEN]`
- **ADDR**: 24-bit address (little-endian)
- **LEN**: Number of bytes to read (1-255)

**Response:** `[STATUS] [BYTE1] [BYTE2] ... [BYTEN]`

**Example:** Read 4 bytes from $800000
```
Send: 03 00 00 80 04
Recv: 00 AA BB CC DD
```

#### 0x04: Write Memory
**Format:** `04 [ADDR_LO] [ADDR_MID] [ADDR_HI] [LEN] [BYTE1] ... [BYTEN]`

**Response:** `[STATUS]`

#### 0x05: Continue Execution
**Format:** `05`
- Resumes execution from current PC
- Boot ROM exits debug loop, jumps to game code
- Can re-enter via breakpoint or interrupt

**Response:** `[STATUS]`

#### 0x06: Step Instruction
**Format:** `06 [COUNT]`
- **COUNT**: Number of CPU instructions to execute (1-255)
- Returns after executing COUNT instructions

**Response:** `[STATUS] [PC_LO] [PC_HI] [PB]`
- Returns new PC after step

#### 0x07: Set Breakpoint
**Format:** `07 [ADDR_LO] [ADDR_MID] [ADDR_HI]`
- Sets CPU breakpoint at 24-bit address
- Max 16 breakpoints supported

**Response:** `[STATUS] [BP_ID]`
- **BP_ID**: Breakpoint slot (0-15)

#### 0x08: Clear Breakpoint
**Format:** `08 [BP_ID]`

**Response:** `[STATUS]`

#### 0x09: Get All CPU State
**Format:** `09`
- Returns complete CPU state dump

**Response:** `[STATUS] [A_LO] [A_HI] [X_LO] [X_HI] [Y_LO] [Y_HI] [SP_LO] [SP_HI] [D_LO] [D_HI] [PC_LO] [PC_HI] [PB] [DB] [P]`
- Total: 1 status + 15 data bytes

#### 0x0A: Read PPU Register
**Format:** `0A [REG]`
- **REG**: PPU register (0-63)

**Response:** `[STATUS] [DATA_LO] [DATA_HI]`

#### 0x0B: Write PPU Register
**Format:** `0B [REG] [DATA_LO] [DATA_HI]`

**Response:** `[STATUS]`

#### 0x0C: Read PPU Memory
**Format:** `0C [ADDR_LO] [ADDR_HI] [LEN]`
- **ADDR**: 16-bit PPU memory address

**Response:** `[STATUS] [BYTE1] ... [BYTEN]`

#### 0x0D: Read APU Register
**Format:** `0D [REG]`
- **REG**: APU register (0-255)

**Response:** `[STATUS] [DATA]`

#### 0x0E: Read APU Memory
**Format:** `0E [ADDR_LO] [ADDR_HI] [LEN]`

**Response:** `[STATUS] [BYTE1] ... [BYTEN]`

#### 0x10: Reset System
**Format:** `10`
- Soft reset (like pressing reset button)
- Re-enters boot ROM debug loop in dev mode

**Response:** `[STATUS]`

#### 0x11: Get System Info
**Format:** `11`

**Response:** `[STATUS] [MASTER_CYCLE_0] [MASTER_CYCLE_1] [MASTER_CYCLE_2] [MASTER_CYCLE_3] [CPU_STATE] [PPU_STATE] [APU_STATE] [DMA_ACTIVE]`
- **MASTER_CYCLE**: 32-bit cycle count (little-endian)
- **CPU_STATE**: 0=Running, 1=Halted, 2=Waiting
- **PPU_STATE**: PPU state enum
- **APU_STATE**: APU state enum
- **DMA_ACTIVE**: Number of active DMA channels

## Future extensions

- A guest-side monitor library implementing the commands above
- Watchpoints (break on memory write), conditional breakpoints
