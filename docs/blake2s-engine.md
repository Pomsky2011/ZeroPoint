# BLAKE2s engine

Hardware hash unit at `$D80060-$D8009F` (I/O bank). It hashes a range of **cartridge ROM** with BLAKE2s (RFC 7693, unkeyed, 32 bytes) in the
background, so neither the Boot ROM nor a game ever hashes in software. Test: `bin/test_blake2s_engine <cartridge.zpb>`.

| address | | |
|---|---|---|
| `$D80060-62` | SRC | cartridge address, little-endian (bank `$00-$7F`) |
| `$D80064-67` | LEN | bytes, 32-bit |
| `$D80068` | CMD | write `$01` = hash `[SRC, SRC+LEN)`, `$80` = reset |
| `$D80069` | STATUS | bit 0 BUSY, bit 1 DONE (digest valid), bit 2 ERR (the last start was rejected) |
| `$D80080-9F` | DIGEST | the 32 bytes, in order; reads 0 until DONE |

- **Timing**: `BLAKE2S_CYCLES_PER_BLOCK` (64) CPU cycles per 64-byte block, minimum one block: the 8-bit ROM bus feeds one byte per CPU
  cycle, which is the bound (a compression is 10 rounds x 8 G, so 80 cycles with one G unit, 40 with two). ~0.49 s per 8 MB at the CPU
  clock; at the 68 MHz master clock with a byte per cycle it would be ~123 ms. The busy window runs on the CPU's own cycle counter, so
  it is the same under any batching or thread count. Registers are locked while BUSY (only CMD `$80` works).
- **Rejects**: slot empty, bank above `$7F`, or the range running past `$800000`: ERR, no job, digest 0.
- **Reads the raw ROM, not the gated bus**: it only ever returns a hash, never bytes, so a v3 cartridge can't use it to read poison.
  (It does let a cartridge hash chunks it hasn't verified; harmless, it already has the bytes' hash from the manifest.)
- A yank, a soft/hard reset or a Stop 'n' Swop drops a job in flight. State is in `CPU::stateHash()`.

## Use by the Boot ROM

`ZPbootROM/def88186/rsa.def` `hw_blake2s` drives it for **trailer v2** (`rsa_verify_composite`: `code_digest = BLAKE2s(payload[0..codeSize))`,
`data_digest = BLAKE2s(payload[codeSize..))`, signed digest `SHA256(header || code_digest || data_digest)`). With the engine a v2 ROM
verifies its whole payload at boot in well under a second, with **no per-chunk verification** and no gating, so a cartridge never calls
`COP #$FF`. `zpbuild --codesize N --trailer 2` produces it (default trailer stays 3); `ZPdevtools/ucode/layers/demo/build_demo.py` now builds v2
(`--v3` for the old scheme). v3 is unchanged and still hashes chunks in software (`verify_chunk`, ~0.5 s each on the real Boot ROM).

Open: `verify_chunk` compares the digest in WRAM scratch (`B2_H0`), which a cartridge DMA could in principle stomp between the hash and
the compare; an engine "verify chunk i against the manifest" command that sets the bit in hardware would remove that. Not built.
