# ZeroPoint TODO

Open items only. History is in `git log`; what the system is, is in `CLAUDE.md` and `docs/`. (This file used to be a dated changelog
of finished work, which had gone stale; it was cut down to what is actually open on 2026-11-21.)

## Known bugs

- **`tools/test_hle_cop.sh` fails.** The native (HLE) Boot ROM and the real one disagree on `COP #$F7`'s cycle cost (1944 vs 1801) and on
  the PPU line-blit status byte `$034A`. It fails the same way on the revision-10 emulator with the old Boot ROM, so it predates the dev
  unit work. Fix the HLE or the test, but not the emulation (frozen).
- **ARM64 JIT under QEMU** crashes (instruction cache flush); fine on real ARM64 hardware. Use the interpreter under QEMU.

## Dev unit and exceptions (built; loose ends)

- **The native boot has no exception entries.** A cartridge that sends an exception to `$E0:0048-$5B` prints one stderr line and halts
  (`System::reportHleException`). Needs the real dev Boot ROM (`--boot`) for a handler image.
- **`COP #$FF` chunk verification still checks data on a dev unit**, which sits oddly with "signatures always pass".
- **Qt has no `--boot` flag**: its Boot ROM path comes from the settings dialog, so `--exception-handler` was never exercised end to end
  there. The core and the SDL frontend are tested.
- **Dev-unit faults:** an instruction fetch from unmapped memory still just reads `$FF` and halts; handler images arrive at 192 bytes
  per 60 Hz tick, so very large ones load slowly.
- **A guest-side monitor** (peek, poke, step over the UART) is designed in `docs/debug-protocol.md` but not built; the handler-image
  mechanism could carry it.
- **Windows and macOS runtime** is untested for the serial TCP endpoint, the Qt Serial Console and the exception flow (they compile
  on every CI platform).

## Boot ROM

- **The APU is not reset at handoff** (known flaw, kept on purpose; `docs/boot-handoff.md`): a cartridge must reset-halt it before
  loading its own program.
- **`ZPbootROM/def88186/load_tiles.def` is generated and tracked**, and `build.sh` rewrites it with whichever variant (retail or dev)
  it built last, so the tracked copy flips. Stop tracking it or generate it into `build/`.
- **No crash screen or jingle on a lock-up** (a retail lock stops the music and leaves the splash animating). Left to individual games
  on purpose; a Boot ROM version is possible.

## Ideas, not started

- **APU-initiated DMA or an APU-to-CPU interrupt.** The APU's only link out is the `$1800` mailbox; the CPU's DMA can reach into the
  APU window but the APU cannot start anything. A new emulator revision: needs a spec and a decision first.
- **PPU and APU debugger views** beyond the CLI debugger (`bin/debugger`).
- **Save states.**
- **JIT for more architectures** (ARMv8/7/6, PPC32/64, RISC-V). The "JIT" is the batched executor, not native codegen.

## Release readiness

See `ROADMAP.md`: tag and release process, CI coverage for ZPbootROM and ZPdevtools and for the serial/exception tests, user manual,
and an adversarial review of the dev/retail separation.
