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

## Folding the event cycle's PPU/display tick

On an event cycle `step()` ticks the PPU and display first, then does the CPU/APU/audio/timer work. When that event
is *pure* (DMA idle and no display edge on it: `quietCycles()` < its `displayQuiet` out-param) nothing between the
quiet stretch and that cycle's PPU/display tick can tell them apart, so `runCycles` ticks both together with the
stretch (`advanceQuiet(q, true)`: one `PPU::tickMany(q + 1)` entry instead of `tickMany(q)` plus `PPU::tick()`) and
runs only `tickRest()` (the part of `tickComponents()` after the DMA tick), `checkInterrupts()`, `updateTimers()`
for real. A display-edge cycle, or an event that coincides with one, still goes through `step()`. Took ~5% of
cycles off the boot-ROM verify workload; `test_batched` (50-frame checkpoints) stays identical.

## Catch-up scheduling (the CPU runs ahead of the PPU/display)

`runCycles` no longer ticks the PPU and display per CPU event. While nothing needs them in lockstep it runs the
CPU/APU/timer/audio events in order (`runAhead`) and lets the PPU and display fall behind (`videoLazy`,
`videoCycle` = how many master cycles they have been ticked through). They catch up (`syncVideo`) in long
`PPU::tickMany` stretches split only at display edges, in `step()`'s order (ppu, display, blank-status hand-off),
when:

- the CPU touches them: `readMapped`/`writeMapped` call `System::syncVideoForAccess()` for regions flagged
  `touchesVideo` (the PPU window and the PPU-control and display-status I/O blocks), which ticks them through the
  *current* cycle, where lockstep `step()` would already have ticked them before the instruction;
- `runCycles` returns (the frontends read the display between calls).

Lockstep (`lockstepEvent`, the older quiet-stretch + folded-event code above) still runs whenever DMA is active
(it ticks alongside and reads/writes PPU memory) or a blank IRQ is enabled (`setV/HBlankIRQEnabled`: a display edge
would have to raise a CPU IRQ on its exact cycle). The PPU's blank status is refreshed after any cycle the CPU
touched the video chips (a PPU reset in that instruction clears it) and whenever it is out of date at a sync.

Checked by `test_batched` (state hash incl. PPU/display, per-cycle vs batched) on the boot ROM with two
cartridges, plus `examples/boot-rom/sync-probe` (`tools/gen_sync_probe.py`): a boot ROM that races a spinning PPU
program and the display's pixel counter and logs what it sees. Syncing one cycle early fails it at the first
checkpoint. ~23% of cycles off the boot-ROM verify workload.

## Video worker thread

`System::setVideoThread(true)` (the frontends turn it on; `ZEROPOINT_VIDEO_THREAD=0` turns it off; ignored below 2
cores) moves the catch-up onto a second thread. The video chips (PPU, display) have exactly one owner at a time: the
worker while `vtDone < vtRequest`, the thread running `runCycles()` otherwise. `runAhead` hands the worker a stretch
(`publishVideo`, never past the cycle the CPU is at, so the worker cannot overtake an access) once at least
`VIDEO_CHUNK` cycles have built up since the last hand-off or inline sync; any CPU access (`syncVideo`) first takes
the chips back (`waitVideoIdle`, release/acquire on `vtDone`) and catches up inline to exactly its cycle, and
`leaveRunAhead` does the same at the end of `runCycles`. A CPU that polls the video chips more often than every
`VIDEO_CHUNK` cycles never hands the worker anything and runs exactly as without the thread.

Results do not depend on thread timing, only on that hand-off protocol: `test_batched ... --vthread` checks it
against the per-cycle reference (and a ThreadSanitizer build, `-fsanitize=thread`, of the same run is clean).
~1.4x wall-clock on the boot-ROM verify workload (worker ~92% PPU; the CPU thread is then the bottleneck).

## PPU: decoded-op cache and the instruction-granular `tickMany`

`PPU::executeInstruction()` no longer decodes with a `switch`: each instruction is decoded once, on first execution
from an address, into a `DecodedOp` (handler function pointer, the fields its encoding carries, base cycle cost),
cached per code address. The handlers (`opINC`, `opTILEDRAW`, ...) are the only definition of what an instruction
does, so `tick()`, `tickMany()` and `runBatch()` share them. Every path that writes PPU memory drops the cached
decodes covering the byte (`codeWritten()`: `writeMemory`, `pushReturnAddress`, SETPOS, MOVDP, `loadMicrocode`,
`reset`), so self-modifying code behaves as before.

`tickMany` runs one instruction at a time instead of one cycle: a stall is burned in one step
(`burnBlitCycles`, which still lets a blit complete in exactly its own cycle), pending interrupts are a local flag
(they can only come from the single edge latch at the start of the call or from before it), and the state is
rechecked after each instruction (a write to the VOC halt bit stops the PPU).

Checked by `tools/fuzz_ppu` (random programs with self-modifying writes, interrupts, blank-line changes and resets;
`tools/fuzz_ppu_check.sh` runs per-cycle `tick()` against `tickMany()` and against a build of an older PPU): 0
mismatches over 300+ seeds x 600 chunks. The fuzzer also found two out-of-bounds reads that made results depend on
ASLR (palette loads with DP near $FFFF, and an instruction fetch at $FFFF); both now wrap at 16 bits.

## CPU thread: burst loop and inline fetch

`runAhead` first calls `runCpuBurst`: it computes the first cycle of a non-CPU event once (APU instruction, timer,
audio sample) and runs CPU events back to back until then, rechecking after every event whether a CPU instruction
changed what is due (timer control, APU halt, DMA). The CPU/APU/audio work of a cycle is one shared function
(`cpuApuAudioCycle`) used by both the burst and `tickRest`. `CPU::fetch` is inline with a flat per-bank fast path
(`bankData`/`bankReadable`, which honours the Boot ROM's exemption from chunk gating). `nextAudioCycle` caches the
cycle of the next audio sample so the scheduler does not divide on every query.

## Not latching unseen rasters

`Display::setLatchEnabled(false)` skips copying finished scanlines into the frame frontends read (nothing in the
machine reads it). The frontends step ~13 rasters per displayed frame and need only one, chosen by
`FramePacer::shouldLatch` - not just "the last due": a tick that runs out of step budget stops early, and on a
slow host that would never latch (black screen). `test_latch_skip` checks frames and machine state are unchanged.

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
