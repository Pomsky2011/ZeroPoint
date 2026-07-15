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
| +4                             | 1    | Trailer version (currently 1)         |
| +5                             | 1    | Key size in 64-bit words (32 = 2048-bit) |
| +6                             | 2    | Signature length in bytes, little-endian (256) |
| +8                             | 32   | SHA-256 digest of `header \|\| payload` |
| +40                            | 256  | RSA-2048 signature (PKCS#1 v1.5), big-endian |

For `version == 2`, `ROM::load()` reads and shape-checks this trailer
(magic + sane siglen) but does not verify the signature. `System::loadROM()`
then caches the raw 64-byte header and the trailer verbatim
(`ROM::getRawHeader()` / `ROM::getTrailer()`), and `System::reset()` maps
them read-only into CPU memory at bank `$E1` (right after the Boot ROM at
`$E0`) once the cartridge bus connects - `$E1:0000-003F` is the header,
`$E1:0040-` is the trailer (magic/version/keysize/siglen, then the 32-byte
digest, then the 256-byte signature). This is what lets the Boot ROM's
`rsa_verify` (see `ZPbootROM/def88186/rsa.def`) re-hash the header and
payload (payload is already visible at bank `$00`) and check the signature
without any of it passing through the cartridge's own read/write window.
Loading an unsigned (`version == 1`) ROM leaves bank `$E1` unmapped.

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
