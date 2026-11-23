"""Fit YM2612 FM patches to the 16 Pac-Land CUS30 waveforms.

Steady-state model of one YM2612 channel (ymfm conventions): operator
phase in cycles; a modulator at full level shifts the next operator by
+-4 cycles; operator 1 feedback adds (o[n-1] + o[n-2]) * 2^(fb-7) cycles;
TL attenuates 0.75 dB per step; MUL 1..15 (integer harmonics only, so the
period is the note's). Algorithms 0-7 as on the chip. Envelopes are held at
full level (the player keys a channel on once and follows the arcade's
volume with the carriers' TL), so only the waveform has to match.

Calibrated against Genesis Plus GX recordings (18 Nov 2026): modulation
depth 4 cycles at full level and the feedback scaling match for feedback
1-5; 6 and 7 behave chaotically on the chip, so the search stays at 0-5.
Patches are scored at two pitches (96 and 256 chip samples per period).

Each wave's target is its magnitude spectrum (harmonics 1..16, the 4-bit
32-sample table); the score is the error in dB over the audible harmonics,
weighted by the target's level. Random search plus coordinate refinement.
Writes res/generated/audio/fm_patches.json and a text report."""
import json, sys, numpy as np
from pathlib import Path

N = 128           # samples per period in the model (a mid-range note)
H = 16
ALG_CARRIERS = {0: [3], 1: [3], 2: [3], 3: [3], 4: [1, 3], 5: [1, 2, 3], 6: [1, 2, 3], 7: [0, 1, 2, 3]}

def wave_table(img, i):
    s = []
    for b in img[i * 16:i * 16 + 16]: s += [(b >> 4) - 8, (b & 15) - 8]
    return np.array(s, float)

def target_spectrum(w):
    F = np.abs(np.fft.rfft(w))[1:H + 1]
    return F / F.max()

def simulate(P, N=128):
    """P: array (batch, 14) = alg, fb, mul1..4, tl1..4 (ints). Returns (batch, N) output of 2nd period."""
    B = P.shape[0]
    alg = P[:, 0]; fb = P[:, 1]; mul = P[:, 2:6].astype(float); tl = P[:, 6:10].astype(float)
    att = 10 ** (-0.75 * tl / 20)
    fbk = np.where(fb > 0, 2.0 ** (fb - 7), 0.0)
    o = np.zeros((B, 4)); p1 = np.zeros(B); p2 = np.zeros(B)
    out = np.zeros((B, N))
    for per in range(3):
        for n in range(N):
            t = n / N
            m0 = fbk * (p1 + p2)
            o0 = np.sin(2 * np.pi * (mul[:, 0] * t + m0)) * att[:, 0]
            p2 = p1; p1 = o0
            # algorithm routing
            o1i = np.where(np.isin(alg, [0, 3, 4, 5, 6]), o0 * 4, 0.0)
            o1 = np.sin(2 * np.pi * (mul[:, 1] * t + o1i)) * att[:, 1]
            o2i = np.select([alg == 0, alg == 1, alg == 2, alg == 5], [o1 * 4, (o0 + o1) * 4, o1 * 4, o0 * 4], 0.0)
            o2 = np.sin(2 * np.pi * (mul[:, 2] * t + o2i)) * att[:, 2]
            o3i = np.select([np.isin(alg, [0, 1]), alg == 2, alg == 3, alg == 4, alg == 5],
                            [o2 * 4, (o0 + o2) * 4, (o1 + o2) * 4, o2 * 4, o0 * 4], 0.0)
            o3 = np.sin(2 * np.pi * (mul[:, 3] * t + o3i)) * att[:, 3]
            y = np.select([alg <= 3, alg == 4, np.isin(alg, [5, 6])], [o3, o1 + o3, o1 + o2 + o3], o0 + o1 + o2 + o3)
            if per == 2: out[:, n] = y
    return out

def spectra(out):
    F = np.abs(np.fft.rfft(out, axis=1))[:, 1:H + 1]
    return F / np.maximum(F.max(axis=1, keepdims=True), 1e-9)

def score(S, T):
    w = 0.15 + T                                            # louder harmonics matter more
    db = lambda x: 20 * np.log10(np.maximum(x, 10 ** (-36 / 20)))
    return ((db(S) - db(T)[None]) ** 2 * w[None]).sum(axis=1) / w.sum()

def random_patches(B, rng):
    P = np.zeros((B, 10), int)
    P[:, 0] = rng.integers(0, 8, B); P[:, 1] = rng.integers(0, 6, B)
    P[:, 2:6] = rng.integers(1, 9, (B, 4))
    P[:, 6:10] = rng.integers(0, 60, (B, 4))
    return P

def carriers_level(P):
    return P

def score2(P, T):
    return 0.5 * (score(spectra(simulate(P, 96)), T) + score(spectra(simulate(P, 256)), T))

def fit(T, rng, rounds=6):
    P = random_patches(4000, rng); s = score2(P, T)
    best = P[np.argsort(s)[:40]].copy()
    for it in range(rounds):
        # mutate the best: each child changes 1-3 parameters
        kids = np.repeat(best, 120, axis=0)
        for k in range(kids.shape[0]):
            for _ in range(rng.integers(1, 4)):
                j = rng.integers(0, 10)
                if j == 0: kids[k, 0] = rng.integers(0, 8)
                elif j == 1: kids[k, 1] = np.clip(kids[k, 1] + rng.integers(-2, 3), 0, 5)
                elif j < 6: kids[k, j] = np.clip(kids[k, j] + rng.integers(-2, 3), 1, 15)
                else: kids[k, j] = np.clip(kids[k, j] + rng.integers(-8, 9), 0, 100)
        allp = np.vstack([best, kids]); s = score2(allp, T)
        order = np.argsort(s); best = allp[order[:40]]
    s = score2(best[:1], T)
    return best[0], float(s[0])

if __name__ == '__main__':
    import pickle
    snap = pickle.load(open(sys.argv[1], 'rb'))['snaps'][0]
    out = Path(sys.argv[2]); waves = [int(x) for x in sys.argv[3:]] or range(16)
    res = json.loads(out.read_text()) if out.exists() else {}
    rng = np.random.default_rng(1)
    for i in waves:
        T = target_spectrum(wave_table(snap, i))
        cands = [fit(T, rng, rounds=25) for _ in range(2)]
        p, s = min(cands, key=lambda c: c[1])
        S = spectra(simulate(p[None]))[0]
        res[str(i)] = dict(alg=int(p[0]), fb=int(p[1]), mul=[int(x) for x in p[2:6]], tl=[int(x) for x in p[6:10]], err_db=round(s ** 0.5, 2),
                           target=[round(float(x), 3) for x in T], got=[round(float(x), 3) for x in S])
        print(i, res[str(i)]['alg'], res[str(i)]['fb'], res[str(i)]['mul'], res[str(i)]['tl'], 'rms dB', res[str(i)]['err_db'], flush=True)
        out.write_text(json.dumps(res, indent=1))
