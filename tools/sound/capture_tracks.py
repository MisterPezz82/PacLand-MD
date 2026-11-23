"""Capture every Pac-Land sound request (sound board RAM 1285-12A5, main CPU
6A85-6AA5) from the sound MCU simulation: one CUS30 register image per
frame from the request until it ends (the driver clears the request byte
and the voices fall silent), or one full pass plus the loop point for the
looping tracks (the main CPU keeps their request set). Writes
build/sound/track_XX.pkl: {req, frames: [64-byte register images], waves:
256-byte wave RAM, loop: frame index or None, clear_at_start: bool}."""
import sys, pickle
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import mcu

# loop structure measured with the request held (tools/sound: loop search on
# 4500-frame captures): request -> (loop start frame, period)
LOOPS = {0x87: (9, 1344), 0x89: (1, 2304), 0x8A: (1, 1920), 0x8B: (257, 1536)}

def capture(n, limit=1200):
    m = mcu.MCU()
    for _ in range(30): m.run_frame()
    req = 0x1200 + n
    m.mem[req] = 1
    frames = []; cleared_at = None
    loop = LOOPS.get(n)
    total = loop[0] + loop[1] if loop else limit
    for f in range(total):
        if loop: m.mem[req] = 1
        m.run_frame()
        frames.append(bytes(m.mem[0x1100:0x1140]))
        if not loop and m.mem[req] == 0 and cleared_at is None: cleared_at = f
        if not loop and cleared_at is not None:
            active = any(frames[-1][v * 8] & 15 or frames[-1][v * 8 + 4] & 15 for v in range(8))
            if not active and f > cleared_at + 2: break
    # trim trailing silence (not for loops: their length is exact)
    while not loop and frames and not any(frames[-1][v * 8] & 15 or frames[-1][v * 8 + 4] & 15 for v in range(8)): frames.pop()
    return dict(req=n, frames=frames, waves=bytes(m.mem[0x1000:0x1100]), loop=loop[0] if loop else None,
                clear_at_start=(cleared_at == 0))

if __name__ == '__main__':
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    for a in sys.argv[2:]:
        n = int(a, 16) & 0xFF
        t = capture(n)
        pickle.dump(t, open(out / ('track_%02X.pkl' % n), 'wb'))
        print(hex(n), 'frames', len(t['frames']), 'loop', t['loop'], 'oneshot', t['clear_at_start'], flush=True)
