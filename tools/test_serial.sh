#!/bin/sh
# The debug serial port end to end (docs/debug-protocol.md): the unit tests (against the real Boot ROM too, if built), then
# a guest program on the SDL frontend with a dev unit - banner out, input echoed back - then the same session recorded as a
# movie and replayed, whose output must be byte-identical; then the Qt frontend (test_qt_serial, if built).
# Needs ../ZPdevtools/executables (cpuasm, zplink, zpbuild); uses ../ZPbootROM/build/bootrom.bin + bootrom_dev.bin (retail + dev Boot ROM) when they exist.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=../ZPdevtools/executables
BOOT=../ZPbootROM/build/bootrom.bin
BOOT_DEV=../ZPbootROM/build/bootrom_dev.bin
bad=0
check() { if [ "$1" = ok ]; then echo "  ok  : $2"; else echo "  FAIL: $2"; bad=$((bad+1)); fi; }

{ [ -f "$BOOT" ] && [ -f "$BOOT_DEV" ]; } || { BOOT=; BOOT_DEV=; }   # the real Boot ROM legs need both (ZPbootROM/build.sh)
./build/bin/test_serial "$W" $BOOT $BOOT_DEV > "$W/unit.log" 2>&1 || { tail -20 "$W/unit.log"; bad=$((bad+1)); }
tail -1 "$W/unit.log"

$D/cpuasm ../ZPdevtools/examples/cpu/serial_echo.asm "$W/echo.bin" > /dev/null
$D/zplink -o "$W/echo.elf" --cpu "$W/echo.bin@0x8000" --entry 0x8000 > /dev/null
$D/zpbuild "$W/echo.elf" -o "$W/echo.zpb" --title ECHO > /dev/null
printf 'ping' > "$W/in.txt"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
want=$(printf 'Hello from ZeroPoint!\nping')

echo "guest program on zeropoint_sdl --dev-unit"
timeout 60 ./build/bin/zeropoint_sdl "$W/echo.zpb" --dev-unit --serial-out "$W/out.txt" --serial-in "$W/in.txt" --frames 120 > /dev/null 2>&1 || true
[ "$(cat "$W/out.txt" 2>/dev/null)" = "$want" ] && check ok "banner out, input echoed back" || check no "banner out, input echoed back (got: $(cat "$W/out.txt" 2>/dev/null))"

echo "recorded and replayed"
mkdir "$W/m" && cp "$W/echo.zpb" "$W/m/"
timeout 60 ./build/bin/zeropoint_sdl "$W/m/echo.zpb" --dev-unit --record "$W/m/rec.zpm" --serial-out "$W/m/out1.txt" --serial-in "$W/in.txt" --frames 120 > /dev/null 2>&1 || true
./build/bin/zp_movie info "$W/m/rec.zpm" | grep -q 'serial bytes in 4' && check ok "the movie holds the 4 typed bytes" || check no "the movie holds the 4 typed bytes"
./build/bin/zp_movie seal "$W/m/rec.zpm" "$W/m/sealed.zpm" > /dev/null 2>&1 && check ok "sealed with checkpoints" || check no "sealed with checkpoints"
timeout 60 ./build/bin/zeropoint_sdl --play "$W/m/sealed.zpm" --serial-out "$W/m/out2.txt" --frames 400 > "$W/m/play.log" 2>&1 || true
grep -q DESYNC "$W/m/play.log" && check no "replay did not desync" || check ok "replay did not desync"
cmp -s "$W/m/out1.txt" "$W/m/out2.txt" && [ "$(cat "$W/m/out2.txt")" = "$want" ] && check ok "replayed output is byte-identical" || check no "replayed output is byte-identical"
frac=$(./build/bin/zp_movie play "$W/m/sealed.zpm" 2>&1 | grep -o '[0-9]*/[0-9]* checkpoints verified' | cut -d' ' -f1)
[ -n "$frac" ] && [ "${frac%/*}" = "${frac#*/}" ] && check ok "zp_movie play verifies every checkpoint ($frac)" || check no "zp_movie play verifies every checkpoint (${frac:-none})"

if [ -x ./build/bin/test_qt_serial ]; then
    echo "Qt frontend (offscreen): console, --serial-out, record and replay"
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy timeout 120 ./build/bin/test_qt_serial "$W" "$W/echo.zpb" > "$W/qt.log" 2>&1 && check ok "test_qt_serial" || { grep FAIL "$W/qt.log"; check no "test_qt_serial"; }
fi

if [ -x ../ZPdevtools/c_compiler/def88186cc ]; then
    echo "C SDK (zp_serial.h) compiled with def88186cc"
    cat > "$W/ser.c" <<'CEOF'
#include <zeropoint/zeropoint.h>
int main(void)
{
    char msg[4];
    msg[0] = 'o'; msg[1] = 'k'; msg[2] = 10; msg[3] = 0;
    zp_serial_puts(msg);
    zp_serial_flush();
    while (1) {
        if (zp_serial_ready()) zp_serial_putc(zp_serial_getc());
    }
    return 0;
}
CEOF
    DT=$(cd ../ZPdevtools && pwd)
    $DT/c_compiler/def88186cc -DZP_CC_DOS -I$DT/include "$W/ser.c" -o "$W/ser.asm" > /dev/null 2>&1
    printf '.org $8000\n.include "%s/lib/crt0_entry.asm"\n.include "%s/ser.asm"\n.include "%s/lib/zp_mmio.asm"\n.include "%s/lib/zp_long.asm"\n' "$DT" "$W" "$DT" "$DT" > "$W/cser.asm"
    $D/cpuasm "$W/cser.asm" "$W/cser.bin" > /dev/null
    $D/zplink -o "$W/cser.elf" --cpu "$W/cser.bin@0x8000" --entry 0x8000 > /dev/null
    $D/zpbuild "$W/cser.elf" -o "$W/cser.zpb" --title CSER > /dev/null
    printf 'xy' > "$W/cin.txt"
    timeout 60 ./build/bin/zeropoint_sdl "$W/cser.zpb" --dev-unit --serial-out "$W/cout.txt" --serial-in "$W/cin.txt" --frames 120 > /dev/null 2>&1 || true
    [ "$(cat "$W/cout.txt" 2>/dev/null)" = "$(printf 'ok\nxy')" ] && check ok "a C program prints and echoes through zp_serial.h" || check no "a C program prints and echoes through zp_serial.h"
fi

[ $bad -eq 0 ] && echo "all serial end-to-end checks passed"
[ $bad -eq 0 ]
