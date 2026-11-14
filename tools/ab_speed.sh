#!/bin/sh
# Wall-clock real-time ratio (average of the console seconds profile_bootrom prints) of several builds/modes,
# alternating rounds so machine load and clock changes hit all of them. Each spec is "label=binary[ extra args]".
# usage: ab_speed.sh <bootrom.bin> <cart.zpb> <frames> <rounds> "label=binary args" ...
BOOT=$1; CART=$2; FRAMES=$3; ROUNDS=$4; shift 4
r=0
while [ $r -lt "$ROUNDS" ]; do
    line=""
    for spec in "$@"; do
        label=${spec%%=*}; rest=${spec#*=}; bin=${rest%% *}; extra=""
        [ "$rest" != "$bin" ] && extra=${rest#* }
        ratio=$("$bin" "$BOOT" "$FRAMES" "$CART" $extra | awk '/console s/ {gsub("x","",$4); s+=$4; n++} END {if (n) printf "%.2f", s/n}')
        line="$line $label=${ratio}x"
    done
    echo "$line"
    r=$((r+1))
done
