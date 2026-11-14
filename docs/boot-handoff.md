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
