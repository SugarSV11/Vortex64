#!/usr/bin/env bash
# Construye Vortex64-x86_64.AppImage, un ejecutable único que corre en cualquier distro Linux
# x86-64 reciente (Fedora, Silverblue/Atomic, Ubuntu, Debian, Arch, openSUSE, SteamOS...).
#
# Reglas que hacen que sea portable:
#   1. Compilar en un sistema con glibc VIEJA (ubuntu-22.04, glibc 2.35): el AppImage solo
#      corre en sistemas con glibc igual o más nueva. Compilarlo en Fedora 44 lo ata a glibc 2.43.
#   2. NO_STRIP=1: el `strip` interno de linuxdeploy corrompe librerías modernas (RELR) y provoca
#      SIGSEGV en _dl_init (ver check-elf-integrity.py). Es obligatorio.
#   3. No se empaqueta lo que debe venir del sistema (glibc, libGL/Mesa, libX11, libxcb, drivers):
#      lo gestiona la lista de exclusión de linuxdeploy.
#
# Uso:  packaging/linux/build-appimage.sh            (usa ./build-appimage como carpeta de trabajo)
# Variables: QMAKE (ruta de qmake6), WORK, VERSION, TOOLS (donde se guardan linuxdeploy y su plugin Qt)
set -euo pipefail

SRC="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${WORK:-$SRC/build-appimage}"
TOOLS="${TOOLS:-$WORK/tools}"
APPDIR="$WORK/AppDir"
VERSION="${VERSION:-$(git -C "$SRC" describe --tags --always 2>/dev/null || echo dev)}"
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[ -n "$QMAKE" ] || { echo "No encuentro qmake6 (exporta QMAKE=/ruta/a/qmake6)"; exit 1; }

mkdir -p "$TOOLS"
fetch() { [ -x "$TOOLS/$1" ] || { curl -fsSL -o "$TOOLS/$1" "$2"; chmod +x "$TOOLS/$1"; }; }
fetch linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

echo ">> Compilando"
cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH="$("$QMAKE" -query QT_INSTALL_PREFIX)"
cmake --build "$WORK/build"

echo ">> Instalando en el AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$WORK/build"

echo ">> Empaquetando dependencias (sin strip)"
export NO_STRIP=1 APPIMAGE_EXTRACT_AND_RUN=1 QMAKE
export LD_LIBRARY_PATH="$APPDIR/usr/lib64/vortex64:$APPDIR/usr/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PATH="$TOOLS:$PATH"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
export DEPLOY_PLATFORM_THEMES=0           # sin GTK: es lo que más pesa y más rompe entre distros

APPLIB="$APPDIR/usr/lib64/vortex64"
ARGS=(--appdir "$APPDIR" --plugin qt
      --desktop-file "$SRC/packaging/linux/vortex64.desktop"
      --icon-file "$SRC/packaging/linux/icons/vortex64-256.png" --icon-filename vortex64
      --executable "$APPLIB/vortex64")
for so in "$APPLIB"/*.so; do ARGS+=(--library "$so"); done
# el ejecutable busca los plugins junto a sí mismo; AppRun solo debe lanzarlo
ARGS+=(--custom-apprun "$SRC/packaging/linux/AppRun" --output appimage)

( cd "$WORK" && "$TOOLS/linuxdeploy-x86_64.AppImage" "${ARGS[@]}" )

echo ">> Verificando integridad de las librerías empaquetadas"
"$SRC/packaging/linux/check-elf-integrity.py" "$APPDIR"

OUT="$SRC/Vortex64-x86_64.AppImage"
mv -f "$WORK"/Vortex64*-x86_64.AppImage "$OUT"
echo ">> Listo: $OUT"
