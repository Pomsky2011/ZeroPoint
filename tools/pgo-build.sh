#!/bin/sh
# Profile-guided release build (GCC): instrument, train on the boot-ROM workload, rebuild with the profile.
# ~10% fewer cycles than the plain -O3/LTO build on the boot ROM verify phase; same behavior. Opt-in and local:
# the profile is tied to this compiler version and source tree, so redo it after changing the core.
#
#   tools/pgo-build.sh <bootrom.bin> <cart.zpb> [more carts...]     -> build-pgo/bin/*
set -e
[ $# -ge 2 ] || { echo "usage: $0 <bootrom.bin> <cart.zpb> [more carts...]" >&2; exit 1; }
BOOT=$(realpath "$1"); shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build-pgo"
DATA="$OUT/pgo-data"
rm -rf "$DATA"; mkdir -p "$OUT" "$DATA"

cmake -S "$ROOT" -B "$OUT" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fprofile-generate -fprofile-update=single -fprofile-dir=$DATA" \
    -DCMAKE_EXE_LINKER_FLAGS="-fprofile-generate" >/dev/null
cmake --build "$OUT" -j"$(nproc)" --target profile_bootrom

for cart in "$@"; do
    "$OUT/profile_bootrom" "$BOOT" 3500 "$(realpath "$cart")" >/dev/null
done

# Same build dir on purpose: the .gcda names encode the object paths of the instrumented build.
cmake -S "$ROOT" -B "$OUT" \
    -DCMAKE_CXX_FLAGS="-fprofile-use -fprofile-correction -Wno-missing-profile -fprofile-dir=$DATA" \
    -DCMAKE_EXE_LINKER_FLAGS="" >/dev/null
cmake --build "$OUT" -j"$(nproc)"
echo "PGO build in $OUT"
