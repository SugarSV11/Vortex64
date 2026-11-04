#!/bin/bash
# Runs the tests that need no X server and no game to run (Qt "offscreen", isolated XDG directories):
# main menu, ROM browser, Settings pages, plugin configuration dialogs, plugin loading, Common.
# Usage: run-offscreen-tests.sh <build dir> <ROM> (a ROM for the browser and its ROM information
# dialog: the one used by run-gui-tests.sh; it is copied to a temporary directory as sm64.z64)
BUILD=${1:?build dir}; ROM=${2:?rom}
TMP=$(mktemp -d)
mkdir -m 700 -p "$TMP/run" "$TMP/roms"
cp "$ROM" "$TMP/roms/sm64.z64"
export XDG_RUNTIME_DIR=$TMP/run QT_QPA_PLATFORM=offscreen DBUS_SESSION_BUS_ADDRESS=unix:path=$TMP/no-bus
RC=0
run() { # name, command...
    local Name=$1; shift
    export XDG_CONFIG_HOME=$TMP/config-$Name XDG_DATA_HOME=$TMP/data-$Name
    mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    local Out
    Out=$("$@" 2>&1 | grep -E "^(PASS|FAIL|FAILED)" | tail -3)
    echo "$Name: ${Out:-no result}"
    echo "$Out" | grep -q "^PASS" || RC=1
}
run mainmenu "$BUILD/bin/mainmenu-test"
run rombrowser "$BUILD/bin/rombrowser-test" "$TMP/roms"
run vortex "$BUILD/bin/vortex-test"
# the languages (Spanish and English, applied at once, kept between runs, no text of the other one) and the layout of the windows
run plugindir "$BUILD/bin/plugindir-test"
run language "$BUILD/bin/language-test"
run layout "$BUILD/bin/layout-test"
# the vector controller of the Input dialog: geometry, states and aspect ratio at the screen scales
for Scale in 1 1.25 1.5 2; do
    QT_SCALE_FACTOR=$Scale run "controller-widget-$Scale" "$BUILD/bin/controller-widget-test"
done
# the same layout at other screen scales (HiDPI)
for Scale in 1.25 1.5 2; do
    QT_SCALE_FACTOR=$Scale run "layout-$Scale" "$BUILD/bin/layout-test"
done
# write and read must share the directories
export XDG_CONFIG_HOME=$TMP/config-settings XDG_DATA_HOME=$TMP/data-settings; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
for Mode in write read rdb lookup; do
    Out=$("$BUILD/bin/settings-test" $Mode 2>&1 | grep -E "^(PASS|FAIL|FAILED)" | tail -1); echo "settings $Mode: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
done
# every control of every page, in its own directories
export XDG_CONFIG_HOME=$TMP/config-sweep XDG_DATA_HOME=$TMP/data-sweep; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
# (the Game Status page edits the database: the test runs next to a copy of it)
mkdir -p "$TMP/sweep-bin"; cp "$BUILD/bin/settings-test" "$TMP/sweep-bin/"; cp -r "$BUILD/bin/Config" "$BUILD/bin/Lang" "$TMP/sweep-bin/" 2>/dev/null
for Mode in sweep-write sweep-read; do
    Out=$("$TMP/sweep-bin/settings-test" $Mode 2>&1 | grep -E "^(sweep|FAIL|FAILED|PASS)" | tail -4); echo "settings $Mode: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
done
for Plugin in video audio input; do
    export XDG_CONFIG_HOME=$TMP/config-$Plugin XDG_DATA_HOME=$TMP/data-$Plugin; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
    Modes="write read"; [ $Plugin = video ] && Modes="prep write read"
    for Mode in $Modes; do
        Out=$("$BUILD/bin/plugin-config-test" $Plugin $Mode 2>&1 | grep -E "^(PASS|FAIL|FAILED)" | tail -1); echo "plugin-config $Plugin $Mode: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
    done
done
export XDG_CONFIG_HOME=$TMP/config-inputcap XDG_DATA_HOME=$TMP/data-inputcap; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
for Mode in write read; do
    Out=$("$BUILD/bin/input-capture-test" $Mode 2>&1 | grep -E "^(PASS|FAIL|FAILED|row)" | tail -4); echo "input-capture $Mode: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
done
export XDG_CONFIG_HOME=$TMP/config-inputcapb XDG_DATA_HOME=$TMP/data-inputcapb; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
for Mode in write read; do
    Out=$("$BUILD/bin/input-capture-test" $Mode B 2>&1 | grep -E "^(PASS|FAIL|FAILED|row)" | tail -4); echo "input-capture $Mode B: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
done
# the look of the Input dialog: selector, cards, capsules, drawing linked to the rows, test page (both languages)
for Lang in es en; do
    for Scale in 1 1.25 1.5 2; do
        QT_SCALE_FACTOR=$Scale run "input-ui-$Lang-$Scale" "$BUILD/bin/input-ui-test" $Lang
    done
done
# a virtual gamepad (uinput): hot plug, assignment, GetKeys, unplug, plug again (the container has no udev events: inotify)
export XDG_CONFIG_HOME=$TMP/config-pad XDG_DATA_HOME=$TMP/data-pad; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
if [ -x "$BUILD/bin/input-gamepad-test" ]; then
    Out=$(SDL_JOYSTICK_DISABLE_UDEV=1 timeout -s KILL 120 "$BUILD/bin/input-gamepad-test" 2>&1 | grep -E "^(PASS|FAIL|FAILED|SKIP|BAD)" | tail -4); Rc=${PIPESTATUS[0]}
    echo "input-gamepad: ${Out:-no result}"
    echo "$Out" | grep -q "^PASS" || echo "$Out" | grep -q "^SKIP" || RC=1
fi
# the synchronization of the presentation with the monitor: the policy (Auto/On/Off), refresh rate != emulation speed, the screens read by Qt
run present-policy "$BUILD/bin/present-policy-test"
# the filters of the texture enhancement with several threads: the result is the one of the whole image (the submodule cuts the texture in
# separate bands: a seam at every cut, in every filter); the original FAILS the same test
if [ -x "$BUILD/bin/txfilter-seams-test" ] && [ -x "$BUILD/bin/txfilter-seams-original-test" ]; then
    Out=$(ASAN_OPTIONS=detect_leaks=0 timeout -s KILL 900 "$BUILD/bin/txfilter-seams-test" 2>&1 | grep -E "^(PASS|FAIL|FAILED|.*ERROR: AddressSanitizer)" | tail -2); echo "txfilter-seams: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
    if ASAN_OPTIONS=detect_leaks=0 timeout -s KILL 900 "$BUILD/bin/txfilter-seams-original-test" >/dev/null 2>&1; then echo "txfilter-seams-original: the test did not see the seams"; RC=1; else echo "txfilter-seams-original: fails as it must"; fi
fi
# the texture cache of the texture enhancement (5xBRZ / 6xBRZ filled it and corrupted the heap): the cache of Vortex64 passes under
# AddressSanitizer, and the original one of the submodule FAILS the same test (the test sees the fault)
if [ -x "$BUILD/bin/txcache-test" ] && [ -x "$BUILD/bin/txcache-original-test" ]; then
    Out=$(ASAN_OPTIONS=detect_leaks=0 timeout -s KILL 600 "$BUILD/bin/txcache-test" 2>&1 | grep -E "^(PASS|FAIL|FAILED|.*ERROR: AddressSanitizer)" | tail -2); echo "txcache: ${Out:-no result}"; echo "$Out" | grep -q "^PASS" || RC=1
    if ASAN_OPTIONS=detect_leaks=0 timeout -s KILL 600 "$BUILD/bin/txcache-original-test" >/dev/null 2>&1; then echo "txcache-original: the test did not see the fault"; RC=1; else echo "txcache-original: fails as it must"; fi
fi
run plugins "$BUILD/bin/plugins-test"
run common "$BUILD/bin/common-test"
# the folders of Vortex64 and the copy from those of Project64 (temporary directories only)
run migration "$BUILD/bin/migration-test"
# the first run, in a HOME and XDG directories made by the test: a new installation (empty library, no recents, favourites, history, saves or
# paths of the person who runs it, the defaults of the registration) and an old one of Project64 (synthetic data brought once, the old folders as they were)
run first-run-new "$BUILD/bin/first-run-test" new
run first-run-legacy "$BUILD/bin/first-run-test" legacy
rm -rf "$TMP"
exit $RC
