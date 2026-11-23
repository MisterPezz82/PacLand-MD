"""Reserve the Fairyland sprites' colours while no enemy sprites are visible.

The normal walking captures never visit this scene. Keep Pac-Man and the
HUD's original pens, then use the spare sprite pens for the fairy herself.
All output is locally generated from the supplied arcade data.
"""
import json
import re
from pathlib import Path
from extract_arcade import ROOT, palettes
from convert_palettes import cram, rgb, distance


def main():
    gen = ROOT / 'res/generated'
    prom = (gen / 'arcade/proms.bin').read_bytes()
    pals = palettes(prom)
    art = json.loads((gen / 'arcade/sprites.json').read_text())
    stages = json.loads((gen / 'levels/manifest.json').read_text())
    data = (gen / 'native/data.c').read_text()
    rows, cells, offsets = [], [], []
    shared = {}
    queen_cells, queen_offsets, queen_alt_offsets, queen_shared = [], [], [], {}
    for n, stage in enumerate(stages):
        native = [int(v, 0) for v in re.search(rf'palette_{n}\[\] = \{{([^}}]+)', data)[1].split(',')][48:64]
        k = max(i for i in range(1, 15) if native[i])
        chosen = native[1:k + 1]
        pal = pals[stage['palette_bank']]
        for p in range(16):
            c = prom[0x1000 + 0x34 * 16 + p]
            if c & 127 != 127 and cram(pal[c]) not in chosen:
                chosen.append(cram(pal[c]))
        # The large Fairy Queen uses group32, not the little fairy's group34.
        # One spare pen preserves her pink dress without changing player/fairy pens.
        pink = cram(pal[prom[0x1324]])
        if pink not in chosen: chosen.append(pink)
        assert len(chosen) <= 14, (n, chosen)
        row = [0] + chosen + [0] * (14 - len(chosen)) + [native[15]]
        mapping = [0 if prom[0x1340 + p] & 127 == 127 else row.index(cram(pal[prom[0x1340 + p]]), 1) for p in range(16)]
        rows.append(row)
        # Same palette => same converted sprite cells; store once per bank.
        key = tuple(mapping)
        if key not in shared:
            shared[key] = len(cells) // 32
            for code in range(0x1E0, 0x1F0):
                for tx, ty in ((0, 0), (0, 1), (1, 0), (1, 1)):
                    for y in range(8):
                        w = 0
                        for x in range(8):
                            w = (w << 4) | mapping[art[code][ty * 8 + y][tx * 8 + x]]
                        cells.append(w)
        offsets.append(shared[key])
        # Original FA45/FA54 alternate groups32/33 while the skin pens stay
        # identical. Both animation palettes need Fairyland-specific cells.
        for group, offsets_out in ((0x32, queen_offsets), (0x33, queen_alt_offsets)):
            lut = 0x1000 + group * 16
            queen_mapping = [0 if prom[lut + p] & 127 == 127 else
                             min(range(1, 16), key=lambda i: distance(pal[prom[lut + p]], rgb(row[i])))
                             for p in range(16)]
            qkey = tuple(queen_mapping)
            if qkey not in queen_shared:
                queen_shared[qkey] = len(queen_cells) // 32
                for code in range(0x1D0, 0x1E0):
                    for tx, ty in ((0, 0), (0, 1), (1, 0), (1, 1)):
                        for y in range(8):
                            w = 0
                            for x in range(8):
                                w = (w << 4) | queen_mapping[art[code][ty * 8 + y][tx * 8 + x]]
                            queen_cells.append(w)
            offsets_out.append(queen_shared[qkey])
    out = ['/* GENERATED from the user-supplied arcade ROM. Do not distribute. */', '#include <genesis.h>']
    out.append('const u16 fairy_pal[32][16] = {' + ','.join('{' + ','.join(hex(v) for v in row) + '}' for row in rows) + '};')
    out.append('const u16 fairy_cell_start[32] = {' + ','.join(str(x) for x in offsets) + '};')
    out.append('const u32 fairy_cells[] __attribute__((aligned(128))) = {' + ','.join(hex(x) for x in cells) + '};')
    out.append('const u16 queen_cell_start[32] = {' + ','.join(str(x) for x in queen_offsets) + '};')
    out.append('const u16 queen_alt_cell_start[32] = {' + ','.join(str(x) for x in queen_alt_offsets) + '};')
    out.append('const u32 queen_cells[] __attribute__((aligned(128))) = {' + ','.join(hex(x) for x in queen_cells) + '};')
    (ROOT / 'build/fairy_data.c').write_text('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
