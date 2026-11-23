"""Reference MC6809 interpreter (Python) for differential testing of the
recompiled object system. Written independently of the C code generator;
only the instruction decoder (m6809.py) is shared.

Memory model matches src/recomp_rt.c:
  0000-37FF arcade RAM, 3C00 bank select (data & 7), 4000-5FFF banked ROM
  (region 10000 + bank*2000), 6800-6BFF shared MCU RAM, 8000-FFFF fixed ROM.
  Other writes are recorded in `io_writes` and otherwise ignored; other reads
  return 0.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m6809 import decode  # noqa: E402

RET_SENTINEL = 0xFFFF


class CPU:
    def __init__(self, rom, ram=None, mcu=None):
        self.rom = rom
        self.ram = bytearray(ram if ram is not None else bytes(0x3800))
        self.mcu = bytearray(mcu if mcu is not None else bytes(0x400))
        self.bank = 0
        self.a = self.b = 0; self.x = self.y = self.u = 0; self.s = 0x3700; self.dp = 0x20
        self.n = self.z = self.v = self.c = self.h = 0
        self.i_flag = 1; self.f_flag = 1; self.e_flag = 0
        self.io_writes = []
        self.cache = {}
        self.steps = 0
        self.hooks = {}  # addr -> callable(cpu); return from hook acts as RTS

    # ---- memory
    def rd(self, a):
        a &= 0xFFFF
        if a < 0x3800: return self.ram[a]
        if 0x4000 <= a < 0x6000: return self.rom[0x10000 + self.bank * 0x2000 + (a - 0x4000)]
        if 0x6800 <= a < 0x6C00: return self.mcu[a - 0x6800]
        if a >= 0x8000: return self.rom[a]
        return 0

    def wr(self, a, v):
        a &= 0xFFFF; v &= 0xFF
        if a < 0x3800: self.ram[a] = v; return
        if 0x6800 <= a < 0x6C00: self.mcu[a - 0x6800] = v; return
        if 0x3C00 <= a < 0x3E00: self.bank = v & 7
        self.io_writes.append((a, v))

    def rd16(self, a): return (self.rd(a) << 8) | self.rd(a + 1)

    def wr16(self, a, v): self.wr(a, v >> 8); self.wr(a + 1, v)

    # ---- registers
    @property
    def d(self): return (self.a << 8) | self.b

    @d.setter
    def d(self, v): self.a = (v >> 8) & 0xFF; self.b = v & 0xFF

    def cc(self):
        return (self.e_flag << 7) | (self.f_flag << 6) | (self.h << 5) | (self.i_flag << 4) | \
               (self.n << 3) | (self.z << 2) | (self.v << 1) | self.c

    def set_cc(self, v):
        self.e_flag = (v >> 7) & 1; self.f_flag = (v >> 6) & 1; self.h = (v >> 5) & 1; self.i_flag = (v >> 4) & 1
        self.n = (v >> 3) & 1; self.z = (v >> 2) & 1; self.v = (v >> 1) & 1; self.c = v & 1

    def getr(self, code):
        return {0: self.d, 1: self.x, 2: self.y, 3: self.u, 4: self.s, 5: self.pc, 8: self.a, 9: self.b,
                10: self.cc(), 11: self.dp}.get(code, 0xFF)

    def setr(self, code, v):
        if code == 0: self.d = v & 0xFFFF
        elif code == 1: self.x = v & 0xFFFF
        elif code == 2: self.y = v & 0xFFFF
        elif code == 3: self.u = v & 0xFFFF
        elif code == 4: self.s = v & 0xFFFF
        elif code == 5: self.pc = v & 0xFFFF
        elif code == 8: self.a = v & 0xFF
        elif code == 9: self.b = v & 0xFF
        elif code == 10: self.set_cc(v & 0xFF)
        elif code == 11: self.dp = v & 0xFF

    def push(self, v): self.s = (self.s - 1) & 0xFFFF; self.wr(self.s, v)

    def pull(self): v = self.rd(self.s); self.s = (self.s + 1) & 0xFFFF; return v

    def nz8(self, r): self.n = (r >> 7) & 1; self.z = int((r & 0xFF) == 0)

    def nz16(self, r): self.n = (r >> 15) & 1; self.z = int((r & 0xFFFF) == 0)

    # ---- effective address
    def ea(self, i):
        if i.mode == 'dir': return (self.dp << 8) | i.val
        if i.mode == 'ext': return i.val
        ix = i.ix; k = ix['kind']; reg = ix['reg']
        get = lambda: getattr(self, reg.lower())
        put = lambda v: setattr(self, reg.lower(), v & 0xFFFF)
        if k == 'off': e = get() + ix['off']
        elif k == 'inc1': e = get(); put(e + 1)
        elif k == 'inc2': e = get(); put(e + 2)
        elif k == 'dec1': put(get() - 1); e = get()
        elif k == 'dec2': put(get() - 2); e = get()
        elif k == 'A': e = get() + (self.a - 256 if self.a & 0x80 else self.a)
        elif k == 'B': e = get() + (self.b - 256 if self.b & 0x80 else self.b)
        elif k == 'D': e = get() + self.d
        elif k in ('pcr', 'abs'): e = ix['off']
        e &= 0xFFFF
        if ix['ind']: e = self.rd16(e)
        return e

    def operand8(self, i):
        return i.val & 0xFF if i.mode == 'imm8' else self.rd(self.ea(i))

    def operand16(self, i):
        return i.val if i.mode == 'imm16' else self.rd16(self.ea(i))

    # ---- arithmetic helpers
    def add8(self, a, b, carry=0):
        r = a + b + carry
        self.h = ((a ^ b ^ r) >> 4) & 1
        self.v = (((a ^ r) & (b ^ r)) >> 7) & 1
        self.c = (r >> 8) & 1; r &= 0xFF; self.nz8(r); return r

    def sub8(self, a, b, carry=0):
        r = a - b - carry
        self.v = (((a ^ b) & (a ^ r)) >> 7) & 1
        self.c = int(r < 0); r &= 0xFF; self.nz8(r); return r

    def add16(self, a, b):
        r = a + b
        self.v = (((a ^ r) & (b ^ r)) >> 15) & 1
        self.c = (r >> 16) & 1; r &= 0xFFFF; self.nz16(r); return r

    def sub16(self, a, b):
        r = a - b
        self.v = (((a ^ b) & (a ^ r)) >> 15) & 1
        self.c = int(r < 0); r &= 0xFFFF; self.nz16(r); return r

    def rmw(self, mn, v):
        if mn == 'NEG': r = (-v) & 0xFF; self.v = int(v == 0x80); self.c = int(v != 0); self.nz8(r); return r
        if mn == 'COM': r = (~v) & 0xFF; self.v = 0; self.c = 1; self.nz8(r); return r
        if mn == 'LSR': self.c = v & 1; r = v >> 1; self.nz8(r); return r
        if mn == 'ROR': r = (v >> 1) | (self.c << 7); self.c = v & 1; self.nz8(r); return r
        if mn == 'ASR': self.c = v & 1; r = (v >> 1) | (v & 0x80); self.nz8(r); return r
        if mn == 'ASL': self.c = v >> 7; r = (v << 1) & 0xFF; self.v = ((v >> 7) ^ (v >> 6)) & 1; self.nz8(r); return r
        if mn == 'ROL': r = ((v << 1) | self.c) & 0xFF; self.v = ((v >> 7) ^ (v >> 6)) & 1; self.c = v >> 7; self.nz8(r); return r
        if mn == 'DEC': r = (v - 1) & 0xFF; self.v = int(v == 0x80); self.nz8(r); return r
        if mn == 'INC': r = (v + 1) & 0xFF; self.v = int(v == 0x7F); self.nz8(r); return r
        if mn == 'TST': self.v = 0; self.nz8(v); return None
        if mn == 'CLR': self.n = 0; self.z = 1; self.v = 0; self.c = 0; return 0
        raise ValueError(mn)

    def cond(self, mn):
        n, z, v, c = self.n, self.z, self.v, self.c
        return {'BRA': True, 'BRN': False, 'BHI': not (c or z), 'BLS': bool(c or z), 'BCC': not c, 'BCS': bool(c),
                'BNE': not z, 'BEQ': bool(z), 'BVC': not v, 'BVS': bool(v), 'BPL': not n, 'BMI': bool(n),
                'BGE': n == v, 'BLT': n != v, 'BGT': (not z) and n == v, 'BLE': bool(z) or n != v}[mn]

    # ---- execution
    def call(self, addr, limit=2_000_000):
        """JSR to addr with a sentinel return address; run until it returns."""
        self.push(RET_SENTINEL & 0xFF); self.push(RET_SENTINEL >> 8)
        depth_s = self.s + 2
        self.pc = addr
        for _ in range(limit):
            if self.pc == RET_SENTINEL and self.s == depth_s: return
            self.step()
        raise RuntimeError(f'call {addr:04X} did not return')

    def step(self):
        pc = self.pc
        if pc in self.hooks:
            self.hooks[pc](self); hi = self.pull(); lo = self.pull(); self.pc = (hi << 8) | lo; return
        key = (pc, self.bank if 0x4000 <= pc < 0x6000 else 0)
        i = self.cache.get(key) if pc >= 0x4000 else None  # never cache RAM code
        if i is None:
            i = decode(self.rd, pc)
            if pc >= 0x4000: self.cache[key] = i
        self.pc = (pc + i.size) & 0xFFFF; self.steps += 1
        mn = i.mn
        if mn == 'ILL': raise RuntimeError(f'illegal opcode at {pc:04X}')
        short = mn[1:] if mn.startswith('LB') and mn != 'LBSR' else mn
        if short in ('BRA', 'BRN', 'BHI', 'BLS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BVC', 'BVS', 'BPL', 'BMI', 'BGE',
                     'BLT', 'BGT', 'BLE'):
            if self.cond(short): self.pc = i.val
            return
        if mn in ('BSR', 'LBSR', 'JSR'):
            target = i.val if mn != 'JSR' else self.ea(i)
            self.push(self.pc & 0xFF); self.push(self.pc >> 8); self.pc = target; return
        if mn == 'JMP': self.pc = self.ea(i); return
        if mn == 'RTS': hi = self.pull(); lo = self.pull(); self.pc = (hi << 8) | lo; return
        if mn in ('NOP',): return
        reg8 = {'A': 'a', 'B': 'b'}
        # 8-bit register ALU
        if mn[:-1] in ('SUB', 'CMP', 'SBC', 'AND', 'BIT', 'LD', 'EOR', 'ADC', 'OR', 'ADD') and mn[-1] in 'AB' \
                and mn not in ('ADDD', 'SUBD', 'CMPD'):
            r = reg8[mn[-1]]; op = mn[:-1]; a = getattr(self, r); b = self.operand8(i)
            if op == 'SUB': setattr(self, r, self.sub8(a, b))
            elif op == 'CMP': self.sub8(a, b)
            elif op == 'SBC': setattr(self, r, self.sub8(a, b, self.c))
            elif op == 'ADD': setattr(self, r, self.add8(a, b))
            elif op == 'ADC': setattr(self, r, self.add8(a, b, self.c))
            else:
                res = {'AND': a & b, 'BIT': a & b, 'LD': b, 'EOR': a ^ b, 'OR': a | b}[op]
                self.v = 0; self.nz8(res)
                if op != 'BIT': setattr(self, r, res)
            return
        if mn in ('STA', 'STB'):
            v = getattr(self, reg8[mn[-1]]); self.wr(self.ea(i), v); self.v = 0; self.nz8(v); return
        r16 = {'D': 'd', 'X': 'x', 'Y': 'y', 'U': 'u', 'S': 's'}
        if mn in ('LDD', 'LDX', 'LDY', 'LDU', 'LDS'):
            v = self.operand16(i); setattr(self, r16[mn[-1]], v); self.v = 0; self.nz16(v); return
        if mn in ('STD', 'STX', 'STY', 'STU', 'STS'):
            v = getattr(self, r16[mn[-1]]); self.wr16(self.ea(i), v); self.v = 0; self.nz16(v); return
        if mn in ('ADDD', 'SUBD'):
            b = self.operand16(i)
            self.d = self.add16(self.d, b) if mn == 'ADDD' else self.sub16(self.d, b); return
        if mn in ('CMPD', 'CMPX', 'CMPY', 'CMPU', 'CMPS'):
            self.sub16(getattr(self, r16[mn[-1]]), self.operand16(i)); return
        if mn in ('LEAX', 'LEAY', 'LEAU', 'LEAS'):
            e = self.ea(i); setattr(self, r16[mn[-1]], e)
            if mn in ('LEAX', 'LEAY'): self.z = int(e == 0)
            return
        base = mn[:-1] if mn[-1] in 'AB' and mn[:-1] in ('NEG', 'COM', 'LSR', 'ROR', 'ASR', 'ASL', 'ROL', 'DEC', 'INC', 'TST', 'CLR') else None
        if base and i.mode == 'inh':
            r = reg8[mn[-1]]; res = self.rmw(base, getattr(self, r))
            if res is not None: setattr(self, r, res)
            return
        if mn in ('NEG', 'COM', 'LSR', 'ROR', 'ASR', 'ASL', 'ROL', 'DEC', 'INC', 'TST', 'CLR'):
            e = self.ea(i); res = self.rmw(mn, self.rd(e))
            if res is not None: self.wr(e, res)
            return
        if mn in ('PSHS', 'PSHU'):
            other = 'u' if mn == 'PSHS' else 's'; sp = 's' if mn == 'PSHS' else 'u'
            pb = i.val
            def push(v):
                setattr(self, sp, (getattr(self, sp) - 1) & 0xFFFF); self.wr(getattr(self, sp), v)
            if pb & 0x80: push(self.pc & 0xFF); push(self.pc >> 8)
            if pb & 0x40: o = getattr(self, other); push(o & 0xFF); push(o >> 8)
            if pb & 0x20: push(self.y & 0xFF); push(self.y >> 8)
            if pb & 0x10: push(self.x & 0xFF); push(self.x >> 8)
            if pb & 0x08: push(self.dp)
            if pb & 0x04: push(self.b)
            if pb & 0x02: push(self.a)
            if pb & 0x01: push(self.cc())
            return
        if mn in ('PULS', 'PULU'):
            other = 'u' if mn == 'PULS' else 's'; sp = 's' if mn == 'PULS' else 'u'
            pb = i.val
            def pull():
                v = self.rd(getattr(self, sp)); setattr(self, sp, (getattr(self, sp) + 1) & 0xFFFF); return v
            def pull16(): hi = pull(); return (hi << 8) | pull()
            if pb & 0x01: self.set_cc(pull())
            if pb & 0x02: self.a = pull()
            if pb & 0x04: self.b = pull()
            if pb & 0x08: self.dp = pull()
            if pb & 0x10: self.x = pull16()
            if pb & 0x20: self.y = pull16()
            if pb & 0x40: setattr(self, other, pull16())
            if pb & 0x80: self.pc = pull16()
            return
        if mn == 'TFR': self.setr(i.val & 15, self.getr(i.val >> 4)); return
        if mn == 'EXG':
            a, b = i.val >> 4, i.val & 15; va, vb = self.getr(a), self.getr(b)
            self.setr(a, vb); self.setr(b, va); return
        if mn == 'ABX': self.x = (self.x + self.b) & 0xFFFF; return
        if mn == 'MUL':
            r = self.a * self.b; self.d = r; self.z = int(r == 0); self.c = (r >> 7) & 1; return
        if mn == 'SEX':
            self.a = 0xFF if self.b & 0x80 else 0; self.nz16(self.d); return
        if mn == 'ORCC': self.set_cc(self.cc() | i.val); return
        if mn == 'ANDCC': self.set_cc(self.cc() & i.val); return
        if mn == 'DAA':
            a = self.a; cf = 0; lo = a & 0x0F; hi = a >> 4
            if self.h or lo > 9: cf |= 0x06
            if self.c or hi > 9 or (hi > 8 and lo > 9): cf |= 0x60
            r = a + cf; self.c = int(self.c or r > 0xFF); self.a = r & 0xFF; self.nz8(self.a); return
        raise RuntimeError(f'unimplemented {i.text()} at {pc:04X}')
