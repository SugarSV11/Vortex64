#!/bin/bash
# Installs the build into a temporary prefix, makes the prefix read-only (what /usr, an AppImage or a
# Flatpak is), and runs the application from it with its own XDG directories: it has to start, run a
# game on the recompiler and leave the prefix untouched. Usage: test-readonly-install.sh <build dir> <rom>
BUILD=${1:?build dir}; ROM=${2:?rom}
TMP=$(mktemp -d)
cleanup() { chmod -R u+w "$TMP" 2>/dev/null; rm -rf "$TMP"; kill $XV 2>/dev/null; }
trap cleanup EXIT
cmake --install "$BUILD" --prefix "$TMP/inst" >/dev/null || { echo "FAIL install"; exit 1; }
find "$TMP/inst" -type f -exec sha256sum {} + | sort -k2 > "$TMP/before.txt"
chmod -R a-w "$TMP/inst"
mkdir -m 700 -p "$TMP/run" "$TMP/config" "$TMP/data"
export DBUS_SESSION_BUS_ADDRESS=unix:path=$TMP/no-bus XDG_RUNTIME_DIR=$TMP/run XDG_CONFIG_HOME=$TMP/config XDG_DATA_HOME=$TMP/data XDG_CACHE_HOME=$TMP/cache
export PIPEWIRE_RUNTIME_DIR=/run/user/1000 PULSE_RUNTIME_PATH=/run/user/1000/pulse
export QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
Xvfb :78 -screen 0 1024x768x24 +extension GLX >"$TMP/xvfb.log" 2>&1 &
XV=$!
export DISPLAY=:78
sleep 2
RC=0
# the GUI opens the game (the main window; the first run shows the welcome dialog only without a game)
timeout -s KILL 60 "$TMP/inst/bin/vortex64" "$ROM" >"$TMP/gui.log" 2>&1 &
GUI=$!
sleep 25
if kill -0 $GUI 2>/dev/null; then echo "gui: running after 25 s"; else echo "FAIL gui: ended early"; tail -5 "$TMP/gui.log"; RC=1; fi
# a screenshot of the window proves a picture is drawn
if command -v import >/dev/null 2>&1; then
    import -window root "$TMP/shot.png" 2>/dev/null && python3 - "$TMP/shot.png" <<'PY' || { echo "FAIL gui: the screen is blank"; RC=1; }
import sys
try:
    from PIL import Image
    im = Image.open(sys.argv[1]).convert("RGB").resize((64, 48))
    colors = len(set(im.getdata()))
    print("gui: %d colors on the screen" % colors)
    sys.exit(0 if colors > 20 else 1)
except ImportError:
    sys.exit(0)
PY
fi
kill -TERM $GUI 2>/dev/null; sleep 5; kill -KILL $GUI 2>/dev/null
find "$TMP/inst" -type f -exec sha256sum {} + | sort -k2 > "$TMP/after.txt"
if cmp -s "$TMP/before.txt" "$TMP/after.txt" && [ -z "$(find "$TMP/inst" -newer "$TMP/before.txt")" ]; then echo "prefix: untouched"; else echo "FAIL prefix: changed"; RC=1; fi
# what the program wrote went to the XDG directories
[ -d "$TMP/config/vortex64" ] && echo "config: $(find "$TMP/config" -type f | wc -l) files under XDG_CONFIG_HOME" || { echo "FAIL no configuration written"; RC=1; }
[ $RC -eq 0 ] && echo PASS || echo FAILED
exit $RC
