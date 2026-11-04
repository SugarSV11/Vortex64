#!/bin/bash
# windowed -> full screen -> windowed (several times) with each video plugin on several screens: 1080p, 1080p with
# QT_SCALE_FACTOR=2 (HiDPI), a vertical monitor (1080x1920), a small one. Xvfb has no window manager: the minimal one
# of this directory honours _NET_WM_STATE_FULLSCREEN. Usage: run-fullscreen-matrix.sh <build dir> <rom>
BUILD=${1:?build dir}; ROM=${2:?rom}
TMP=$(mktemp -d)
export DBUS_SESSION_BUS_ADDRESS=unix:path=$TMP/no-bus
mkdir -m 700 -p "$TMP/run"
export PIPEWIRE_RUNTIME_DIR=/run/user/1000 PULSE_RUNTIME_PATH=/run/user/1000/pulse XDG_RUNTIME_DIR=$TMP/run QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
cc -o "$TMP/minimal-wm" "$(dirname "$0")/minimal-wm.c" -lX11 || { echo "no compiler/libX11"; exit 2; }
RC=0
run() { # name size scale
    local Name=$1 Size=$2 Scale=$3 Disp=:77
    Xvfb $Disp -screen 0 ${Size}x24 +extension GLX >"$TMP/xvfb-$Name.log" 2>&1 &
    local XV=$!
    export DISPLAY=$Disp
    sleep 2
    "$TMP/minimal-wm" >/dev/null 2>&1 &
    local WM=$!
    sleep 1
    for Plugin in libGLideN64.so libProject64-video.so; do
        rm -rf "$TMP/config" "$TMP/data"; mkdir -p "$TMP/config" "$TMP/data"
        local Out
        Out=$(env XDG_CONFIG_HOME=$TMP/config XDG_DATA_HOME=$TMP/data QT_SCALE_FACTOR=$Scale GLIDEN64_TEST_CAPTURE=1 GLIDEN64_TEST_FULLSCREEN=4 GLIDEN64_TEST_SESSION=1 GLIDEN64_TEST_PLUGIN=$Plugin timeout -s KILL 600 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/captures-$Name-$Plugin" 2>&1 | grep -E "^(fullscreen|FAIL|FAILED|PASS)")
        local Rc=${PIPESTATUS[0]}
        echo "$Name $Plugin (scale $Scale): $(echo "$Out" | tail -1) exit $Rc"
        echo "$Out" | grep -E "^fullscreen" | tail -2
        [ $Rc -ne 0 ] && RC=1
    done
    kill $WM $XV 2>/dev/null; wait $WM $XV 2>/dev/null
}
run 1080p 1920x1080 1
run hidpi 1920x1080 2
run vertical 1080x1920 1
run small 800x600 1
rm -rf "$TMP"
exit $RC
