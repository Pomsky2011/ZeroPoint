# ZeroPoint

A fantasy console emulator featuring a tile-based PPU with microcode programming and an 8-bit audio processor.

## Overview

ZeroPoint is a fantasy console with custom programmable graphics (PPU) and audio (APU) processors. Unlike traditional fixed-function hardware, both processors run custom instruction sets that give developers full control over rendering and sound.

## The Story

**This story is not AI-written, unlike the emulator code and dev tools.**
- In 1992, PowerGame developed and released the ZeroPoint, which was the most powerful console of its
- generation. However, because of its late start and difficult development cycle, it sold very badly and
- was discontinued in only 1994 once it was no longer the most powerful system on the market. PowerGame
- then went out of business, but their hardware documentation remained and some Boot ROM dumps did too,
- so once those were ultimately leaked, people began making emulators and dev chains. Very few
- functional systems remain though... Will you be able to program the system?

## Features

### System Clock Synchronization
- **Master Clock**: 68.011355 MHz (NTSC colorburst x19, PPU pixel clock)
- **Integer-based**: Deterministic 16-cycle execution pattern
- **PPU**: 68.011355 MHz (every cycle) - Graphics microcode
- **DMA**: 34.005678 MHz (every 2 cycles) - Memory transfers
- **CPU**: 17.002839 MHz (every 4 cycles) - System master
- **APU**: 4.250710 MHz (every 16 cycles) - Audio synthesis
- **Display**: 68.011355 MHz (every cycle) - Video output

### DEF88186 Main CPU ✨
- **Hybrid 65C816/8086 16-bit processor** @ 17.002839 MHz
- **ALL 256 opcodes implemented** - Production ready!
- **24-bit addressing** - 16 MB addressable (256 banks × 64 KB)
- **8/16-bit modes** - Configurable via M and X flags
- **Hardware multiply/divide** - 8-13 cycles (vs 120+ software)
- **Hardware loops** - LOOP/LPEND instructions
- **14 addressing modes** - Immediate, absolute, direct page, indirect, indexed, stack-relative
- **Block moves** - MVN/MVP for fast memory copying
- **BCD decimal mode** - For financial calculations
- **Interrupts** - BRK, COP, IRQ, NMI, RTI
- **Full 65816 compatibility** + 8086-inspired extensions
- Interrupt vectors are read from the cartridge (`$00:FFE4-$FFFE`); the Boot ROM adds fixed exception entry points (see Dev unit below)

### PPU (Picture Processing Unit)
- **Microcode-based graphics processor** @ 68.011355 MHz
- **1 instruction per cycle** - 68.0 million instructions/second
- **35 instructions** (15 basic + 16 extended Preset F + 4 extended Preset E)
- **256×256 display** with dual color modes (16-bit/32-bit RGBA)
- **Rolling framebuffer** - 8 banks × 1 KiB with H-Blank rotation ✨ **FULLY FUNCTIONAL**
- **NTSC timing** - Authentic 60 Hz display at 5.3 MHz pixel clock
- **Video Output Coprocessor (VOC)** - 16 hardware registers for display control
- **Tile system** - 8×8 pixel tiles, 256 tiles per bank × 4 banks (`SETTILEBANK`), DIY placement with translucency
- **Concurrent TILEDRAW blitter** - up to 8 tile draws in flight at once, dispatched async (~8 cycle issue cost) instead of stalling the PPU for the full ~264/392-cycle draw
- **Interrupts** - V-Blank and H-Blank with automatic stack management
- **64 × 16-bit registers** with special registers (PC, DP, SP)
- **Batched executor (`--jit`)** - Fast interpreter path that runs whole instructions and collapses timing stalls, 1.2x-4x faster than per-cycle ticking (not native code generation, despite the flag name)

### APU (Audio Processing Unit)
- **8-bit RISC processor** @ 4.25 MHz (~2.1 MIPS: 2 cycles per instruction); an opt-in fast mode runs it at 8.5 MHz (~4.25 MIPS) at the price of bus contention on the CPU's APU window
- **57 instruction mnemonics** (5-bit opcode + 11-bit operands)
- **16-bit stack** with push/pop operations and function calls
- **Up to 256 × 8-bit registers** + special registers (PC, SP, RP, DP, DB, BF)
- **MMP** - Music Mixing Processor: 16 sample channels with per-channel pitch, volume and pan, plus a stereo PCM stream port (how a whole song plays)
- **SST** - Sample Storage System with looping and effects
- **64 KiB addressable memory** + 448 KiB banked ROM
- **Full-system audio output** - `System` polls the MMP mixer at 48 kHz and the SDL frontend pushes it live to the audio device, so cartridge-generated sound reaches real speakers, not just the standalone APU test tool

### Boot ROM
- **Native (HLE) boot** - with no `--boot`, `System` boots the cartridge itself: it verifies the signature (strict by default), jumps to the entry point and answers the Boot ROM's `COP #$F0-$FF` services natively (`docs/boot-hle.md`)
- **Real Boot ROM** - the separate `ZPbootROM` project, loaded with `--boot`: splash screen and boot jingle, RSA-2048 signature verification of zpbuild-signed ROM trailers (v1/v2/v3, `docs/zpb-format.md`), hardware-assisted BLAKE2s hashing, and a handoff to the cartridge (`docs/boot-handoff.md`). Any failure halts. v3 adds runtime chunk verification (unverified data chunks read as poison until checked via `COP #$FF`). Services for cartridges: trig/sqrt (`COP #$F0-$F6`) and PPU program load (`COP #$F7`)
- **Two images** - `ZPbootROM/build.sh` builds `bootrom.bin` (retail: contains no dev-unit code) and `bootrom_dev.bin` (the same, plus the dev-unit hooks below)
- **Binaries in `bootroms/`** - the retail and dev Boot ROMs and the APU BIOS as built binaries (public domain, CC0 1.0; the source is private), refreshed by `tools/update_bootroms.sh`. Every release zip carries the folder beside the emulator: `zeropoint_sdl game.zpb --boot bootroms/bootrom.bin --apu-bios bootroms/bios_full.bin`
- **Default stub** (`examples/boot-rom/boot.asm`) - a 35-byte trampoline for standalone use; verifies nothing
- **Bank $E0** - 64 KiB (banks `$E0-$E1` for the real ROM), read-only; hardware reset lands here whenever a Boot ROM is loaded

### Cartridge save RAM
- A cartridge can carry 1 KiB-512 KiB of battery-backed SRAM at banks `$C0-$C7` (`zpbuild --save-size 8K`; the size is in the signed header's flags byte). Blank is `$FF`; it survives resets and power cycles and goes with the cartridge on a yank or swap. The frontends keep it in `<rom>.sav` (`--save FILE`, `--no-save`); movies embed their starting save and never touch the file. On a dev unit the dev Boot ROM erases it when all four shoulder inputs are held at boot. `docs/cart-save.md`

### Dev unit, debug serial port and exception entries
- **Dev unit** (`zeropoint_sdl --dev-unit`, `zeropoint_qt --dev-unit`) - a host-only switch the guest can read but never set. It turns the debug serial port (`$D8003C-$D8003E`) into a 115200-baud UART (`--serial-out`, `--serial-in`, `--serial-tcp`; typed input is recorded in movies; the Qt frontend has a Serial Console window) and makes signature checks always pass. A retail unit has the port's registers but nothing ever reads them (`docs/debug-protocol.md`)
- **Exception entries** - both Boot ROMs have a fixed table at `$E0:0044` that a cartridge's own vector stubs can jump to. The retail image locks; the dev image asks the host for a handler image over the UART (`--exception-handler FILE`), and a dev unit also raises ABORT for writes to ROM and unmapped accesses. A fault inside the handler is a double fault and locks (`docs/exceptions.md`)

### Movies and determinism
- **Determinism** - same ROM + input gives the same state on any host or thread count; checked by `test_determinism`, `fuzz_determinism` and cross-host golden digests (`docs/determinism.md`)
- **Movies** (`.zpm`) - record and replay both controllers, resets, cartridge swaps and serial input at exact master cycles, with embedded state checkpoints (`--record`, `--play`, `bin/zp_movie`; `docs/movies.md`)

### DMA Controller ✅ COMPLETE!
- **16 independent channels** @ 34.005678 MHz
- **4 transfer modes** - DataCopy (3 cyc/byte), ConstCopy (1 cyc/byte), RepeatTransfer (3 cyc/byte), ConstRepeat (2 cyc/byte)
- **Max 2 channels active** - Simultaneous transfers with automatic queueing
- **Interrupt-aware** - Pauses during CPU interrupts, resumes automatically
- **I/O registers** - Full status monitoring at $D80020-$D8002F
- **Integrated** - Connected to CPU memory system with callbacks

### Hardware Timers ✅ COMPLETE! ✨ NEW!
- **8 independent timers** with CPU IRQ support
- **Flexible periods** - V-blank (60Hz), H-blank, 1s, 250ms, 125ms, ~977μs, ~1ms, 60-frame
- **I/O registers** - Control at $D80050, Status at $D80051, Interrupt Enable at $D80052
- **Bit pattern** - 0bVHSQETAR for easy multi-timer control
- **IRQ integration** - Automatic CPU interrupt on timer expiration
- **Status flags** - Read timer state, write 1 to clear
- **Master clock based** - Precise timing at 68.011355 MHz

### Display & Rendering ✨ NEW!
- **Vulkan Renderer** - Native GPU acceleration (28% faster than SDL)
  - Persistent mapped staging buffer (zero allocation overhead)
  - Optimized command buffer recording (single submit)
  - MAILBOX/IMMEDIATE present modes (lowest latency)
  - Cross-platform: Linux (native), Windows (ready)
  - Performance: 83 MHz sustained (123% of target 67.1 MHz)
- **SDL2 Renderer** - Fallback renderer
- **Qt Widget** - Integrated display in Qt GUI

### Development Tools
- **cpuasm** - DEF88186 CPU assembler ✨ NEW!
- **ppuasm** - PPU microcode assembler
- **apuasm** - APU assembly language compiler
- **test_cpu** - CPU interpreter test suite ✨ NEW!
- **Instruction Implementation Framework** - Table-driven dispatch system for easy CPU/PPU/APU instruction modification ✨ NEW!
  - 256 CPU handler functions (cpu_inst_0xXX), 16 PPU handlers (ppu_exec_0xX), 32 APU handlers (apu_exec_0xXX)
  - See `docs/INSTRUCTION_IMPLEMENTATION_GUIDE.md` for comprehensive implementation guide
- Two frontends:
  - **SDL**: Standalone emulator with simple display
  - **Qt**: Full GUI with ROM loading and configuration

## Minimum Requirements

To run the emulator at full speed you need a host with:

- **2 CPU cores**
- **PassMark single-thread rating of 1500** (see [cpubenchmark.net](https://www.cpubenchmark.net/) to look up your CPU)

## Building

### Quick Start

**Build Everything (Recommended):**
```bash
./build-all.sh              # Builds emulator AND all dev tools in one step
```

**Build Emulator Only:**
```bash
./build-linux.sh            # Linux (x86_64 & ARM64)
build-windows.bat           # Windows (x64 & ARM64)
./build-windows-cross.sh    # Windows cross-compile from Linux
```
(macOS is CI-only — see Platform Support below; there is no `build-macos.sh` for local builds.)

**Build Dev Tools Only:**
```bash
cd ../ZPdevtools
make                        # Native build (Linux)
make -f Makefile.dos        # MS-DOS 4.01+ build (Turbo C/MS C)
```

**See `BUILD.md` for complete build instructions.**

### Manual Build

Requires CMake 3.16+, C++17 compiler, SDL2, and Qt5/Qt6.

```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

### Platform Support

| Platform | Architecture | JIT Support | CI/CD |
|----------|-------------|-------------|-------|
| Windows  | x64         | ✅ x86-64   | ✅    |
| Windows  | ARM64       | ✅ ARM64    | ✅    |
| Linux    | x86_64      | ✅ x86-64   | ✅    |
| Linux    | ARM64       | ✅ ARM64    | ✅    |
| macOS    | ARM64 (Apple Silicon) | ✅ ARM64 | ✅ (native runner) |
| macOS    | x86_64 (Intel) | ✅ x86-64 | ✅ (Rosetta cross-compile, experimental) |

macOS is CI-only — there's no local dev machine to verify against, and there's
no `.app`/`.icns` packaging (plain executables only). See
`.github/workflows/build.yml` for the current approach.

### Executables
- `bin/zeropoint_sdl` - SDL frontend
- `bin/zeropoint_qt` - Qt frontend
- `bin/test_cpu` - DEF88186 CPU interpreter test
- `bin/test_ppu` - PPU microcode test suite
- `bin/test_apu <program.bin>` - APU program tester
- `bin/test_dma` - DMA controller test suite
- `bin/run_demo <demo.bin> [--jit]` - Run PPU demo with SDL window (optional batched-executor mode)
- `bin/test_demo <demo.bin>` - Run PPU demo headless (testing)
- `bin/run_apu_demo <program.bin>` - Run APU program with audio output
- `bin/zp_movie` - inspect, seal and replay `.zpm` movies
- `bin/test_serial`, `bin/test_exception`, `bin/test_qt_serial` - dev unit, serial port and exception-entry tests (`tools/test_serial.sh`, `tools/test_exception.sh`)

## Creating Demos

### PPU Assembly
Assemblers live in the sibling `ZPdevtools` repo.

```bash
cd ../ZPdevtools && make
executables/ppuasm examples/ppu/simple_white_pixel.asm -o output.bin
cd ../ZeroPoint/build
./bin/run_demo ../../ZPdevtools/output.bin
```

### APU Assembly
```bash
cd ../ZPdevtools && make
executables/apuasm examples/apu/hello.asm
cd ../ZeroPoint/build
./bin/test_apu ../../ZPdevtools/examples/apu/hello.bin 10000
```

## Documentation

### Core Documentation
- `CLAUDE.md` - **Condensed system architecture** (the working reference for the whole project)
- `README.md` - This file (quick start and overview)
- `ROADMAP.md` / `TODO.md` - What is left, and what a release needs
- `docs/` - Per-subsystem documentation: `boot-hle.md`, `boot-handoff.md`, `zpb-format.md`, `debug-protocol.md`, `exceptions.md`, `movies.md`, `determinism.md`, `ppu.md`, `display.md`, `dma.md`
- `docs/INSTRUCTION_IMPLEMENTATION_GUIDE.md` - **Complete guide for implementing CPU/PPU/APU instructions** (400+ lines) ✨ NEW!

### CPU (DEF88186)
- `ZPdevtools/docs/cpu/README.md` - CPU documentation index
- `ZPdevtools/docs/cpu/architecture.md` - Registers, flags, and how it differs from a 65C816
- `ZPdevtools/docs/cpu/opcode-table.md` - All 256 opcodes with measured bytes, cycles and flags
- `ZPdevtools/docs/cpu/addressing-modes.md` - Operand forms and their traps
- `ZPdevtools/docs/cpu/programming-guide.md` - Patterns and examples (all assembled by a checker)

### PPU
- `docs/display.md` - Display system and color modes
- `ZPdevtools/docs/ppu/instruction-set.md` - All 35 instructions, with cycle costs
- `ZPdevtools/docs/ppu/assembler.md` - ppuasm, shorthands and the target-register trap

### APU
- `ZPdevtools/docs/apu/README.md` - APU documentation index
- `ZPdevtools/docs/apu/instruction-set.md` - What every instruction does
- `ZPdevtools/docs/apu/programming-guide.md` - Complete programming examples
- `ZPdevtools/docs/apu/mmp.md` - Music Mixing Processor
- `ZPdevtools/docs/apu/sst.md` - Sample Storage System

## Status

**CPU (DEF88186)**: ✅ **COMPLETE!** All 256 opcodes implemented and tested. Production ready. Can execute any valid DEF88186 program.

**PPU**: ✅ **DISPLAY COMPLETE!** Rolling framebuffer with NTSC timing fully functional. Tile system, interrupts, microcode execution, and VOC all operational. Stable batched executor for x86-64/ARM64 (use `--jit` flag; it's a fast interpreter, not native codegen).

**APU**: ✅ **MMP AUDIO WORKING!** Full instruction set, stack operations, function calls, and MMP audio mixing (16 mono channels with pan) all implemented. SST header bug fixed - clean audio playback confirmed, and the main emulator now streams that audio live to the SDL audio device (not just the standalone `run_apu_demo` tool).

**DMA**: ✅ **COMPLETE!** All 4 transfer modes implemented with 16-channel support. Fully integrated with CPU memory system and interrupt handling. Comprehensive test suite (7/7 passing).

**Boot ROM**: ✅ **COMPLETE.** Native (HLE) boot by default, or the real `ZPbootROM` (retail or dev image) via `--boot`: signature verification, splash, jingle, handoff, math and PPU-load services, and exception entries.

**Dev tooling**: ✅ dev unit with a debug serial UART, handler images for exceptions, and movie record/replay, on both the SDL and Qt frontends (the Vulkan target only plays PPU programs, with no `System` behind it).

The emulator core is at **revision 13** (`System::CORE_REVISION`), frozen: ROMs, movies and golden hashes from revision 13 must keep running identically. Revision 13 stretched the raster so a frame is exactly 1/60 s (a line is 4343 master cycles, it was 340) and replays nothing older. `tools/run_tests.sh` runs every test suite.

## License

Licensed under the GNU Affero General Public License v3.0 (AGPL-3.0) — see [LICENSE](LICENSE).

The AGPL's network-use clause means that if you run a modified version of this emulator as a network-accessible service, you must also offer that modified source to its users. Games/ROMs built with the ZPdevtools toolchain (licensed separately under MPL-2.0) are not derivative works of the emulator and are unaffected by this.
