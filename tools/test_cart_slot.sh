#!/bin/sh
# Builds a v1 and a v3 signed ROM with zpbuild, then runs test_cart_slot.
# Needs ../ZPdevtools/executables/zpbuild, a stub ELF built here (tools/stub_elf.sh) and build/test_bare.zpb.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
Z=../ZPdevtools/executables/zpbuild
./tools/stub_elf.sh "$W"
E=$W/stub.elf
$Z $E -o "$W/v1.zpb" --title slotv1 > /dev/null
$Z $E -o "$W/v3.zpb" --title slotv3 --codesize 4096 > /dev/null
cp build/test_bare.zpb "$W/unsigned.zpb"
./build/bin/test_cart_slot "$W/v1.zpb" "$W/v3.zpb" "$W/unsigned.zpb"
