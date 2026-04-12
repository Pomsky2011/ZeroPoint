#!/usr/bin/env bash
# Cross-compile ZeroPoint for Windows x64 using MinGW-w64 (from Linux)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-windows-x64"
TOOLCHAIN="${SCRIPT_DIR}/cmake/toolchain-mingw64-x64.cmake"

# ─── Dependency check ────────────────────────────────────────────────────────

check_pkg() {
    pacman -Qi "$1" &>/dev/null || { echo "  missing: $1"; return 1; }
    return 0
}

check_aur_pkg() {
    pacman -Qi "$1" &>/dev/null || { echo "  missing (AUR): $1"; return 1; }
    return 0
}

echo "==> Checking dependencies..."
MISSING=0

check_pkg mingw-w64-gcc       || MISSING=1
check_pkg mingw-w64-crt       || MISSING=1
check_pkg mingw-w64-headers   || MISSING=1
check_aur_pkg mingw-w64-sdl2                  || MISSING=1
check_aur_pkg mingw-w64-vulkan-headers        || MISSING=1
check_aur_pkg mingw-w64-vulkan-icd-loader     || MISSING=1
check_aur_pkg mingw-w64-qt6-base              || MISSING=1

if [[ $MISSING -ne 0 ]]; then
    echo ""
    echo "Install missing packages with:"
    echo "  sudo pacman -S mingw-w64-toolchain"
    echo "  yay -S mingw-w64-sdl2 mingw-w64-vulkan-headers mingw-w64-vulkan-icd-loader mingw-w64-qt6-base"
    exit 1
fi

echo "  all dependencies present"

# ─── Detect native Qt6 for host tools (moc/uic/rcc) ─────────────────────────

HOST_QT6_MOC="$(command -v moc 2>/dev/null || true)"
if [[ -z "$HOST_QT6_MOC" ]]; then
    # Arch puts Qt6 moc at /usr/lib/qt6/bin/moc
    HOST_QT6_MOC="$(ls /usr/lib/qt6/bin/moc 2>/dev/null || true)"
fi

if [[ -z "$HOST_QT6_MOC" ]]; then
    echo "ERROR: Could not find native Qt6 moc. Install qt6-base."
    exit 1
fi

HOST_QT6_PREFIX="$(realpath "$(dirname "$HOST_QT6_MOC")/../..")"
echo "==> Native Qt6 prefix: ${HOST_QT6_PREFIX}"

# ─── Configure ───────────────────────────────────────────────────────────────

echo "==> Configuring (build dir: ${BUILD_DIR})"
cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DQT_HOST_PATH="${HOST_QT6_PREFIX}" \
    -DCMAKE_BUILD_TYPE=Release \
    "${@}"   # pass any extra -D flags through

# ─── Build ───────────────────────────────────────────────────────────────────

JOBS="${JOBS:-$(nproc)}"
echo "==> Building with ${JOBS} jobs..."
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo ""
echo "==> Done. Executables in ${BUILD_DIR}/bin/"
echo "    To run on Windows, also ship:"
echo "      SDL2.dll  (from /usr/x86_64-w64-mingw32/bin/)"
echo "      vulkan-1.dll  (from Windows Vulkan runtime)"
echo "      Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll  (from /usr/x86_64-w64-mingw32/bin/)"
echo "      platforms/qwindows.dll  (Qt platform plugin)"
