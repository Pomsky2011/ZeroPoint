# CPU-PPU-APU integration demo (alternate Boot ROM payload)

Runs entirely from bank $E0 (Boot ROM) with no cartridge: the CPU copies a
beam-synced PPU color-bar program and a small APU "heartbeat" counter into
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

- A colored vertical bar along the left 16 pixels of the screen, one shade
  per scanline (`ppu-gradient.asm`, beam-synced via an H-blank ISR).
- The APU counts X from 0 to 60 and writes its progress to $1000 each pass,
  then halts - inspect via a debugger/test harness, there's no audio output
  in this version (see `apu-heartbeat.asm`).

## Why only a 16px bar, not a full-screen gradient

`ppu-gradient.asm` started as `ZPdevtools/examples/ppu/beam_gradient.asm`,
whose H-blank ISR repaints all 256 pixels of a scanline (16x-unrolled inner
loop, repeated 16 times) on every H-blank. That's ~1024 cycles of MOVDP/ADD
alone under this PPU's real per-instruction costs (`CYC_MEM=3`,
`CYC_BASE=1`) - but a scanline (one H-blank period) is only
`TOTAL_PIXELS_PER_LINE`=340 master cycles. The ISR never finishes a single
pass before the next H-blank preempts it and re-enters it from the top,
leaking 2 bytes of PPU stack (SP) every interrupt - confirmed via
cycle-by-cycle tracing (X frozen at 1, SP climbing +2 every H-blank, never
-2). After ~60 frames SP/DP wrap and corrupt the render: a brief flash of
the first partial paint, then black - reproduced identically in the real
`zeropoint_sdl --boot` run before this fix.

`ppu-gradient.asm` here is the *same* setup/slot-computation logic but does
one pass of the 16-pixel unrolled block (no outer INC-X/CMP/JNZ repeat) -
~113 cycles measured, comfortably under 340. Verified stable for 600 frames
(10s) with `SP` pinned at a constant value the whole time (no leak). Fixing
`beam_gradient.asm` itself (or the PPU's interrupt re-entry protection so
an overrunning ISR can't leak) is a separate, out-of-scope task - flagged,
not touched.

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
