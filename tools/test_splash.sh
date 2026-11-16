#!/usr/bin/env bash
# Boots the real ZPbootROM through the whole System and checks its splash frames byte for byte against
# tools/splash_golden.txt. The splash draws everything with TILEDRAW and the line blitter, so a change to either blitter
# or to the VOC registers its PPU image is DMA'd over shows up here (a stray byte in $00FC once turned on a row clip and
# blanked the lightning image).
#
#   test_splash.sh      needs ../ZPbootROM built (build.sh) and its handoff cartridges (tests/handoff/run_test.sh)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DUMP=${DUMP_SPLASH:-"$HERE/../build/bin/dump_splash"}
BOOTROM="$ROOT/ZPbootROM/build/bootrom.bin"
CART="$ROOT/ZPbootROM/tests/handoff/build/noppu.zpb"
[ -x "$DUMP" ] || { echo "build dump_splash first ($DUMP)" >&2; exit 2; }
[ -f "$BOOTROM" ] || { echo "run ZPbootROM/build.sh first" >&2; exit 2; }
[ -f "$CART" ] || { echo "run ZPbootROM/tests/handoff/run_test.sh first (it builds $CART)" >&2; exit 2; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
frames=$(grep -v '^#' "$HERE/splash_golden.txt" | awk '{print $1}' | tr '\n' ' ')
"$DUMP" "$BOOTROM" "$CART" "$TMP/f" $frames > /dev/null 2>&1

fail=0
while read -r frame want; do
    [ -z "$frame" ] && continue
    got=$(sha256sum "$TMP/f_$frame.rgba" | awk '{print $1}')
    if [ "$got" = "$want" ]; then echo "  ok  : splash frame $frame"; else echo "  FAIL: splash frame $frame ($got)"; fail=1; fi
done < <(grep -v '^#' "$HERE/splash_golden.txt")
[ $fail -eq 0 ] && echo "all ok"
exit $fail
