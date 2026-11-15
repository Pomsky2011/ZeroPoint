# APU ADPCM player

Plays a stereo IMA/DVI 4-bit ADPCM track, looping, decoded **in software on the APU** and fed to the MMP stream port.
One byte per frame (left nibble high, right low): 44.1 kHz stereo is 44 KB/s, so 117 s is 5.2 MB of the 8 MB ROM.

| piece | file |
|---|---|
| codec: encoder (never lets the predictor leave int16), exact reference decoder, APU tables | `tools/ima_adpcm.py` |
| APU decoder generator (apuasm text + tables) | `tools/gen_adpcm_apu.py` |
| CPU loader/feeder | `examples/adpcm-player/player.asm` |
| audio file -> ROM | `tools/build_adpcm_rom.py <audio> <out.zpb> [--rate HZ] [--seconds N] [--decoded-wav f.wav]` |
| tests | `tools/test_adpcm.py` (codec + bit-exact APU decode, via `bin/test_adpcm_apu`), `tools/test_adpcm_rom.py` (whole System) |

Run the ROM with the stock boot stub (unsigned). The whole track is the loop: the CPU wraps its ROM pointer after the
last page and the APU reloads the initial predictor/index at the same page, so the loop is exactly as seamless as the
source's own end-to-start. The track is stretched by under half a page (~0.003% of a 2 minute track) to a whole
number of 256-frame pages.

## Why it fits on the APU

The APU has two 8-bit registers and ~2.1 MIPS (4.25 MIPS in fast mode, `APUCLK` $D8001A, see `ZPdevtools/docs/apu/
instruction-set.txt`). Decoding costs **77.5 instructions per stereo frame**, so 44.1 kHz stereo needs ~3.4 MIPS: fast
mode only (normal mode tops out near 27 kframes/s). The cost comes from doing no arithmetic a table can do:

- per magnitude `m` (0-7): `DLO_m`/`DHI_m` (the 16-bit step difference for each of the 89 step indexes) and `NEXT_m`
  (the next index, clamped). One 256-byte page each, so one `SDP` + one `STRX` is a lookup;
- a jump table maps the whole input byte to the address of one of 32 stubs (channel x sign x magnitude), so no nibble
  is ever extracted: `X = byte`, two `STRX` give `Y:X = target`, `JSX`. Each stub is 19 instructions;
- no clamp: the encoder keeps the predictor inside int16, so the stream decodes identically on a clamping standard IMA
  decoder (the output is the predictor's high byte: the stream ring is 8-bit).

Page-level work (advance the ring page, commit `WRPTR` high byte first, wait for room, publish the read page, loop
bookkeeping) happens once per 128 or 256 frames. The code patches its own `SDP` operands for the input and output pages.

## Memory (APU addresses)

vars `$0200`, main loop `$0300`, page routines + `start` `$0400-$05FF`, stubs `$0600-$0BFF`, tables `$9000-$ABFF`,
stream ring `$3000-$37FF` (8 pages x 128 frames), input ring `$3800-$6FFF` (56 pages x 256 frames), mailbox: `$1000`
(CPU: next page it fills) and `$1800` (APU: page it is reading). DMA into the APU window runs at half rate while the
APU is in fast mode, so the loader uploads first and sets fast mode last.

## Gotchas found building it

- `apuasm` silently drops tokens past the 10th on a line: a `.db` with 16 values loses half of them. Emit 8 per line.
- `CMN`/`CML` branch backward only, `CME`/`CMG` forward only. "Skip if X != 0" forward is `CMG X, Y(=0)`.
- `SDP` takes only an immediate and the BIOS region ($8000-$8FFF) ignores writes, so code lives in GP RAM and the
  decoder tables in SST.
- Signed (v3) ROMs: the builder signs by default and the player verifies each 16 KB chunk (`COP #$FF`) the first time
  round. The HLE boot (the default frontends) charges almost nothing for that. The **real Boot ROM (`--boot`) takes
  ~0.52 s per chunk**, so at 44.1 kHz (0.37 s of audio per chunk) every chunk leaves a ~0.15 s silent gap (measured:
  29% of the time). The ring (56 pages) covers a stall at rates up to ~24 kHz: build with `--rate 24000` for `--boot`.
- The boot jingle's channels keep looping when the APU is taken over mid-jingle (an endless 880 Hz drone), so `start`
  zeroes all 16 STL registers; and the Boot ROM leaves its result (1) in mailbox $1000, so the player clears both
  mailboxes before starting the APU.
- Host speed: decoding adds ~4M APU events/s to the scheduler. The run-ahead CPU burst now runs APU instructions as
  ordinary events (`System::runCpuBurst`); `test_batched` checks it is state-identical.
