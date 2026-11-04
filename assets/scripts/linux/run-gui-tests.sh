#!/bin/bash
# Runs the automated GUI tests. Usage: run-gui-tests.sh <build dir> <rom>
# Uses a private Xvfb display and isolated XDG directories; nothing outside a
# temporary directory (removed at the end) is touched.
BUILD=${1:?build dir}; ROM=${2:?rom}
TMP=$(mktemp -d)
# no desktop services of the user (the screen saver request of the main window goes to the session bus)
export DBUS_SESSION_BUS_ADDRESS=unix:path=$TMP/no-bus
mkdir -m 700 -p "$TMP/run"
# the audio server keeps its place
export PIPEWIRE_RUNTIME_DIR=/run/user/1000 PULSE_RUNTIME_PATH=/run/user/1000/pulse XDG_RUNTIME_DIR=$TMP/run XDG_CONFIG_HOME=$TMP/config XDG_DATA_HOME=$TMP/data XDG_CACHE_HOME=$TMP/cache QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
Xvfb :79 -screen 0 1024x768x24 +extension GLX >"$TMP/xvfb.log" 2>&1 &
XV=$!
export DISPLAY=:79
sleep 2
timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 3 2>&1 | grep -E "^(cycle|plugin|recompiler|dialog|error|closing|closed|PASS|FAIL|FAILED)"
RC=${PIPESTATUS[0]}; RC1=$RC
# The x64 recompiler: random programs run by the interpreter and by the recompiler must leave the same
# state (a 32 bit core, a 64 bit core, each of the three block linking modes; the directed programs of
# the branches, jumps, loads/stores and arithmetic run first), the game runs on the recompiler, the debugger with it
if [ -x "$BUILD/bin/jit-fuzz-test" ]; then
    for Mode in "link=0 core64" "link=0 core32" "link=1 core64" "link=1 core32" "link=2 core64" "link=2 core32"; do
        timeout -s KILL 900 "$BUILD/bin/jit-fuzz-test" "$ROM" 1000 7 $Mode 2>&1 | grep -E "^(jit-fuzz( directed)?: [0-9]|program [0-9]|directed |FAIL|FAILED|PASS)"
        RC7=${PIPESTATUS[0]}
        [ $RC7 -ne 0 ] && RC=$RC7
    done
    LIFECYCLE_JIT=1 timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 1 2>&1 | grep -E "^(cycle|plugin|recompiler|dialog|error|closing|closed|PASS|FAIL|FAILED)"
    RC8=${PIPESTATUS[0]}
    [ $RC8 -ne 0 ] && RC=$RC8
    # the game is started and ended at once, many times (the end comes while the emulation thread starts)
    for Core in "" "LIFECYCLE_JIT=1"; do
        env $Core LIFECYCLE_STRESS=20 timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 1 2>&1 | grep -E "^(stress|dialog|error|closed|PASS|FAIL|FAILED)"
        RC14=${PIPESTATUS[0]}
        [ $RC14 -ne 0 ] && RC=$RC14 && RC8=$RC14
    done
    # a game that was ended is started again from the menu
    LIFECYCLE_RESTART=1 timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 1 2>&1 | grep -E "^(restart|dialog|error|closed|PASS|FAIL|FAILED)"
    RC15=${PIPESTATUS[0]}
    [ $RC15 -ne 0 ] && RC=$RC15 && RC8=$RC15
    # plugins that are missing or are not plugins: an error, no crash, and the game runs once they are back
    LIFECYCLE_BADPLUGIN=1 timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 1 2>&1 | grep -E "^(badplugin|cycle|plugin|recompiler|dialog|error|closing|closed|PASS|FAIL|FAILED)"
    RC13=${PIPESTATUS[0]}
    [ $RC13 -ne 0 ] && RC=$RC13 && RC8=$RC13
    # a texture pack for the video plugin (Rice naming) is read: HiRes textures were Windows-only code
    if command -v python3 >/dev/null 2>&1; then
        mkdir -p "$TMP/config-hq/vortex64" "$TMP/data-hq/vortex64/Textures/SUPER MARIO 64"
        printf '[Video]\nghq_hirs=131072\ntexenh_options=1\n' > "$TMP/config-hq/vortex64/Project64.cfg"
        python3 - "$TMP/data-hq/vortex64/Textures/SUPER MARIO 64" <<'PY'
import struct, sys, zlib
def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
rows = b''.join(b'\x00' + b''.join(bytes([(x * 4) & 255, (y * 4) & 255, 128, 255]) for x in range(64)) for y in range(64))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 64, 64, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
for crc in ('12345678', '9ABCDEF0'):
    open('%s/SUPER MARIO 64#%s#0#2_all.png' % (sys.argv[1], crc), 'wb').write(png)
PY
        env XDG_CONFIG_HOME=$TMP/config-hq XDG_DATA_HOME=$TMP/data-hq PJ64_VIDEO_REPORT=1 LIFECYCLE_JIT=1 timeout -s KILL 300 "$BUILD/bin/lifecycle-test" "$ROM" 1 > "$TMP/hq.log" 2>&1
        RC16=$?
        grep -E "hires cache|^PASS|^FAIL" "$TMP/hq.log" | sort | uniq | head -3
        [ $RC16 -ne 0 ] && { echo "FAIL hires lifecycle exit $RC16"; RC=$RC16; }
        grep -q "hires cache: 2 textures" "$TMP/hq.log" || { echo "FAIL hires pack not read"; RC=1; }
    fi
    # the recompiler with the virtual lookup of functions and with the check of their code, each with
    # its own empty settings (a game that is not in the database: its memory is not the size of the
    # default of the settings, a state brings the size of the memory)
    for Cfg in "LIFECYCLE_FUNCFIND=2 LIFECYCLE_SMMFUNC=1" "LIFECYCLE_FUNCFIND=2 LIFECYCLE_SMMFUNC=0" "LIFECYCLE_FUNCFIND=1 LIFECYCLE_SMMFUNC=0"; do
        mkdir -p "$TMP/config-lk" "$TMP/data-lk"
        rm -rf "$TMP/config-lk"/* "$TMP/data-lk"/*
        env XDG_CONFIG_HOME=$TMP/config-lk XDG_DATA_HOME=$TMP/data-lk LIFECYCLE_JIT=1 LIFECYCLE_SOAK=10 $Cfg timeout -s KILL 600 "$BUILD/bin/lifecycle-test" "$ROM" 1 2>&1 | grep -E "^(cycle|plugin|recompiler|dialog|error|closing|closed|soak|PASS|FAIL|FAILED)"
        RC10=${PIPESTATUS[0]}
        [ $RC10 -ne 0 ] && RC=$RC10 && RC8=$RC10
    done
fi
if [ -x "$BUILD/bin/debugger-test" ]; then
    timeout -s KILL 300 "$BUILD/bin/debugger-test" "$ROM" 2>&1 | grep -E "^(commands|stopped|step|breakpoint|ended|search|dialog|PASS|FAIL|FAILED)"
    RC2=${PIPESTATUS[0]}
    [ $RC2 -ne 0 ] && RC=$RC2
fi
if [ -x "$BUILD/bin/input-e2e-test" ] && [ -x "$BUILD/bin/input-capture-test" ]; then
    # what the dialog stores (keys, mouse buttons, wheel) drives the N64 pad: XTEST events -> SDL -> GetKeys
    mkdir -p "$TMP/config-e2e" "$TMP/data-e2e"
    XDG_CONFIG_HOME=$TMP/config-e2e XDG_DATA_HOME=$TMP/data-e2e QT_QPA_PLATFORM=offscreen "$BUILD/bin/input-capture-test" write 2>&1 | grep -E "^(PASS|FAIL|FAILED)"
    XDG_CONFIG_HOME=$TMP/config-e2e XDG_DATA_HOME=$TMP/data-e2e timeout -s KILL 120 "$BUILD/bin/input-e2e-test" 2>&1 | grep -E "^(ok|BAD|PASS|FAIL|FAILED)"
    RC11=${PIPESTATUS[0]}
    [ $RC11 -ne 0 ] && RC=$RC11
    # the second set: modifier pairs, arrows, letters, numbers, keypad, the rest of the mouse (every row assigned)
    mkdir -p "$TMP/config-e2eb" "$TMP/data-e2eb"
    XDG_CONFIG_HOME=$TMP/config-e2eb XDG_DATA_HOME=$TMP/data-e2eb QT_QPA_PLATFORM=offscreen "$BUILD/bin/input-capture-test" write B 2>&1 | grep -E "^(PASS|FAIL|FAILED)"
    RC17=${PIPESTATUS[0]}
    [ $RC17 -ne 0 ] && RC=$RC17
    XDG_CONFIG_HOME=$TMP/config-e2eb XDG_DATA_HOME=$TMP/data-e2eb timeout -s KILL 120 "$BUILD/bin/input-e2e-test" B 2>&1 | grep -E "^(ok|BAD|PASS|FAIL|FAILED)"
    RC17=${PIPESTATUS[0]}
    [ $RC17 -ne 0 ] && RC=$RC17
fi
if [ -x "$BUILD/bin/input-ingame-test" ] && [ -x "$BUILD/bin/input-capture-test" ]; then
    # the keyboard and the mouse of a game that runs in the main window (Qt has the focus, not an SDL window): the pad answers with
    # the pointer over the picture, the menu, the status bar and outside the window
    mkdir -p "$TMP/config-ig" "$TMP/data-ig"
    XDG_CONFIG_HOME=$TMP/config-ig XDG_DATA_HOME=$TMP/data-ig QT_QPA_PLATFORM=offscreen "$BUILD/bin/input-capture-test" write 2>&1 | grep -E "^(PASS|FAIL|FAILED)"
    XDG_CONFIG_HOME=$TMP/config-ig XDG_DATA_HOME=$TMP/data-ig timeout -s KILL 300 "$BUILD/bin/input-ingame-test" "$ROM" 2>&1 | grep -E "^(BAD|PASS|FAIL|FAILED)"
    RC20=${PIPESTATUS[0]}
    [ $RC20 -ne 0 ] && RC=$RC20
    # one key, one action (a button of the pad that is also a shortcut goes to the game only) and the pause while a window of configuration
    # is open (nested, the user had paused, every way to close it)
    if [ -x "$BUILD/bin/hotkey-pause-test" ]; then
        XDG_CONFIG_HOME=$TMP/config-ig XDG_DATA_HOME=$TMP/data-ig timeout -s KILL 300 "$BUILD/bin/hotkey-pause-test" "$ROM" 2>&1 | grep -E "^(part|ok|BAD|PASS|FAIL|FAILED)"
        RC30=${PIPESTATUS[0]}
        [ $RC30 -ne 0 ] && RC=$RC30 && RC20=$RC30
    fi
    # every key that the system identifies, every button of the mouse and the wheel: assigned with the real dialog (events of
    # the X server), then the same input to the running game; and the file of settings read by another process
    mkdir -p "$TMP/config-mx" "$TMP/data-mx"
    XDG_CONFIG_HOME=$TMP/config-mx XDG_DATA_HOME=$TMP/data-mx timeout -s KILL 900 "$BUILD/bin/input-ingame-test" "$ROM" matrix 2>&1 | grep -E "^(matrix:|NOTE|BAD|PASS|FAIL|FAILED)"
    RC21=${PIPESTATUS[0]}
    [ $RC21 -ne 0 ] && RC=$RC21 && RC20=$RC21
    for Batch in 0 4 10; do
        mkdir -p "$TMP/config-mp$Batch" "$TMP/data-mp$Batch"
        XDG_CONFIG_HOME=$TMP/config-mp$Batch XDG_DATA_HOME=$TMP/data-mp$Batch timeout -s KILL 300 "$BUILD/bin/input-ingame-test" "$ROM" matrix-write:$Batch 2>&1 | grep -E "^(matrix write|BAD|FAIL|FAILED)"
        XDG_CONFIG_HOME=$TMP/config-mp$Batch XDG_DATA_HOME=$TMP/data-mp$Batch timeout -s KILL 300 "$BUILD/bin/input-ingame-test" "$ROM" matrix-read:$Batch 2>&1 | grep -E "^(matrix:|BAD|PASS|FAIL|FAILED)" | grep -v "inputs$\|batches\|pointer"
        RC22=${PIPESTATUS[0]}
        [ $RC22 -ne 0 ] && RC=$RC22 && RC20=$RC22
    done
fi
if [ -x "$BUILD/bin/debugger-test" ]; then
    # its own settings (the watches of the first run are kept in them)
    mkdir -p "$TMP/config-jit" "$TMP/data-jit"
    XDG_CONFIG_HOME=$TMP/config-jit XDG_DATA_HOME=$TMP/data-jit PJ64_TEST_JIT=1 timeout -s KILL 300 "$BUILD/bin/debugger-test" "$ROM" 2>&1 | grep -E "^(commands|stopped|step|breakpoint|ended|search|dialog|PASS|FAIL|FAILED)"
    RC9=${PIPESTATUS[0]}
    [ $RC9 -ne 0 ] && RC=$RC9
fi
if [ -x "$BUILD/bin/script-test" ]; then
    timeout -s KILL 400 "$BUILD/bin/script-test" "$ROM" 2>&1 | grep -E "^(script|dialog|FAIL|FAILED|PASS)"
    RC3=${PIPESTATUS[0]}
    [ $RC3 -ne 0 ] && RC=$RC3
fi
if [ -x "$BUILD/bin/mainwindow-test" ]; then
    timeout -s KILL 300 "$BUILD/bin/mainwindow-test" "$ROM" 2>&1 | grep -E "^(mainwindow|dialog|FAIL|FAILED|PASS)"
    RC5=${PIPESTATUS[0]}
    [ $RC5 -ne 0 ] && RC=$RC5
fi
if [ -x "$BUILD/bin/gliden64-test" ] && [ -f "$BUILD/bin/libGLideN64.so" ]; then
    timeout -s KILL 300 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/gliden64-captures" 2>&1 | grep -E "^(plugin|capture|colors|video|dialog|FAIL|FAILED|PASS)"
    RC6=${PIPESTATUS[0]}
    [ $RC6 -ne 0 ] && RC=$RC6
    # the plugin's video thread, and the recompiler
    # every control of the settings dialog of the plugin: changed, OK, reopened (the profile of the game)
    mkdir -p "$TMP/config-rt" "$TMP/data-rt"   # (it leaves every control changed: its own settings)
    XDG_CONFIG_HOME=$TMP/config-rt XDG_DATA_HOME=$TMP/data-rt GLIDEN64_TEST_CONFIGRT=1 GLIDEN64_TEST_NO_SWITCH=1 timeout -s KILL 300 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/gliden64-captures-rt" 2>&1 | grep -E "^(plugin|config|dialog|FAIL|FAILED|PASS)"
    RC10=${PIPESTATUS[0]}
    [ $RC10 -ne 0 ] && RC=$RC10
    for Var in GLIDEN64_TEST_THREADED=1 GLIDEN64_TEST_JIT=1; do
        env $Var timeout -s KILL 300 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/gliden64-captures-$Var" 2>&1 | grep -E "^(plugin|capture|colors|video|dialog|FAIL|FAILED|PASS)"
        RC11=${PIPESTATUS[0]}
        [ $RC11 -ne 0 ] && RC=$RC11 && RC6=$RC11
    done
    # windowed -> full screen -> windowed, three times, with each video plugin; Xvfb has no window manager,
    # so a minimal one that honours _NET_WM_STATE_FULLSCREEN runs for these
    if cc -o "$TMP/minimal-wm" "$(dirname "$0")/minimal-wm.c" -lX11 2>/dev/null; then
        "$TMP/minimal-wm" >/dev/null 2>&1 &
        WM=$!
        sleep 1
        for Plugin in libGLideN64.so libProject64-video.so; do
            env GLIDEN64_TEST_CAPTURE=1 GLIDEN64_TEST_FULLSCREEN=3 GLIDEN64_TEST_SESSION=2 GLIDEN64_TEST_CHAOS=60 GLIDEN64_TEST_PLUGIN=$Plugin timeout -s KILL 600 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/gliden64-captures-fs-$Plugin" 2>&1 | grep -E "^(plugin|dialog|fullscreen|FAIL|FAILED|PASS)"
            RC12=${PIPESTATUS[0]}
            [ $RC12 -ne 0 ] && RC=$RC12 && RC6=$RC12
        done
        # Glide64's own settings sheet while the game runs: the renderer is made again after it, the game goes on drawing
        env GLIDEN64_TEST_GLIDE64CFG=1 GLIDEN64_TEST_CAPTURE=1 GLIDEN64_TEST_NO_SWITCH=1 GLIDEN64_TEST_PLUGIN=libProject64-video.so timeout -s KILL 600 "$BUILD/bin/gliden64-test" "$ROM" "$TMP/gliden64-captures-g64cfg" 2>&1 | grep -E "^(plugin|dialog|FAIL|FAILED|PASS)"
        RC19=${PIPESTATUS[0]}
        [ $RC19 -ne 0 ] && RC=$RC19 && RC6=$RC19
        # the synchronization of the presentation with the monitor, with each video plugin: 60..240 Hz (VORTEX_FAKE_REFRESH) x Auto/On/Off,
        # the speed of the game, fast forward, a change of monitor while it runs, full screen three times in every mode
        if [ -x "$BUILD/bin/present-sync-test" ]; then
            for Plugin in libGLideN64.so libProject64-video.so; do
                timeout -s KILL 900 "$BUILD/bin/present-sync-test" "$ROM" $Plugin 2>&1 | grep -E "^(plugin|display|reference|dialog|FAIL|FAILED|PASS)"
                RC24=${PIPESTATUS[0]}
                [ $RC24 -ne 0 ] && RC=$RC24 && RC6=$RC24
            done
        fi
        kill $WM 2>/dev/null
    else
        echo "fullscreen: no compiler/libX11 for the minimal window manager: not tested"
    fi
fi
if [ -x "$BUILD/bin/audio-test" ]; then
    # the audio plugin's real output (SDL's disk driver, no sound card): the volume of the settings is applied, mute is
    # silence, the game is closed and opened again with data queued
    for Case in "100|" "40|[Audio-Settings]\nVolume=40\n" "mute|[Audio]\nEnable Audio=0\n"; do
        mkdir -p "$TMP/config-au/vortex64" "$TMP/data-au"
        printf "${Case#*|}" > "$TMP/config-au/vortex64/Project64.cfg"
        env XDG_CONFIG_HOME=$TMP/config-au XDG_DATA_HOME=$TMP/data-au SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=$TMP/audio.raw QT_QPA_PLATFORM=offscreen timeout -s KILL 120 "$BUILD/bin/audio-test" "${Case%%|*}" 2>&1 | grep -E "^(output|FAIL|FAILED|PASS)"
        RC18=${PIPESTATUS[0]}
        [ $RC18 -ne 0 ] && RC=$RC18
    done
fi
# every region and every plugin and core: the game of the tests and copies of it with the country of Japan, Europe, Germany and Australia
# (games that are not in the database), each with Project64 Video, GLideN64 and the interpreter, on a real X server: a picture is on the screen.
# VORTEX_ROM_MATRIX_EXTRA="rom1:rom2" adds ROMs of your own to the matrix (they are only read)
if [ -x "$BUILD/bin/vortex64" ] && command -v python3 >/dev/null 2>&1; then
    Extra=(); IFS=: read -ra Extra <<< "${VORTEX_ROM_MATRIX_EXTRA:-}"
    python3 "$(dirname "$0")/rom-matrix.py" "$BUILD" --synthetic "$ROM" "$ROM" "${Extra[@]}" 2>&1 | grep -E "^(ROM |[^ ].* \| .* \| (PASS|BLACK|NOT RUNNING|NO DISPLAY|EXIT)|PASS|FAIL|FAILED|Traceback|Error)"
    RC23=${PIPESTATUS[0]}
    [ $RC23 -ne 0 ] && RC=$RC23
fi
# the memory the video plugin sees is the memory the core gave the game (a ROM hack that uses the Expansion Pak drew nothing)
if [ -x "$BUILD/bin/vortex64" ] && command -v python3 >/dev/null 2>&1; then
    "$(dirname "$0")/test-video-memory.sh" "$BUILD" "$ROM" 2>&1 | grep -E "^(video memory|FAIL|FAILED|PASS)"
    RC19=${PIPESTATUS[0]}
    [ $RC19 -ne 0 ] && RC=$RC19
fi
if [ -x "$BUILD/bin/common-test" ]; then
    timeout -s KILL 60 "$BUILD/bin/common-test" 2>&1 | grep -E "^(FAIL|FAILED|PASS)"
    RC4=${PIPESTATUS[0]}
    [ $RC4 -ne 0 ] && RC=$RC4
fi
echo "exit codes: lifecycle ${RC1:-0} fuzz ${RC7:-0} jit-lifecycle ${RC8:-0} debugger ${RC2:-0} debugger-jit ${RC9:-0} script ${RC3:-0} mainwindow ${RC5:-0} gliden64 ${RC6:-0} video-memory ${RC19:-0} input-ingame ${RC20:-0} rom-matrix ${RC23:-0} common ${RC4:-0}"
kill $XV 2>/dev/null
rm -rf "$TMP"
exit $RC
