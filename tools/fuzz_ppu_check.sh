#!/bin/sh
# Differential check of the PPU: for each seed, fuzz_ppu in per-cycle (tick) mode and in batched (tickmany) mode
# must print the same state-hash stream. Optionally compare against another build's stream (a binary built from an
# older PPU): pass its path as the 4th argument.
# usage: fuzz_ppu_check.sh <fuzz_ppu binary> <first-seed> <last-seed> [other fuzz_ppu binary]
BIN=$1; FIRST=$2; LAST=$3; OTHER=$4
T=$(mktemp -d); bad=0; seed=$FIRST
while [ "$seed" -le "$LAST" ]; do
    "$BIN" "$seed" 500 tick > "$T/a"; "$BIN" "$seed" 500 tickmany > "$T/b"
    cmp -s "$T/a" "$T/b" || { echo "seed $seed: tick != tickmany"; bad=$((bad+1)); }
    if [ -n "$OTHER" ]; then
        "$OTHER" "$seed" 500 tick > "$T/c"
        cmp -s "$T/a" "$T/c" || { echo "seed $seed: differs from $OTHER"; bad=$((bad+1)); }
    fi
    seed=$((seed+1))
done
rm -rf "$T"; echo "$bad mismatches"; [ "$bad" -eq 0 ]
