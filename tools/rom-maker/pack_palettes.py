"""Parallelise independent round palette searches without changing their seeds."""
import argparse
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_native import cell_info
from extract_arcade import palettes
from convert_palettes import cram
import palette_pack


def one_round(index):
    base = ROOT / 'res/generated'
    stage = json.loads((base / 'levels/manifest.json').read_text())[index]
    segments = json.loads((base / 'levels/segments.json').read_text())
    prom = (base / 'arcade/proms.bin').read_bytes()
    pal = palettes(prom)[stage['palette_bank']]
    groups = Counter()
    # Identical colour-set construction to prepare_native.compile_resources().
    for layer in ('fg', 'bg'):
        columns = sum((segments[str(35 if layer == 'fg' and index == 26 and 0 < i < 32 else i)][layer]
                       for i in stage['segment_ids']), [])
        for word, count in Counter(w for column in columns for w in column).items():
            code, group, attr = cell_info(word, layer)
            lut = 0x800 if layer == 'fg' else 0xc00
            colours = frozenset(cram(pal[prom[lut + 4 * group + pen]]) for pen in range(4)
                if (layer == 'bg' and cram(pal[prom[lut + 4 * group + pen]]) != 0)
                or (layer == 'fg' and prom[lut + 4 * group + pen] & 127 != 127))
            groups[colours] += count
    cache = ROOT / f'build/palette-round-{index + 1:02d}.json'
    palette_pack.pack_cached(list(groups.items()), cache)
    return index + 1, json.loads(cache.read_text())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs', type=int, default=8)
    a = p.parse_args()
    merged = {}
    with ProcessPoolExecutor(max_workers=max(1, min(a.jobs, 32))) as pool:
        futures = [pool.submit(one_round, n) for n in range(32)]
        for future in as_completed(futures):
            number, cache = future.result()
            merged.update(cache)
            print(f'Packed round {number:02d}; completed {len(merged)}/32 searches', flush=True)
    target = ROOT / 'res/generated/native/palette_pack_cache.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(merged))


if __name__ == '__main__':
    main()
