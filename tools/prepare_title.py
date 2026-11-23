"""Arcade attract-mode title sequence for the Mega Drive.

Runs the whole arcade board (main 6809 + sound/input MCU) in the Python
co-simulation (tools/sound/cosim.py) from the post-self-test entry (DDEA,
task set-up) and records, per frame, the foreground/background video RAM,
the scroll registers and the sprite list for the title and high-score
screens. These are converted to Mega Drive tiles, four palette lines and a
per-frame stream of tilemap writes, scroll values and sprites that
src/title.c plays back. Writes res/generated/native/title_data.c
(arcade-derived: do not distribute)."""
import json, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE / 'sound'))
from extract_arcade import ROOT, palettes  # noqa: E402
from convert_palettes import cram, rgb, distance  # noqa: E402
import cosim  # noqa: E402

# Zero-based frame777 begins the arcade demo scenery redraw (first moving
# sprites at784). Keep only the title/high-score sequence: even a partial
# recorded level would otherwise play with ATTRACT DEMO set to OFF.
FRAMES = 777

def cell_info(code, attr, bg):
    code |= (attr & 1) << 8
    g = (((attr & 0x3E) >> 1) + ((code & 0x1C0) >> 1)) if bg else (((attr & 0x1E) >> 1) + ((code & 0x1E0) >> 1))
    return code, g

def main():
    gen = ROOT / 'res/generated'
    prom = (gen / 'arcade/proms.bin').read_bytes(); pal = palettes(prom)[0]
    fg = json.loads((gen / 'arcade/fg_chars.json').read_text())
    bg = json.loads((gen / 'arcade/bg_chars.json').read_text())
    spr = json.loads((gen / 'arcade/sprites.json').read_text())
    b = cosim.Board(); m = b.main; m.pc = 0xDDEA; m.s = 0x3700; m.dp = 0x20
    frames = []
    while b.frame < FRAMES:
        b.run_frame()
        frames.append((bytes(m.ram[0:0x2000]), bytes(m.ram[0x2780:0x2800]), bytes(m.ram[0x2F80:0x3000]),
                       bytes(m.ram[0x3780:0x3800]), m.scroll0, m.scroll1))
    # colour sets of every tile and sprite cell used
    def tile_colours(bgl, code, g):
        lut = 0xC00 if bgl else 0x800; out = {}
        for p in range(4):
            c = prom[lut + 4 * g + p]
            out[p] = None if (not bgl and (c & 0x7F) == 0x7F) else cram(pal[c])
        return out
    def spr_colours(col):
        out = {}
        for p in range(16):
            c = prom[0x1000 + col * 16 + p]
            out[p] = None if (c & 0x7F) == 0x7F else cram(pal[c])
        return out
    tiles = {}; sprites = {}
    for vr, sc, sp, fl, s0, s1 in frames:
        for i in range(0, 0x2000, 2):
            bgl = i >= 0x1000; code, g = cell_info(vr[i], vr[i + 1], bgl)
            tiles.setdefault((bgl, code, g), tile_colours(bgl, code, g))
        for k in range(64):
            o = 2 * k
            if not (sc[o] or sc[o + 1] or sp[o] or sp[o + 1] or fl[o]): continue
            f = fl[o]; sz = (f >> 2) & 3; code = (sc[o] + ((f & 0x80) << 1)) & ~(sz & 1) & ~(sz & 2)
            for cy in range((sz >> 1) + 1):
                for cx in range((sz & 1) + 1):
                    cc = code + 2 * cy + cx
                    sprites.setdefault((cc, sc[o + 1] & 63), spr_colours(sc[o + 1] & 63))
    # palette lines: line 3 for sprites, 0..2 for tiles (greedy, as prepare_native)
    sets = Counter_sets = {}
    for key, cols in tiles.items():
        sets.setdefault(frozenset(c for c in cols.values() if c is not None), []).append(key)
    lines = [set(), set(), set()]
    assign = {}
    for cs in sorted(sets, key=lambda s: -len(s)):
        fits = [i for i, l in enumerate(lines) if len(l | cs) <= 15]
        if not fits: fits = [min(range(3), key=lambda i: len(cs - lines[i]))]
        i = min(fits, key=lambda i: (len(cs - lines[i]), i)); lines[i] |= cs
        for key in sets[cs]: assign[key] = i
    spr_set = set(c for cols in sprites.values() for c in cols.values() if c is not None)
    pal_rows = []
    for l in lines + [spr_set]:
        row = [0] + sorted(l)[:15]; pal_rows.append(row + [0] * (16 - len(row)))
    # The port's Namco ASCII overlay (Start/B prompt and saved high score)
    # draws with pen15. Unused palette entries must not make it disappear.
    pal_rows[0][15] = 0xEEE
    def index(line, c):
        row = pal_rows[line]
        if c in row[1:]: return row.index(c, 1)
        return min(range(1, 16), key=lambda i: distance(rgb(c), rgb(row[i])))
    # tiles
    data = [[0] * 8]; tile_ix = {}
    for key, cols in tiles.items():
        bgl, code, g = key; line = assign[key]; t = (bg if bgl else fg)[code & 511]
        words = []
        for y in range(8):
            v = 0
            for x in range(8):
                c = cols[t[y][x]]; v = (v << 4) | (0 if c is None else index(line, c))
            words.append(v)
        tile_ix[key] = len(data); data.append(words)
    spr_ix = {}
    for key, cols in sprites.items():
        code, col = key; t = spr[code & 511]; spr_ix[key] = len(data)
        for tx, ty in ((0, 0), (0, 1), (1, 0), (1, 1)):
            words = []
            for y in range(8):
                v = 0
                for x in range(8):
                    c = cols[t[ty * 8 + y][tx * 8 + x]]; v = (v << 4) | (0 if c is None else index(3, c))
                words.append(v)
            data.append(words)
    BASE = 256    # first VRAM tile used (title screen only: scenery not loaded)
    # stream: per frame: u16 nwrites, u16 hscroll fg (scrolled rows), u16 hscroll bg, u16 nsprites,
    # writes (u16 plane<<15 | row<<6 | col, u16 attr), sprites (4 x u16 like VDPSprite, link filled at run time)
    stream = []; prev = bytes(0x2000)
    for f, (vr, sc, sp, fl, s0, s1) in enumerate(frames):
        writes = []
        for i in range(0, 0x2000, 2):
            if f == 0 and vr[i:i + 2] == bytes(2): continue
            if vr[i:i + 2] == prev[i:i + 2] and f: continue
            bgl = i >= 0x1000; cell = (i & 0xFFF) >> 1; row, col = cell >> 6, cell & 63
            code, g = cell_info(vr[i], vr[i + 1], bgl); a = vr[i + 1]
            attr = (BASE + tile_ix[(bgl, code, g)]) | (assign[(bgl, code, g)] << 13) | ((a & 0x40) << 5) | ((a & 0x80) << 5)
            if not bgl and a & 0x20: attr |= 0x8000
            writes += [(bgl << 15) | (((row - 2) & 31) << 6) | col, attr]
        prev = vr
        sprl = []
        for k in range(64):
            o = 2 * k
            if not (sc[o] or sc[o + 1] or sp[o] or sp[o + 1] or fl[o]): continue
            flg = fl[o]; sz = (flg >> 2) & 3; code = (sc[o] + ((flg & 0x80) << 1)) & ~(sz & 1) & ~(sz & 2)
            sx = sp[o + 1] + 256 * (fl[o + 1] & 1) - 47; sy = 256 - sp[o] + 9 - 16 * (sz >> 1); sy = (sy & 255) - 32
            for cy in range((sz >> 1) + 1):
                for cx in range((sz & 1) + 1):
                    ox = cx ^ ((sz & 1) if flg & 1 else 0); oy = cy ^ ((sz >> 1) if flg & 2 else 0)
                    key = (code + 2 * oy + ox, sc[o + 1] & 63)
                    X = sx + 16 * cx - 24 + 16; Y = sy + 16 * cy - 16
                    if -16 < X < 320 and -16 < Y < 224:
                        sprl += [Y + 128, 0x0500, (BASE + spr_ix[key]) | (3 << 13) | ((flg & 2) << 11) | ((flg & 1) << 11), X + 128]
        stream += [len(writes) // 2, (-s0 - 8) & 0xFFFF, (-(s1 - 3) - 8) & 0xFFFF, len(sprl) // 4] + writes + sprl
    out = ['/* GENERATED by tools/prepare_title.py from the user\'s Pac-Land ROMs. Do not distribute. */',
           '#include <genesis.h>',
           'const u16 title_pal[64] = {' + ','.join(hex(v) for r in pal_rows for v in r) + '};',
           f'const u16 title_tile_base = {BASE};',
           f'const u16 title_tile_count = {len(data)};',
           'const u32 title_tiles[] = {' + ','.join(hex(v) for t in data for v in t) + '};',
           f'const u16 title_frames = {len(frames)};',
           'const u16 title_stream[] = {' + ','.join(str(v & 0xFFFF) for v in stream) + '};']
    (gen / 'native/title_data.c').write_text('\n'.join(out) + '\n')
    print(f'title: {len(frames)} frames, {len(data)} tiles, stream {len(stream) * 2} bytes, '
          f'colours per line {[len(l) for l in lines]} + sprites {len(spr_set)}')

if __name__ == '__main__':
    main()
