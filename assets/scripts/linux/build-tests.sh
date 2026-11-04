#!/bin/bash
# Builds everything and every test program (they are EXCLUDE_FROM_ALL). Usage: build-tests.sh <build dir> [extra cmake -D options...]
# The build directory is created (configured) when it does not exist.
BUILD=${1:?build dir}; shift
[ -f "$BUILD/CMakeCache.txt" ] || cmake -S "$(dirname "$0")/../.." -B "$BUILD" "$@" || exit 1
cmake --build "$BUILD" -j"$(nproc)" || exit 1
cmake --build "$BUILD" -j"$(nproc)" --target \
    audio-test common-test controller-widget-test debugger-test gliden64-test hotkey-pause-test input-capture-test input-capture-x11-test \
    input-e2e-test input-gamepad-test input-ingame-test input-ui-test jit-fuzz-test language-test layout-test lifecycle-test mainmenu-test \
    mainwindow-test migration-test first-run-test plugin-config-test plugindir-test plugins-test present-policy-test present-sync-test rombrowser-test \
    script-test settings-test txcache-original-test txcache-test txfilter-seams-original-test txfilter-seams-test vortex-test
