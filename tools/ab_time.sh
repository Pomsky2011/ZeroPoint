#!/bin/sh
# A/B comparison of two profile_bootrom builds by user-space instruction count and cycles (perf stat), which
# unlike wall/CPU time barely move when other programs load the machine.
# usage: ab_time.sh <old-binary> <new-binary> <bootrom.bin> <cart.zpb> [frames] [--frontend]
OLD=$1; NEW=$2; BOOT=$3; CART=$4; FRAMES=${5:-1500}; EXTRA=${6:-}
stat() { perf stat -x, -e instructions:u,cycles:u "$1" "$BOOT" "$FRAMES" "$CART" $EXTRA 2>&1 >/dev/null | awk -F, '{printf "%s %.3fG  ", $3, $1/1e9}'; echo; }
echo "old: $(stat "$OLD")"
echo "new: $(stat "$NEW")"
