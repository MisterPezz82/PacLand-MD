"""Static recompiler: Pac-Land main-CPU object routines (6809) -> C.

Input : res/generated/arcade/maincpu.bin, closure from tools/closure6809.py
Output: src/recomp_objects.c (generated, arcade-derived: not for the source
        archive, same policy as res/generated/)

Model (see src/recomp_rt.h):
  * registers and flags are globals (rA rB rX rY rU rS, fN fZ fV fC fH);
  * the 6809 stack lives in arcade RAM exactly as on the original (JSR pushes
    the real return address), so memory state matches the original;
  * DP is the constant 20h (the game never changes it; enforced here);
  * flags are computed only where a later instruction can read them
    (backward liveness per routine; all flags live at routine exit, and a
    callee is assumed not to read its caller's flags);
  * each routine is a C function; branches are gotos; JSR/BSR are C calls;
    JSR/JMP through tables go through rc_call(), a generated switch.
"""
import argparse
import json
import re
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m6809 import decode, flow  # noqa: E402
from closure6809 import resolve, load_rom, EXCLUDE  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
N, Z, V, C, H = 'N', 'Z', 'V', 'C', 'H'
ALL = frozenset('NZVCH')
DP = 0x20
COND_USE = {'BRA': '', 'BRN': '', 'BHI': 'CZ', 'BLS': 'CZ', 'BCC': 'C', 'BCS': 'C', 'BNE': 'Z', 'BEQ': 'Z',
            'BVC': 'V', 'BVS': 'V', 'BPL': 'N', 'BMI': 'N', 'BGE': 'NV', 'BLT': 'NV', 'BGT': 'NVZ', 'BLE': 'NVZ'}
COND_C = {'BRA': '1', 'BRN': '0', 'BHI': '!(fC|fZ)', 'BLS': '(fC|fZ)', 'BCC': '!fC', 'BCS': 'fC',
          'BNE': '!fZ', 'BEQ': 'fZ', 'BVC': '!fV', 'BVS': 'fV', 'BPL': '!fN', 'BMI': 'fN',
          'BGE': '(fN==fV)', 'BLT': '(fN!=fV)', 'BGT': '(!fZ&&fN==fV)', 'BLE': '(fZ||fN!=fV)'}
# D2B3 / D3A2: map column decoders that write foreground/background tile
# columns into arcade video RAM (0000-1FFF). Only called from D266.
VIDEO_ONLY = {0xD2B3, 0xD3A2}
# Leaf routines implemented by hand in src/recomp_rt.c (same register, flag and
# RAM results, checked by tests/recomp_diff.py): hot spots where the generated
# code is several times slower than a direct C version.
NATIVE = {0xC031, 0xC010, 0xC020, 0x9142, 0xD191, 0xC068, 0xC0AD, 0xC0B4, 0xA1DA, 0xAECE, 0xAEDA,
          0xB102, 0xB17F, 0xB192, 0xB194, 0xB1BC, 0xB1C8, 0xB1FE, 0x9BF8, 0x9CEA, 0xA3F2, 0xD1BD, 0xD266, 0x8E40, 0x8EA2}   # hand-written in src/recomp_rt.c
RMW = ('NEG', 'COM', 'LSR', 'ROR', 'ASR', 'ASL', 'ROL', 'DEC', 'INC', 'TST', 'CLR')
REG16 = {'D': 'DD', 'X': 'X', 'Y': 'Y', 'U': 'U', 'S': 'S'}


def short(mn):
    return mn[1:] if mn.startswith('LB') and mn != 'LBSR' else mn


def flags_of(i):
    """(defs, uses) flag sets for liveness."""
    mn = short(i.mn)
    if mn in COND_USE: return set(), set(COND_USE[mn])
    if mn in ('BSR', 'LBSR', 'JSR'): return set(ALL), set()      # callee result replaces flags
    if mn in ('RTS', 'JMP'): return set(), set(ALL)
    if mn in ('PSHS', 'PSHU'): return set(), (set(ALL) if i.val & 1 else set())
    if mn in ('PULS', 'PULU'):
        if i.val & 0x80: return set(), set(ALL)
        return (set(ALL) if i.val & 1 else set()), set()
    if mn in ('TFR', 'EXG'):
        src, dst = i.val >> 4, i.val & 15
        d = set(ALL) if dst == 10 or (mn == 'EXG' and src == 10) else set()
        u = set(ALL) if src == 10 or (mn == 'EXG' and dst == 10) else set()
        return d, u
    if mn in ('ORCC', 'ANDCC'): return set(ALL), set(ALL)
    if mn == 'DAA': return set('NZC'), set('CH')
    if mn in ('ADDA', 'ADDB'): return set('NZVCH'), set()
    if mn in ('ADCA', 'ADCB'): return set('NZVCH'), set('C')
    if mn in ('SBCA', 'SBCB'): return set('NZVC'), set('C')
    if mn in ('SUBA', 'SUBB', 'CMPA', 'CMPB', 'ADDD', 'SUBD', 'CMPD', 'CMPX', 'CMPY', 'CMPU', 'CMPS'):
        return set('NZVC'), set()
    if mn in ('ANDA', 'ANDB', 'BITA', 'BITB', 'LDA', 'LDB', 'STA', 'STB', 'EORA', 'EORB', 'ORA', 'ORB',
              'LDD', 'LDX', 'LDY', 'LDU', 'LDS', 'STD', 'STX', 'STY', 'STU', 'STS'):
        return set('NZV'), set()
    base = mn[:-1] if mn[-1] in 'AB' and mn[:-1] in RMW else mn
    if base in RMW:
        d = {'NEG': 'NZVC', 'COM': 'NZVC', 'LSR': 'NZC', 'ROR': 'NZC', 'ASR': 'NZC', 'ASL': 'NZVC', 'ROL': 'NZVC',
             'DEC': 'NZV', 'INC': 'NZV', 'TST': 'NZV', 'CLR': 'NZVC'}[base]
        u = 'C' if base in ('ROR', 'ROL') else ''
        return set(d), set(u)
    if mn in ('LEAX', 'LEAY'): return set('Z'), set()
    if mn == 'MUL': return set('ZC'), set()
    if mn == 'SEX': return set('NZ'), set()
    return set(), set()


class Gen:
    profile_mode = False
    access = {}

    def __init__(self, rom, funcs, itables):
        self.rom = rom; self.funcs = funcs; self.itables = itables
        self.rd = lambda a: rom[a & 0xFFFF]
        self.tmp = 0

    # --- memory expressions
    def mem8(self, ea, const=None):
        if const is not None:
            if const < 0x3800: return f'R6[0x{const:04X}]'
            if const >= 0x8000: return f'ROMB[0x{const:04X}]'
            return f'rd8(0x{const:04X})'
        return f'RD8({ea})'

    def wr8(self, ea, val, const=None):
        if const is not None and const < 0x3800: return f'R6[0x{const:04X}]=(u8)({val});'
        if const is not None: return f'wr8(0x{const:04X},(u8)({val}));'
        return f'WR8({ea},(u8)({val}));'

    def mem16(self, ea, const=None):
        if const is not None and const < 0x37FF and not const & 1: return f'R6W(0x{const:04X})'
        if const is not None and const < 0x37FF: return f'((u16)(R6[0x{const:04X}]<<8|R6[0x{const + 1:04X}]))'
        if const is not None and const >= 0x8000: return f'((u16)(ROMB[0x{const:04X}]<<8|ROMB[0x{(const + 1) & 0xFFFF:04X}]))'
        if const is not None: return f'rd16(0x{const:04X})'
        return f'RD16({ea})'

    def wr16(self, ea, val, const=None):
        if const is not None and const < 0x37FF and not const & 1:
            return f'R6W_SET(0x{const:04X},{val});'
        if const is not None and const < 0x37FF:
            return f'{{u16 _w=(u16)({val});R6[0x{const:04X}]=_w>>8;R6[0x{const + 1:04X}]=(u8)_w;}}'
        if const is not None: return f'wr16(0x{const:04X},(u16)({val}));'
        return f'WR16({ea},(u16)({val}));'

    def ea(self, i):
        """Return (prelude statements, ea C expression or None, constant ea or None)."""
        if i.mode == 'dir': c = (DP << 8) | i.val; return '', None, c
        if i.mode == 'ext': return '', None, i.val
        ix = i.ix; k = ix['kind']
        if k in ('pcr', 'abs') and not ix['ind'] and k == 'pcr': return '', None, ix['off']
        if k == 'abs':  # [n16]
            return 'ea=' + self.mem16(None, ix['off']) + ';', 'ea', None
        r = REG16[ix['reg']]
        if k == 'off': pre = f'ea=(u16)({r}+({ix["off"]}));'
        elif k == 'inc1': pre = f'ea={r};{r}=(u16)({r}+1);'
        elif k == 'inc2': pre = f'ea={r};{r}=(u16)({r}+2);'
        elif k == 'dec1': pre = f'{r}=(u16)({r}-1);ea={r};'
        elif k == 'dec2': pre = f'{r}=(u16)({r}-2);ea={r};'
        elif k == 'A': pre = f'ea=(u16)({r}+(s8)A);'
        elif k == 'B': pre = f'ea=(u16)({r}+(s8)B);'
        elif k == 'D': pre = f'ea=(u16)({r}+DD);'
        elif k == 'pcr': pre = f'ea=0x{ix["off"]:04X};'
        if ix['ind']: pre += 'ea=RD16(ea);'
        return pre, 'ea', None

    def op8(self, i):
        if i.mode == 'imm8': return '', f'0x{i.val:02X}'
        pre, e, c = self.ea(i)
        return pre, self.mem8(e, c)

    def op16(self, i):
        if i.mode == 'imm16': return '', f'0x{i.val:04X}'
        pre, e, c = self.ea(i)
        return pre, self.mem16(e, c)

    # --- flag helpers
    @staticmethod
    def nz8(r, live):
        s = ''
        if N in live: s += f'fN=(({r})&0x80)!=0;'
        if Z in live: s += f'fZ=!(u8)({r});'
        return s

    @staticmethod
    def nz16(r, live):
        s = ''
        if N in live: s += f'fN=(({r})&0x8000)!=0;'
        if Z in live: s += f'fZ=!(u16)({r});'
        return s

    def dispatch(self, site):
        """Call through a resolved jump table: switch over that table's own targets."""
        tg = sorted({t for t in self.itables.get(site, []) if t})
        if not tg or len(tg) > 64:
            return 'rc_call(_t);'
        cases = ''.join(f'case 0x{t:04X}:f_{t:04X}();break;' for t in tg)
        return f'switch(_t){{{cases}default:rc_call(_t);}}'

    def direct_table(self, i, labels):
        """A table register loaded immediately before a dispatch is constant.
        Index native function pointers directly, retaining an exact fallback
        for odd/out-of-range indices. Never infer across a branch entry."""
        if i.addr in labels or i.addr not in self.itables: return None
        ix = i.ix
        if not ix or not ix.get('ind') or ix['kind'] not in ('A', 'B'): return None
        tg = self.itables[i.addr]
        if not tg or len(tg) > 64: return None
        for size in (3, 4):
            prev = decode(self.rd, i.addr-size)
            if prev.mn == 'LD'+ix['reg'] and prev.mode == 'imm16' and prev.addr+prev.size == i.addr:
                if all(t is None or ((self.rom[prev.val+2*k]<<8)|self.rom[prev.val+2*k+1]) == t for k,t in enumerate(tg)):
                    reg=ix['kind'];base=REG16[ix['reg']]
                    if any(t is None for t in tg):
                        return (f'{{void (*_fn)(void)=(!({reg}&1)&&{reg}<{len(tg)*2})?rc_table_{i.addr:04X}[{reg}>>1]:0;'
                                f'if(_fn)_fn();else rc_call(RD16((u16)({base}+(s8){reg})));}}')
                    return (f'if(!({reg}&1)&&{reg}<{len(tg)*2})rc_table_{i.addr:04X}[{reg}>>1]();'
                            f'else rc_call(RD16((u16)({base}+(s8){reg})));')
        return None

    # ---- access-site specialisation -----------------------------------------
    # Every runtime-address access gets a site id (instruction address * 8 + n).
    # --profile builds record which memory regions each site touches; normal
    # builds use the merged profile (res/generated/recomp/access_profile.json):
    # RAM-only sites index the arcade RAM directly, ROM-only sites read the ROM
    # image directly, everything else (mixed, I/O, or never executed) keeps the
    # full address check.
    def tag_accesses(self, code, addr):
        n = [0]
        def sub(m):
            kind = m.group(1)
            sid = addr * 8 + n[0]; n[0] += 1
            if self.profile_mode:
                return f'{kind}S({sid},'
            cls = self.access.get(sid)
            if cls == 1: return f'{kind}R('
            if cls == 2 and kind.startswith('RD'): return f'{kind}O('
            return f'{kind}('
        return re.sub(r'\b(RD8|WR8|RD16|WR16)\(', sub, code)

    # RAM-only accesses at register + small constant offset: emit the base
    # register and offset separately so the cartridge build can use one
    # 68000 indexed access (d8(An,Rn.w)) instead of add + mask + index.
    def indexed(self, code):
        m = re.search(r'ea=\(u16\)\((X|Y|U|S)\+\((-?\d+)\)\);', code)
        if not m or len(re.findall(r'\bea=', code)) != 1: return code
        r, off = m.group(1), int(m.group(2))
        if not -128 <= off <= 126: return code
        for e in ('ea', 'ea2'):
            if e == 'ea2' and 'ea2=ea;' not in code: continue
            code = re.sub(r'\b(RD8|RD16)R\(' + e + r'\)', lambda q: f'{q.group(1)}X({r},{off})', code)
            code = re.sub(r'\b(WR8|WR16)R\(' + e + r',', lambda q: f'{q.group(1)}X({r},{off},', code)
        return code

    def ins(self, i, live, labels, body):
        mn = i.mn; sm = short(mn); out = []
        if sm in COND_C:
            tgt = i.val
            if tgt not in body: raise RuntimeError(f'branch out of body {i.addr:04X}->{tgt:04X}')
            if sm == 'BRN': return ''
            if sm == 'BRA': return f'goto L{tgt:04X};'
            return f'if({COND_C[sm]})goto L{tgt:04X};'
        if mn in ('BSR', 'LBSR') or (mn == 'JSR' and i.mode == 'ext'):
            ret = i.addr + i.size; t = i.val
            if t in VIDEO_ONLY:
                # arcade video-RAM column drawing: skipped when the native
                # renderer owns the scenery (rc_arcade_video == 0)
                return f'if(rc_arcade_video){{S-=2;PUSHRET(S,0x{ret:04X});@OUT@f_{t:04X}();@IN@}}'
            return f'S-=2;PUSHRET(S,0x{ret:04X});@OUT@f_{t:04X}();@IN@'
        if mn == 'JSR':
            ret = i.addr + i.size
            fast=self.direct_table(i,labels)
            if fast: return f'S-=2;PUSHRET(S,0x{ret:04X});@OUT@{fast}@IN@'
            pre, e, c = self.ea(i)
            tgt = e if e else f'0x{c:04X}'
            return f'{pre}{{u16 _t={tgt};S-=2;PUSHRET(S,0x{ret:04X});@OUT@{self.dispatch(i.addr)}@IN@}}'
        if mn == 'JMP':
            if i.mode == 'ext':
                t = i.val
                if t in body: return f'goto L{t:04X};'
                return f'@OUT@f_{t:04X}();return;'
            fast=self.direct_table(i,labels)
            if fast: return f'@OUT@{fast}return;'
            pre, e, c = self.ea(i)
            return f'{pre}{{u16 _t={e if e else hex(c)};@OUT@{self.dispatch(i.addr)}return;}}'
        if mn == 'RTS': return 'S+=2;@OUT@return;'
        if mn in ('NOP',): return ''
        # 8-bit ALU
        if mn[-1] in 'AB' and mn[:-1] in ('SUB', 'CMP', 'SBC', 'AND', 'BIT', 'LD', 'EOR', 'ADC', 'OR', 'ADD'):
            r = mn[-1]; op = mn[:-1]; pre, src = self.op8(i)
            s = pre + f'{{u16 a={r},b={src},r;'
            if op in ('SUB', 'CMP'):
                s += 'r=a-b;'
                if V in live: s += 'fV=((a^b)&(a^r)&0x80)!=0;'
                if C in live: s += 'fC=a<b;'
            elif op == 'SBC':
                s += 'r=a-b-fC;'
                if V in live: s += 'fV=((a^b)&(a^r)&0x80)!=0;'
                if C in live: s += 'fC=r>0xFF;'
            elif op in ('ADD', 'ADC'):
                s += f'r=a+b{"+fC" if op == "ADC" else ""};'
                if H in live: s += 'fH=((a^b^r)&0x10)!=0;'
                if V in live: s += 'fV=((a^r)&(b^r)&0x80)!=0;'
                if C in live: s += 'fC=r>0xFF;'
            else:
                s += {'AND': 'r=a&b;', 'BIT': 'r=a&b;', 'LD': 'r=b;', 'EOR': 'r=a^b;', 'OR': 'r=a|b;'}[op]
                if V in live: s += 'fV=0;'
            s += self.nz8('r', live)
            if op not in ('CMP', 'BIT'): s += f'{r}=(u8)r;'
            return s + '(void)a;}'
        if mn in ('STA', 'STB'):
            r = mn[-1]; pre, e, c = self.ea(i)
            s = pre + self.wr8(e, r, c)
            if V in live: s += 'fV=0;'
            return s + self.nz8(r, live)
        if mn in ('LDD', 'LDX', 'LDY', 'LDU', 'LDS'):
            pre, src = self.op16(i); r = REG16[mn[-1]]
            s = pre + (f'{{u16 v={src};SETDL(v);' if r == 'DD' else f'{{u16 v={src};{r}=v;')
            if V in live: s += 'fV=0;'
            return s + self.nz16('v', live) + '}'
        if mn in ('STD', 'STX', 'STY', 'STU', 'STS'):
            r = REG16[mn[-1]]; pre, e, c = self.ea(i)
            s = pre + f'{{u16 v={r};' + self.wr16(e, 'v', c)
            if V in live: s += 'fV=0;'
            return s + self.nz16('v', live) + '}'
        if mn in ('ADDD', 'SUBD', 'CMPD', 'CMPX', 'CMPY', 'CMPU', 'CMPS'):
            pre, src = self.op16(i)
            r = 'DD' if mn in ('ADDD', 'SUBD', 'CMPD') else REG16[mn[-1]]
            s = pre + f'{{u16 a={r},b={src},r;'
            if mn == 'ADDD':
                s += 'r=a+b;'
                if V in live: s += 'fV=((a^r)&(b^r)&0x8000)!=0;'
                if C in live: s += 'fC=r<a;'
            else:
                s += 'r=a-b;'
                if V in live: s += 'fV=((a^b)&(a^r)&0x8000)!=0;'
                if C in live: s += 'fC=a<b;'
            s += self.nz16('r', live)
            if mn in ('ADDD', 'SUBD'): s += 'SETDL(r);'
            return s + '}'
        if mn in ('LEAX', 'LEAY', 'LEAU', 'LEAS'):
            pre, e, c = self.ea(i); r = REG16[mn[-1]]
            s = pre + f'{r}={e if e else hex(c)};'
            if mn in ('LEAX', 'LEAY') and Z in live: s += f'fZ=!{r};'
            return s
        base = mn[:-1] if mn[-1] in 'AB' and mn[:-1] in RMW and i.mode == 'inh' else None
        if base or mn in RMW:
            if base:
                r = mn[-1]; pre = ''; load = r; store = lambda v: f'{r}=(u8)({v});'
            else:
                base = mn; pre, e, c = self.ea(i)
                if e: pre += 'ea2=ea;'
                load = self.mem8('ea2' if e else None, c)
                store = (lambda v, e=e, c=c: self.wr8('ea2' if e else None, v, c))
            s = pre + f'{{u16 v={load},r;(void)v;'
            if base == 'NEG':
                s += 'r=(-v)&0xFF;'
                if V in live: s += 'fV=v==0x80;'
                if C in live: s += 'fC=v!=0;'
            elif base == 'COM':
                s += 'r=(~v)&0xFF;'
                if V in live: s += 'fV=0;'
                if C in live: s += 'fC=1;'
            elif base == 'LSR':
                s += 'r=v>>1;' + ('fC=v&1;' if C in live else '')
            elif base == 'ROR':
                s += 'r=(v>>1)|(fC?0x80:0);' + ('fC=v&1;' if C in live else '')
            elif base == 'ASR':
                s += 'r=(v>>1)|(v&0x80);' + ('fC=v&1;' if C in live else '')
            elif base == 'ASL':
                s += 'r=(v<<1)&0xFF;'
                if V in live: s += 'fV=((v^(v<<1))&0x80)!=0;'
                if C in live: s += 'fC=(v&0x80)!=0;'
            elif base == 'ROL':
                s += 'r=((v<<1)|fC)&0xFF;'
                if V in live: s += 'fV=((v^(v<<1))&0x80)!=0;'
                if C in live: s += 'fC=(v&0x80)!=0;'
            elif base == 'DEC':
                s += 'r=(v-1)&0xFF;' + ('fV=v==0x80;' if V in live else '')
            elif base == 'INC':
                s += 'r=(v+1)&0xFF;' + ('fV=v==0x7F;' if V in live else '')
            elif base == 'TST':
                s += 'r=v;' + ('fV=0;' if V in live else '')
            elif base == 'CLR':
                s += 'r=0;' + ('fV=0;' if V in live else '') + ('fC=0;' if C in live else '')
            s += self.nz8('r', live)
            if base != 'TST': s += store('r')
            return s + '}'
        if mn in ('PSHS', 'PSHU'):
            sp = 'S' if mn == 'PSHS' else 'U'; other = 'U' if mn == 'PSHS' else 'S'; pb = i.val; s = ''
            if pb & 0x80: s += f'{sp}-=2;WR16({sp},0x{(i.addr + i.size) & 0xFFFF:04X});'
            if pb & 0x40: s += f'{sp}-=2;WR16({sp},{other});'
            if pb & 0x20: s += f'{sp}-=2;WR16({sp},Y);'
            if pb & 0x10: s += f'{sp}-=2;WR16({sp},X);'
            if pb & 0x08: s += f'{sp}-=1;WR8({sp},0x20);'
            if pb & 0x04: s += f'{sp}-=1;WR8({sp},B);'
            if pb & 0x02: s += f'{sp}-=1;WR8({sp},A);'
            if pb & 0x01: s += f'{sp}-=1;WR8({sp},GETCC());'
            return s
        if mn in ('PULS', 'PULU'):
            sp = 'S' if mn == 'PULS' else 'U'; other = 'U' if mn == 'PULS' else 'S'; pb = i.val; s = ''
            if pb & 0x01: s += f'SETCC(RD8({sp}));{sp}+=1;'
            if pb & 0x02: s += f'A=RD8({sp});{sp}+=1;'
            if pb & 0x04: s += f'B=RD8({sp});{sp}+=1;'
            if pb & 0x08: s += f'rc_dp(RD8({sp}));{sp}+=1;'
            if pb & 0x10: s += f'X=RD16({sp});{sp}+=2;'
            if pb & 0x20: s += f'Y=RD16({sp});{sp}+=2;'
            if pb & 0x40: s += f'{other}=RD16({sp});{sp}+=2;'
            if pb & 0x80:
                if mn != 'PULS': raise RuntimeError('PULU PC')
                s += f'{sp}+=2;@OUT@return;'
            return s
        if mn in ('TFR', 'EXG'):
            names = {0: 'DD', 1: 'X', 2: 'Y', 3: 'U', 4: 'S', 8: 'A', 9: 'B', 10: 'CC'}
            a, b = i.val >> 4, i.val & 15
            if a not in names or b not in names: raise RuntimeError(f'unsupported {i.text()} at {i.addr:04X}')
            get = lambda n: {'CC': 'GETCC()'}.get(names[n], names[n])
            def put(n, v):
                nm = names[n]
                if nm == 'DD': return f'SETDL({v});'
                if nm == 'CC': return f'SETCC({v});'
                if nm in ('A', 'B'): return f'{nm}=(u8)({v});'
                return f'{nm}=(u16)({v});'
            if mn == 'TFR': return put(b, get(a))
            return f'{{u16 _a={get(a)},_b={get(b)};{put(a, "_b")}{put(b, "_a")}}}'
        if mn == 'ABX': return 'X=(u16)(X+B);'
        if mn == 'MUL':
            s = '{u16 r=(u16)A*B;SETDL(r);'
            if Z in live: s += 'fZ=!r;'
            if C in live: s += 'fC=r>>7&1;'
            return s + '}'
        if mn == 'SEX':
            return 'A=(B&0x80)?0xFF:0;' + self.nz16('DD', live)
        if mn == 'ORCC': return f'SETCC(GETCC()|0x{i.val:02X});'
        if mn == 'ANDCC': return f'SETCC(GETCC()&0x{i.val:02X});'
        if mn == 'DAA':
            return ('{u16 a=A,cf=0,r;if(fH||(a&15)>9)cf|=6;if(fC||(a>>4)>9||((a>>4)>8&&(a&15)>9))cf|=0x60;'
                    'r=a+cf;fC=fC|(r>0xFF);A=(u8)r;' + self.nz8('A', live) + '}')
        raise RuntimeError(f'unsupported {i.text()} at {i.addr:04X}')

    # ---- flag analysis across routines -------------------------------------
    # Flags are C locals inside each routine. A routine "exports" flags when
    # some caller reads them after the call returns (it then stores them to the
    # globals gfN.. on return), and "imports" them when it reads flags before
    # setting them (callers then store theirs before the call).
    def targets_of(self, i):
        f, t = flow(i)
        if f == 'call' or (f == 'jump' and i.mn == 'JMP'): return [t]
        if f in ('icall', 'ijump'): return [x for x in self.itables.get(i.addr, []) if x]
        return []

    def analyse(self, entry, addrs):
        rd = self.rd
        body = {a: decode(rd, a) for a in addrs}
        succ = {}
        for a, i in body.items():
            f, t = flow(i); s = []
            if f in ('next', 'call', 'icall', 'cond'): s.append(a + i.size)
            if f in ('cond', 'jump') and t in body: s.append(t)
            succ[a] = [x for x in s if x in body]
            if f in ('next', 'call', 'icall', 'cond') and (a + i.size) not in body:
                raise RuntimeError(f'fallthrough out of body at {a:04X} in {entry:04X}')
        exported = self.export.get(entry, set())
        defs = {}; uses = {}
        for a, i in body.items():
            d, u = flags_of(i)
            f, t = flow(i)
            if i.mn in ('RTS', 'RTI') or (i.mn == 'PULS' and i.val & 0x80):
                u = set(exported)
            elif f in ('call', 'icall') or f == 'ijump' or (f == 'jump' and t not in body):
                u = set()
                for x in self.targets_of(i): u |= self.import_.get(x, set())
            elif f == 'jump':
                u = set()          # JMP inside the routine: a plain goto
            if i.mn in ('PSHS', 'PSHU') and i.val & 1: u = set(ALL)
            defs[a] = d; uses[a] = u
        live_in = {a: set() for a in body}; live_out = {a: set() for a in body}
        changed = True
        while changed:
            changed = False
            for a in sorted(body, reverse=True):
                out = set()
                for x in succ[a]: out |= live_in[x]
                inn = uses[a] | (out - defs[a])
                if out != live_out[a] or inn != live_in[a]:
                    live_out[a] = out; live_in[a] = inn; changed = True
        return body, live_in, live_out

    def solve_flags(self):
        self.export, self.import_ = {}, {}
        while True:
            exp, imp = {}, {}
            self.cache = {}
            for e, addrs in self.funcs.items():
                body, li, lo = self.analyse(e, addrs)
                self.cache[e] = (body, li, lo)
                if li[e] & ALL: imp[e] = set(li[e] & ALL)
                for a, i in body.items():
                    f, t = flow(i)
                    need = set()
                    if f in ('call', 'icall'):
                        r = a + i.size
                        if r in li: need = li[r] & ALL
                    if f == 'ijump' or (f == 'jump' and t not in body):
                        need = set(self.export.get(e, set()))
                    if need:
                        for x in self.targets_of(i): exp.setdefault(x, set()).update(need)
            for k, v in self.export.items(): exp.setdefault(k, set()).update(v)
            for k, v in self.import_.items(): imp.setdefault(k, set()).update(v)
            if exp == self.export and imp == self.import_: break
            self.export, self.import_ = exp, imp

    def function(self, entry, addrs):
        body, live_in, live_out = self.cache[entry]
        st = lambda fl: ''.join(f'gf{k}=f{k};' for k in 'NZVCH' if k in fl)
        targets = set()
        for a, i in body.items():
            f, t = flow(i)
            if f in ('cond', 'jump') and t in body: targets.add(t)
        targets.add(entry)
        lines = []
        if min(body) != entry:
            lines.append(f'goto L{entry:04X};')
        prev = None
        dvalid = False   # D (A:B) is known to equal the local dc (set by SETDL)
        wr_ab = re.compile(r'(?<![\w.])[AB]=(?!=)|SETDL\(|\bf_[0-9A-F]{4}\(|rc_call|switch\(')
        for a in sorted(body):
            i = body[a]
            if prev is not None and prev != a:
                lines.append('rc_trap(0x%04X);' % prev)  # gap: previous instruction must not fall through
            lab = f'L{a:04X}:' if a in targets else ''
            code = self.ins(i, live_out[a] | self.extra_live(i), targets, body)
            if a != entry and a in NATIVE and not self.export.get(entry) and not self.import_.get(a):
                lines.append(f'{lab}/*{a:04X}: shared native tail*/ f_{a:04X}();return;')
                prev = a+i.size
                dvalid = False
                continue

            code = self.tag_accesses(code, a)
            code = self.indexed(code)
            # D cache: reuse the value last loaded with SETDL instead of
            # recombining A and B, while nothing has written A or B since.
            if a in targets: dvalid = False
            ws = [m for m in wr_ab.finditer(code)]
            cut = ws[0].start() if ws else len(code)
            if dvalid: code = re.sub(r'\bDD\b', 'dc', code[:cut]) + code[cut:]
            if ws: dvalid = ws[-1].group(0) == 'SETDL('
            f, t = flow(i)
            tg = self.targets_of(i)
            if i.mn in ('RTS', 'RTI') or (i.mn == 'PULS' and i.val & 0x80):
                code = code.replace('@OUT@', st(self.export.get(entry, set())))
            elif f in ('call', 'icall', 'jump', 'ijump'):
                need = set()
                for x in tg: need |= self.import_.get(x, set())
                pre = st(need)
                post = ''
                r = a + i.size
                if f in ('call', 'icall') and r in live_in:
                    post = ''.join(f'f{k}=gf{k};' for k in 'NZVCH' if k in live_in[r])
                code = code.replace('@OUT@', pre).replace('@IN@', post)
            lines.append(f'{lab}/*{a:04X} {i.text()}*/ {code}')
            prev = a + i.size
        f, _ = flow(body[max(body)])
        if f not in ('end', 'jump', 'ijump'): lines.append('rc_trap(0xFFFF);')
        text = '\n'.join(lines)
        init = ''.join(f'f{k}=gf{k};' for k in 'NZVCH' if k in self.import_.get(entry, set()))
        return (f'void f_{entry:04X}(void){{u16 ea,ea2,dc=0;(void)dc;u8 fN=0,fZ=0,fV=0,fC=0,fH=0;'
                f'(void)ea;(void)ea2;(void)fN;(void)fZ;(void)fV;(void)fC;(void)fH;{init}\n' + text + '}')

    @staticmethod
    def extra_live(i):
        return set()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, default=ROOT / 'src/recomp_objects.c')
    p.add_argument('--profile', action='store_true', help='emit access-recording build for the host')
    a = p.parse_args()
    rom = load_rom()
    ov_path = ROOT / 'tools/recomp_overrides.json'
    ov = json.loads(ov_path.read_text()) if ov_path.exists() else {}
    funcs, itables, hazards = resolve(rom, 32, ov.get('indirect', {}))
    funcs = {k: v for k, v in funcs.items() if k >= 0x8000}
    g = Gen(rom, funcs, itables)
    g.profile_mode = a.profile
    prof = ROOT / 'res/generated/recomp/access_profile.json'
    if not a.profile and prof.exists():
        g.access = {int(k): v for k, v in json.loads(prof.read_text()).items()}
    g.solve_flags()
    print(f'flags: {sum(1 for v in g.export.values() if v)} routines return flags to callers, '
          f'{sum(1 for v in g.import_.values() if v)} read incoming flags')
    for f, body in funcs.items():
        for x in body:
            i = decode(g.rd, x)
            if i.mn == 'TFR' and (i.val & 15) == 11 or i.mn == 'EXG' and 11 in (i.val >> 4, i.val & 15):
                raise SystemExit(f'DP write at {x:04X}')
    parts = ['/* GENERATED by tools/recomp6809.py from the user\'s Pac-Land main CPU ROM.',
             ' * Arcade-derived: do not distribute. Regenerate instead of editing. */',
             '#ifndef RC_HOST', '#pragma GCC optimize ("O2")', '#endif',
             '#define RC_GLOBAL_REGS', '#define RC_R6_REG',
             '#include "recomp_rt.h"',
             '#define A rA', '#define B rB', '#define X rX', '#define Y rY', '#define U rU', '#define S rS',
             '#define DD ((u16)((A << 8) | B))',
             '#define SETDL(v) do { u16 _v = (v); dc = _v; A = (u8)(_v >> 8); B = (u8)_v; } while (0)']
    names = sorted(funcs)
    parts += [f'void f_{e:04X}(void);' for e in sorted(set(names) | EXCLUDE | NATIVE)]
    for site, tg in sorted(itables.items()):
        if tg and len(tg)<=64:
            parts.append(f'static void (*const rc_table_{site:04X}[])(void)={{'+','.join(f'f_{t:04X}' if t is not None else '0' for t in tg)+'};')
    for e in names:
        if e not in NATIVE: parts.append(g.function(e, funcs[e]))
    parts.append('void rc_call(u16 a){switch(a){')
    for e in names + sorted(EXCLUDE):
        parts.append(f'case 0x{e:04X}:f_{e:04X}();return;')
    parts.append('default:rc_unknown(a);}}')
    parts.append(f'const u16 rc_function_count={len(names)};')
    parts.append('const u8 rc_div9[1152]={'+','.join(str(i//9)for i in range(1152))+'};')
    # per-type update handlers (table 8324) for the native object loop
    upd = [(rom[0x8324 + 2 * t] << 8) | rom[0x8325 + 2 * t] for t in range(50)]
    parts.append('void (*const rc_update_fn[50])(void)={' + ','.join(f'f_{u:04X}' for u in upd) + '};')
    # Combined gate bounds/update pointer lets the assembly scan use one
    # indexed descriptor rather than repeatedly decoding the gate kind.
    parts.append('const struct {u16 width, bias;void (*update)(void);} rc_slot_info[50]={')
    bounds={0x826B:(1,0),0x826D:(2,0),0x8272:(592,432),0x8287:(432,216),0x829C:(64,32),0x82B1:(0,0)}
    for typ, target in enumerate(upd):
        gate=(rom[0x82C0+typ*2]<<8)|rom[0x82C1+typ*2]
        width,bias=bounds[gate]
        parts.append(f'{{{width},{bias},f_{target:04X}}},')
    parts.append('};')
    parts.append('#ifndef RC_HOST\n_Static_assert(sizeof(rc_slot_info[0])==8,"native slot descriptor layout");\n#endif')

    a.out.write_text('\n'.join(parts) + '\n')
    n_ins = sum(len(v) for v in funcs.values())
    print(f'wrote {a.out} : {len(names)} functions, {n_ins} instructions (with duplication)')
    if not a.profile:
        text = a.out.read_text()
        print('access sites: RAM-only', len(re.findall(r'\b(?:RD8|WR8|RD16|WR16)R\(', text)),
              'ROM-only', len(re.findall(r'\b(?:RD8|RD16)O\(', text)),
              'checked', len(re.findall(r'\b(?:RD8|WR8|RD16|WR16)\(', text)))


if __name__ == '__main__':
    main()
