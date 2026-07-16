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
code can read it directly, no privileged call needed. Before loading (DMA-
ing) any 16384-byte data chunk `i` out of ROM, cartridge code should:

1. Compute `BLAKE2s` over the 16384 bytes about to be loaded (the same
   algorithm as `ZPbootROM/def88186/blake2s.def` - the RFC 7693 spec in
   that file's header comment is the reference; a cartridge can compile
   its own copy, or link against the SDK's, since none of this needs the
   private key).
2. Compare the result, byte-for-byte, against `manifest[i]` at bank `$E1`
   offset `$016C + i*32`.
3. Refuse to load (or otherwise abort) on mismatch, same as the boot ROM
   does for a bad signature.

This needs no RSA and no call into the boot ROM: chunk verification is
sound precisely *because* the checker runs from inside the already-
RSA-verified code region and `manifest[]` is only trusted once its own
signed digest has checked out - not because the check itself carries any
new cryptographic authority. Deferring this to load time (rather than
hashing everything at boot) is what makes an 8 MB cartridge with a modest
code region boot in seconds instead of minutes.

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
