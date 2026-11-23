"""Whole-board Pac-Land co-simulation in Python: main 6809 (tools/sim6809.py
interpreter) + sound/input MCU HD63701 (mcu.py) sharing CUS30 RAM. Used to
capture the attract/title screen and the music the main CPU requests."""
import sys, os, zipfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE)); sys.path.insert(0, str(HERE.parent))
for _p in (HERE.parent / 'tools', Path.home() / 'pl/tools'):
    if (_p / 'sim6809.py').exists(): sys.path.insert(0, str(_p))
import sim6809, mcu as mcumod

class Main(sim6809.CPU):
    def __init__(s, rom, board):
        super().__init__(rom); s.board = board; s.irq_mask = False; s.irq_line = False
        s.scroll = [0, 0, 0, 0]; s.scroll0 = s.scroll1 = 0; s.flip = 0; s.cycles = 0
    def rd(s, a):
        a &= 0xFFFF
        if a < 0x3800: return s.ram[a]
        if 0x4000 <= a < 0x6000: return s.rom[0x10000 + s.bank * 0x2000 + (a - 0x4000)]
        if 0x6800 <= a < 0x6C00: return s.board.mcu.mem[0x1000 + a - 0x6800]
        if 0x7800 <= a < 0x8000: return 0          # watchdog
        if a >= 0x8000: return s.rom[a]
        return 0
    def wr(s, a, v):
        a &= 0xFFFF; v &= 0xFF
        if a < 0x3800: s.ram[a] = v; return
        if 0x3800 <= a < 0x3802: s.scroll0 = v + 256 * (a - 0x3800); s.scroll[a - 0x3800] = v; return
        if 0x3A00 <= a < 0x3A02: s.scroll1 = v + 256 * (a - 0x3A00); s.scroll[2 + a - 0x3A00] = v; return
        if 0x3C00 <= a < 0x3E00: s.bank = v & 7; s.palbank = (v >> 3) & 3; return
        if 0x6800 <= a < 0x6C00: s.board.mcu.mem[0x1000 + a - 0x6800] = v; return
        if 0x7000 <= a < 0x8000:
            s.irq_mask = not (a & 0x800)
            if not s.irq_mask: s.irq_line = False
            return
        if 0x8000 <= a < 0x9000: s.board.mcu_reset(not (a & 0x800)); return
        if 0x9000 <= a < 0xA000: s.flip = not (a & 0x800); return
    def irq(s):
        # full-state push (E=1)
        s.e_flag = 1
        def push(v): s.s = (s.s - 1) & 0xFFFF; s.wr(s.s, v)
        push(s.pc & 0xFF); push(s.pc >> 8); push(s.u & 0xFF); push(s.u >> 8); push(s.y & 0xFF); push(s.y >> 8)
        push(s.x & 0xFF); push(s.x >> 8); push(s.dp); push(s.b); push(s.a); push(s.cc())
        s.i_flag = 1; s.pc = s.rd16(0xFFF8); s.waiting = False
    def step(s):
        op = s.rd(s.pc)
        if op == 0x3B:                                   # RTI
            def pull(): v = s.rd(s.s); s.s = (s.s + 1) & 0xFFFF; return v
            s.set_cc(pull())
            if s.e_flag:
                s.a = pull(); s.b = pull(); s.dp = pull()
                s.x = pull() << 8 | pull(); s.y = pull() << 8 | pull(); s.u = pull() << 8 | pull()
            s.pc = pull() << 8 | pull(); s.steps += 1; return
        if op in (0x13, 0x3C):                           # SYNC / CWAI: wait for the interrupt
            raise RuntimeError(f'wait instruction {op:02X} at {s.pc:04X}')
        super().step()

class Board:
    def __init__(s, zippath=None):
        z = zipfile.ZipFile(zippath or mcumod.ZIP)
        root = Path(sys.modules['sim6809'].__file__).resolve().parent.parent
        rom = (root / 'res/generated/arcade/maincpu.bin').read_bytes()   # 8000-FFFF fixed, banks at 10000
        s.mcu = mcumod.MCU(); s.mcu_held = False
        s.main = Main(bytes(rom), s); s.main.palbank = 0
        s.main.pc = s.main.rd16(0xFFFE); s.main.s = 0
        s.frame = 0
    def mcu_reset(s, hold):
        if hold: s.mcu_held = True
        elif s.mcu_held:
            s.mcu_held = False; s.mcu.cpu.pc = s.mcu.cpu.r16(0xFFFE); s.mcu.cpu.cc = 0xD0; s.mcu.cpu.wai = False
    def run_frame(s, main_instr=5200):
        m = s.main
        if m.irq_mask: m.irq_line = True
        mc = s.mcu
        if mc.irq_mask: mc.irq_line = True
        chunk = 8
        for k in range(chunk):
            for _ in range(main_instr // chunk):
                if m.irq_line and not m.i_flag: m.irq()
                m.step()
            if not s.mcu_held:
                end = mc.cpu.cycles + mcumod.CYC_FRAME // chunk
                cpu = mc.cpu
                while cpu.cycles < end:
                    if not cpu.cc & 0x10:
                        if mc.regs[8] & 0x40 and mc.regs[8] & 0x08: cpu.interrupt(0xFFF4)
                        elif mc.irq_line: cpu.interrupt(0xFFF8)
                    c0 = cpu.cycles; cpu.step(); d = cpu.cycles - c0
                    old = mc.counter; mc.counter = (old + d) & 0xFFFF
                    if (mc.ocr - old - 1) & 0xFFFF < d: mc.regs[8] |= 0x40
                    if old + d > 0xFFFF: mc.regs[8] |= 0x20
        s.frame += 1; mc.frame += 1

if __name__ == '__main__':
    import time
    b = Board(); t = time.time()
    for f in range(int(sys.argv[1]) if len(sys.argv) > 1 else 60):
        b.run_frame()
    print('frames', b.frame, 'time', round(time.time() - t, 1), 'main pc', hex(b.main.pc), 'mcu pc', hex(b.mcu.cpu.pc))
    vr = b.main.ram
    print('fg nonblank', sum(1 for i in range(0, 0x1000, 2) if vr[i] not in (0, 0x20, 0xEE)))
