# ZeroPoint Roadmap

Status as of **2026-11-21**. The emulator, the Boot ROM and the dev/debug tooling are built; what is left is mostly release
engineering, coverage and documentation. (The old phase plan, week estimates and checklist here described the project as it was in
July and had gone stale; the history is in `git log`. Day-to-day open items are in `TODO.md`, the architecture is in `CLAUDE.md`.)

## What is done

- **Hardware:** the DEF88186 CPU (all 256 opcodes), PPU (tiles, banks, the concurrent TILEDRAW blitter, line blitter, tile-row queue,
  layers/sprites microcode), APU (MMP mixer, stream port, ADPCM player, ZPA sequencer), DMA, timers, interrupt controller, BLAKE2s engine.
- **Boot:** a native (HLE) boot, and the real `ZPbootROM`: splash, jingle, RSA-2048 verification of v1/v2/v3 signed ROMs, chunk
  verification, math and PPU-load services, handoff, exception entries. Two images: retail (no dev code) and dev.
- **Dev unit:** a host-only switch with a debug serial UART, handler images for exceptions, dev-unit faults, and serial input
  recorded in movies. SDL and Qt frontends.
- **Movies and determinism:** `.zpm` record/replay with checkpoints, cross-host golden digests, fuzzers, sanitizer builds.
- **Toolchain (`ZPdevtools`):** assemblers and disassemblers for all three processors, the linker and signer, two C compilers, an SDK,
  and a rewritten documentation tree whose tables are generated and whose code blocks are assembled by a checker.
- **Platforms:** Windows, Linux, macOS on x64 and ARM64, all built by CI (green as of `ec2b89e`).
- **Compatibility rule:** the emulator is frozen at revision 13 behaviour. Revision 13 stretched the raster (a line is 4343 cycles, was 340; a
  frame is 1/60 s, was 1.3 ms) and deliberately broke everything before it, so ROMs, movies and golden hashes from revision 12 and earlier no
  longer replay; from here on, revision 13 ones must keep running identically. `tools/run_tests.sh` runs every suite.

## What a release needs

None of this exists yet.

1. **A release process.** No tags, GitHub releases, version number (only `CORE_REVISION`) or changelog exist. Decide the version
   scheme and what a release ships: emulator binaries per platform, the retail `bootrom.bin`, and the ZPdevtools bundle.
2. **A Boot ROM distribution decision.** `*.bin` is not tracked, so `bootrom.bin` has to be built and published. The dev image bypasses
   signatures, so decide whether it ships at all, and how it is named so nobody mistakes it for retail. Any movie pinning a Boot ROM
   by hash needs the file it was recorded with.
3. **CI coverage.** ZPbootROM and ZPdevtools have no CI. ZeroPoint's CI only builds: `test_serial`, `test_exception`,
   `test_qt_serial`, the handoff test and the determinism goldens do not run there (they need the sibling repos and the ROM builds).
   A release workflow that checks out all three repos, builds both Boot ROMs and runs the suites would close this.
4. **A clean test run.** `test_hle_cop` fails (pre-existing); fix it or waive it explicitly.
5. **Runtime checks on Windows and macOS** for the serial port, the Qt console and the exception flow.
6. **An adversarial review of the dev/retail separation** (the latch, the dev image, the exception table, the fault gating) before
   anything security-relevant is called secure.
7. **Docs:** a user manual and a getting-started guide ("hello world" from assembly to a signed ROM running on the emulator); this
   README and `docs/` are current, the manual does not exist.
8. **Examples:** a small demo collection for PPU, APU and CPU, and a complete game demo (`ZPdevtools/examples/` has the building blocks;
   the game template lives in its own repo).

## Beyond 1.0 (optional)

- **Hardware:** APU-initiated DMA or an APU-to-CPU interrupt (a new revision, needs a spec first), reverb and effects on the APU,
  more blend modes.
- **Tooling:** PPU and APU debugger views, save states, a guest-side serial monitor, assembler macros, compiler optimisation passes.
- **Targets:** JIT for ARMv8/7/6 (Switch, Vita, 3DS), PowerPC 32/64 (Wii/U, 360, PS3), RISC-V. The current "JIT" is the batched
  executor, not native codegen.
- **Community:** a program repository and sharing tools.
