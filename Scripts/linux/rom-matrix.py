#!/usr/bin/env python3
"""Does a game draw? For each ROM: the header (CRC1, CRC2, country, size, title), the entry of Config/Project64.rdb and
Config/Video.rdb found with the key of the core (CRC1-CRC2-C:country), the memory the core allocated and the plugin sees, the
microcode and the display lists (VORTEX_VIDEO_TRACE of Project64 Video), and whether the screen of a real run shows a picture
(a real X server, Xvfb, the whole application), with Project64 Video and with GLideN64, on the recompiler and on the interpreter.

  rom-matrix.py <build dir> [--synthetic <base rom>] [--quick] [rom ...]

--synthetic makes copies of <base rom> in a temporary directory with the country byte of another region (the copies are deleted
and never kept): they tell whether the region of a game that is not in the database changes anything. --quick: only the
recompiler with each plugin. Nothing is copied to the repository. Exit code 0 when every combination draws."""
import os, shutil, subprocess, sys, tempfile, re, struct, time

def header(path):
    with open(path, 'rb') as f:
        h = bytearray(f.read(0x40))
    size = os.path.getsize(path)
    magic = bytes(h[:4])
    if magic == bytes.fromhex('80371240'): order = 'z64 (big endian)'
    elif magic == bytes.fromhex('37804012'):
        order = 'v64 (byte swapped)'
        for i in range(0, len(h), 2): h[i], h[i + 1] = h[i + 1], h[i]
    elif magic == bytes.fromhex('40123780'):
        order = 'n64 (little endian)'
        for i in range(0, len(h), 4): h[i:i + 4] = h[i:i + 4][::-1]
    else: order = 'unknown %s' % magic.hex()
    crc1, crc2 = struct.unpack('>II', bytes(h[0x10:0x18]))
    return dict(size=size, order=order, crc1=crc1, crc2=crc2, country=h[0x3E], title=bytes(h[0x20:0x34]).decode('latin1').strip(), code=bytes(h[0x3B:0x3F]).decode('latin1'))

def read_rdb(path):
    db, cur = {}, None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\r\n')
        m = re.match(r'^\[([0-9A-Fa-f]{8}-[0-9A-Fa-f]{8}-C:[0-9A-Fa-f]+)\]$', line)
        if m: cur = db.setdefault(m.group(1).upper(), {})
        elif line.startswith('['): cur = None
        elif cur is not None and '=' in line:
            k, v = line.split('=', 1); cur[k] = v
    return db

def ident(h): return '%08X-%08X-C:%X' % (h['crc1'], h['crc2'], h['country'])

def set_country(d, country):
    """the country byte of the header (offset 0x3E of a z64 file) in the order of the bytes of the file"""
    magic = bytes(d[:4])
    index = 0x3E if magic == bytes.fromhex('80371240') else (0x3F if magic == bytes.fromhex('37804012') else 0x3D)
    d[index] = country

def synthetic(base, tmp, rdb):
    """copies of the base ROM with the country byte of other regions (the checksums of the header are the game's own: the boot
    code of the console checks them, a copy with other ones does not start). The key of the database has the country, so each
    copy is a game that is not in the database: the way of the games that are not there, in each region"""
    data = bytearray(open(base, 'rb').read())
    out = []
    for name, country in (('JPN', 0x4A), ('EUR', 0x50), ('GER', 0x44), ('AUS', 0x55)):
        d = bytearray(data)
        set_country(d, country)
        p = os.path.join(tmp, 'synthetic-unknown-' + name + '.z64'); open(p, 'wb').write(d); out.append(('synthetic-unknown-' + name, p))
    return out

def run(build, rom, plugin, interp, tmp, display):
    t = tempfile.mkdtemp(dir=tmp)
    cfg = os.path.join(t, 'c', 'project64'); os.makedirs(cfg); os.makedirs(os.path.join(t, 'd')); os.makedirs(os.path.join(t, 'r'), 0o700)
    open(os.path.join(cfg, 'Project64.cfg'), 'w').write('[Settings]\nCurrent Language=English\nForce Interpreter CPU=%d\n[Plugin]\nGraphics Dll=%s\n[Game Directory]\nDirectory=%s/\nUse Selected=1\n' % (1 if interp else 0, plugin, os.path.dirname(rom)))
    env = dict(os.environ, DISPLAY=display, XDG_CONFIG_HOME=os.path.join(t, 'c'), XDG_DATA_HOME=os.path.join(t, 'd'), XDG_RUNTIME_DIR=os.path.join(t, 'r'), DBUS_SESSION_BUS_ADDRESS='unix:path=' + t + '/nobus',
               QT_QPA_PLATFORM='xcb', SDL_VIDEODRIVER='x11', LIBGL_ALWAYS_SOFTWARE='1', VORTEX_TEST_ROM=rom, VORTEX_SHOTS=os.path.join(t, 'shots'), VORTEX_VIDEO_TRACE='1',
               VORTEX_SHOTS_STEPS='playenv,wait:6000,report:a,screen:a,wait:4000,report:b,screen:b,quit')
    p = subprocess.run([os.path.join(build, 'bin', 'vortex64')], env=env, capture_output=True, text=True, timeout=120)
    states = [l for l in p.stdout.splitlines() if l.startswith('state ')]
    running = len(states) == 2 and all('running=1' in l and 'library=0' in l for l in states)
    info = dict(running=running, exit=p.returncode, trace=[l for l in p.stderr.splitlines() if l.startswith('[video]')], shots=[os.path.join(t, 'shots', n + '.png') for n in 'ab'])
    return info

def read_png(path):
    """width, height, rows of RGB bytes of a PNG of 8 bits, colour type 2 or 6, no interlace (what Qt writes)"""
    import zlib
    d = open(path, 'rb').read()
    pos, idat, w = 8, b'', None
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8]); body = d[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b'IHDR': w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
        elif t == b'IDAT': idat += body
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat); stride = w * bpp; rows = []; prev = bytearray(stride); i = 0
    for _ in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0; b = prev[x]; c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        rows.append(line); prev = line
    return w, h, rows, bpp

def draws(png):
    if not os.path.exists(png): return (0, 0.0)
    w, h, rows, bpp = read_png(png)
    colors, lit, total = set(), 0, 0
    for y in range(295, min(h, 775)):
        row = rows[y]
        for x in range(640, min(w, 1280)):
            r, g, b = row[x * bpp], row[x * bpp + 1], row[x * bpp + 2]
            colors.add((r >> 3, g >> 3, b >> 3)); total += 1
            if r + g + b > 60: lit += 1
    return (len(colors), lit / max(total, 1))

def verdict(info, plugin):
    """a picture, of a game that runs (the window is the game's, not the library), the process ended well and, for the plugin that
    traces, display lists with triangles went through it"""
    best = max((draws(s) for s in info['shots']), key=lambda c: c[0])
    drew = best[0] >= 150 and best[1] >= 0.15
    traced = plugin != 'libVortex64-video.so' or any('display list' in l for l in info['trace'])
    if not info['running']: return 'NOT RUNNING', best
    if info['exit'] != 0: return 'EXIT %d' % info['exit'], best
    if not traced: return 'NO DISPLAY LISTS', best
    return ('PASS' if drew else 'BLACK'), best

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return 2
    build = a.pop(0); base = None; quick = False; roms = []
    while a:
        x = a.pop(0)
        if x == '--synthetic': base = a.pop(0)
        elif x == '--quick': quick = True
        else: roms.append((os.path.basename(x), x))
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    pdb = read_rdb(os.path.join(build, 'bin', 'Config', 'Project64.rdb')); vdb = read_rdb(os.path.join(build, 'bin', 'Config', 'Video.rdb'))
    tmp = tempfile.mkdtemp(prefix='rom-matrix-')
    display = ':%d' % (90 + os.getpid() % 9)
    xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '1920x1080x24', '+extension', 'GLX'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2)
    failures = 0
    try:
        if base: roms += synthetic(base, tmp, pdb)
        rows = []
        for name, path in roms:
            h = header(path); key = ident(h); e = pdb.get(key); v = vdb.get(key)
            combos = [('Project64 Video', 'libVortex64-video.so', False), ('GLideN64', 'libGLideN64.so', False)]
            if not quick: combos += [('Project64 Video, interpreter', 'libVortex64-video.so', True)]
            res = {}; trace = []
            for label, plugin, interp in combos:
                info = run(build, path, plugin, interp, tmp, display)
                res[label] = verdict(info, plugin)
                if plugin == 'libVortex64-video.so' and not interp: trace = info['trace']
            rdram = (e or {}).get('RDRAM Size', '-')
            seen = next((l for l in trace if 'RomOpen' in l), '')
            m = re.search(r'RDRAM size ([0-9a-f]+) .*core ([0-9a-f]+)', seen)
            uc = next((re.sub(r'.*crc ([0-9a-f]+): found as (-?\d+).*', r'\1=\2', l) for l in trace if 'microcode' in l), '-')
            tris = next((re.sub(r'.*#(\d+): (\d+) triangles.*', r'\2', l) for l in reversed(trace) if 'display list #' in l and '#0:' not in l), '-')
            rows.append((name, h, key, e is not None, v is not None, rdram, m.groups() if m else ('-', '-'), uc, tris, res))
            for label, r in res.items():
                if r[0] != 'PASS': failures += 1
        print('ROM | country | header order | size | RDB (database) | RDRAM config | plugin sees / core gave | microcode | triangles | ' + ' | '.join(['Project64 Video', 'GLideN64'] + ([] if quick else ['Project64 Video, interpreter'])))
        for name, h, key, inrdb, invideo, rdram, mem, uc, tris, res in rows:
            print(' | '.join([name[:34], '%s (%s)' % (chr(h['country']) if 32 < h['country'] < 127 else '?', hex(h['country'])), h['order'].split()[0], '%dMB' % (h['size'] >> 20), ('Project64.rdb ' + ('yes' if inrdb else 'no') + ', Video.rdb ' + ('yes' if invideo else 'no')),
                                    (rdram + ' MB') if rdram != '-' else 'default', '%s / %s' % (mem[0], mem[1]), uc, tris] + ['%s (%d colours)' % (r[0], r[1][0]) for r in res.values()]))
    finally:
        xvfb.terminate(); shutil.rmtree(tmp, ignore_errors=True)
    print('PASS rom-matrix' if failures == 0 else 'FAILED rom-matrix: %d combinations without a picture' % failures)
    return 0 if failures == 0 else 1

if __name__ == '__main__': sys.exit(main())
