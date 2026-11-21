#!/bin/sh
# Builds a v1 and a v3 signed ROM with zpbuild, then runs test_hle_cop against the real ZPbootROM.
# Needs ../ZPdevtools/executables/zpbuild, a stub ELF built here (tools/stub_elf.sh) and ../ZPbootROM/build/bootrom.bin.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
Z=../ZPdevtools/executables/zpbuild
./tools/stub_elf.sh "$W"
E=$W/stub.elf
$Z $E -o "$W/v1.zpb" --title hlev1 > /dev/null
$Z $E -o "$W/v3.zpb" --title hlev3 --codesize 4096 > /dev/null
# An unsigned ROM: header version 1, no trailer (build/test_bare.zpb is one when it's been generated).
cp build/test_bare.zpb "$W/unsigned.zpb"
./build/bin/test_hle_cop ../ZPbootROM/build/bootrom.bin "$W/v1.zpb" "$W/v3.zpb" "$W/unsigned.zpb" "$W"
