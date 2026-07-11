# CPU-PPU-APU integration demo (alternate Boot ROM payload)

Runs entirely from bank $E0 (Boot ROM) with no cartridge: the CPU copies a
beam-synced PPU gradient program and a small APU "heartbeat" counter into
their respective memory windows, starts both, and then idles in an infinite
loop while the two coprocessors run independently in the background.

## Build

```sh
# From the ZPdevtools directory (needs the fixed apuasm - see apuasm.c's
# SCR/LDA-immediate and HLT encoding fixes):
./executables/ppuasm ppu-gradient.asm -o ppu-gradient.bin
./executables/apuasm apu-heartbeat.asm apu-heartbeat.bin
```

`orchestrator.asm` embeds the resulting bytes as `LDA #imm / STA long` pairs
directly (see the comments at the top of the file for why - `LDA label,X`
and `BRA label` both hit real cpuasm gaps/bugs). If you change either
sub-program, regenerate those `.byte`-style LDA/STA pairs from the new
`.bin` and splice them into `orchestrator.asm` in place of the existing ones
(a short Python script beats hand-editing 200+ lines).

```sh
./executables/cpuasm orchestrator.asm orchestrator.bin
```

## Run

```sh
./bin/zeropoint_sdl --boot orchestrator.bin
```

No `rom.rom` argument - `--boot` alone runs the Boot ROM with no cartridge
attached (`System::loadBootROM` + `System::reset()`, see `src/main.cpp`).

## What to expect

- A full-screen gradient (16-scanline beam-synced repaint via an H-blank
  ISR - see `ppu-gradient.asm`, which is `beam_gradient.asm` from
  `ZPdevtools/examples/ppu/`).
- The APU counts X from 0 to 60 and writes its progress to $1000 each pass,
  then halts - inspect via a debugger/test harness, there's no audio output
  in this version (see `apu-heartbeat.asm`).

## Known limitations (not fixed here, out of scope for this demo)

- `LDA <label>,X` has no non-long opcode and cpuasm won't auto-promote a
  label-indexed operand to the long form the way it does for numeric
  literals - so a generic byte-copy loop can't reference the embedded data
  tables by label. Worked around by fully unrolling the copies.
- `BRA <label>` emits only 1 operand byte (truncates the resolved address
  to its low byte) instead of the 2 bytes `opBRA()` (`fetch16`) expects.
  Worked around by using `JMP long <literal address>` for the idle loop.
- `JMP <label>` (non-long) resolves via the DB (data bank) register, not
  PB, in the interpreter (`addrAbsolute()`) - safe only if DB has been set
  to match the current bank, which nothing here does.
