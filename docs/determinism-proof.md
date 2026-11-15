# Why a TAS can't desync: the determinism argument

This is a proof **modulo premises**, not a machine-checked proof. Each premise is marked with how it is established:

- **checked** - a tool fails the build/test if it's false, for all code (static analysis, compile-time asserts);
- **tested** - exercised by tests/fuzzing/sanitizers: strong evidence, covers the paths run, not a proof for all paths;
- **assumed** - nothing in this repo establishes it.

Honest summary: P2-P6 are *checked*, P7-P9 are *tested*, and P1 (no undefined behaviour reachable by a hostile guest) is
*tested but not proven*, which is the one gap between this and a real proof. A fully machine-checked proof would need
the core in a verifiable subset (Frama-C/CBMC-class tooling, none installed here) or a rewrite; see the end.

## 1. The claim

Let the **input** be: the cartridge image, the boot ROM image and APU BIOS image (or "use the HLE ones"), the flags
(`verifySignatures`, dev mode), and a **host event schedule** `E = [(c_i, a_i)]` where `c_i` is a master-cycle count
and `a_i` is a controller state change (either port), a bus write, a program load, a soft or hard reset, a cartridge yank or
a Stop 'n' Swop, applied by the host between `runCycles` calls at exactly cycle `c_i`. (A TAS is an `E` made only of
controller changes and those four console actions; `docs/movies.md` is the file format and the replay driver.)

**Theorem.** For every master cycle `n`, the machine state `S(n)` - every byte of RAM/VRAM/APU memory, every register,
every scheduler counter, and the audio samples and latched video frames produced so far - is a function of
`(input, n)` alone. In particular it does not depend on: how the run is cut into `runCycles`/`step` calls, whether the
video worker thread is on, the number of cores, the contents of uninitialised host memory, other `System`s in the
process, the wall clock, the environment, the host OS/CPU/compiler (given P2), or the order events were issued within
one cycle boundary (they are applied in schedule order).

## 2. Proof sketch

Take the **reference semantics** `δ`: one call of `System::step()` advances master cycle `m` to `m+1` by
`tickComponents()` (PPU, display, DMA, CPU/APU/audio, interrupts, timers, in that fixed order). Everything else
(`runCycles`, run-ahead, quiet-cycle batching, the video worker) is an optimisation that must equal repeated `δ`.

1. *`δ` is a function of `(S, input)`.* It is straight-line C++ over members of `System` and the host event applied at
   that boundary. By P2-P6 nothing else can influence a value it computes: no uninitialised read (P3), no hidden
   state (P4), no clock/RNG/environment/pointer value/hash order (P5), no floating point (P6), no
   implementation-defined behaviour that varies by host (P2); and by P1 the program has defined behaviour at all.
2. *`S(n+1) = δ(S(n), a)`* where `a` is the event (if any) scheduled at boundary `n`, hence by induction on `n`,
   `S(n)` is a function of the input. This is the whole theorem for the reference semantics.
3. *The optimised paths equal the reference* (P7, P8): `runCycles(k)` is defined to leave exactly the state `k` calls of
   `δ` would; slicing a run into `k1 + k2` therefore changes nothing, and the video worker computes the same PPU/display
   ticks the runner would, in the same order, handing the chips back before anything reads them.

## 3. The premises

| # | Premise | How | Status |
|---|---|---|---|
| **P1** | No undefined behaviour is reachable by a guest (a ROM and event schedule can't make the C++ do something the standard leaves undefined: out-of-bounds, signed overflow, bad shift, invalid enum, ...) | `fuzz_determinism` under clang `-fsanitize=address,undefined` with random programs / MMIO / DMA / PPU+APU programs (it found one: PPU `MUL` was signed `int` overflow via integer promotion, fixed; 240 further seeds clean); `test_determinism` | **tested, not proven** |
| **P2** | The implementation-defined facts the arithmetic relies on hold (8-bit char, 16/32/64-bit short/int/long long, arithmetic `>>` on negatives, two's-complement conversions, little-endian) | `include/determinism_assumptions.h` `static_assert`s fail the build otherwise | **checked** |
| **P3** | Every piece of state is initialised before use, independent of what was in memory | `clang-tidy cppcoreguidelines-pro-type-member-init,init-variables,clang-analyzer-core.uninitialized.*` clean on every core TU (member default initialisers everywhere); `test_determinism`/`fuzz_determinism` build the `System` in memory pre-filled with `A5` and `5A` and compare | **checked** (static) + tested |
| **P4** | No hidden state outside `System`: no non-const static/global/thread-local variable in the core, no process-wide "current system" | `tools/determinism_audit.py` (clang AST: every non-const `static`/namespace-scope `VarDecl` in the core files); positive control: an injected static is reported | **checked** |
| **P5** | Nothing reads the wall clock, RNG, environment, pointer values, hash-ordered containers, host topology | same script, token pass over the core, with an explicit exception list (each with a reason it can't reach state) | **checked** |
| **P6** | No floating point in the core (host rounding / FMA contraction / libm) | same script; the APU mixer's cubic and the HLE trig tables are integer/constants now | **checked** |
| **P7** | The video worker thread is race-free and gives the same result as running the chips inline | single-owner handoff protocol (`system.h`, `System::syncVideo`, release/acquire `vtDone`); `-fsanitize=thread` clean on `test_determinism` and `fuzz_determinism`; every fuzz seed is run with and without it | **tested** |
| **P8** | `runCycles`/run-ahead/batching/quiet-cycle skipping equal `step()` for every slicing | `test_batched` (per-cycle vs batched hashes), `test_determinism`, and `fuzz_determinism` (random slices from 1 cycle up, not aligned to events or frames, random event schedules) | **tested** |
| **P9** | Host events are applied at exact cycle boundaries and are plain stores/calls | `fuzz_determinism` schedules events at random exact cycles and compares every checkpoint after each event | **tested** |

Cross-host: `tools/determinism_golden.txt` holds the digest of every checkpoint of 24 fixed fuzz seeds. The digests
are integer arithmetic over emulated state, so they must be the same on every platform; the CI build jobs
(Linux x86-64, Linux ARM64 under QEMU, macOS x86-64 and ARM64) run `fuzz_determinism --golden-check` after building.
Windows isn't in CI for this yet, and the CI steps themselves have not run on those platforms yet (added blind, see
the commit). Verified locally: the 24 digests are identical from the GCC release build and from a clang
`-fsanitize=thread -O1` build (different compiler, optimiser and instrumentation), and a plain clang release build
agrees with GCC on the seeds I compared.

## 4. What still can desync a TAS (outside the theorem)

- **A different core.** Any change to emulation changes `S(n)`. A TAS must be pinned to a core version and to the
  hashes of the ROM, boot ROM and APU BIOS images (`--boot`/`--apu-bios`/HLE are different initial conditions: HLE
  boot skips the splash and jingle, so cycle counts differ from LLE by design). `determinism_golden.txt` catches an
  accidental behaviour change in CI; refresh it deliberately when emulation is meant to change.
- **Input timing in a frontend.** The core is deterministic given `E`. `MovieSession` records each input at the movie cycle
  the frontend applied it (once per 60 Hz poll) and replays it at exactly that cycle, so a recording replays on any host; what
  a *new* TAS tool does to author `E` is its own business, but it can't produce a movie that replays differently than it plays.
- **Savestates.** There are none, so none can corrupt anything. When added, they must serialise everything
  `stateHash` covers *and* the scheduler/thread bookkeeping, and `stateHash` is the test (save, load, run, compare).
- **UB the fuzzer hasn't found (P1).** See below.
- **Unhashed state.** `stateHash` lists members by hand: a member added later but not hashed would not show up in the
  tests (it still can't be uninitialised: P3 is checked statically).

## 5. What a real proof would take

P1 is the gap. Options, in increasing cost: (a) longer/coverage-guided fuzzing (libFuzzer on the guest surface) and
more sanitizer builds (MSan needs an instrumented libc++); (b) bounded model checking of each instruction handler
(CBMC) for UB freedom over all operand values - the handlers are small and table-driven, so this is feasible per
handler but not installed here; (c) a deductive proof of the scheduler equivalence (P8) for `runCycles` vs `step`, which
needs a formal model of the C++ (Frama-C/WP or a rewrite of the scheduler in a verifiable subset); (d) rewriting the
core in a language with defined behaviour by construction (Rust without `unsafe`), which removes P1 and half of
P2-P6 as obligations. None of this is done here.
