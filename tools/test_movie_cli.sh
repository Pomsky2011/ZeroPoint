#!/bin/sh
# zp_movie end to end: author a text movie, assemble, seal (embed checkpoints), replay, tamper through the text form.
# test_movie writes the two cartridges it uses (a.zpb, b.zpb) into the work dir first.
cd "$(dirname "$0")/.." || exit 2
ROOT=$(pwd)
Z="$ROOT/build/bin/zp_movie"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
FAILED=0
ok() { echo "  ok  : $1"; }
bad() { echo "  FAIL: $1"; FAILED=1; }

"$ROOT/build/bin/test_movie" "$W" > /dev/null || { echo "test_movie failed"; exit 1; }   # leaves a.zpb and b.zpb behind
cd "$W" || exit 2

# A hand-written movie. The numbers are master cycles (movie time), not frames.
cat > script.txt <<'EOS'
zpm-text 1
verify off
boot hle
apubios hle
rom 0 a.zpb
rom 1 b.zpb
initial 0
author zp_movie test
@0       input 1 0x40 0x01 0x00
@200000  input 2 0x01 0x03 0x00
@400000  input 1 0x04 0x01 0x10
@600000  softreset
@700000  input 1 0x10 0x01 0x20
@900000  yank
@1000000 insert 1
@1100000 input 1 0x80 0x01 0x40
@1300000 hardreset
@1400000 input 1 0x01 0x01 0x80
EOS
echo "length 1600000" >> script.txt

$Z asm script.txt hand.zpm > /dev/null && ok "script assembles" || bad "asm"
$Z info hand.zpm | grep -q "checkpoints   : 0" && ok "info says it is unsealed" || bad "info"
$Z play hand.zpm | grep -q "no checkpoints" && ok "an unsealed movie plays but says nothing was verified" || bad "play unsealed"

$Z seal hand.zpm sealed.zpm --interval 300000 > seal.out && ok "seal replays it and embeds checkpoints" || bad "seal"
$Z play sealed.zpm --wav out.wav --ppm out.ppm > play.out && grep -q "checkpoints verified" play.out && ok "a sealed movie replays and verifies" || bad "play sealed"
[ -s out.wav ] && [ -s out.ppm ] && ok "--wav and --ppm produce output" || bad "wav/ppm"
FIRST=$(grep -o "final machine hash [0-9A-F]*" play.out)
$Z play sealed.zpm > play2.out && [ "$(grep -o 'final machine hash [0-9A-F]*' play2.out)" = "$FIRST" ] && ok "replaying twice gives the same final hash" || bad "repeat"

# Tamper through the text form: drop the yank. The sealed checkpoints must catch it.
$Z dis sealed.zpm sealed.txt > /dev/null
grep -v " yank" sealed.txt > tampered.txt
$Z asm tampered.txt tampered.zpm > /dev/null
if $Z play tampered.zpm > tamper.out; then
    bad "a movie without its yank still verified"
else
    grep -q "DESYNC at movie cycle" tamper.out && ok "dropping the yank is caught: $(grep -o 'state differs in: [a-z, +]*' tamper.out)" || bad "no DESYNC message"
fi

# The text form round-trips the sealed movie exactly.
$Z asm sealed.txt again.zpm > /dev/null
cmp -s sealed.zpm again.zpm && ok "dis | asm reproduces the sealed movie byte for byte" || bad "text round trip"

# Without its cartridges the movie is refused by name (the library is the movie's own directory plus --roms).
mkdir lonely && cp sealed.zpm lonely/
if (cd lonely && "$Z" play sealed.zpm > ../missing.out 2>&1); then
    bad "played without its ROMs"
else
    grep -q "not in the ROM library" missing.out && ok "a missing cartridge is refused by name" || bad "missing-ROM message"
fi
(cd lonely && "$Z" play sealed.zpm --roms "$W" > /dev/null 2>&1) && ok "...and --roms finds it again" || bad "--roms"
exit $FAILED
