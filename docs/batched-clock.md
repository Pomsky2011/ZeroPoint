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

## Where the time goes now

Measured (non-LTO, perf): with a busy PPU, ~70% of host time is the PPU interpreter (~10-14 ns per PPU
cycle including blits; the console needs 68 M of them per second); with the PPU idle and the CPU spinning,
~70% is scheduler overhead per CPU instruction (an event every ~12 cycles). So the batching alone is worth
~1.4x (boot workload) to ~1.8x (idle PPU). A 3x-headroom target needs the PPU at ~4 ns/cycle, i.e. a PPU JIT
sitting on this scheduler; and a catch-up scheduler (run the CPU ahead until it touches PPU-visible state) for
CPU-heavy games.
