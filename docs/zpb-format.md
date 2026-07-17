# ZeroPoint Binary (.zpb) ROM Format

## Overview

The .zpb format is the ROM file format for ZeroPoint console games. It contains the game code, data, and metadata.

## File Structure

### Header (64 bytes)

| Offset | Size | Type     | Description                           |
|--------|------|----------|---------------------------------------|
| 0x00   | 4    | char[4]  | Magic number: "ZPB\0"                 |
| 0x04   | 1    | uint8    | Format version (currently 1)          |
| 0x05   | 1    | uint8    | Reserved                              |
| 0x06   | 2    | uint16   | Header size (64 bytes)                |
| 0x08   | 4    | uint32   | ROM size in bytes                     |
| 0x0C   | 4    | uint32   | Entry point address                   |
| 0x10   | 32   | char[32] | Game title (null-terminated)          |
| 0x30   | 16   | char[16] | Developer name (null-terminated)      |

### ROM Data

Starting at offset 0x40 (64 bytes), the ROM data follows. This is loaded into the console's memory at address 0x000000.

## Memory Map

When a .zpb ROM is loaded:
- ROM is mapped starting at address 0x000000
- Maximum ROM size: 8 MB (`ROM::load`'s `MAX_ROM_SIZE`, `src/rom.cpp`) -
  matches the CPU's addressable ROM window, banks $00-$7F (see
  `CLAUDE.md`'s CPU Memory Map)
- Entry point specifies where execution begins

## Signature Verification

The emulator's loader (`src/rom.cpp`) does **not** perform any signature
verification itself — it only checks the magic bytes, `version` (1 or 2),
header size, and ROM size, then loads unconditionally either way. Verifying
the signature is the Boot ROM's job (see `ZPbootROM/def88186/rsa.def`), not
something `ROM::load()` enforces.

`version == 1` is the plain (unsigned) format described above: header
immediately followed by `romSize` bytes of payload, nothing else.

`version == 2` is the *signed* format produced by `ZPdevtools/zpbuild` (or
`zplink`'s signed output mode): the same 64-byte header (with `flags` bit 0
set) and payload, followed by a "ZPSG" trailer:

| Offset (from end of payload) | Size | Description                          |
|-------------------------------|------|--------------------------------------|
| +0                             | 4    | Magic: "ZPSG"                         |
| +4                             | 1    | Trailer version (1, 2, or 3)          |
| +5                             | 1    | Key size in 64-bit words (32 = 2048-bit) |
| +6                             | 2    | Signature length in bytes, little-endian (256) |
| +8                             | 32   | SHA-256 digest (meaning depends on trailer version, see below) |
| +40                            | 256  | RSA-2048 signature (PKCS#1 v1.5), big-endian |
| +296                           | 4    | **Trailer version 2 and 3:** codeSize, little-endian u32 |
| +300                           | varies | **Trailer version 3 only:** per-chunk manifest - `ceil((payload_size-codeSize)/16384)` 32-byte BLAKE2s digests, one per 16384-byte data chunk (last chunk short), back to back. Length is never stored explicitly; both signer and verifier derive the chunk count from `codeSize` and the header's `romSize`. |

**Trailer version 1** (single-region signing, `zpbuild`'s default): the
digest is `SHA256(header || payload)` - the whole payload is hashed as one
blob and RSA-signed. This is fast to verify for small payloads but scales
linearly with payload size (see the boot-time discussion in
`ZPbootROM`'s `rsa.def`).

**Trailer version 2** (code/data-split composite signing, opt-in via
`zpbuild --codesize N`): the payload is split into `payload[0..codeSize)`
("code") and `payload[codeSize..payload_size)` ("data"). The digest field
is a *composite* hash, not a hash of the raw payload:

```
code_digest   = SHA256(header || payload[0..codeSize))
data_digest   = BLAKE2s(payload[codeSize..payload_size))
digest        = SHA256(header || code_digest || data_digest)
```

`digest` is what gets RSA-signed. The point of the split is that BLAKE2s
is substantially cheaper per byte than SHA-256 on the boot ROM's 16-bit
CPU (see `ZPbootROM/def88186/blake2s.def`), so a large bulk-data region
verifies much faster than hashing it with SHA-256 directly - while still
being just as tamper-resistant as version 1, because `data_digest` is
folded into the same signed hash `code_digest` is, not compared separately
against an unsigned stored value (which would give zero tamper
resistance - an attacker could just recompute a fresh, correct BLAKE2s
digest for whatever they swap in). `codeSize` itself is not separately
bound into the signed hash, but this is fail-safe rather than fail-open:
tampering `codeSize` shifts which bytes land in `code_digest` vs
`data_digest`, changing the composite digest and failing the signature
check exactly like tampering any other byte would.

**Trailer version 3** (code + chunked-data-manifest signing, also produced
by `zpbuild --codesize N` - this is what `zpbuild` actually emits now;
version 2 remains supported for verification but zpbuild no longer
produces it) exists because version 2 still hashes the *entire* data
region synchronously at boot, and for a large cartridge (up to the full
8 MB ROM window) that is minutes, not seconds, even with BLAKE2s - see the
boot-time discussion in `ZPbootROM/def88186/rsa.def`. Version 3 keeps code
verification exactly as before, but replaces "hash all of data now" with
"hash a small signed manifest of the data now, and check each chunk's own
hash later, only when it's actually about to be used":

```
code_digest     = BLAKE2s(payload[0..codeSize))
manifest[i]     = BLAKE2s(payload[codeSize+16384*i .. codeSize+16384*(i+1)))
                  (last chunk short if the data region isn't a multiple of
                  16384 bytes; chunk_count = ceil(dataSize/16384))
manifest_digest = BLAKE2s(manifest[0] || manifest[1] || ... || manifest[chunk_count-1])
digest          = SHA256(header || code_digest || manifest_digest)
```

Only `code_digest` and `manifest_digest` are verified by the boot ROM,
synchronously, before it will run any code or trust `manifest[]` - see
`ZPbootROM/def88186/rsa.def`'s `rsa_verify_composite_manifest`. The bulk
data itself is **not** hashed at boot. Checking any one chunk's actual
content against `manifest[i]` is deferred to whenever cartridge code is
about to load that chunk (DMA it out of ROM) - see "Runtime chunk
verification" below. This is still genuinely tamper-resistant, not just
corruption detection: the per-chunk checker code lives inside the same
RSA-verified code region, and `manifest[]` is only ever trusted after the
signed `manifest_digest` has verified, so an attacker can neither forge a
chunk's expected hash nor strip the check out of the code without
breaking the signature. `chunk_count` is never stored in the trailer -
both signer and verifier derive it from `codeSize` and the header's
`romSize`, so (as with `codeSize` itself) tampering it can only shift the
split to a different, still-checked, wrong digest - never bypass the check.

### Runtime chunk verification (trailer version 3 only)

Once the boot ROM has verified `code_digest` and `manifest_digest` and
jumped to the cartridge entry point, `manifest[]` sits at bank `$E1`
offset `$016C` (right after `codeSize`, per the table above) and stays
memory-mapped read-only for the CPU's whole lifetime - ordinary cartridge
code can read it directly, no privileged call needed. Each 16384-byte data
chunk `i` in `[codeSize, romSize)` is hardware-gated: reads of it (whether a
direct `LDA` or a DMA transfer - both go through the same memory-access path)
return a poison pattern until the chunk has been verified, and the only way
to verify one is a GBA-SWI-style call into the boot ROM itself. This isn't
advisory - a cartridge cannot opt out of it, and cannot forge a "verified"
result, because the verified/unverified state isn't ordinary writable memory.

**Calling convention**: `COP #$FF` with the chunk index in `X` (16-bit index
mode, i.e. `P.X` clear). `$FF` is a reserved signature byte recognized only
when a Boot ROM is mapped; every other `COP #imm` value keeps the normal
65C816-style behavior (vectors through the cartridge's own `$00:FFE4`
handler, untouched by any of this). The call vectors to a fixed address,
`$E0:0004`, analogous to GBA's fixed SWI vector - a permanently-stable slot
distinct from the `$E0:0000` hardware-reset entry, so it never drifts as the
Boot ROM's own code grows. Control returns via a normal `RTI`, using the same
pushed `(PB, PC, P)` convention as any other `COP`/`BRK`.

**What the call does** (see `ZPbootROM/def88186/rsa.def`'s `verify_chunk`):
compute `BLAKE2s` over the 16384 bytes at `codeSize + i*16384` (reusing the
same `blake2s_hash` routine already used for `code_digest`/
`manifest_digest` - no separate crypto implementation), compare against
`manifest[i]`, and on a match, mark chunk `i` verified. On mismatch, the
routine just returns without marking it - there's no separate error/status
channel; a still-unverified chunk simply keeps reading as poison afterward,
which *is* the failure signal. Calling it again for an already-verified
chunk is a cheap no-op (it checks the bit first) rather than a re-hash.

**The poison pattern**: within `[codeSize, romSize)`, a byte at an even
global payload offset reads as `0xDE`, an odd offset as `0xAF` - so a
naturally-aligned word read comes back as the bytes `DE AF` in memory order.
This applies uniformly to direct CPU reads and to DMA transfers sourced from
ROM, since both go through the same gated read path - but the two paths
check different things, for a reason worth spelling out (this took two
passes to get right):

**Why "PB == 0xE0" alone is not a sound privilege check.** This ISA has no
execute-privilege/ring concept - bank `$E0` is just ordinary mapped,
executable ROM, so *any* code can reach `PB == 0xE0` with a plain `JML`, not
just a genuine `COP #$FF` call. Two consequences, both closed:

- *Read side, exploited via DMA*: DMA transfers run across many
  master-clock ticks, asynchronously from CPU execution. A cartridge could
  queue a transfer sourced from the poisoned region (while `PB` is still its
  own bank - queuing requires writing the 9-byte `$D80020-$D8028` sequence,
  which only cartridge code can do), then immediately `JML` into *any*
  Boot ROM code (not `verify_chunk` - literally anywhere) purely to hold
  `PB == 0xE0` while the transfer's later ticks fired, reading real
  unverified bytes out through a channel that never checked anything. Fixed
  by capturing a `privileged` flag on `DMAConfig` *once*, at the exact
  moment `DMAController::queueDMA()` runs (`CPU::getPB() == 0xE0` at that
  instant) - a cartridge can never make this true for a transfer it queues
  itself, and nothing it does afterward can retroactively change an
  already-captured flag. `CPU::readByteForDMA()` checks this captured value
  instead of live `PB`; ordinary CPU reads (`CPU::readByte()`) still check
  live `PB`, which is safe there since a single instruction's read has no
  time-of-check/time-of-use gap. `verify_chunk`'s own hash computation
  legitimately depends on DMA too (`blake2s_hash_multibank` stages the chunk
  through WRAM via a real DMA transfer it queues itself, at `PB == 0xE0`),
  which is exactly why this couldn't just be "DMA reads of the gated region
  are never exempt, full stop" - that would have broken verification
  entirely.
- *Write side, exploited directly, no DMA needed*: a cartridge can `JML`
  straight into `verify_chunk`'s own bit-setting tail, with attacker-chosen
  `BITMAP_BYTE_IDX`/`BIT_MASK` already sitting in WRAM (ordinary, cartridge-
  writable scratch), and reach the `STA` that sets a chunk's bit *without
  the hash/compare ever running*. Fixed with a second signal alongside
  `PB == 0xE0`: `CPU::bootServiceActive`, a flag set true only by `opCOP()`'s
  own `COP #$FF` dispatch branch and cleared unconditionally by `opRTI()`.
  A raw jump into the Boot ROM never sets it, so it can't be forged that
  way either. (This flag is a plain bool, not a properly nested
  push/pull-style save - sound only because nothing in this codebase
  currently triggers NMI/ABORT automatically or lets guest code trigger
  them, and maskable IRQ is blocked by `opCOP()` setting `P.I` for the
  call's duration, so `verify_chunk` cannot currently be interrupted or
  re-entered. If NMI/ABORT ever become reachable from guest code, this
  needs to become part of the properly nested interrupt state instead.)

The per-chunk verified/unverified state itself lives in a small bitmap
exposed at bank `$E1`, immediately after the trailer/manifest blob (i.e. at
offset `$016C + chunkCount*32`, one bit per chunk). Reads of it always
return the live bitmap; writes take effect only when both `PB == 0xE0` *and*
`bootServiceActive` hold - and separately, `CPU::writeByteForDMA()` (the
path any DMA-driven write goes through) refuses to write there *at all*,
unconditionally, regardless of any privilege signal, since there is no
legitimate reason for DMA to ever write this region (`verify_chunk`'s own
bit-set is always a plain `STA`, never DMA) - simpler and more robust than
trying to reuse the read side's trigger-time-privilege mechanism for a case
that has no legitimate use to preserve. A cartridge therefore has no path to
mark a chunk "verified" other than asking the Boot ROM to actually check it.

This needs no RSA: per-chunk verification is sound precisely *because* the
checker runs from inside the already-RSA-verified Boot ROM and `manifest[]`
is only trusted once its own signed `manifest_digest` has checked out - not
because the check itself carries any new cryptographic authority. Deferring
this to load time (rather than hashing everything at boot) is what makes an
8 MB cartridge with a modest code region boot in seconds instead of minutes,
while still making it impossible to consume unverified data undetected.

For any version, `ROM::load()` reads and shape-checks the trailer
(magic + sane siglen, plus the extra codeSize field for version 2 and 3,
plus the manifest for version 3) but does not verify the signature.
`System::loadROM()` then caches the raw 64-byte header and the trailer
verbatim (`ROM::getRawHeader()` / `ROM::getTrailer()`), and `System::reset()`
maps them read-only into CPU memory at bank `$E1` (right after the Boot
ROM at `$E0`) once the cartridge bus connects - `$E1:0000-003F` is the
header, `$E1:0040-` is the trailer (magic/version/keysize/siglen, the
32-byte digest, the 256-byte signature, the 4-byte codeSize at `$E1:0168`
for version 2/3, and the manifest starting at `$E1:016C` for version 3).
For version 3, the chunk-verified bitmap from the previous section
immediately follows the manifest, at `$016C + chunkCount*32` -
`CPU::configureDataGating()` registers it as a distinct read/privileged-write
region rather than part of the plain ROM-backed header/trailer bytes, which
is what enforces the write restriction described above.
This is what lets the Boot ROM's `rsa_verify` (see
`ZPbootROM/def88186/rsa.def`) re-hash the header and payload (payload is
already visible at bank `$00`) and check the signature without any of it
passing through the cartridge's own read/write window. `rsa_verify`
dispatches on the trailer version byte at `$E1:0044` to pick the version-1,
version-2 (composite), or version-3 (composite + manifest) path.
Loading an unsigned (`version == 1`) ROM leaves bank `$E1` unmapped (note:
this is the *header's* `version` field, distinct from the *trailer's*
version byte discussed above, which only exists for signed ROMs).

## Example

```
Offset(h) 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F

00000000  5A 50 42 00 01 00 40 00 00 10 00 00 00 00 00 00  ZPB...@.........
00000010  54 65 73 74 20 47 61 6D 65 00 00 00 00 00 00 00  Test Game.......
00000020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000030  44 65 76 20 54 65 61 6D 00 00 00 00 00 00 00 00  Dev Team........
00000040  [ROM data starts here...]
```

This example shows:
- Magic: "ZPB\0"
- Version: 1
- Header size: 64 (0x0040)
- ROM size: 4096 bytes (0x00001000)
- Entry point: 0x00000000
- Title: "Test Game"
- Developer: "Dev Team"
