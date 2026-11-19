#!/usr/bin/env bash
# Builds the two-layer + sprite microcode (ZPdevtools/ucode/layers) and runs the tile-row queue and layers tests.
#   test_layers.sh [seed...]      LAYERS_ALLPHASES=1 also runs the saturation test over all 64 fine-Y phase pairs
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BIN="$HERE/../build/bin"
LAYERS="$ROOT/ZPdevtools/ucode/layers"
[ -x "$BIN/test_layers" ] && [ -x "$BIN/test_row_queue" ] || { echo "build test_layers and test_row_queue first" >&2; exit 2; }
[ -x "$ROOT/ZPdevtools/executables/ppuasm" ] || { echo "build ZPdevtools first (ppuasm)" >&2; exit 2; }
python3 "$LAYERS/gen_layers.py" > /dev/null
"$BIN/test_row_queue" | tail -1
for seed in "${@:-1 2 3}"; do
    "$BIN/test_layers" "$LAYERS/build/layers.bin" "$seed" | grep -E 'FAIL|all ok' | tail -1
done
