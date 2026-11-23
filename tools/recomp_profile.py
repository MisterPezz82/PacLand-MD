"""Collect the access-site profile used to specialise recompiled memory accesses.

Builds a profiling host binary (tools/recomp6809.py --profile, -DRC_PROFILE),
runs every round from a fresh set-up, walking through the whole round in its
own direction, plus the post-death re-initialisation path, merges which memory
regions each access site touched (1 RAM, 2 ROM, 4 other) and writes
res/generated/recomp/access_profile.json. Sites never executed stay checked."""
import json
from pathlib import Path
import subprocess
import sys
import os

ROOT = Path(__file__).resolve().parents[1]
gen = ROOT / 'res/generated'
build = ROOT / 'build'; build.mkdir(exist_ok=True)
src = build / 'recomp_objects_profile.c'
subprocess.run([sys.executable, str(ROOT / 'tools/recomp6809.py'), '--profile', '--out', str(src)], check=True)
exe = build / ('recomp-host-profile.exe' if os.name == 'nt' else 'recomp-host-profile')
zero = build / 'zero-ram.bin'
zero.write_bytes(bytes(0x3800))
subprocess.run(['gcc', '-DRC_HOST', '-DRC_PROFILE', '-O1', '-w', '-iquote', str(ROOT / 'include'), str(src),
                str(ROOT / 'src/recomp_rt.c'), str(ROOT / 'tests/recomp_host.c'), '-o', str(exe)], check=True)
manifest = json.loads((gen / 'levels/manifest.json').read_text())
merged = bytearray(0x80000)
runs = 0
for st in manifest:
    n = st['round'] - 1
    walk = -2 if st['world_end'] < st['world_start'] else 2
    for setup, frames, w in ((3, 1800, walk), (3, 900, walk // 2), (2, 300, walk)):
        snap = str(zero) if setup == 3 else str(gen / f'reference/round_{n + 1:02d}/ram.bin')
        prof = build / 'prof.bin'
        subprocess.run([str(exe), str(gen / 'arcade/maincpu.bin'), snap, str(build / 'prof-ram.bin'), str(n), str(setup),
                        str(frames), str(w), str(prof)], check=True)
        data = prof.read_bytes()
        for i, v in enumerate(data):
            if v: merged[i] |= v
        runs += 1
(build / 'prof-ram.bin').unlink(missing_ok=True)
out = {str(i): v for i, v in enumerate(merged) if v}
(gen / 'recomp').mkdir(exist_ok=True)
(gen / 'recomp/access_profile.json').write_text(json.dumps(out))
cls = [v for v in out.values()]
print(f'{runs} runs; sites seen {len(cls)}: RAM-only {cls.count(1)}, ROM-only {cls.count(2)}, mixed/other {len(cls) - cls.count(1) - cls.count(2)}')
