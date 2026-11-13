#!/bin/bash
# Wrapper of CTest: runs the offscreen suite with header-only fake ROMs (no game needed).
# Usage: ctest-offscreen.sh <build dir>      (VORTEX_TEST_ROM=/path/to/real.z64 uses a real ROM)
#
# A fake ROM has no real boot code, so the two checks of the ROM information dialog that read the
# CRCs and the CIC of Super Mario 64 (rombrowser_test.cpp, "CIC-NUS-6102") cannot hold; they are the
# only failures tolerated, and only when no real ROM was given.
BUILD=${1:?build dir}
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
if [ -n "$VORTEX_TEST_ROM" ]; then
    ROM=$VORTEX_TEST_ROM; TOLERATED='^$'
else
    python3 "$HERE/make-fake-roms.py" "$TMP/roms" || exit 1
    ROM=$(ls "$TMP"/roms/*.z64 | head -1)
    TOLERATED='rombrowser_test.cpp:(278|279):'
fi
"$HERE/run-offscreen-tests.sh" "$BUILD" "$ROM" > "$TMP/out.txt" 2>&1
cat "$TMP/out.txt"
# real failures = FAIL lines that are not tolerated, tests without result, and the final FAILED of a tolerated test
BAD=$(grep -E "no result|FAIL" "$TMP/out.txt" | grep -vE "$TOLERATED" | grep -vE "^(rombrowser: )?FAILED$" || true)
if [ -n "$BAD" ]; then echo "TESTS FAILED:"; echo "$BAD"; exit 1; fi
echo "ALL TESTS PASSED"
