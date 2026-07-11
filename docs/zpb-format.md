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
- Maximum ROM size: 2 MB (to fit in main RAM)
- Entry point specifies where execution begins

## Signature Verification

The emulator's loader (`src/rom.cpp`) does **not** perform any signature
verification — it only checks the magic bytes, `version == 1`, header size,
and ROM size, then loads unconditionally. There is no signature field in
this 64-byte header at all.

This is a different (and currently incompatible) format from the *signed*
`.zpb` produced by `ZPdevtools/zpbuild` — that tool writes `version = 2` and
appends a "ZPSG" trailer (32-byte SHA-256 digest + 256-byte RSA-2048
signature) after the ROM payload. Since `src/rom.cpp:46` rejects any
`version != 1`, the emulator currently refuses to load a zpbuild-signed ROM
at all. Verifying that trailer is meant to be the boot ROM's job (see
`ZPbootROM/def88186/rsa.def`), not something the emulator's loader enforces
today — but the version check needs to accept 2 (and ignore/skip the
trailer) before a signed ROM will even load.

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
