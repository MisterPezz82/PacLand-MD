"""Convert the captured Pac-Land sound requests (build/sound/track_XX.pkl,
tools/sound/capture_tracks.py) into data for the Z80 sound driver
(src/z80/musdrv.c, layout in include/musdrv.h).

Each request becomes a stream of the CUS30 voice parameters it drives, one
entry per 60 Hz frame: for every frame a mask of the track's voices that
changed, then per changed voice a flags byte (bit 0 volume and noise,
bit 1 waveform, bit 2 pitch) and the new values. The pitch is stored ready
for the chips: YM2612 block/F-number and PSG period (one or two octaves up
for the square waveforms 14 and 15; for a noise voice the period of PSG
tone channel 2, which clocks the noise). The loop frame of a looping track
is stored in full so the player can jump back to it.

FM patches come from tools/sound/fm_patches.json (tools/sound/fmfit.py);
each is levelled so that a voice at volume 15 has the loudness of the
arcade waveform it stands for. Writes res/generated/audio/music_data.c."""
import json, pickle, sys, math
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import fmfit

ROOT = Path(__file__).resolve().parents[2]
SQUARES = {1, 14, 15}                      # exact on the PSG
FM_BELOW_PSG_DB = 0.0                      # FM square at volume 15 was 6.4 dB louder than the PSG at full (emulator)
OPOFS = [0, 8, 4, 12]                      # register slot of OP1..OP4

def voice_state(img, v):
    g = img[v * 8:v * 8 + 8]
    noise = (img[((v - 1) % 8) * 8 + 4] & 0x80) != 0
    return (g[0] & 15) | (0x80 if noise else 0), g[1] >> 4, (g[1] & 15) << 16 | g[2] << 8 | g[3]

def ym_fb(freq):
    f0 = ((freq >> 4) * 59055) >> 12; blk = 0       # CUS30 192 kHz/2^23 Hz per unit -> YM 53267 Hz/2^21
    while f0 >= 2048 and blk < 7: f0 >>= 1; blk += 1
    return blk << 11 | min(f0, 2047)

def psg_per(freq, wave, noise):
    if noise:                                         # CUS30 noise shifts at (f & FFh) * 375 Hz; tone 2 shifts it at 111861/N Hz
        f = freq & 0xFF
        return max(1, (298 + (f >> 1)) // f) if f else 1023
    if wave == 14: freq <<= 1
    elif wave == 15: freq <<= 2
    if not freq: return 1023
    return min(1023, 4887180 // max(freq, 4778))

def partners(track, voices):
    """Detuned copies: voice v follows an earlier voice u of the track on the
    same waveform within 2 % for most of the time both sound."""
    F = track['frames']; part = {}
    for j, v in enumerate(voices):
        best = 0
        for u in voices[:j]:
            both = same = 0
            for img in F:
                a = voice_state(img, u); b = voice_state(img, v)
                if (a[0] & 15) and (b[0] & 15) and not (a[0] | b[0]) & 0x80:
                    both += 1
                    if a[1] == b[1] and a[2] and abs(a[2] - b[2]) * 50 <= a[2]: same += 1
            if both >= 4 and same * 2 >= both and same > best: part[v] = u; best = same
    return part

def encode(track):
    F = track['frames']
    voices = sorted({v for img in F for v in range(8) if img[v * 8] & 15})
    voices = [v for v in voices]
    out = bytearray(); loop_off = 0xFFFF
    prev = [None] * len(voices)
    for i, img in enumerate(F):
        full = track['loop'] is not None and i == track['loop']
        if full: loop_off = len(out)
        mask = 0; body = bytearray()
        for k, v in enumerate(voices):
            vol, wave, freq = voice_state(img, v)
            p = prev[k]
            fl = 0
            if full or p is None or p[0] != vol: fl |= 1
            if full or p is None or p[1] != wave: fl |= 2
            if full or p is None or p[2] != freq or p[1] != wave or (p[0] ^ vol) & 0x80: fl |= 4
            if fl:
                mask |= 1 << k; body.append(fl)
                if fl & 1: body.append(vol)
                if fl & 2: body.append(wave)
                if fl & 4:
                    fb = ym_fb(freq); per = psg_per(freq, wave, vol & 0x80)
                    body += bytes([fb >> 8, fb & 255, per >> 8, per & 255])
            prev[k] = (vol, wave, freq)
        out.append(mask); out += body
    return voices, bytes(out), loop_off

def patch_regs(p, wave_rms):
    """Register bytes for one patch: [B0 (fb/alg), carrier mask, base TL x4,
    DT/MUL x4, ...]. Carriers' TL is levelled to the target loudness."""
    P = np.array([[p['alg'], p['fb']] + p['mul'] + p['tl']])
    y = fmfit.simulate(P)[0]
    rms = float(np.sqrt((y ** 2).mean())); peak = float(np.abs(y).max())
    return rms, peak

def main():
    snapdir = ROOT / 'build/sound'
    patches = json.loads((Path(__file__).parent / 'fm_patches.json').read_text())
    tracks = {}
    for f in sorted(snapdir.glob('track_*.pkl')):
        t = pickle.load(open(f, 'rb')); tracks[t['req']] = t
    waves = tracks[0x86]['waves']
    # loudness of each arcade waveform (4-bit table, centred as the chip does)
    wrms = []
    for i in range(16):
        w = fmfit.wave_table(waves, i); wrms.append(float(np.sqrt((w ** 2).mean())) / 8)
    # level every patch: carriers' TL offset so rms = K * wave rms, K chosen
    # so that the loudest patch at volume 15 just reaches full scale
    stats = [patch_regs(patches[str(i)], wrms[i]) for i in range(16)]
    K = min(stats[i][0] / wrms[i] / stats[i][1] for i in range(16) if wrms[i] > 0)
    lines = ['/* GENERATED by tools/sound/music.py from the Pac-Land sound ROMs (arcade-derived: do not distribute). */',
             '#include <genesis.h>']
    tables = bytearray()
    for i in range(16):
        p = patches[str(i)]; rms, peak = stats[i]
        want = K * wrms[i]                                  # wanted rms at volume 15
        ofs = 0 if rms <= 0 else 20 * math.log10(rms / want) / 0.75
        ofs += p.get('trim_db', 0) / 0.75                    # measured on the emulated YM2612
        ofs += FM_BELOW_PSG_DB / 0.75
        car = fmfit.ALG_CARRIERS[p['alg']]
        tl = [min(127, max(0, round(p['tl'][o] + (ofs if o in car else 0)))) for o in range(4)]
        tables += bytes([(p['fb'] << 3) | p['alg'], sum(1 << o for o in car)] + list(p['mul']) + tl)
    tables += bytes(1 if i in SQUARES else 0 for i in range(16))
    assert len(tables) == 176
    # blob: 32 track entries of 24 bytes, then the streams
    hdr = bytearray(); body = bytearray()
    for n in range(0x85, 0xA5):
        t = tracks.get(n)
        if not t or not t['frames']:
            hdr += bytes(6) + bytes([0, 0, 0xFF, 0xFF, 0, 0]) + bytes([0xFF] * 8) + bytes([0xFF] * 4); continue
        voices, data, loop_off = encode(t)
        part = partners(t, voices)
        off = 32 * 24 + len(body); lo = off + (loop_off if loop_off != 0xFFFF else 0)
        nf = len(t['frames']); loop = t['loop'] if t['loop'] is not None else 0xFFFF
        pv = [part.get(v, 15) for v in range(8)]
        hdr += bytes([off >> 16, (off >> 8) & 255, off & 255, lo >> 16, (lo >> 8) & 255, lo & 255,
                      nf & 255, nf >> 8, loop & 255, loop >> 8, len(voices), 1 if t['clear_at_start'] else 0])
        hdr += bytes(voices + [0xFF] * (8 - len(voices)))
        hdr += bytes(pv[i] | pv[i + 1] << 4 for i in range(0, 8, 2))
        body += data
        print('%02X voices %s frames %d bytes %d loop %s partners %s' % (n, voices, nf, len(data), t['loop'], part))
    blob = hdr + body
    assert len(hdr) == 32 * 24
    lines.append('const u8 mus_tables[%d] = {%s};' % (len(tables), ','.join(str(b) for b in tables)))
    lines.append('const u8 mus_blob[%d] __attribute__((aligned(2))) = {%s};' % (len(blob), ','.join(str(b) for b in blob)))
    out = ROOT / 'res/generated/audio/music_data.c'; out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text('\n'.join(lines) + '\n')
    print('levelling K', round(K, 3), 'blob', len(blob), 'bytes, written', out)

if __name__ == '__main__':
    main()
