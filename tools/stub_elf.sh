#!/bin/sh
# Builds the smallest cartridge ELF there is (a few instructions per processor, tools/stub_cart/) into <dir>/stub.elf, for tests that need
# something for zpbuild to sign. Needs ../ZPdevtools/executables.
set -e
cd "$(dirname "$0")/.."
D=${1:?usage: stub_elf.sh <dir>}
X=../ZPdevtools/executables
$X/cpuasm tools/stub_cart/cpu.asm "$D/stub_cpu.bin" > /dev/null
$X/ppuasm tools/stub_cart/ppu.asm -o "$D/stub_ppu.bin" > /dev/null
$X/apuasm tools/stub_cart/apu.asm "$D/stub_apu.bin" > /dev/null
$X/zplink -o "$D/stub.elf" --cpu "$D/stub_cpu.bin@0x8000" --ppu "$D/stub_ppu.bin@0x100000" --apu "$D/stub_apu.bin@0x200000" --entry 0x8000 > /dev/null
