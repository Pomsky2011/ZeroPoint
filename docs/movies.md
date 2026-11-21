# Input movies (.zpm)

A movie is a recording of everything a player can do to the console, replayable bit for bit. It is what a TAS is made of,
so the format is built around one promise: **a replay either reproduces the run or tells you the exact cycle and chip where
it stopped doing so.** (Why replay can't silently drift: `docs/determinism-proof.md`.)

## What a movie contains

- **Setup**, pinned by SHA-256 of the file bytes: the boot ROM (or "native"), the APU BIOS (or "native"), whether signatures
  are verified at boot, whether the machine is a **dev unit** (debug serial UART, signatures always pass;
  `docs/debug-protocol.md`), and the cartridge in the slot at power-on (or an empty slot). A replay refuses to start if it can't
  find a pinned image by hash (`RomLibrary`: the movie's own folder plus `--roms`), and names it.
- **Events at exact master cycles of movie time** (cycles since power-on; a master cycle is 1/68,011,355 s of console time):
  - `input`: the three controller bytes (`PlayerInput::` direction/control/buttons) of port 1 or 2
  - `serial`: a byte the host types into the debug serial port (dev unit only; the guest sees it at that cycle, and a full
    queue drops it on replay exactly as it did when recording)
  - `softreset`: the reset button (CPU and chips reset, work RAM kept, boot ROM runs again)
  - `hardreset`: a power cycle (every byte of state back to power-on; the cartridge stays in the slot)
  - `yank`: cartridge pulled while running. Banks `$00-$7F` and `$E2` read `CA FE CA FE...` (not `$FF` = HLT, not the `DE AF`
    chunk poison); the CPU keeps executing; `CART_ENTRY` reads 0; chunk gating is gone
  - `insert N`: Stop 'n' Swop: ROM `N` of the table is mapped in that instant (replacing whatever was there). No reset, no
    boot ROM, no signature check; trailer-v3 chunks start unverified
- **Checkpoints**: `System::machineHashParts()` (cpu incl. all RAM, ppu, display, apu, dma, timers+irq, clock, cartridge, audio) at
  intervals. A replay checks each one and reports `DESYNC at movie cycle C: state differs in: cpu, apu`.
- `coreRevision`: `System::CORE_REVISION`. Bump it whenever a change to the emulation changes what a program computes.
  A core replays the revisions from `System::MIN_COMPATIBLE_REVISION` up to its own: revision 11 added the dev unit, and with it
  off the machine is bit-identical to revision 10, so rev-10 movies still replay (and their checkpoints still match). A movie
  from an older or newer revision is refused (`--ignore-core-revision` to try anyway). A movie that uses the dev unit or
  `serial` events must say revision 11 or later.

Movie time keeps counting while a halted CPU freezes the machine, and across resets, so a reset event on a halted machine
still lands at its cycle.

### Events at the same cycle, and what a checkpoint sees

Same-cycle events apply in file order. A checkpoint at cycle `c` sees the machine *before* the other events at `c` (they
apply when the run continues), except the closing checkpoint, which comes after everything. Checkpoints verify the machine's
state at their own instants: a difference that leaves no trace by the next checkpoint (an input overwritten a few cycles
later, a one-cycle shift the program never reads) isn't a different run. The interval is the resolution; `zp_movie seal
--interval` sets it.

### The checkpoint hash is host-independent

`machineHash()` is `stateHash()` minus what the host controls: the **latched output frame** (frame skip toggles it) and the
**pending audio buffer** (frontends drain it whenever; every sample produced is covered by a running digest instead). Frame skip
and audio draining therefore can't cause a false desync, and `fuzz_determinism` flips both at random on purpose to prove it.

## File format (`.zpm`, little-endian, integer-only)

```
"ZPM\x1A"  u16 format-version(1)  u32 coreRevision  u64 lengthCycles
u8 flags (1 verify signatures, 2 native boot ROM, 4 native APU BIOS, 8 dev unit)
[32 bytes sha256 boot ROM, if not native]  [32 bytes sha256 APU BIOS, if not native]
varint initialRom+1 (0 = empty slot)   varint nRoms   nRoms x { 32 bytes sha256, varint len + title }
varint len + author   varint len + description   varint checkpointInterval
varint nEvents  then per event: varint cycleDelta  u8 type  payload
   1 Input      u8 port(0/1) u8 dir u8 ctrl u8 btn      2 SoftReset   3 HardReset   4 Yank
   5 Insert     varint romIndex                          6 Checkpoint  u8 n, n x u64 part hashes
   7 SerialIn   u8 byte (dev unit)
u64 FNV-1a of everything above
```
The parser bounds-checks everything and rejects corruption (checksum, truncation, bad indices); `test_movie` throws 4000 mutated
movies at it under ASan/UBSan.

### Text form (`zp_movie dis` / `asm`)

```
zpm-text 1
verify off
devunit on                     # optional: a dev unit (debug serial UART)
boot hle                       # or  boot sha256:<hex>  or  boot path/to/bootrom.bin
apubios hle
rom 0 game.zpb                 # hashed from the file; dis prints  rom 0 sha256:<hex> game.zpb
rom 1 other.zpb
initial 0                      # or none
@0       input 1 0x40 0x01 0x00    # port (1|2), dir, ctrl, btn
@200000  input 2 0x01 0x03 0x00
@400000  serial 0x68           # a byte typed into the debug serial port
@600000  softreset
@900000  yank
@1000000 insert 1
@1300000 hardreset
length 1600000
```

## Tools

| | |
|---|---|
| `zp_movie info m.zpm` | header, ROM table, event counts, whether it is sealed |
| `zp_movie asm s.txt m.zpm` / `dis m.zpm` | text <-> binary (byte-exact round trip) |
| `zp_movie seal in.zpm out.zpm [--interval N]` | replay it and embed checkpoints (the TAS workflow: author, seal, ship) |
| `zp_movie play m.zpm [--roms PATH]... [--wav f] [--ppm f]` | replay and verify; exit 1 and the desync report on failure |
| `zeropoint_sdl ROM --record out.zpm [--author X]` | record from power-on; ESC saves |
| `zeropoint_sdl --play in.zpm [--roms PATH]...` | watch a movie (keys do nothing but ESC) |
| Qt: **Console** menu | soft/hard reset, yank, swap, two players, record/play/stop movie |

Frontend keys (SDL and Qt): F1 soft reset, F2 hard reset, F3 yank, F4 Stop 'n' Swop (SDL: next `--swap-rom`, Qt: file dialog).
Player 2 (`--two-players` / Console > Two Players): `i j k l` direction, `7 8 9 0` buttons, `u o n m` bumpers/triggers,
backspace menu, `p` pause. Player 1 is as before.

## How recording and replay stay identical

Both go through `MovieSession`. The frontend runs the machine only through `run()` and sends host actions through
`input/softReset/hardReset/yank/insert`; recording applies each one and logs it at the current movie cycle, playback applies
the logged one at the same cycle, through the same `applyEvent`. Both start from `setupMachine`, which does a power cycle
(`hardReset`) with the pinned boot ROM/BIOS/cartridge, so a frontend that reuses one `System` across ROM loads starts from the
same state a fresh one does (`test_movie` replays on a deliberately dirtied System).

## Hardware changes this needed

- **Port 2 is a controller** (`$D80038-$D8003A`, same layout as port 1, unplugged = all zero until plugged in). The debug
  serial interface that used to sit there moved to `$D8003C-$D8003E`.
- **Cartridge slot**: `System::insertCartridge/ejectCartridge`; an empty slot reads `CA FE`.
- **`hardReset`** is a real power cycle (RAM, VRAM, chip registers, display, cartridge bus); `softReset` is the old `reset()`.

## Not done

- No savestates: a movie always starts at power-on. (A movie can't be started from the middle.)
- No frame-based authoring UI: events are cycle-exact, and frontends record at the cycle their input poll ran (once per
  60 Hz tick, so about every 1.13 M cycles). A TAS tool that wants frame stepping drives `MovieSession::run` itself.
- Windows isn't in the CI determinism check yet (`docs/determinism-proof.md`).
