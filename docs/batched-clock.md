# Batched master clock (`System::runCycles`)

`System::step()` runs one 68 MHz master cycle: PPU tick, display tick, DMA (odd cycles), CPU and APU budget
accrual and instructions, blank-line interrupt edges, timers, the audio sampler. Almost every cycle is
"quiet": only the PPU ticks and the raster's pixel counter moves. `System::runCycles(n)` (and so `stepFrame()`,
which both frontends use) finds the next cycle where anything else can happen, applies the quiet cycles before
it in bulk, and runs that one cycle through the unchanged `step()`. It is **state-identical** to calling
`step()` n times; `step()` stays the reference.

An event is: a CPU or APU budget accrual (every 4th / 16th cycle, or an instruction due now), DMA not idle,
a timer due, an audio sample due, or a display edge (H-blank at pixel 256, end of line). Quiet cycles are
`PPU::tickMany(n)` (n ticks with multi-cycle stalls collapsed, blits finishing in their own cycle),
`Display::advanceQuiet(n)` (pixel counter only) and the audio accumulator. The PPU only reads the display's
scanline, render mode and framebuffer, never the pixel counter, which is why a bulk-advanced display is safe.

PPU blits are timed by absolute completion time (`blitClock`/`completeAt`/`blitNextDone`) instead of per-channel
countdowns, so a cycle costs one compare; completion order (channel index order within a cycle) is unchanged.

## Checking it

`bin/test_batched <bootrom.bin|none> <rom.zpb> <seconds> [checkpoint-frames] [--dump]` runs a ROM both ways and
hashes the whole machine (CPU, WRAM, `PPU::stateHash()`, `Display::stateHash()`, APU, every audio sample) at
checkpoints; it fails at the first difference and prints both modes' wall time. `--dump` prints just the
per-cycle hashes, to diff against an older build's (done once against the pre-refactor tree: identical over
8 s of boot, fade, handoff and PPU load). Any change to PPU/Display/System timing should keep both green.

## Budget accruals in bulk

The CPU/APU run an instruction on the cycle their budget first goes positive; budget accrues by 1 every
4th/16th cycle and an instruction debits its cost. So after an instruction the accrual cycles up to the next
one do nothing but count. `quietCycles()` jumps straight to the cycle the next instruction is due and
`advanceQuiet()` adds the skipped accruals (`(c+n)/4 - c/4`) to the budget. This took the event count from
~cost per CPU instruction (and ~4 per APU instruction) down to 1, and was the largest single win.

## Where the time goes now

Measured (non-LTO, perf), CPU time, state-identical to per-cycle stepping on all three:

| workload | per-cycle | batched | |
|---|---|---|---|
| boot ROM + splash, CPU hashing (8 s) | 0.56x real time | 1.03x | 1.83x |
| cartridge loading a PPU program (8 s) | 0.52x | 0.98x | 1.88x |
| PPU idle, CPU spinning, APU engine (10 s) | 1.05x | 2.99x | 2.85x |

With a busy PPU the profile is now: PPU interpreter ~30% (`tickMany`, `executeInstruction`, `executePresetF`),
blits ~24% (`executeBlit`, `Display::setPixelRow32`), scheduler ~14%, the rest CPU and display. The PPU still
needs ~10 ns per PPU cycle against 68 M cycles/s, so a busy PPU alone costs about 0.7 s per console second. A
3x-headroom target needs the PPU at ~4 ns/cycle: a PPU JIT/fused-block executor on this scheduler (safe inside a
quiet stretch: nothing can raise an interrupt in the middle of it), and cheaper blit pixel writes.
