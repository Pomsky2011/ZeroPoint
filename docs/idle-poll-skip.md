# Idle-poll skip

Status: design + determinism harness + phases 1-2 (`include/idle_detect.h`, `src/idle_detect.cpp`, `System::trySkipIdle`). `System::setIdlePollSkip(true)`
(frontends: on by default, `--no-idle-skip` / `ZEROPOINT_IDLE_SKIP=0` turn it off) fast-forwards loops that only poll ROM, work RAM, controller
ports or `$D80051`; PPU/APU-memory polls are still run (phase 3). `System::idleStats()` reports what was found and skipped.
Harness: `bin/fuzz_idle_skip` (`tools/fuzz_idle_skip.cpp`).

## Why

Firmware waits for the PPU/timers by spinning: the layers demo's `wait_frame` is `lda $B01E04 / beq`, its `tick_wait`
`lda $D80051 / and #$80 / beq`. Emulating that costs twice:

- **CPU time.** 73M guest reads per 3000 demo frames, nearly all in those loops (about 25% of a single-threaded run).
- **Serialisation.** A read of the PPU window must see the PPU as of exactly that cycle, so every poll is
  `syncVideoForAccess()`: wait for the video worker to go idle, then catch the PPU up inline. Measured with the video
  thread on (powersave, 1.8 GHz): 32.7k syncs per wall second, the runner waiting 54% of wall time in
  `waitVideoIdle`, the CPU thread at 31% of a core and the worker at 70%, 22 fps - no better than the single thread.

Removing the polling removes both. It does not by itself make 60 fps: the video worker is then the limit (about 1.84 core-seconds per console second
in that mode), so it is the first of two steps (the other is the PPU's own cost).

## The contract

For every master cycle at which `runCycles()` may stop, the whole machine (`System::stateHashParts()`: CPU, clock,
PPU, APU, display, DMA, timers) is identical to the machine with the skip off, and to `System::step()` run one cycle
at a time. Not "equivalent when the loop ends": a slice boundary, a host event or a movie checkpoint can fall in the
middle of a skip, so the skip has to be able to materialise the exact state of any cycle inside it.
No new hidden state: whatever the skip remembers is derived and dropped whenever anything it depends on changes.

## What may be skipped

A loop is skippable when one iteration is a pure function of values the loop reads, and it leaves the CPU where it started:

1. **Closed.** After each iteration the CPU state (PC, PB, A/X/Y, SP, D, DB, P) equals the state before it, and the
   iteration took the same number of master cycles `T` as the one before.
2. **Read-only.** It executes no store, push/pull, interrupt entry or DMA/APU/PPU control access, and every load it
   makes is from a *watchable* address (below). The loop's own code bytes must not change: ROM bytes below
   `gatedCodeSize` are fixed while a cartridge stays inserted; code in RAM is only skippable if writes to its bytes
   invalidate it.
3. **Stable.** Every load returned the same value in the last two iterations.

Detection is dynamic: watch backward branches, keep the CPU state signature, and require two equal iterations.
Nothing is decided from opcode patterns, so `LDA/BEQ`, `LDA/AND/BEQ`, `LDA/CMP/BEQ`, padded loops and two-address
loops all fall out of the same rule.

## Watchable addresses

A polled address is watchable if everything that can change its value can be timestamped.

| Address | Value changes when | v1 |
|---|---|---|
| Timer status `$D80051`, IRQ status `$D80058` | A timer/IRQ source fires: `nextTimerEventCycle` is known exactly | yes |
| Work RAM, APU window RAM | A host write, a DMA write, or a CPU store: all go through `writeByte`/DMA, which can raise the watch | yes |
| Controller ports `$D80030-32/38-3A` | The frontend/movie sets input between `runCycles()` calls | yes |
| PPU memory that is not a port or the framebuffer | The PPU program, a DMA or the host writes it (one write path to hook) | yes |
| PPU ports (`$00E0` scanline, `$00F0-FF` VOC, `$034A` blit status, `$0360/$0364` queue count/sticky bits) | Derived from live PPU/display state: scanline edges, blit completions, queue steps | no, until each has a derivation |
| Framebuffer `$E000-$FFFF` | Blits retire into it at known PPU cycles and the window clears slots as it slides | no |

`PPU::handleMemoryRead` is `const`, so a PPU read has no side effects, which is what makes polling it repeatable. Reads with side effects (none known today) must never be skippable.

## When a skip ends

The skip ends at the first cycle at which any of these can happen; the loop is then resumed instruction by instruction.

- A watched value changes. The CPU resumes at the first poll whose *read cycle* is at or after the change, so it
  reads the new value at exactly the cycle the per-cycle machine would have.
- An interrupt would be taken at an instruction boundary (IRQ pending and `I` clear, NMI, abort). Timers give the cycle
  exactly; blank IRQs keep the existing lockstep path, which is never skipped.
- DMA becomes active, the APU's clock mode changes (fast mode adds a CPU cycle to APU-window reads, so `T` changes),
  or anything resets, ejects, inserts or reloads a program (the loop's code or the watch may no longer be what it was).
- The run stops. `runCycles(n)` ending inside a skip materialises the cycle `target`: advance the iteration count by
  `floor((target - c0) / T)` in bulk (cycle and instruction counters), then execute the remainder, less than one
  iteration, normally.

Things that keep running in the normal order while the CPU is parked: APU instructions, timer events, audio sample
accrual, DMA. The skip only elides CPU instructions that would have produced no change.

## The PPU case (the part that removes the serialisation)

For a PPU-memory poll the skip cannot know in advance when the PPU will write the address. So the CPU *parks*:
it hands the video chips to the worker and does not touch them. Parked, nothing can write PPU memory from the CPU side,
so the worker may run freely, and `PPU` raises a one-shot watch hit (with its master cycle) the first time the watched
byte changes (hooked at the PPU's memory write paths: instruction stores, window writes, DMA). The CPU then resumes at the
first poll read cycle at or after the hit. No per-poll handoff, so the worker is no longer starved by the runner.
The hit cycle is the PPU's own cycle count, the same clock `syncVideoForAccess()` orders accesses by, so "a poll at
cycle c sees every PPU effect of cycles up to c" stays the rule.

## Hazards to design against

- **Budget arithmetic.** The CPU runs on `cpuCycleBudget`, which accrues on `c % 4 == 3`, and the APU on its own period
  (8 or 16 cycles); materialising a skip has to leave both budgets and `masterCycleCount` exactly where per-cycle
  stepping would. The harness stops runs a cycle or two before every event specifically to land on those edges.
- **Blank-status hand-off.** After a CPU video access the PPU is told the display's blank lines (`setBlankStatus`) and
  otherwise `tickVideoTo` falls back to single ticks while it is stale. Dropping accesses must not change when the PPU's view
  of them is refreshed. The state hashes cover the PPU's latches, so this is tested, not assumed.
- **Stale detection.** Every input of a skip (loop code, mapping, gating limits `bankUngated`, watch) is derived state.
  The gated-read work showed what an unrebuilt derived limit costs: its differential test only caught the
  missing rebuild once a case with no metadata region was added. Expect the same shape of bug here.
- **Hash parity across threads.** The video worker timestamps hits; nothing the worker does may depend on wall-clock
  timing, only on the cycle count it was asked to reach.

## Plan

0. Switch + harness (done): `fuzz_idle_skip` runs skip on/off/per-cycle, sliced, with the worker thread.
1. Detector only: recognise closed, read-only, stable loops and count how many cycles they would have skipped;
   a self-check mode replays each recognised loop per-cycle and asserts the prediction.
2. Skip non-video polls (timer status, RAM, controller). The wake time is exact; no worker involvement.
3. PPU-memory polls with the watch and parking.
4. Per-port derivations (`$00E0`, `$034A`, queue registers), only where each has an exact one.

CORE_REVISION does not change: the machine is bit-identical, only faster.

## Phase 1 result (2026-11-18)

`CPU::setAccessLog` records every `readByte`/`writeByte` while a candidate loop (a taken backward branch) is tracked; two identical iterations (same
CPU signature incl. hidden loop counters/latched IRQs, same cycle cost, same loads, no writes) confirm it. A confirmed loop ends by a changed byte,
an interrupt (a store/push, or an IRQ latching while `I` is set), cycle drift, or "unexplained" (every load equal: the self-check, must be 0).
- Layers demo, 6000 frames: **99% of guest CPU cycles are in confirmed loops**: 63.6M of 66.6M in `$D80051` timer polls, 2.6M in `$B01E04`
  PPU-memory polls, nothing unwatchable, 0 unexplained. (`ZP_IDLE_STATS=1 bin/dump_splash none <cart> OUT <frame>`.)
- `fuzz_idle_skip` prints the detector totals over its skip runs and fails if `unexplained > 0`; a mutant that stops logging PPU-window loads is caught.
- Machine state is untouched (demo md5, goldens, fuzz all identical). Not yet measured: the cost of the one `accessLog` branch in `readByte`/`writeByte`.

## Phase 2 (2026-11-18): how the skip is exact

At the first instruction of a fresh iteration of a confirmed loop, `trySkipIdle()` runs k whole iterations as *one CPU instruction of k*T cycles
at this very cycle* (`cpuCycleBudget -= k*T`, `cycleCount += k*T`, `instructionCount += k*I`); APU, timers, audio and video run as always. Why that is
the per-cycle machine: an instruction starts on an accrual cycle (`c % 4 == 3`) with a budget of exactly 1, and one of n cycles makes the next due
4n cycles later, so iteration j starts at `c + (j-1)*4T` and the k-th ends where the budget refills. Both are checked (`(c & 3) == 3`, budget == 1)
rather than assumed. k is the most iterations that end by `min(nextTimerEventCycle, end of this runCycles)`:
- the timer status is set at the *end* of its event cycle, which is also the only thing that can latch an IRQ, so no load or interrupt boundary
  falls after a change; stopping at the run's end means no stop ever sees the CPU ahead;
- the reference loads must be fresh: a timer that fired during the last *real* iteration, after the loop's load, would make the next iteration differ.
  `lastTimerFireCycle >= validSince()` refuses the skip (found by `fuzz_idle_skip` seed 46, which the first version failed);
- only in `runCpuBurst` (run-ahead: DMA idle, no blank IRQs), never with a deliverable interrupt; the detector is dropped at every `runCycles()`
  entry because the host may have changed inputs/RAM/ROM between calls;
- loads must be ROM (ungated), work RAM, controller ports or `$D80051`. `$D80058` is `Other` until its V/H pending bits are understood; APU RAM and
  PPU memory wait for phase 3.
Result on the layers demo (6000 frames): 127M of 133M guest CPU cycles skipped (95%), same frame md5, single-thread user time 16.6s -> 13.9s (-16%).
With the video thread on (powersave) the frontend gains only ~4% (25 -> 26 fps): the worker, not the CPU, is the limit, which is phase 3 and the PPU scheduler.
A planted mutant (k one too high) fails on seed 1 within 5 stops.

## Decisions (2026-11-18)

- **Switch:** frontend-side, default on (`--no-idle-skip`, plus an env var), wired to `setIdlePollSkip()`. The machine is bit-identical either way.
- **PPU write hook:** route every PPU `memory[]` store (`opSETBYTE`/`MOV`/window/DMA) through one funnel first, then hook only that.
- **Code in RAM:** never skipped. Only loops whose code is in cartridge ROM below `gatedCodeSize` are eligible (so no code-byte invalidation is needed).

## The harness

`bin/fuzz_idle_skip <workdir> [first-seed] [count] [max-frames]` (`--seed N` for one verbose run). Each seed assembles a
guest from 1-4 polling loops (spin while zero / while nonzero / while equal to a variable / padded / two polls) on PPU
memory, PPU ports, the framebuffer, timer status, work RAM, controller ports, APU RAM and IRQ status, with the exit
actions chained into the next loop, and optionally an IRQ handler. A schedule of host events at exact master cycles pokes the
polled addresses, the timers, inputs, DMA configs, PPU/APU programs, and rarely yanks, swaps or resets the cartridge.
Four runs must match `stateHashParts()` at every event, a cycle or two before it and at random cycles in between:
reference, per-cycle `step()`, skip on in random slices, skip on with the video worker thread. It reports how often
the reference CPU was inside a loop at a stop (about 70%), so a generator that stops producing idle time shows.

Checked that it can fail: a mutant "skip" that loses one cycle depending on where a slice starts is flagged on every seed
within the first few stops, in both skip modes and not in the per-cycle run (`clock`/`cpu` parts differ).
