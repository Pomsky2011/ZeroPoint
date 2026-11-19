#!/usr/bin/env bash
# Make zeropoint_qt the default app for .zpb ROMs (per user, no root): registers the application/x-zeropoint-rom
# MIME type (*.zpb, or a file starting "ZPB"), installs a zeropoint-qt.desktop that runs the binary with the ROM
# path, and sets it as the default handler.
#
#   qt/desktop/install-desktop.sh [path/to/zeropoint_qt]     (default: build/bin/zeropoint_qt in this checkout)
#   qt/desktop/install-desktop.sh --uninstall
#
# The .desktop file holds the absolute path of the binary, so re-run this if you move it.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
APPS="$DATA/applications"; MIME="$DATA/mime"

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$APPS/zeropoint-qt.desktop" "$MIME/packages/zeropoint-rom.xml"
    update-mime-database "$MIME"; update-desktop-database "$APPS"
    echo "removed"; exit 0
fi

EXE="$(readlink -f "${1:-$HERE/../../build/bin/zeropoint_qt}")"
[ -x "$EXE" ] || { echo "no executable at $EXE (build it, or pass the path)" >&2; exit 2; }

mkdir -p "$APPS" "$MIME/packages"
install -m 644 "$HERE/zeropoint-rom.xml" "$MIME/packages/zeropoint-rom.xml"
sed "s|@EXEC@|$EXE|" "$HERE/zeropoint-qt.desktop.in" > "$APPS/zeropoint-qt.desktop"
update-mime-database "$MIME"
update-desktop-database "$APPS"
xdg-mime default zeropoint-qt.desktop application/x-zeropoint-rom
echo "zeropoint_qt ($EXE) is now the default for $(xdg-mime query default application/x-zeropoint-rom | head -1) .zpb files"
