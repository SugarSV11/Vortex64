#!/usr/bin/env python3
"""Detecta librerías ELF corruptas dentro de un AppDir o AppImage extraído.

Causa conocida del crash "SEGV en libxcb-util.so.1 al cargar libqxcb.so": un `strip`
antiguo (el que lleva linuxdeploy dentro) reescribe mal las librerías compiladas con
RELR/.init modernos y deja DT_INIT apuntando a una dirección que ya no es la sección
.init. El cargador dinámico salta ahí y revienta con SIGSEGV.

Uso: check-elf-integrity.py <directorio> ; sale con código 1 si encuentra alguna corrupta.
"""
import os
import re
import subprocess
import sys


def readelf(flag, path):
    return subprocess.run(["readelf", flag, "-W", path], capture_output=True, text=True).stdout


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def corrupt(path):
    dyn = readelf("-d", path)
    m = re.search(r"\(INIT\)\s+0x([0-9a-f]+)", dyn)
    if not m:
        return False
    init = int(m.group(1), 16)
    for line in readelf("-S", path).splitlines():
        s = re.search(r"\]\s+\.init\s+PROGBITS\s+([0-9a-f]+)", line)
        if s:
            return int(s.group(1), 16) != init
    return False


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    bad = []
    for d, _, files in os.walk(root):
        for f in files:
            p = os.path.join(d, f)
            if os.path.islink(p) or not is_elf(p):
                continue
            if corrupt(p):
                bad.append(p)
    for p in bad:
        print("CORRUPTA:", p)
    print(f"{len(bad)} librerías corruptas")
    return 1 if bad else 0


sys.exit(main())
