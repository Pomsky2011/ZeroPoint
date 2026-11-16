# Boot ROM -> cartridge handoff: the PPU

The signed header carries no PPU fields, so **the cartridge owns its PPU program**. `zplink` puts one at ROM
`$10:0000` by convention, but nothing loads it automatically; the cartridge's startup code does, with
`COP #$F7`. What the Boot ROM guarantees (`ZPbootROM/def88186/handoff.def`):

- **Before the jump**, after a good signature, the lightning splash fades to black (~0.3 s of console time:
  16 palette levels, one per 60 Hz V-blank timer tick, then three ticks of black) and the PPU is **reset**:
  idle, memory cleared, black screen. A cartridge with no PPU program just keeps that; the PPU costs
  (almost) nothing to emulate. On a failed signature the boot ROM still halts with the splash running.
- **`COP #$F7` loads and starts a PPU program.** In: `A` = source bank (low byte), `X` = source offset,
  `Y` = length in 256-byte pages (1-256, 0 means 256). Out: `A` = 0. It resets the PPU, DMA-copies the pages
  to PPU address 0 in chunks of up to 1024 bytes, then starts it (execution begins at PPU address 0, so put a
  `JMR main` trampoline there). Same contract as the other services: M/X width doesn't matter, DB/D/SP and
  P are preserved, WRAM `$80:2A40-$80:2A4F` is clobbered.
- **Swapping programs later** (scene changes) is the same call; it resets first, so nothing runs
  half-overwritten. It does not fade anything.

Caveat: the copy goes through the PPU window, so the PPU sees each byte as a *write*. A byte landing on a live
register (`$00F0` is the VOC render mode, enabling the blank interrupts) takes effect mid-copy. The splash's
`main.ppu` clears `$00F0` as its very first act for this reason; a PPU program should too.

## How the fade works

The splash is interrupt driven and its vblank handler is flag-free on purpose (it can pre-empt the hblank
handler between a `CMP` and its branch), so the splash code is untouched. Its vblank handler loads slot
`R44 mod R45` of a 32-slot palette table at PPU `$9400` every frame. The CPU writes `R45` (FADE_FRAMES) = 1
through `PPUREG_ADDR`/`PPUREG_DATA` (one byte: atomic), so only slot 0 is ever read, and then copies the next
dimmed palette into slot 0 each tick. The levels are `fadeout_patch.bin` (made by `gen_mosaic.py`, patched
into bank `$E0` at `$F500` by `build.sh`).

Tests: `ZPbootROM/tests/handoff/run_test.sh` (needs `bin/test_boot_handoff`): brightness must fall
monotonically to black over the last 0.3 s before the handoff, the PPU must be idle afterwards, and a
cartridge's `COP #$F7`-loaded program must run (a marker word shows up in PPU memory).

## The APU is NOT reset at handoff (a known flaw: read this before starting an APU program)

The Boot ROM resets the PPU before it jumps to the cartridge; it does **not** touch the APU. A cartridge gets the APU
in whatever state the BIOS jingle engine left it, and that state is different for the two BIOS flavours:

| APU state a cartridge sees | native HLE BIOS (no `--apu-bios`) | real BIOS image (`--apu-bios`; the Qt frontend loads one from its saved settings) |
|----------------------------|-----------------------------------|---------------------------------------------------|
| PC (idle loop)             | `$8000`                           | `$82C4`                                           |
| SP / flags                 | `$0000` / `00`                    | `$0100` / `04`                                    |
| RP / DP                    | `$80` / `$00`                     | `$82` / `$02`                                     |

(Measured two seconds after power-on with the real Boot ROM. Both can also leave jingle channels sounding with their STL
addresses and volumes set, and the Boot ROM's verify result `1` in mailbox `$A0:1000`.)

A program that only writes the APU's PC and runs inherits all of that. The ADPCM player did, and with the real APU BIOS
its decoder's stack grew up through its own code (SP `$0B02`, inside the program) and it played loud, clipped noise; it
was perfect with the HLE BIOS, which is why it went unnoticed. Which instruction first leaked the stack was not traced
(different starting flags are the likely cause, since a conditional jump on this APU pushes a return address), but a
reset before the start fixed it completely.

**What to do.** Start every APU program from a reset, in this order:

1. `APUCTRL` (`$D80010`) = `$03`: reset **and halt**. A plain reset restarts the BIOS engine at `$8000` while you upload.
2. Upload the program and its data (DMA to the `$A0` window). A reset clears APU RAM, so the reset comes first.
3. Write the PC (`$D80012/13`); set the fast-mode bit (`APUCLK`, `$D8001A`) if you use it, since a reset clears it.
4. `APUCTRL` = `$04`: resume.

The SDK's `zp_apu_boot()` resets first but leaves the APU *running*, which only helps while the upload is short: with the
real APU BIOS the engine is still at its reset values 8,000 master cycles after a reset, but by 40,000 (~0.6 ms) it has
restarted and set SP `$0100`, DP `$30` and flags `02`, and it keeps those if your upload is still going. A long upload
(the ADPCM player's is ~10 KB) should reset **and halt** as above; a hand-written loader has to do it itself (see
`ZPdevtools/examples/adpcm-player/player.asm`). Test an APU program with `--apu-bios ZPbootROM/build/bios_full.bin`
as well as the default HLE BIOS: `ZPdevtools/scripts/test_adpcm_rom.py` does both.

Decision (2026-11-15): left as it is. The Boot ROM *could* reset the APU at handoff, which would make every cartridge
safe and make the two BIOS flavours agree, but it is a documented platform flaw instead; do not rely on a clean APU.

