#!/usr/bin/env bash
# Runs the emulator's test suites in one go and prints ok / FAIL / SKIP per suite, then a total. Exit status is the number of failures.
#   run_tests.sh [quick|boot|all]     quick: self-contained unit tests (seconds). boot: the real Boot ROM suites (minutes, needs ../ZPbootROM and
#                                     ../ZPdevtools built). all (default): both, plus determinism, movies and the layers microcode.
#   LOG=dir     keep each suite's output in dir/<name>.log (default: a temp dir, printed for any failure)
# A suite whose inputs are missing is a SKIP, never an ok. Needs `make` done in build/.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
ZP="$ROOT/ZeroPoint"
BIN="$ZP/build/bin"
DEV="$ROOT/ZPdevtools"
BOOT="$ROOT/ZPbootROM"
MODE=${1:-all}
LOG=${LOG:-$(mktemp -d)}
mkdir -p "$LOG"
fails=0; skips=0; oks=0

# t NAME CMD...: runs CMD in a scratch directory (a few tests write files into their working directory), 15 minute cap, ok on exit 0
t() {
    local name=$1; shift
    mkdir -p "$LOG/cwd"
    if (cd "$LOG/cwd" && timeout 900 "$@") > "$LOG/$name.log" 2>&1; then
        printf '  ok   : %s\n' "$name"; oks=$((oks + 1))
    else
        printf '  FAIL : %s (exit %s, see %s/%s.log)\n' "$name" "$?" "$LOG" "$name"; fails=$((fails + 1))
    fi
}
skip() { printf '  SKIP : %s (%s)\n' "$1" "$2"; skips=$((skips + 1)); }
need() { [ -e "$1" ] || { skip "$2" "missing $1"; return 1; }; }

cd "$ZP" || exit 2
[ -x "$BIN/test_cpu" ] || { echo "build first: cd build && make -j\$(nproc)" >&2; exit 2; }

DEMO="$DEV/ucode/layers/demo/build/demo.zpb"       # a v2-signed cartridge that exercises both layers, sprites, the window and the line blitter
RETAIL="$BOOT/build/bootrom.bin"
DEVBOOT="$BOOT/build/bootrom_dev.bin"
NOPPU="$BOOT/tests/handoff/build/noppu.zpb"
WITHPPU="$BOOT/tests/handoff/build/withppu.zpb"

if [ "$MODE" = quick ] || [ "$MODE" = all ]; then
    echo "== unit tests"
    for n in test_cpu test_ppu test_dma test_apu_clock test_mmp test_line_blit test_blit_order test_tile_attrs test_tiledraw_clip \
             test_tile_lut test_tile_rowclip test_row_queue test_queue_timing test_frame_pacer test_stream test_serial test_diag_rom \
             test_boot_rom test_white_fill; do
        t "$n" "$BIN/$n"
    done
    t fuzz_queue "$BIN/fuzz_queue" 1 12
    need "$BOOT/def88186/trig_model.py" "boot_trig_hle" && t test_boot_trig_hle "$BIN/test_boot_trig" --hle "$BOOT/def88186/trig_model.py"
    need "$BOOT/build/jingle.zpa" "zpa_engine_hle" && t test_zpa_engine_hle "$BIN/test_zpa_engine" --hle "$BOOT/build/jingle.zpa" 200
    need "$DEMO" "blake2s_engine" && t test_blake2s_engine "$BIN/test_blake2s_engine" "$DEMO"
fi

if [ "$MODE" = all ]; then
    echo "== layers microcode"
    if need "$DEV/executables/ppuasm" "layers"; then
        python3 "$DEV/ucode/layers/gen_layers.py" > "$LOG/gen_layers.log" 2>&1
        for seed in 1 2 3; do t "test_layers_seed$seed" "$BIN/test_layers" "$DEV/ucode/layers/build/layers.bin" "$seed"; done
    fi
    echo "== determinism and movies"
    t test_determinism_hle "$BIN/test_determinism" none "$DEMO" 2
    t test_batched_hle "$BIN/test_batched" none "$DEMO" 2
    t fuzz_determinism_golden "$BIN/fuzz_determinism" "$LOG" --golden-check "$ZP/tools/determinism_golden.txt"
    t test_movie "$BIN/test_movie" "$LOG"
    t test_movie_cli sh "$ZP/tools/test_movie_cli.sh"
fi

if [ "$MODE" = boot ] || [ "$MODE" = all ]; then
    echo "== real Boot ROM"
    if need "$RETAIL" "boot suites"; then
        need "$NOPPU" "boot_handoff" && t test_boot_handoff "$BIN/test_boot_handoff" "$RETAIL" "$NOPPU" "$WITHPPU"
        need "$DEMO" "boot_jingle" && t test_boot_jingle "$BIN/test_boot_jingle" "$RETAIL" "$DEMO"
        need "$NOPPU" "splash" && t test_splash bash "$ZP/tools/test_splash.sh"
        need "$DEVBOOT" "exception" && t test_exception sh "$ZP/tools/test_exception.sh"
        need "$DEMO" "determinism_boot" && t test_determinism_boot "$BIN/test_determinism" "$RETAIL" "$DEMO" 8
        need "$DEMO" "latch_skip" && t test_latch_skip "$BIN/test_latch_skip" "$RETAIL" "$DEMO"
    fi
fi

echo
echo "$oks ok, $fails FAIL, $skips skipped (logs in $LOG)"
exit $fails
