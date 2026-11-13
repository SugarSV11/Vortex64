#!/bin/bash
# The memory that Project64 Video (Glide64) sees must be the memory the core gave the game.
#   test-video-memory.sh <build dir> <rom>      (needs an X display: DISPLAY, as in run-gui-tests.sh)
# A game that is not in the database (a ROM hack: its header is not in Config/Project64.rdb) gets 8 MB from the core
# (the Expansion Pak). The plugin took the size from the entry of the database, which for a
# game that is not there says 4 MB: the addresses above 4 MB were cut to 4 MB, the display lists that such a game builds
# there (B3313) were read from the wrong place and nothing was drawn, while the game went on and the audio played.
# The plugin prints what it sees with VORTEX_VIDEO_TRACE=1; the ROM of the unknown game is a copy of the given one
# with another title and country (made here, never kept: the checksums of the header are the game's own, the game checks them).
BUILD=${1:?build dir}; ROM=${2:?rom}
TMP=$(mktemp -d)
RC=0
python3 - "$ROM" "$TMP/unknown.z64" <<'PY'
import sys
data = bytearray(open(sys.argv[1], 'rb').read())
magic = bytes(data[:4])
data[0x3E if magic == bytes.fromhex('80371240') else (0x3F if magic == bytes.fromhex('37804012') else 0x3D)] = ord('J')   # another country (the byte in the order of the file): its entry is not in the database
data[0x20:0x34] = b'VORTEX MEMORY PROBE '                                    # title
open(sys.argv[2], 'wb').write(data)
PY
Run() { # name rom
    local Name=$1 Rom=$2
    export XDG_CONFIG_HOME=$TMP/config-$Name XDG_DATA_HOME=$TMP/data-$Name XDG_CACHE_HOME=$TMP/cache-$Name
    mkdir -p "$XDG_CONFIG_HOME/vortex64" "$XDG_DATA_HOME"
    printf "[Settings]\nCurrent Language=English\n[Plugin]\nGraphics Dll=libVortex64-video.so\n[Game Directory]\nDirectory=%s/\nUse Selected=1\n" "$(dirname "$Rom")" > "$XDG_CONFIG_HOME/vortex64/Project64.cfg"
    VORTEX_VIDEO_TRACE=1 VORTEX_TEST_ROM="$Rom" VORTEX_SHOTS="$TMP/shots" VORTEX_SHOTS_STEPS="playenv,wait:4000,quit" timeout -s KILL 60 "$BUILD/bin/vortex64" 2>&1 | grep -E "^\[video\] (RomOpen|display list)" > "$TMP/$Name.log"
}
Check() { # name expected-size-or-empty
    local Name=$1 Expected=$2
    local Line; Line=$(grep "RomOpen" "$TMP/$Name.log" | head -1)
    if [ -z "$Line" ]; then echo "FAIL video memory $Name: the plugin printed nothing (no game started?)"; RC=1; return; fi
    local Seen Mask Alloc
    Seen=$(echo "$Line" | sed -E 's/.*RDRAM size ([0-9a-f]+) .*/\1/'); Mask=$(echo "$Line" | sed -E 's/.*mask ([0-9a-f]+)\).*/\1/'); Alloc=$(echo "$Line" | sed -E 's/.*core ([0-9a-f]+).*/\1/')
    echo "video memory $Name: the plugin sees $Seen (mask $Mask), the core gave $Alloc"
    [ "$((16#$Seen))" -eq "$((16#$Alloc))" ] && [ "$((16#$Mask))" -eq "$((16#$Seen - 1))" ] || { echo "FAIL video memory $Name: the plugin does not see the memory of the core"; RC=1; }
    [ -z "$Expected" ] || [ "$((16#$Alloc))" -eq "$Expected" ] || { echo "FAIL video memory $Name: the core gave $Alloc, expected $(printf %x "$Expected")"; RC=1; }
    grep -q "display list" "$TMP/$Name.log" || { echo "FAIL video memory $Name: no display list was processed"; RC=1; }
}
Run unknown "$TMP/unknown.z64"
Check unknown $((0x800000))   # (the case that failed: the plugin said 4 MB)
Run known "$ROM"
Check known ""
[ $RC -eq 0 ] && echo PASS || echo FAILED
rm -rf "$TMP"
exit $RC
