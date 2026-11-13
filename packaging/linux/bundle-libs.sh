#!/usr/bin/env bash
# Copia al AppDir las librerías que el ejecutable necesita y que el sistema destino puede no tener,
# SIN modificarlas. (linuxdeploy usa patchelf para cambiar el RPATH y su patchelf corrompe las
# librerías modernas con RELR: SIGSEGV en _dl_init. AppRun usa LD_LIBRARY_PATH, no hace falta RPATH.)
#
# Uso: bundle-libs.sh <AppDir> <qt plugins dir>
set -euo pipefail
APPDIR=${1:?AppDir}; QTPLUGINS=${2:?directorio de plugins de Qt}
LIBDIR="$APPDIR/usr/lib"; PLUGDIR="$APPDIR/usr/plugins"
APPLIB="$APPDIR/usr/lib64/vortex64"
mkdir -p "$LIBDIR" "$PLUGDIR"

# Lo que DEBE venir del sistema: glibc, GPU/GL/Vulkan, X11 base, sonido, systemd/udev, fuentes, runtime de C++
EXCLUDE='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libutil|libresolv|libnsl|libanl|libnss_.*|libBrokenLocale|libcrypt|libstdc\+\+|libgcc_s|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libGLESv2|libvulkan|libdrm|libgbm|libglapi|libX11|libX11-xcb|libxcb|libXext|libXau|libXdmcp|libasound|libpipewire-0\.3|libpulse|libpulse-simple|libudev|libsystemd|libdbus-1|libglib-2\.0|libgobject-2\.0|libgio-2\.0|libgmodule-2\.0|libfontconfig|libfreetype|libexpat|libselinux|libmount|libblkid|libwayland-client|libwayland-server|libffi|libz)\.so'

# Plugins de Qt: lo imprescindible (xcb), imágenes e iconos. Sin tema GTK.
for p in platforms/libqxcb.so xcbglintegrations imageformats iconengines platforminputcontexts; do
    [ -e "$QTPLUGINS/$p" ] || continue
    mkdir -p "$PLUGDIR/$(dirname "$p")"; cp -aL "$QTPLUGINS/$p" "$PLUGDIR/$(dirname "$p")/"
done
# las librerías de imagen KDE/OpenEXR arrastran decenas de dependencias: no se usan, fuera
rm -f "$PLUGDIR"/imageformats/libkimg_* "$PLUGDIR"/imageformats/libqjp2* "$PLUGDIR"/imageformats/libqtiff* 2>/dev/null || true

# Dependencias: ldd de todo ELF del AppDir, repetido hasta que no aparezcan nuevas
copy_deps() {
    local added=1
    while [ $added -eq 1 ]; do
        added=0
        while IFS= read -r elf; do
            while read -r name _ path _; do
                [ -f "$path" ] || continue
                base=$(basename "$name"); stem=${base%%.so*}
                [[ "$base.so" =~ $EXCLUDE ]] && continue
                [[ "${stem}.so" =~ $EXCLUDE ]] && continue
                [ -e "$LIBDIR/$base" ] && continue
                cp -L "$path" "$LIBDIR/$base"; added=1
            done < <(LD_LIBRARY_PATH="$LIBDIR:$APPLIB:${LD_LIBRARY_PATH:-}" ldd "$elf" 2>/dev/null | grep -E '=> /')
        done < <(find "$APPDIR/usr" -type f \( -name '*.so*' -o -perm -u+x \) -exec sh -c 'head -c4 "$1" | grep -q ELF' _ {} \; -print)
    done
}
copy_deps
echo "libs empaquetadas: $(ls "$LIBDIR" | wc -l)"
