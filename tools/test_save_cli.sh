#!/bin/sh
# The SDL frontend's .sav handling, headless (docs/cart-save.md): a tiny cartridge with 2 KiB of save RAM that stores player 1's buttons
# into SRAM[0] in a loop (zero held: SRAM[0] becomes 0), run on zeropoint_sdl. Checks the file is created / picked up / never overwritten
# when the wrong size / suppressed by --no-save / redirected by --save, and that a recording and its replay never touch it.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
bad=0
check() { if [ "$1" = ok ]; then echo "  ok  : $2"; else echo "  FAIL: $2"; bad=$((bad+1)); fi; }
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
SDL=./build/bin/zeropoint_sdl

python3 - "$W/cart.zpb" <<'PY'
import struct, sys
code = bytes([0x3D, 0x32, 0x00, 0xD8, 0x55, 0x00, 0x00, 0xC0,   # lda $D80032 ; sta $C00000
              0x10, 0x00, 0x00, 0x00])                           # jmp $00:0000
hdr = b"ZPB\0" + bytes([1, 2 << 4]) + struct.pack("<HII", 64, len(code), 0) + b"savecli".ljust(32, b"\0") + b"test".ljust(16, b"\0")
assert len(hdr) == 64
open(sys.argv[1], "wb").write(hdr + code)
PY
size() { wc -c < "$1" | tr -d ' '; }
byte() { od -An -tu1 -j"$2" -N1 "$1" | tr -d ' '; }
run() { timeout 60 $SDL "$W/cart.zpb" --no-verify --frames 30 "$@" > "$W/out.log" 2>&1 || true; }

echo "no .sav yet"
run
[ -f "$W/cart.sav" ] && [ "$(size "$W/cart.sav")" = 2048 ] && check ok "cart.sav is created next to the ROM, 2048 bytes" || check no "cart.sav is created next to the ROM, 2048 bytes"
[ "$(byte "$W/cart.sav" 0)" = 0 ] && [ "$(byte "$W/cart.sav" 1)" = 255 ] && check ok "the guest's store reached it (SRAM[0] = 0), the rest is blank (\$FF)" || check no "the guest's store reached it"

echo "an existing .sav"
python3 -c "
d = bytearray([0xFF] * 2048); d[0] = 0x99; d[1] = 0x42
open('$W/cart.sav', 'wb').write(d)"
run
[ "$(byte "$W/cart.sav" 0)" = 0 ] && [ "$(byte "$W/cart.sav" 1)" = 66 ] && check ok "loaded (byte 1 = \$42 kept) and written back (byte 0 overwritten by the guest)" || check no "loaded and written back"

echo "--no-save and --save"
rm -f "$W/cart.sav"
run --no-save
[ ! -f "$W/cart.sav" ] && check ok "--no-save: no file" || check no "--no-save: no file"
run --save "$W/elsewhere.sav"
{ [ -f "$W/elsewhere.sav" ] && [ ! -f "$W/cart.sav" ]; } && check ok "--save FILE: the file goes there" || check no "--save FILE: the file goes there"

echo "a file of the wrong size"
head -c 100 /dev/zero > "$W/cart.sav"
run
{ [ "$(size "$W/cart.sav")" = 100 ] && grep -q "not using it" "$W/out.log"; } && check ok "left alone, and reported" || check no "left alone, and reported"

echo "a recording and its replay"
python3 -c "
d = bytearray([0xFF] * 2048); d[1] = 0x42
open('$W/cart.sav', 'wb').write(d)"
cp "$W/cart.sav" "$W/before.sav"
mkdir "$W/m"
cp "$W/cart.zpb" "$W/m/cart.zpb"
cp "$W/before.sav" "$W/m/cart.sav"
timeout 60 $SDL "$W/m/cart.zpb" --no-verify --record "$W/m/rec.zpm" --frames 40 > "$W/rec.log" 2>&1 || true
cmp -s "$W/before.sav" "$W/m/cart.sav" && check ok "recording never writes the .sav" || check no "recording never writes the .sav"
./build/bin/zp_movie info "$W/m/rec.zpm" 2>&1 | grep -q "starts with a 2048-byte save" && check ok "the movie carries the starting save" || check no "the movie carries the starting save"
rm -f "$W/m/cart.sav"
timeout 60 $SDL --play "$W/m/rec.zpm" --frames 400 > "$W/play.log" 2>&1 || true
{ grep -q "Movie finished" "$W/play.log" && [ ! -f "$W/m/cart.sav" ]; } && check ok "the replay works from the embedded save and creates no .sav" || { check no "the replay works from the embedded save and creates no .sav"; tail -3 "$W/play.log"; }

if [ -x ./build/bin/test_qt_save ]; then
    echo "the Qt frontend (offscreen)"
    mkdir "$W/qt"
    QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy timeout 120 ./build/bin/test_qt_save "$W/qt" > "$W/qt.log" 2>&1 && check ok "test_qt_save" || { grep FAIL "$W/qt.log"; check no "test_qt_save"; }
fi

[ "$bad" = 0 ] && echo "all ok" || { echo "$bad FAILED"; exit 1; }
