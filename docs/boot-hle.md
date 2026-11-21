# HLE Boot ROM

With no real Boot ROM loaded (the default), `System` does the Boot ROM's job natively. `--boot` /
`System::loadBootROM()` maps the real ZPbootROM into bank $E0 and switches all of this off
(`System::isBootHLE()` is `!cpu.hasBootROM()`). Same split as the APU's HLE BIOS (`apu_hle.cpp`).

| piece | where | what |
|---|---|---|
| boot | `System::hleBootCartridge()` (reset) | verify the signature, then jump to the entry point |
| COP #$F0-$F6 | `System::hleService`, `BootHLE::sinQ15`... | SIN/COS/SINCOS/ATAN2/ATAN/SQRT/HYPOT, bit-exact with `trig_model.py` |
| COP #$F7 | `System::hlePPULoad` | reset PPU, copy A:X (Y pages) to PPU 0, start, A = 0 |
| COP #$F8-$FE | `hleService` | return at once, registers untouched |
| COP #$FF | `System::hleVerifyChunk` | BLAKE2s the v3 chunk in X against the manifest, mark it verified on a match |

`COP` dispatch: `CPU::setServiceHook()`; `opCOP()` offers every signature to it before pushing anything, so a
handled call costs no stack. `P/DB/D/SP` come back untouched (as after the real `COP...RTI`), A/X as each
service documents. Crypto is vendored from ZPdevtools in `src/boot_hle/` (public key only).

## Verification is strict

An unsigned or badly signed ROM halts the CPU (message on stderr), like the real ROM. Checked: trailer v1
(`SHA256(header||payload)`), v2, v3 (code digest + manifest digest), against the dev key; the digest is
always recomputed, never read from the trailer. An 8 MiB v1 ROM verifies in ~120 ms (real ROM: ~9 min).

A **dev unit** (`System::setDevUnit(true)`, `--dev-unit`; docs/debug-protocol.md) skips the check too, because on one the signature
check always passes. The real **dev** Boot ROM (`ZPbootROM/build/bootrom_dev.bin`) does the same: it reads `$D80048` bit 1, a latch only the host can
set. The retail `bootrom.bin` has no such code and always verifies.

Turn it off for unsigned dev ROMs: `System::setVerifySignatures(false)`; `zeropoint_sdl --no-verify`; Qt and
anything else using `System::verifySignaturesDefault()` honour `ZEROPOINT_NO_VERIFY=1`. Tools that run
unsigned ROMs (`debugger`, `zpb_wav`, `test_batched`, `bench_qt_tick`) opt out themselves; tests default strict.

## What it skips, and where it differs from the real ROM

- **No splash, fade or jingle.** The PPU is idle/black at the entry point, the APU untouched (its HLE BIOS
  waits for the cartridge's program). The real ROM also writes APU mailbox $1000 (1 = good, 2 = bad).
- **Cycles.** Math and $F7 charge the real ROM's measured cost (`COST_*` in `src/system.cpp`; math = worst case,
  $F7 = `216 + 432*pages`). $FF charges a flat 64 (the real one is ~0.5 s per 16 KiB) and boot verify is free.
- **$F7 reads through the gated bus.** An unverified v3 data chunk copies into the PPU as poison; the real
  ROM's privileged DMA would copy the real bytes. Strictly safer, so run `COP #$FF` on the chunk first.
- **Reserved signatures are always ours.** $F0-$FF never reach the cartridge's own `$00:FFE4` handler while
  the HLE is on (before, the 35-byte stub left them to the cartridge). `kDefaultBootROM` is still in the
  library for `test_boot_rom`, which loads it explicitly.
- Side effects kept for parity with the old stub: `$80:0000-0003` holds a JMP-long trampoline, A/N/Z hold the
  entry's bank byte.

## Tests

- `bin/test_boot_trig --hle trig_model.py`: 1929 vectors, bit-exact, plus the calling contract.
- `tools/test_hle_cop.sh` (builds a v1 and a v3 ROM with zpbuild): boot verify and every tamper case, lazy chunk
  verify, and $F7 against the real ROM byte for byte (and its cycle counts).
- `bin/test_zp_cop --hle ...` runs the SDK wrappers on the HLE.
- ZPdevtools `BOOT=none tests/cc/run.sh` now runs the `NEEDS: boot` C tests on the HLE.
