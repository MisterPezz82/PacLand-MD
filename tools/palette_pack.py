"""Scenery palette packing: choose three 15-colour Mega Drive palette lines
for a round so that as many tile colour sets as possible fit one line
exactly, and the rest lose as little as possible.

Each tile (an arcade 8x8 cell in one colour group) needs all its colours in
the same line. The old greedy packing dropped any set that did not fit and
left those tiles to the nearest colours of whichever line was least bad,
which gave visible seams (a green that differs from the green beside it).
Here: two searches from the greedy start (moving whole colour sets between
lines, and annealing the line contents), several restarts, the best kept,
spare slots filled; all scored with the true cost: sum over colour sets of weight x (colour error of the set in its
best line), where the error of a set in a line is the sum over its colours
of the weighted RGB distance to the nearest colour of that line plus a fixed
MISS cost per colour not in the line (0 when the set fits). Deterministic (fixed seed)."""
import hashlib, json, random
from pathlib import Path
from convert_palettes import rgb, distance

# fixed cost for every colour a tile cannot show exactly, on top of the
# colour distance: an approximated colour next to the exact one is a visible
# seam even when the two are close
MISS = 20000
SEAM = 60000                # per approximated pixel of a colour that is exact elsewhere

def _dist_table(colours):
    cs = list(colours)
    return {(a, b): distance(rgb(a), rgb(b)) for a in cs for b in cs}

def cost_of(banks, sets, D):
    total = 0
    exact_w = {}; approx_w = {}
    for colours, w in sets:
        best = None; best_b = None
        for b in banks:
            if not b: continue
            e = 0
            for c in colours:
                if c in b: continue
                e += min(D[c, v] for v in b) + MISS
            if best is None or e < best:
                best = e; best_b = b
                if e == 0: break
        if best is None: total += w * 1e9; continue
        total += w * best
        for c in colours:
            if c in best_b: exact_w[c] = exact_w.get(c, 0) + w
            else: approx_w[c] = approx_w.get(c, 0) + w
    # a colour shown exactly in some tiles and approximated in others makes
    # seams: better to substitute it everywhere (which also frees its slot)
    for c, w in approx_w.items():
        if c in exact_w: total += SEAM * w
    return total

def greedy(sets):
    banks = [set(), set(), set()]
    for colours, w in sorted(sets, key=lambda v: (-v[1], -len(v[0]), sorted(v[0]))):
        fits = [i for i, b in enumerate(banks) if len(b | colours) <= 15]
        if fits:
            best = min(fits, key=lambda i: (len(colours - banks[i]), len(banks[i]), i)); banks[best] |= colours
    return banks

def _fill(banks, sets, D):
    """Spare slots: add the colours that lower the cost most."""
    banks = [set(b) for b in banks]
    universe = sorted({c for s, _ in sets for c in s})
    cur = cost_of(banks, sets, D)
    while True:
        best = None
        for i, b in enumerate(banks):
            if len(b) >= 15: continue
            for c in universe:
                if c in b: continue
                trial = list(banks); trial[i] = b | {c}
                v = cost_of(trial, sets, D)
                if v < cur and (best is None or v < best[0]): best = (v, i, c)
        if not best: return banks, cur
        cur, i, c = best; banks[i] = banks[i] | {c}

def pack(sets, iters=4000, seed=1):
    """Local search on which line each colour set belongs to (a line holds
    the union of its sets, at most 15 colours); sets left out are shown in
    their best line with the nearest colours. Then spare slots are filled.
    sets: list of (frozenset of CRAM colours, weight). Returns (3 sets, cost)."""
    universe = sorted({c for s, _ in sets for c in s})
    D = _dist_table(universe)
    order = sorted(range(len(sets)), key=lambda k: (-sets[k][1], -len(sets[k][0]), sorted(sets[k][0])))
    where = [-1] * len(sets)
    def unions(wh):
        u = [set(), set(), set()]
        for k, b in enumerate(wh):
            if b >= 0: u[b] |= sets[k][0]
        return u
    u = [set(), set(), set()]
    for k in order:
        s = sets[k][0]
        fits = [i for i in range(3) if len(u[i] | s) <= 15]
        if fits:
            i = min(fits, key=lambda i: (len(s - u[i]), len(u[i]), i)); u[i] |= s; where[k] = i
    def score(wh):
        return cost_of(unions(wh), sets, D)
    cur = score(where); best = (cur, list(where))
    rnd = random.Random(seed)
    T0 = max(1.0, cur / max(1, len(sets)))
    for it in range(iters):
        if cur == 0: break
        T = T0 * (1 - it / iters) + 1e-6
        wh = list(where); k = rnd.randrange(len(sets)); s = sets[k][0]
        b = rnd.randrange(3)
        if wh[k] == b: continue
        wh[k] = b
        uu = unions(wh)
        # evict from b until it fits: lightest sets first, never k
        if len(uu[b]) > 15:
            members = sorted((j for j in range(len(sets)) if wh[j] == b and j != k), key=lambda j: (sets[j][1], rnd.random()))
            for j in members:
                wh[j] = -1; uu = unions(wh)
                if len(uu[b]) <= 15: break
            if len(uu[b]) > 15: continue
        # evicted / unplaced sets: try to place anywhere they fit
        for j in rnd.sample(range(len(sets)), len(sets)):
            if wh[j] >= 0: continue
            for i in range(3):
                if len(uu[i] | sets[j][0]) <= 15: wh[j] = i; uu[i] |= sets[j][0]; break
        v = cost_of(uu, sets, D)
        if v <= cur or rnd.random() < pow(2.718281828, (cur - v) / T):
            where = wh; cur = v
            if v < best[0]: best = (v, list(wh))
    banks, cost = _fill(unions(best[1]), sets, D)
    return banks, cost

def pack_colours(sets, iters=30000, seed=1):
    """Simulated annealing on the line contents themselves (colours in and
    out), started from the greedy packing."""
    universe = sorted({c for s, _ in sets for c in s})
    D = _dist_table(universe)
    banks = greedy(sets)
    cur = cost_of(banks, sets, D); best = [set(b) for b in banks]; best_cost = cur
    if cur == 0: return best, 0
    rnd = random.Random(seed)
    T0 = max(1.0, cur / max(1, len(sets)) * 0.5)
    for it in range(iters):
        T = T0 * (1 - it / iters) + 1e-6
        i = rnd.randrange(3); b = banks[i]
        nb = set(b)
        op = rnd.random()
        if (op < 0.4 and len(nb) < 15) or not nb:
            cand = [c for c in universe if c not in nb]
            if not cand: continue
            nb.add(rnd.choice(cand))
        elif op < 0.8:
            nb.remove(rnd.choice(sorted(nb)))
            cand = [c for c in universe if c not in nb]
            if cand: nb.add(rnd.choice(cand))
        else:
            s, _ = rnd.choice(sets)
            if len(nb | s) > 15:
                keep = sorted(nb - s, key=lambda c: rnd.random())
                while len(keep) + len(s) > 15 and keep: keep.pop()
                nb = set(keep) | set(s)
                if len(nb) > 15: continue
            else:
                nb |= s
        trial = list(banks); trial[i] = nb
        c2 = cost_of(trial, sets, D)
        if c2 <= cur or rnd.random() < pow(2.718281828, (cur - c2) / T):
            banks = trial; cur = c2
            if cur < best_cost:
                best_cost = cur; best = [set(b) for b in banks]
                if cur == 0: break
    return _fill(best, sets, D)

def pack_cached(sets, cache_path, iters=20000):
    """pack() with results kept in a JSON file keyed by the colour sets, so a
    rebuild with unchanged scenery does not search again."""
    key = hashlib.sha1(json.dumps(sorted([sorted(s), w] for s, w in sets)).encode()).hexdigest() + f':{iters}:{MISS}:{SEAM}'
    path = Path(cache_path)
    cache = json.loads(path.read_text()) if path.exists() else {}
    if key in cache:
        return [set(b) for b in cache[key]]
    banks, cost = pack(sets, iters, 1)
    for b2, c2 in (pack(sets, iters, 2), pack(sets, iters, 3), pack_colours(sets, 30000, 1)):   # restarts and the other search: keep the best
        if c2 < cost: banks, cost = b2, c2
    cache[key] = [sorted(b) for b in banks]
    path.write_text(json.dumps(cache))
    return banks
