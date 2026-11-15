# Determinism

The machine is meant to be bit-for-bit deterministic: the same ROM + the same input stream gives the same state
(every RAM/VRAM/APU byte, every register) and the same audio and video at every master cycle, on any host, with or
without the video thread, per-cycle or batched, whatever was in the host's memory beforehand, and however many other
`System`s exist in the process. `bin/test_determinism` checks that.

```
test_determinism <bootrom.bin|none> <rom.zpb> <console-seconds> [checkpoint-frames] [--no-verify]
```

One baseline run, then the same cartridge: built into memory pre-filled with `A5` and with `5A` (placement-new, so
any member the constructors/`reset()` miss keeps the pattern), again in the same process after the first (static
leaks), stepped with `step()` per master cycle, with the video worker thread, and interleaved with a second live
`System`. Compared via `System::stateHashParts()` (cpu, ppu, display, apu, dma, timers+irq, clock, audio), so a failure
names the culprit. Plus a latch probe: the $D8 write-sequence latches must be clean after `reset()`, after a dirty
`System` is destroyed, and not shared between live `System`s.

## What this found (and fixed)

- Function-level `static`s in `CPU::setupIORegisters`: the DMA-config write index/buffer, the PPU-register select
  latch, the P2 serial queues (the TX head only advances, so after 255 writes `P2_STATUS` changed for the rest of
  the process) and the DEV_MODE flag. Shared by every `System`, never cleared by `reset()`. Now `CPU` members;
  `reset()` clears the hardware latches (DEV_MODE is the emulator's own setting and survives).
- `System::currentSystem`, the static the DMA callbacks went through: two `System`s alive at once had the older one's
  DMA land in the newer one, and it dangled after a `System` was destroyed. The DMA callbacks now carry a context
  pointer.
- `DMAConfig`/`DMATransfer::patternBuffer` of idle channels were never initialised. Only a clang/ASan build of the
  test saw it (the GCC -O2 build happened to match), so run both: the sanitizer builds are the more sensitive ones.
- Floating point in the APU mixer: `cubicInterpolate` was single-precision `float`. FMA contraction (always on for
  ARM64) or a different libm can flip its truncation by 1 LSB; measured 63 vs 42 mismatches per 20 M samples between
  an FMA and a non-FMA build. Now exact integer math (differs from the old output by <= 1 LSB, ~3 samples per million).
- The HLE Boot ROM's trig tables were built with libm at startup; now generated constants (`src/boot_hle/trig_tables.h`,
  `tools/gen_hle_trig_tables.py`).
- No other `float`/`double` is left in the core (`grep` the sources); `isqrt64` uses a double sqrt but corrects it to
  the exact integer result.

## What it does not cover

- Input. The core is deterministic *given* the controller state; a live frontend feeds it from the keyboard at
  wall-clock times, and `FramePacer` decides how many frames to step from the wall clock. A replay needs an input
  log keyed by frame, which doesn't exist yet.
- Workload coverage: the cartridges used (`ZPgameTemplate`, ZPbootROM's `withppu`, the `darkness_falls` APU score)
  exercise the boot ROM, DMA, PPU programs, the sequencer and the mixer, but not everything (the MMP stream port,
  TILEDRAW-heavy scenes, every IRQ source, timers in anger).
- The `stateHash` functions list their members by hand. A member added later is *not* hashed until it is added to
  `CPU/APU/DMAController::stateHash` and `System::stateHashParts`. MSan can't back this up here (uninstrumented
  libstdc++ makes it report false positives), hence the fill-pattern trick.
- Only x86-64 Linux was run. The ARM64/Windows/macOS CI builds are not checked for identical hashes.

Sanitizer builds (scratch dir, clang): `cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -O1 -g"`
(and `-fsanitize=thread` for the video thread), target `test_determinism`.
