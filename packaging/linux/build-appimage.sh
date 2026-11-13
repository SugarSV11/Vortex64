#!/usr/bin/env bash
# Construye Vortex64-x86_64.AppImage, un ejecutable único que corre en cualquier distro Linux
# x86-64 reciente (Fedora, Silverblue/Atomic, Ubuntu, Debian, Arch, openSUSE, SteamOS...).
#
# Reglas que hacen que sea portable:
#   1. Compilar en un sistema con glibc VIEJA (ubuntu-22.04, glibc 2.35): el AppImage solo
#      corre en sistemas con glibc igual o más nueva. Compilarlo en Fedora 44 lo ata a glibc 2.43.
#   2. Las librerías se copian SIN modificar (bundle-libs.sh): el patchelf/strip de linuxdeploy corrompe
#      librerías modernas (RELR) y provoca SIGSEGV en _dl_init; check-elf-integrity.py lo comprueba.
#   3. No se empaqueta lo que debe venir del sistema (glibc, libGL/Mesa, libX11, libxcb, drivers):
#      lo gestiona la lista EXCLUDE de bundle-libs.sh.
#
# Uso:  packaging/linux/build-appimage.sh            (usa ./build-appimage como carpeta de trabajo)
# Variables: QMAKE (ruta de qmake6), WORK, VERSION, TOOLS (donde se guarda appimagetool)
set -euo pipefail

SRC="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${WORK:-$SRC/build-appimage}"
TOOLS="${TOOLS:-$WORK/tools}"
APPDIR="$WORK/AppDir"
VERSION="${VERSION:-$(git -C "$SRC" describe --tags --always 2>/dev/null || echo dev)}"
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake || true)}"
[ -n "$QMAKE" ] || { echo "No encuentro qmake6 (exporta QMAKE=/ruta/a/qmake6)"; exit 1; }

mkdir -p "$TOOLS"
APPIMAGETOOL="$TOOLS/appimagetool-x86_64.AppImage"
[ -x "$APPIMAGETOOL" ] || { curl -fsSL -o "$APPIMAGETOOL" \
  https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
  chmod +x "$APPIMAGETOOL"; }

echo ">> Compilando"
cmake -S "$SRC" -B "$WORK/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib64 -DCMAKE_PREFIX_PATH="$("$QMAKE" -query QT_INSTALL_PREFIX)"
cmake --build "$WORK/build"

echo ">> Instalando en el AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$WORK/build"

echo ">> Empaquetando dependencias (sin modificarlas)"
"$SRC/packaging/linux/bundle-libs.sh" "$APPDIR" "$("$QMAKE" -query QT_INSTALL_PLUGINS)" "$("$QMAKE" -query QT_INSTALL_LIBS)"

# un AppImage hueco (sin ejecutable o sin Qt) no debe llegar a publicarse
for must in usr/lib64/vortex64/vortex64 usr/lib64/vortex64/libVortex64-video.so usr/lib/libQt6Core.so.6 \
            usr/lib/libQt6Widgets.so.6 usr/lib/libSDL2-2.0.so.0 usr/plugins/platforms/libqxcb.so; do
    [ -e "$APPDIR/$must" ] || { echo "ERROR: falta $must en el AppDir"; exit 1; }
done
cp "$SRC/packaging/linux/AppRun" "$APPDIR/AppRun"; chmod +x "$APPDIR/AppRun"
cp "$SRC/packaging/linux/vortex64.desktop" "$APPDIR/vortex64.desktop"
cp "$SRC/packaging/linux/icons/vortex64-256.png" "$APPDIR/vortex64.png"
cp "$SRC/packaging/linux/icons/vortex64-256.png" "$APPDIR/.DirIcon"

echo ">> Verificando integridad de las librerías empaquetadas"
"$SRC/packaging/linux/check-elf-integrity.py" "$APPDIR"

OUT="$SRC/Vortex64-x86_64.AppImage"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" --no-appstream "$APPDIR" "$OUT"
echo ">> Listo: $OUT"
