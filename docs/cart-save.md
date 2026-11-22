# Cartridge save RAM

A cartridge can carry **battery-backed SRAM**: bytes that keep their value when the console is off. It is how a game keeps a save,
a high score or settings. Added in core revision 14; a cartridge without it behaves exactly as it did on revision 13.

## What the guest sees

- **Banks `$C0-$C7`** (a 512 KB window) are the save RAM. The cartridge's size is a power of two from 1 KiB to 512 KiB and **repeats
  across the window**: with 8 KiB, `$C0:0010`, `$C0:2010`, `$C1:0010` and `$C7:E010` are the same byte. Read and write like RAM
  (any instruction, DMA, code can even run from it).
- A **new cartridge's SRAM is blank: every byte `$FF`** (the look of erased memory).
- It **survives** `softReset`, `hardReset` (a power cycle) and, in the emulator, quitting (`<rom>.sav`, below). It belongs to the
  cartridge: a yank takes it away (the window then reads `CA FE CA FE...` like the ROM banks), and a Stop 'n' Swop to another
  cartridge gives that cartridge its own.
- A cartridge **with no SRAM** leaves `$C0-$C7` unmapped, exactly as before: reads are open bus (`$FF`) and, on a dev unit with the dev
  Boot ROM, a write raises ABORT (kind 3, `docs/exceptions.md`).
- Not modelled: a dying battery, power loss in the middle of a write. Programs should still write defensively (the SDK's
  `zp_save_store()` keeps two checksummed slots, so one is always intact).

## The header

Byte `0x05` of the `.zpb` header is the flags byte (`docs/zpb-format.md`): bit 0 = signed (set by `zpbuild`). **Bits 4-7 are the
save-size code `n`: the SRAM is `512 << n` bytes**, `n = 0` none, `1` = 1 KiB, `2` = 2 KiB, ... `10` = 512 KiB. Codes 11-15 are
refused (the loader reports `Unsupported save size code`). The header is covered by the ROM signature, so the size can't be
changed after signing. Every ROM built before this has 0 there.

    zpbuild game.elf -o game.zpb --save-size 8K        (a power of two, 1K..512K, plain bytes or with a K suffix)
    rominspect game.zpb                                (shows "Save RAM: 8192 bytes")

## The host side: `<rom>.sav`

`System` holds the bytes and a dirty flag; files are the frontend's business (`include/save_file.h`, `SaveFile`).

| API | |
|---|---|
| `System::saveSize()` / `saveData()` | the inserted cartridge's SRAM size / bytes (0 / empty if it has none) |
| `System::setSave(bytes)` | load saved bytes (exactly `saveSize()` of them, else refused and nothing changes); callable any time a cartridge is in |
| `System::saveDirty()` / `takeSaveDirty()` | a byte actually changed since the last take (writing the same value is not a change) |
| `SaveFile::attach(sys, path, note)` | after loading/inserting a cartridge: pick up `path` if it is there and the right size. A file of the wrong size is **never overwritten**: it is reported and saving stays off for that cartridge |
| `SaveFile::poll(sys, now)` / `flush(sys)` | write a changed save at most every 2 s / now; written to `path.tmp` then renamed, so a crash never leaves half a file |

`zeropoint_sdl` and `zeropoint_qt` do this for you: the save is `<rom without extension>.sav` next to the ROM (`--save FILE` to
choose, `--no-save` to start blank and never write). It is flushed every couple of seconds while it changes, before a yank or a
swap, and on exit; a Stop 'n' Swop cartridge gets its own `.sav`. Delete the file to start over.

**Movies never touch the file.** A recording takes the save it finds as its starting state (or blank with `--no-save`) and *embeds it
in the movie*; a replay starts from the embedded bytes, whatever the host has. See `docs/movies.md`. Save contents are part of
`machineHash()` (a `save` part, present only for a cartridge that has SRAM, so every other hash is what it was on revision 13).

## Erasing it: the dev Boot ROM

On a **dev unit** (`docs/debug-protocol.md`) with the **dev Boot ROM** (`bootrom_dev.bin`), holding **all four shoulder inputs of
player 1** (big left, big right, little left, little right: control byte `$D80031` mask `$78`) while the unit boots erases the
save RAM to `$FF` before the cartridge starts (the whole `$C0-$C7` window, whatever the size). Three of the four, any other
button, a unit whose dev-unit latch is clear, or the retail `bootrom.bin` (which has no such code and never reads the
latch) do nothing. The press is an ordinary controller input, so it is recorded in movies and replays the same wipe. The native (HLE) boot
has no wipe: delete the `.sav`.

## For game code

`ZPdevtools/include/zeropoint/zp_save.h` (not part of `zeropoint.h`: it needs `lib/zp_save.asm`, which reaches the save banks;
`zp_peek8`/`zp_poke8` deliberately don't, so no other program's timing changes). `zp_save_read8/write8`, block read/write/erase,
and `zp_save_load()` / `zp_save_store()`: a two-slot store with sequence numbers and CRC-16, committed last. `ZPgameTemplate`
keeps the hero's position in it (`make` builds with `--save-size 1K`; `make wipe-save` forgets it). Tests: `ZPdevtools/tests/cc/t19_save.c`
(both compilers), `bin/test_cart_save`, `bin/test_game_save`, `bin/test_save_wipe`, `fuzz_determinism <dir> --save`.

## Design notes

- Mirrored SRAM is not on the flat per-bank fast path (`CPU::rebuildBankTable` leaves banks `$C0-$C7` to the region walk), so mirroring is one
  mask and the common ROM/RAM accesses are untouched. The CPU region is *not owning*: `System` keeps the bytes so they outlive a
  hard reset or a yank and so the host can read them any time.
- Compatibility: a ROM with save code 0 maps nothing, so the bus, every hash and every golden digest are revision 13's.
  `MIN_COMPATIBLE_REVISION` stays 13; a movie that carries a save must say revision 14 (the parser refuses it on 13).
