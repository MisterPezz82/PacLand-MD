"""Reference renderer for the Namco CUS30 wave sound generator (Pac-Land).

Follows MAME's namco_cus30 device (src/devices/sound/namco.cpp): 8 voices,
16 waveforms of 32 4-bit samples in wave RAM 000-0FF (high nibble first),
registers 100-13F (8 bytes per voice: +0 left volume, +1 waveform (high
nibble) and frequency bits 16-19, +2/+3 frequency bits 8-15/0-7, +4 right
volume, bit 7 = noise for the *next* voice). Internal rate 192 kHz
(24 kHz clock x 8), 18 fractional phase bits: pitch = f * 192000 / 2^23 Hz.

Pac-Land runs the chip in mono (MAME: volume[0] only; the right volume
registers stay 0). render(frames, rate) takes one 64-byte register image
per 60.606 Hz frame (tools/sound/capture_tracks.py) plus the wave RAM and
returns a mono float array."""
import numpy as np

CLOCK = 192000
FPS = 60.606060

def waves(wram):
    w = np.zeros((16, 32))
    for i in range(16):
        for k, b in enumerate(wram[i * 16:i * 16 + 16]):
            w[i, 2 * k] = (b >> 4) - 8; w[i, 2 * k + 1] = (b & 15) - 8
    return w

def render(frames, wram, rate=44100, fps=FPS):
    n_per = rate / fps
    total = int(len(frames) * n_per)
    out = np.zeros(total)
    W = waves(wram)
    phase = np.zeros(8)
    seed = [1] * 8; state = [0] * 8; acc = [0.0] * 8
    for fi, g in enumerate(frames):
        a = int(fi * n_per); b = int((fi + 1) * n_per); n = b - a
        if n <= 0: continue
        for v in range(8):
            vol = g[v * 8] & 15; wsel = g[v * 8 + 1] >> 4
            f = (g[v * 8 + 1] & 15) << 16 | g[v * 8 + 2] << 8 | g[v * 8 + 3]
            noise = (g[((v - 1) % 8) * 8 + 4] & 0x80) != 0
            if noise:
                ticks = (f & 0xFF) * 16 / 4096 * (CLOCK / 2) / rate
                s = np.empty(n)
                for i in range(n):
                    acc[v] += ticks
                    while acc[v] >= 1:
                        acc[v] -= 1
                        if (seed[v] + 1) & 2: state[v] ^= 1
                        if seed[v] & 1: seed[v] ^= 0x28000
                        seed[v] >>= 1
                    s[i] = 7 if state[v] else -7
                out[a:b] += s * (vol >> 1)
                continue
            inc = f / (1 << 18) * CLOCK / rate
            ph = phase[v] + inc * np.arange(1, n + 1)
            phase[v] = ph[-1] % 32
            if vol: out[a:b] += W[wsel][ph.astype(np.int64) & 31] * vol
    return out / (8 * 15 * 8)

def write_wav(path, data, rate=44100):
    import wave
    if data.ndim == 1: data = np.stack([data, data], axis=1)
    pcm = (np.clip(data, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())
