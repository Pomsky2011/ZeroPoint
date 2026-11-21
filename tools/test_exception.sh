#!/bin/sh
# The Boot ROMs' exception entries end to end (docs/exceptions.md): builds the exception_vectors cartridge and the
# exception_handler image from ZPdevtools, then runs test_exception against ZPbootROM's retail and dev Boot ROMs.
# Needs ../ZPdevtools/executables (cpuasm, zplink, zpbuild) and ../ZPbootROM/build/{bootrom,bootrom_dev}.bin (ZPbootROM/build.sh).
# Takes about half a minute: the lock cases wait out the real Boot ROM's RSA check.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=../ZPdevtools/executables
E=../ZPdevtools/examples/cpu
RETAIL=../ZPbootROM/build/bootrom.bin
DEV=../ZPbootROM/build/bootrom_dev.bin
[ -f "$RETAIL" ] && [ -f "$DEV" ] || { echo "build the Boot ROMs first: ../ZPbootROM/build.sh"; exit 2; }

$D/cpuasm $E/exception_vectors.asm "$W/vec.bin" > /dev/null
$D/zplink -o "$W/vec.elf" --cpu "$W/vec.bin@0x8000" --entry 0x8000 > /dev/null
$D/zpbuild "$W/vec.elf" -o "$W/exception_vectors.zpb" --title EXCVEC > /dev/null
$D/cpuasm $E/exception_handler.asm "$W/handler.bin" > /dev/null
./build/bin/test_exception "$RETAIL" "$DEV" "$W/exception_vectors.zpb" "$W/handler.bin"
