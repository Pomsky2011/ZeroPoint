# DMA Controller

## Overview

The ZeroPoint DMA (Direct Memory Access) controller enables efficient data transfers without CPU intervention. It supports 16 independent channels with queuing and 4 different transfer modes.

## Clock Synchronization

The DMA controller runs at 33.554432 MHz (2^25 Hz), half the speed of the master clock:

- **Master clock**: 67.108864 MHz (2^26 Hz)
- **DMA clock**: 33.554432 MHz (2^25 Hz, every 2 master cycles)
- **Execution pattern**: Executes on odd master cycles (1, 3, 5, 7, 9, 11, 13, 15)

This allows the DMA to run efficiently without competing with the PPU for every cycle.

Other system components:
- **PPU**: 67.108864 MHz (every cycle)
- **CPU**: 16.777216 MHz (every 4 cycles)
- **APU**: 4.194304 MHz (every 16 cycles)

## Configuration

Each DMA transfer is configured with a 9-byte structure:

| Offset | Size | Description |
|--------|------|-------------|
| 0      | 1    | Mode byte (SS XX YYYY) |
| 1-3    | 3    | Source address (24-bit, little endian) |
| 4-6    | 3    | Target address (24-bit, little endian) |
| 7      | 1    | Size multiplier |
| 8      | 1    | Any byte — writing this 9th byte is itself the trigger (`src/cpu.cpp`: `dmaPtr->queueDMA(dmaConfigBuffer)` fires as soon as the 9th byte lands); the value is stored (`DMAConfig::interrupt`) but never read anywhere |

### Mode Byte Format

```
Bits 7-6 (SS): Size bits (0-3)
Bits 5-4 (XX): Transfer mode (0-3)
Bits 3-0 (YYYY): Channel number (0-15)
```

### Transfer Size Calculation

```
Total bytes = ((S + 1) × 256) × size_multiplier
Maximum: (4 × 256) × 1 = 1024 bytes per transfer
```

### CPU Register Interface ($D80020-$D8002F)

The 9-byte config struct above isn't written as a block — it's streamed one
byte at a time to `$D80021-$D80028` (`src/cpu.cpp` `registerIORegion(IO_BANK,
0x0020, 16, ...)`):

| Offset | Access | Meaning |
|--------|--------|---------|
| $D80020 | R/W | Read: current write position in the 9-byte buffer. Write (any value): resets the buffer and write position to 0 |
| $D80021-$D80028 | W | Each write appends one byte to the config buffer, in order (mode, source×3, target×3, multiplier, trigger). The 9th write auto-queues the transfer via `DMAController::queueDMA()` and resets the position for the next transfer |
| $D80021 | R | Status byte: `QQQQAAAA` — queued-channel count in bits 7-4, active-channel count in bits 3-0 |
| $D80022 | R | 1 if DMA interrupt is active, else 0 |
| $D80023-$D8002E | R | Per-channel state, one byte per channel — but only **channels 0-11** (12 of the 16 hardware channels have a status byte here; channels 12-15 have no CPU-visible status) |

The buffer position auto-advances on every write and is what determines which
config byte you're setting — the specific offset ($D80021 vs $D80022 vs ...)
is not checked against the position, only validated as being in range. In
practice firmware should still write config bytes 1-9 to offsets $D80021
through $D80028 (cycling back to $D80021 for the 9th byte) to keep source
readable, since the hardware itself doesn't enforce that ordering.

## Transfer Modes

### Mode 0b00: Data Copy
- **Purpose:** Copy memory from one location to another
- **Init cycles:** 0
- **Cycles per byte:** 3
- **Source:** Read from sourceAddr + offset
- **Target:** Write to targetAddr + offset

**Example use:** Copying sprite data, level data, etc.

### Mode 0b01: Const Copy
- **Purpose:** Fill a memory range with a repeating pattern
- **Init cycles:** S + 1 (reads pattern during init)
- **Cycles per byte:** 1
- **Source:** Pattern of S+1 bytes read from sourceAddr
- **Target:** Write pattern to targetAddr + offset

**Example use:** Clearing screen, filling buffers with patterns

### Mode 0b10: Repeat Transfer
- **Purpose:** Stream a pattern to a single address
- **Init cycles:** S + 1 (reads pattern during init)
- **Cycles per byte:** 3
- **Source:** Pattern of S+1 bytes read from sourceAddr
- **Target:** Write pattern bytes to targetAddr (same address)

**Example use:** Streaming data to hardware registers, PPU writes

### Mode 0b11: Const Repeat
- **Purpose:** Stream a constant pattern to a single address
- **Init cycles:** S + 1 (reads pattern during init)
- **Cycles per byte:** 2
- **Source:** Pattern of S+1 bytes read from sourceAddr
- **Target:** Write pattern bytes to targetAddr (same address)

**Example use:** Filling hardware FIFOs, audio buffers

## Timing

### State Machine

Each DMA transfer goes through these states:

1. **Idle** - Channel not in use
2. **Configuring** - 8 bus cycles to configure
3. **Initializing** - Mode-specific init (0 to S+1 cycles)
4. **Transferring** - Active transfer (mode-dependent cycles per byte)
5. **Complete** - Transfer finished

### Total Cycle Calculation

```
Total cycles = 8 (config) + init_cycles + (bytes × cycles_per_byte)
```

**Examples:**

| Mode | S | Bytes | Total Cycles |
|------|---|-------|--------------|
| Data Copy | 0 | 256 | 8 + 0 + (256 × 3) = 776 |
| Const Copy | 3 | 1024 | 8 + 4 + (1024 × 1) = 1036 |
| Repeat Transfer | 1 | 512 | 8 + 2 + (512 × 3) = 1546 |
| Const Repeat | 2 | 256 | 8 + 3 + (256 × 2) = 523 |

## Channel Management

### 16 Channels
- Channels 0-15 available for use
- Each channel can handle one transfer at a time
- **Maximum 2 channels can be active simultaneously**
- Additional channels queued if limit reached

### Queuing
- If a channel is busy, new transfers are queued
- Queue is processed in FIFO order
- New transfers only start if < 2 channels active
- When a channel completes, next queued transfer can start

### Interrupt Handling
- **When an interrupt occurs, ALL DMA is paused**
- DMA does not process during interrupt
- DMA resumes automatically when interrupt is cleared
- Use `triggerInterrupt()` to pause, `clearInterrupt()` to resume

## Usage Example

### Example 1: Clear 256 bytes of VRAM

```
Source: 0x010000 (pattern data: 0x00, 0x00)
Target: 0x020000 (VRAM)
Mode: Const Copy
S = 0 (pattern size = S + 1 = 1 byte)
multiplier = 1
Transfer size = ((S + 1) × 256) × multiplier = (1 × 256) × 1 = 256 bytes
```

Configuration bytes:
```
0: 0b00 01 0000 = 0x10 (S=0, Mode=01, Channel=0)
1: 0x00 (source low)
2: 0x00 (source mid)
3: 0x01 (source high) = 0x010000
4: 0x00 (target low)
5: 0x00 (target mid)
6: 0x02 (target high) = 0x020000
7: 0x01 (multiplier)
8: 0x01 (interrupt/trigger)
```

Timing:
- Config: 8 cycles
- Init: S+1 = 1 cycle (read pattern)
- Transfer: 256 × 1 = 256 cycles
- **Total: 265 cycles**

### Example 2: Copy sprite (64 bytes)

```
Source: 0x030000
Target: 0x040000
Mode: Data Copy
```

We need 64 bytes:
- ((S + 1) × 256) × multiplier = 64
- If S = 0: (1 × 256) × multiplier = 64, so multiplier = 64/256 = 0.25 (invalid)
- Minimum transfer with S=0, multiplier=1 is 256 bytes

**Note:** For transfers less than 256 bytes, you would typically use CPU copies or pad the transfer.

### Example 3: Stream to Audio Register

```
Source: 0x050000 (4-byte audio pattern)
Target: 0xFFFF00 (audio hardware register)
Mode: Repeat Transfer
S: 3 (pattern = 4 bytes)
Size: Send 1024 bytes total
```

Configuration:
```
0: 0b11 10 0001 = 0xE1 (S=3, Mode=10, Channel=1)
1-3: 0x000050 (source)
4-6: 0x00FFFF (target)
7: 0x01 (multiplier: (4 × 256) × 1 = 1024 bytes)
8: 0x01 (interrupt/trigger)
```

Timing:
- Config: 8 cycles
- Init: 4 cycles (read 4-byte pattern)
- Transfer: 1024 × 3 = 3072 cycles
- **Total: 3084 cycles**

## Implementation Details

### Pattern Buffer
- Internal 4-byte buffer stores patterns for repeat modes
- Maximum pattern size is S+1 where S=3, so 4 bytes max
- Pattern is read during initialization phase

### Memory Access
- Uses callback functions for memory reads/writes
- Allows integration with memory mapper and hardware registers
- No direct memory access from DMA controller

### Parallel Execution Limits
- **Maximum 2 channels active at once**
- Third channel waits in queue until a slot frees up
- Channels process in order: channel 0, 1, 2... up to first 2 active
- Once a channel completes, next queued transfer can start

### Conflict Resolution
- Only one DMA per channel at a time
- New transfers to busy channels are queued
- Queue is processed each tick cycle
- FIFO ordering within queue

### Interrupt Behavior
- Interrupts pause ALL DMA execution
- Current channel state is preserved (cycles remaining, bytes transferred)
- DMA resumes from exact state when interrupt clears
- Critical for CPU interrupt handlers that need bus access

## Performance Characteristics

### Most Efficient Modes (by cycles per byte):
1. **Const Copy:** 1 cycle/byte (+ init)
2. **Const Repeat:** 2 cycles/byte (+ init)
3. **Data Copy / Repeat Transfer:** 3 cycles/byte

### Best Use Cases:
- **Clearing memory:** Const Copy
- **Copying large blocks:** Data Copy
- **Streaming to hardware:** Repeat Transfer or Const Repeat
- **Pattern fills:** Const Copy

### Trade-offs:
- Init cycles add overhead for small transfers
- Larger S values increase pattern flexibility but add init time
- Channel conflicts can cause queueing delays
