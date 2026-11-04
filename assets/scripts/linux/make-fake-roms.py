#!/usr/bin/env python3
"""Creates header-only N64 ROM / 64DD disk fixtures to fill the Vortex64 library in tests and screenshots.

    make-fake-roms.py <dir>

The files hold a valid header (z64, big endian) and zeros: they can be listed, never run.
"""
import os
import struct
import sys
import zlib

GAMES = [
    ("Super Mario 64 (USA)", "SUPER MARIO 64", "E", 8),
    ("The Legend of Zelda - Ocarina of Time (USA)", "THE LEGEND OF ZELDA", "E", 32),
    ("GoldenEye 007 (USA)", "GOLDENEYE", "E", 12),
    ("Mario Kart 64 (USA)", "MARIOKART64", "E", 12),
    ("Star Fox 64 (USA)", "STARFOX64", "E", 12),
    ("Banjo-Kazooie (USA)", "BANJO-KAZOOIE", "E", 16),
    ("Donkey Kong 64 (USA)", "DONKEY KONG 64", "E", 32),
    ("Perfect Dark (USA)", "PERFECT DARK", "E", 32),
    ("Paper Mario (USA)", "PAPER MARIO", "E", 40),
    ("F-Zero X (USA)", "F-ZERO X", "E", 16),
    ("The Legend of Zelda - Majora's Mask (USA)", "ZELDA MAJORA'S MASK", "E", 32),
    ("Super Smash Bros. (Europe)", "SMASH BROTHERS", "P", 16),
    ("Mario Party 2 (Japan)", "MARIOPARTY2", "J", 32),
    ("Pokemon Snap (USA)", "POKEMON SNAP", "E", 16),
    ("Wave Race 64 (USA)", "WAVE RACE 64", "E", 8),
]
DISKS = [
    ("F-Zero X Expansion Disk (Japan)", "J"),
    ("Mario Artist - Paint Studio (Japan)", "J"),
    ("SimCity 64 (Japan)", "J"),
    ("Doubutsu no Mori Disk (Japan)", "J"),
]


def rom(title, country, megabytes):
    data = bytearray(0x1000)
    data[0:4] = bytes([0x80, 0x37, 0x12, 0x40])
    crc = zlib.crc32(title.encode())
    data[0x10:0x18] = struct.pack(">II", crc, crc ^ 0x5A5A5A5A)
    data[0x20:0x34] = title.upper().encode("ascii", "replace")[:20].ljust(20, b" ")
    data[0x3B] = ord("N")
    data[0x3C:0x3E] = b"XX"
    data[0x3E] = ord(country)
    return bytes(data), megabytes * 1024 * 1024


def disk():
    data = bytearray(0x1000)
    data[5] = 0x00
    data[6:8] = struct.pack(">H", 0x0010)
    data[0x1C:0x20] = struct.pack(">I", 0x80100000)
    return bytes(data), 0x03DEC800


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, title, country, mb in GAMES:
        head, size = rom(title, country, mb)
        with open(os.path.join(out, name + ".z64"), "wb") as f:
            f.write(head)
            f.truncate(size)
    for name, country in DISKS:
        head, size = disk()
        with open(os.path.join(out, name + ".ndd"), "wb") as f:
            f.write(head)
            f.truncate(size)


main()
