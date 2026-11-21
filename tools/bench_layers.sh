#!/bin/zsh
# Unpaced wall-time bench of the layers demo (headless, native HLE boot): tools/bench_layers.sh [exe] [frame] [runs] [outprefix]
# zeropoint_sdl paces to 60 Hz so its wall time says nothing; dump_splash steps flat out. A frame is 1/60 s of console
# time (core revision 13; it was ~1.3 ms). Prints the wall time per run and the md5 of the final latched frame (A/B correctness: same hash = same pixels).
zmodload zsh/datetime
exe=${1:-build/bin/dump_splash}; frame=${2:-20000}; runs=${3:-3}; out=${4:-${TMPDIR:-/tmp}/bench_layers}
cart=${ZP_LAYERS_CART:-../ZPdevtools/ucode/layers/demo/build/demo.zpb}
for i in $(seq 1 $runs); do
  s=$EPOCHREALTIME; $exe none $cart $out $frame >/dev/null 2>&1; e=$EPOCHREALTIME
  printf '%.2f s\n' $((e - s))
done
md5sum ${out}_${frame}.rgba
