#!/usr/bin/env bash
# Refreshes bootroms/ (the Boot ROM and APU BIOS binaries a release ships next to the emulator) from ../ZPbootROM.
#
#   tools/update_bootroms.sh           rebuild ZPbootROM (build.sh, reproducible) and copy the images in
#
# The ZPbootROM source is private; only these binaries are published, in the public domain (CC0 1.0, bootroms/LICENSE.txt).
# CI can't build them, so they are committed: run this when ZPbootROM changes, commit bootroms/, and the release job puts the
# directory into every platform zip and makes a standalone zip of it (.github/workflows/build.yml). Refuses a ZPbootROM tree with
# uncommitted changes in its source (the README says which commit the binaries came from), apart from the generated load_tiles.def.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZP="$(cd "$HERE/.." && pwd)"
BOOT="${ZPBOOTROM:-$ZP/../ZPbootROM}"
[ -f "$BOOT/build.sh" ] || { echo "no ZPbootROM checkout at $BOOT (set ZPBOOTROM=path)" >&2; exit 2; }

dirty=$(git -C "$BOOT" status --porcelain --untracked-files=no | grep -v ' def88186/load_tiles.def$' || true)
if [ -n "$dirty" ]; then
    echo "ZPbootROM has uncommitted changes; commit them first so the binaries name a real commit:" >&2
    echo "$dirty" >&2
    exit 1
fi
untracked=$(git -C "$BOOT" status --porcelain | grep '^??' || true)
[ -z "$untracked" ] || { echo "ZPbootROM has untracked files; commit or ignore them first:" >&2; echo "$untracked" >&2; exit 1; }

(cd "$BOOT" && ./build.sh > /dev/null)
commit=$(git -C "$BOOT" rev-parse --short HEAD)
rev=$(sed -n 's/.*CORE_REVISION = \([0-9]*\);.*/\1/p' "$ZP/include/system.h" | head -1)

OUT="$ZP/bootroms"
mkdir -p "$OUT/dev"
cp "$BOOT/build/bootrom.bin" "$OUT/bootrom.bin"
cp "$BOOT/build/bootrom_dev.bin" "$OUT/dev/bootrom_dev.bin"
cp "$BOOT/build/bios_full.bin" "$OUT/bios_full.bin"
retail=$(stat -c%s "$OUT/bootrom.bin"); apu=$(stat -c%s "$OUT/bios_full.bin")

cat > "$OUT/README.txt" <<TXT
ZeroPoint Boot ROM and APU BIOS (binaries only; the source is not published)
Built from ZPbootROM $commit for ZeroPoint core revision $rev.
License: public domain (CC0 1.0), see LICENSE.txt. The binaries only: the source is not part of that.

In short:
  bootrom.bin           is the Boot ROM for the retail system.
  dev/bootrom_dev.bin   is the Boot ROM for the dev system.
  bios_full.bin         is the APU BIOS.

  bootrom.bin          RETAIL Boot ROM, $retail bytes (bank \$E0 code, \$E1 PPU splash + boot jingle).
                       Verifies the cartridge's RSA-2048 signature (v1/v2/v3 trailers) and halts on any failure.
                       No setting can skip the check. Use this one for normal play and for movies.
  dev/bootrom_dev.bin  DEV Boot ROM. Identical, except that on a dev unit (zeropoint_sdl --dev-unit, a host-only latch)
                       it skips signature verification, its exception entries ask the host for a handler image over
                       the debug UART, and holding all four shoulder inputs of player 1 while it boots erases the
                       cartridge's save RAM (docs/cart-save.md). With the latch clear it behaves like retail. Do not use
                       it as "the" Boot ROM: a movie pins the Boot ROM by SHA-256.
  bios_full.bin        the APU BIOS, $apu bytes: the ZPA sequencer engine the Boot ROM uses to play the boot jingle.

Run:   zeropoint_sdl game.zpb --boot bootroms/bootrom.bin --apu-bios bootroms/bios_full.bin
       (zeropoint_qt: set the Boot ROM path in the settings dialog)
Without --boot / --apu-bios the emulator uses its built-in HLE Boot ROM and APU BIOS (it also verifies signatures). A cartridge
sees a different APU state at hand-off depending on which is in use (docs/boot-handoff.md): reset-halt the APU before loading
your own APU program.

SHA256SUMS.txt lists the hashes (sha256sum -c SHA256SUMS.txt).
TXT
(cd "$OUT" && sha256sum bootrom.bin dev/bootrom_dev.bin bios_full.bin > SHA256SUMS.txt && cat SHA256SUMS.txt)
echo "bootroms/ now holds ZPbootROM $commit (core revision $rev)"
