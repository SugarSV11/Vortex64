#!/bin/bash
# Runs vortex64 in a private Xvfb with isolated XDG directories and a library of fixtures, saving screenshots.
#   vortex-shots.sh <build dir> <out dir> [steps]
# The steps are those of Source/Vortex64-gui/Vortex/ShotScript.h.
BUILD=${1:?build dir}; OUT=${2:?output dir}
STEPS=${3:-"seed,shot:library,page:recent,shot:recent,page:favorites,shot:favorites,page:disks,select:0,shot:disks,quit"}
TMP=$(mktemp -d)
export DBUS_SESSION_BUS_ADDRESS=unix:path=$TMP/no-bus
mkdir -m 700 -p "$TMP/run"
export XDG_RUNTIME_DIR=$TMP/run XDG_CONFIG_HOME=$TMP/config XDG_DATA_HOME=$TMP/data XDG_CACHE_HOME=$TMP/cache QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$XDG_CONFIG_HOME/vortex64" "$XDG_DATA_HOME" "$TMP/roms" "$OUT"
python3 "$(dirname "$0")/make-fake-roms.py" "$TMP/roms"
# the first run questions (language, directory) are answered by the settings
printf "[Settings]\nCurrent Language=English\n[Game Directory]\nDirectory=%s/\nUse Selected=1\n[Debugger]\nDebugger=1\n" "$TMP/roms" > "$XDG_CONFIG_HOME/vortex64/Project64.cfg"
# EXTRA_CFG: more lines for Project64.cfg (a [Plugin] section to choose the video plugin, [Video] options, ...)
[ -n "$EXTRA_CFG" ] && printf "%b" "$EXTRA_CFG" >> "$XDG_CONFIG_HOME/vortex64/Project64.cfg"
Xvfb :78 -screen 0 1920x1080x24 +extension GLX >"$TMP/xvfb.log" 2>&1 &
XV=$!
export DISPLAY=:78
sleep 1
VORTEX_TEST_ROM="${VORTEX_TEST_ROM:-}" VORTEX_SHOTS="$OUT" VORTEX_SHOTS_STEPS="$STEPS" timeout -s KILL 120 "$BUILD/bin/vortex64" 2>&1 | { if [ "$RAW" = all ]; then cat; elif [ -n "$RAW" ]; then tail -30; else grep -E "^shot|^grid|rror|not found"; fi; } 
kill $XV 2>/dev/null
[ -n "$KEEP" ] && echo "kept $TMP" || rm -rf "$TMP"
