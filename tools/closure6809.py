"""Compute the set of 6809 routines the recompiled object system needs.

Entry points: object init/re-init/gate/update tables (all types except 00,
the player, which stays native), the per-frame tail routines 841A/85D5 and
the round-setup tails 8389/8525/A645 (normal) and 838E/852A (re-init).
Indirect JSR/JMP through `LDY #table ... JSR [A,Y]` style tables are
resolved by reading the table; table length is bounded by the largest index
the code can produce when known, otherwise by the --table-max option.

Writes res/generated/recomp/closure.json and prints hazards: stack-pointer
manipulation, unresolved indirect control flow, SWI/SYNC/CWAI/RTI and calls
into excluded (player) code.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m6809 import decode, flow  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
# DE46: the cooperative-task yield (native in src/recomp_rt.c). Inside a
# sequence started with rc_task_start (round end F1DC, trip intro BEF2) it
# suspends the sequence until the next frame; elsewhere (F272, waiting for
# the previous bonus event, flag 2219) the stub completes the wait by
# clearing 2219 (documented deviation; see docs/recompilation.md). The player routine 8731 is part of
# the recompiled set since 15 Nov 2026 (session 3).
EXCLUDE = {0xDE46}
TABLES = (0x8168, 0x81CC, 0x82C0, 0x8324)
# Per-frame gameplay tick of the original main loop (E9FA..EA56):
#   8230 objects, EBF1 (CB93, CEA7), D130, C1FB sprite list build, CE62.
# Round set-up: 8010 (start) / 80DA (re-init after a life is lost).
# Round start of the original (E975..E994): EBC8 (HUD/state init), CFE9,
# 8010, CE49, D0F0. Cut-scenes run as yielding tasks: round end F1DC (after
# EBF8), trip intro C6C3/BEF2 and the walk-in F0F1.
TAILS = (0x8731, 0xC6B7, 0xEB9D, 0x8010, 0x80DA, 0x8230, 0xEBF1, 0xD130, 0xC1FB, 0xCE62, 0xC1C3, 0xC1DC,
         0xEBC8, 0xCFE9, 0xCE49, 0xD0F0, 0xF1DC, 0xEBF8, 0xC6C3, 0xBEF2, 0xF0F1,
         0x841A, 0x85D5, 0x8389, 0x8525, 0xA645, 0x838E, 0x852A)


def load_rom(root=ROOT):
    return (root / 'res/generated/arcade/maincpu.bin').read_bytes()


def entries(rom):
    w = lambda a: (rom[a] << 8) | rom[a + 1]
    out = set(TAILS)
    for t in TABLES:
        for i in range(1, 50):
            out.add(w(t + 2 * i))
    return out - EXCLUDE


def walk(rom, roots, table_max=32, overrides=None):
    rd = lambda a: rom[a & 0xFFFF]
    w = lambda a: (rd(a) << 8) | rd(a + 1)
    overrides = overrides or {}
    funcs = {}; hazards = []; itables = {}
    todo = sorted(roots)
    while todo:
        e = todo.pop()
        if e in funcs or e in EXCLUDE:
            if e in EXCLUDE: hazards.append(dict(kind='calls-excluded', addr=e))
            continue
        if e < 0x8000:
            hazards.append(dict(kind='banked-or-ram-target', addr=e)); funcs[e] = []; continue
        body = {}; work = [e]; last_imm = {}
        while work:
            a = work.pop()
            while a not in body:
                i = decode(rd, a); body[a] = i
                f, t = flow(i)
                if i.mn in ('LDY', 'LDX', 'LDU') and i.mode == 'imm16':
                    last_imm[i.mn[-1]] = (a, i.val)
                if i.mn == 'ILL': hazards.append(dict(kind='illegal', addr=a, func=e))
                if i.mn in ('SWI', 'SWI2', 'SWI3', 'SYNC', 'CWAI', 'RTI'):
                    hazards.append(dict(kind=i.mn, addr=a, func=e))
                if i.mn in ('LDS', 'STS') or (i.mn == 'TFR' and (i.val & 15) == 4) or \
                        (i.mn == 'LEAS' and not (i.ix['kind'] == 'off' and i.ix['reg'] == 'S')):
                    hazards.append(dict(kind='stack-pointer', addr=a, func=e, text=i.text()))
                if i.mn == 'LEAS' and i.ix['kind'] == 'off' and i.ix['reg'] == 'S' and i.ix['off'] > 0:
                    nxt = decode(rd, a + i.size)
                    hazards.append(dict(kind='stack-drop', addr=a, func=e, text=i.text(), next=nxt.text()))
                if f in ('icall', 'ijump'):
                    ix = i.ix
                    key = f'{a:04X}'
                    if key in overrides:
                        tgts = list(overrides[key])
                    elif ix.get('ind') and ix['kind'] in ('A', 'B') and ix['reg'] in last_imm:
                        base = last_imm[ix['reg']][1]; tgts = []
                        limit = 50 if base in TABLES else table_max
                        for k in range(limit):
                            # Tables sit directly before their handlers: stop at the
                            # first word that overlaps code a previous entry points to.
                            real = [t for t in tgts if t]
                            if base not in TABLES and real and base + 2 * k >= min(real) and min(real) > base:
                                break
                            v = w(base + 2 * k)
                            if v == 0: tgts.append(None); continue   # unused state slot
                            if v < 0x8000: break
                            tgts.append(v)
                        while tgts and tgts[-1] is None: tgts.pop()
                    else:
                        tgts = None
                    if tgts is None:
                        hazards.append(dict(kind='unresolved-indirect', addr=a, func=e, text=i.text()))
                    else:
                        itables[a] = tgts; todo.extend(t for t in tgts if t)
                if f in ('call',) and t is not None:
                    todo.append(t)
                if f in ('cond', 'jump') and t is not None:
                    if t < 0x8000: hazards.append(dict(kind='branch-low', addr=a, func=e))
                    else: work.append(t)
                if f in ('end', 'jump', 'ijump'): break
                a += i.size
        funcs[e] = sorted(body)
    return funcs, itables, hazards


def resolve(rom, table_max=32, manual=None):
    """Walk to a fixpoint, truncating each jump table where it runs into code
    that the walk itself decoded (a table entry cannot overlap an instruction)."""
    manual = {k.upper(): [int(x, 16) for x in v] for k, v in (manual or {}).items()}
    fixed = dict(manual)
    while True:
        funcs, itables, hazards = walk(rom, entries(rom), table_max, fixed)
        code = sorted(set(x for b in funcs.values() for x in b))
        changed = False
        for site, tgts in itables.items():
            key = f'{site:04X}'
            if key in manual: continue
            i = decode(lambda a: rom[a & 0xFFFF], site)
            # base register value: find the nearest preceding LDx #imm in the same function
            base = None
            for back in range(2, 12):
                j = decode(lambda a: rom[a & 0xFFFF], site - back)
                if j.mn in ('LDY', 'LDX', 'LDU') and j.mode == 'imm16' and j.addr + j.size <= site:
                    base = j.val; break
            if base is None: continue
            keep = []
            for k, t in enumerate(tgts):
                slot = base + 2 * k
                if any(slot <= c < slot + 2 for c in code) or any(c == slot + 1 for c in code):
                    break
                keep.append(t)
            if keep != tgts and fixed.get(key) != keep:
                fixed[key] = keep; changed = True
        if not changed:
            return funcs, itables, hazards


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--table-max', type=int, default=32)
    a = p.parse_args()
    rom = load_rom()
    ov_path = ROOT / 'tools/recomp_overrides.json'
    ov = json.loads(ov_path.read_text()) if ov_path.exists() else {}
    funcs, itables, hazards = resolve(rom, a.table_max, ov.get('indirect', {}))
    out = ROOT / 'res/generated/recomp'; out.mkdir(parents=True, exist_ok=True)
    instrs = set(x for b in funcs.values() for x in b)
    (out / 'closure.json').write_text(json.dumps(dict(
        functions={f'{k:04X}': len(v) for k, v in sorted(funcs.items())},
        indirect={f'{k:04X}': [f'{t:04X}' if t else None for t in v] for k, v in itables.items()},
        hazards=[{k: (f'{v:04X}' if isinstance(v, int) else v) for k, v in h.items()} for h in hazards]), indent=1))
    print(f'functions {len(funcs)}, unique instructions {len(instrs)}, indirect sites {len(itables)}')
    from collections import Counter
    print('hazards', Counter(h['kind'] for h in hazards))
    for h in hazards:
        if h['kind'] != 'stack-drop' or True:
            print({k: (f'{v:04X}' if isinstance(v, int) else v) for k, v in h.items()})


if __name__ == '__main__':
    main()
