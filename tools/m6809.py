"""Structured MC6809 instruction decoder shared by the recompiler and the
reference interpreter (tools/recomp6809.py, tools/sim6809.py).

decode(read, addr) -> Ins with fields:
  addr, size, mn (mnemonic, e.g. 'LDA', 'LBNE', 'PSHS'),
  mode: 'inh' 'imm8' 'imm16' 'dir' 'ext' 'idx' 'rel' 'reg' 'stack'
  val: immediate value / direct offset / extended address / branch target / postbyte
  ix: for 'idx': dict(reg, kind, off, ind) where kind in
      'off' (constant off), 'inc1','inc2','dec1','dec2','A','B','D','pcr','abs'
"""
from dataclasses import dataclass, field

R = ['X', 'Y', 'U', 'S']
MEM = ['NEG', None, None, 'COM', 'LSR', None, 'ROR', 'ASR', 'ASL', 'ROL', 'DEC', None, 'INC', 'TST', 'JMP', 'CLR']
BR = ['BRA', 'BRN', 'BHI', 'BLS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BVC', 'BVS', 'BPL', 'BMI', 'BGE', 'BLT', 'BGT', 'BLE']
AOPS = ['SUBA', 'CMPA', 'SBCA', 'SUBD', 'ANDA', 'BITA', 'LDA', 'STA', 'EORA', 'ADCA', 'ORA', 'ADDA', 'CMPX', 'JSR', 'LDX', 'STX']
BOPS = ['SUBB', 'CMPB', 'SBCB', 'ADDD', 'ANDB', 'BITB', 'LDB', 'STB', 'EORB', 'ADCB', 'ORB', 'ADDB', 'LDD', 'STD', 'LDU', 'STU']
P2 = {0x83: 'CMPD', 0x8C: 'CMPY', 0x8E: 'LDY', 0x8F: 'STY', 0xCE: 'LDS', 0xCF: 'STS'}
P3 = {0x83: 'CMPU', 0x8C: 'CMPS'}
TFR = {0: 'D', 1: 'X', 2: 'Y', 3: 'U', 4: 'S', 5: 'PC', 8: 'A', 9: 'B', 10: 'CC', 11: 'DP'}
WIDE = {'SUBD', 'ADDD', 'CMPX', 'LDX', 'STX', 'LDD', 'STD', 'LDU', 'STU', 'CMPD', 'CMPY', 'LDY', 'STY',
        'LDS', 'STS', 'CMPU', 'CMPS'}


@dataclass
class Ins:
    addr: int
    size: int
    mn: str
    mode: str
    val: int = 0
    ix: dict = field(default_factory=dict)

    @property
    def wide(self):
        return self.mn in WIDE

    def text(self):
        m, v = self.mode, self.val
        if m == 'inh': return self.mn
        if m == 'imm8': return f'{self.mn} #${v:02X}'
        if m == 'imm16': return f'{self.mn} #${v:04X}'
        if m == 'dir': return f'{self.mn} <${v:02X}'
        if m in ('ext', 'rel'): return f'{self.mn} ${v:04X}'
        if m == 'reg': return f'{self.mn} {TFR.get(v >> 4, "?")},{TFR.get(v & 15, "?")}'
        if m == 'stack': return f'{self.mn} #${v:02X}'
        i = self.ix; k = i['kind']; r = i['reg']
        s = {'off': f'{i.get("off", 0)},{r}', 'inc1': f',{r}+', 'inc2': f',{r}++', 'dec1': f',-{r}',
             'dec2': f',--{r}', 'A': f'A,{r}', 'B': f'B,{r}', 'D': f'D,{r}', 'pcr': f'${i.get("off", 0):04X},PCR',
             'abs': f'${i.get("off", 0):04X}'}[k]
        return f'{self.mn} [{s}]' if i['ind'] else f'{self.mn} {s}'


def s8(v): return v - 256 if v & 0x80 else v
def s16(v): return v - 65536 if v & 0x8000 else v


def _idx(rd, a):
    pb = rd(a); r = R[(pb >> 5) & 3]
    if not pb & 0x80:
        off = pb & 0x1F
        return dict(reg=r, kind='off', off=off - 32 if off & 0x10 else off, ind=False), 1
    t = pb & 0x0F; ind = bool(pb & 0x10)
    simple = {0: 'inc1', 1: 'inc2', 2: 'dec1', 3: 'dec2', 5: 'B', 6: 'A', 0xB: 'D'}
    if t in simple: return dict(reg=r, kind=simple[t], ind=ind), 1
    if t == 4: return dict(reg=r, kind='off', off=0, ind=ind), 1
    if t == 8: return dict(reg=r, kind='off', off=s8(rd(a + 1)), ind=ind), 2
    if t == 9: return dict(reg=r, kind='off', off=s16((rd(a + 1) << 8) | rd(a + 2)), ind=ind), 3
    if t == 0xC: return dict(reg='PC', kind='pcr', off=(a + 2 + s8(rd(a + 1))) & 0xFFFF, ind=ind), 2
    if t == 0xD: return dict(reg='PC', kind='pcr', off=(a + 3 + ((rd(a + 1) << 8) | rd(a + 2))) & 0xFFFF, ind=ind), 3
    if t == 0xF: return dict(reg=None, kind='abs', off=(rd(a + 1) << 8) | rd(a + 2), ind=True), 3
    return None, 1


def decode(rd, a):
    w = lambda x: (rd(x) << 8) | rd(x + 1)
    op = rd(a)
    if op in (0x10, 0x11):
        page = op; op = rd(a + 1)
        if page == 0x10 and 0x21 <= op <= 0x2F:
            return Ins(a, 4, 'L' + BR[op & 15], 'rel', (a + 4 + w(a + 2)) & 0xFFFF)
        if op == 0x3F: return Ins(a, 2, 'SWI2' if page == 0x10 else 'SWI3', 'inh')
        name = (P2 if page == 0x10 else P3).get(0x80 | (op & 0x4F))
        if name is None or op < 0x80: return Ins(a, 2, 'ILL', 'inh', (page << 8) | op)
        mode = (op >> 4) & 3
        if mode == 0:
            if name.startswith('ST'): return Ins(a, 2, 'ILL', 'inh', op)
            return Ins(a, 4, name, 'imm16', w(a + 2))
        if mode == 1: return Ins(a, 3, name, 'dir', rd(a + 2))
        if mode == 2:
            ix, n = _idx(rd, a + 2)
            return Ins(a, 2 + n, name, 'idx', 0, ix) if ix else Ins(a, 2, 'ILL', 'inh', op)
        return Ins(a, 4, name, 'ext', w(a + 2))
    hi = op >> 4
    if hi in (0, 6, 7):
        name = MEM[op & 15]
        if name is None: return Ins(a, 1, 'ILL', 'inh', op)
        if hi == 0: return Ins(a, 2, name, 'dir', rd(a + 1))
        if hi == 7: return Ins(a, 3, name, 'ext', w(a + 1))
        ix, n = _idx(rd, a + 1)
        return Ins(a, 1 + n, name, 'idx', 0, ix) if ix else Ins(a, 1, 'ILL', 'inh', op)
    if hi == 1:
        simple = {0x12: 'NOP', 0x13: 'SYNC', 0x19: 'DAA', 0x1D: 'SEX'}
        if op in simple: return Ins(a, 1, simple[op], 'inh')
        if op in (0x16, 0x17): return Ins(a, 3, 'LBRA' if op == 0x16 else 'LBSR', 'rel', (a + 3 + w(a + 1)) & 0xFFFF)
        if op in (0x1A, 0x1C): return Ins(a, 2, 'ORCC' if op == 0x1A else 'ANDCC', 'imm8', rd(a + 1))
        if op in (0x1E, 0x1F): return Ins(a, 2, 'EXG' if op == 0x1E else 'TFR', 'reg', rd(a + 1))
        return Ins(a, 1, 'ILL', 'inh', op)
    if hi == 2:
        return Ins(a, 2, BR[op & 15], 'rel', (a + 2 + s8(rd(a + 1))) & 0xFFFF)
    if hi == 3:
        if op <= 0x33:
            ix, n = _idx(rd, a + 1)
            return Ins(a, 1 + n, 'LEA' + 'XYSU'[op & 3], 'idx', 0, ix) if ix else Ins(a, 1, 'ILL', 'inh', op)
        if op <= 0x37: return Ins(a, 2, ['PSHS', 'PULS', 'PSHU', 'PULU'][op - 0x34], 'stack', rd(a + 1))
        simple = {0x39: 'RTS', 0x3A: 'ABX', 0x3B: 'RTI', 0x3D: 'MUL', 0x3F: 'SWI'}
        if op in simple: return Ins(a, 1, simple[op], 'inh')
        if op == 0x3C: return Ins(a, 2, 'CWAI', 'imm8', rd(a + 1))
        return Ins(a, 1, 'ILL', 'inh', op)
    if hi in (4, 5):
        name = MEM[op & 15]
        if name is None or name == 'JMP': return Ins(a, 1, 'ILL', 'inh', op)
        return Ins(a, 1, name + ('A' if hi == 4 else 'B'), 'inh')
    name = (AOPS if hi < 0xC else BOPS)[op & 15]; mode = (op >> 4) & 3
    if op == 0x8D: return Ins(a, 2, 'BSR', 'rel', (a + 2 + s8(rd(a + 1))) & 0xFFFF)
    if mode == 0:
        if op in (0x87, 0x8F, 0xC7, 0xCD, 0xCF): return Ins(a, 1, 'ILL', 'inh', op)
        if name in WIDE: return Ins(a, 3, name, 'imm16', w(a + 1))
        return Ins(a, 2, name, 'imm8', rd(a + 1))
    if mode == 1: return Ins(a, 2, name, 'dir', rd(a + 1))
    if mode == 3: return Ins(a, 3, name, 'ext', w(a + 1))
    ix, n = _idx(rd, a + 1)
    return Ins(a, 1 + n, name, 'idx', 0, ix) if ix else Ins(a, 1, 'ILL', 'inh', op)


COND = {'BRA', 'BRN', 'BHI', 'BLS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BVC', 'BVS', 'BPL', 'BMI', 'BGE', 'BLT', 'BGT', 'BLE'}


def flow(i):
    """('next'|'end'|'jump'|'cond'|'call'|'icall'|'ijump', static_target or None)."""
    mn = i.mn.lstrip('L') if i.mn.startswith('LB') else i.mn
    if i.mn == 'ILL': return 'end', None
    if mn in COND:
        if mn == 'BRA': return 'jump', i.val
        if mn == 'BRN': return 'next', None
        return 'cond', i.val
    if mn in ('BSR', 'BSR') or i.mn == 'LBSR': return 'call', i.val
    if i.mn == 'JSR':
        if i.mode == 'ext': return 'call', i.val
        return 'icall', None
    if i.mn == 'JMP':
        if i.mode == 'ext': return 'jump', i.val
        return 'ijump', None
    if i.mn in ('RTS', 'RTI'): return 'end', None
    if i.mn in ('PULS', 'PULU') and i.val & 0x80: return 'end', None
    if i.mn == 'TFR' and (i.val & 15) == 5: return 'end', None
    if i.mn == 'EXG' and ((i.val & 15) == 5 or (i.val >> 4) == 5): return 'end', None
    return 'next', None
