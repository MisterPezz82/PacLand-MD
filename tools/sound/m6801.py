"""HD63701 (6801 + Hitachi extensions) CPU core and disassembler, for the
Pac-Land sound MCU (CUS60 internal ROM + pl1_7.3e driver)."""
T = {}
def op(o, m, mode, c): T[o] = (m, mode, c)
inh = [(0x01,'NOP',1),(0x04,'LSRD',1),(0x05,'ASLD',1),(0x06,'TAP',1),(0x07,'TPA',1),(0x08,'INX',1),(0x09,'DEX',1),
 (0x0A,'CLV',1),(0x0B,'SEV',1),(0x0C,'CLC',1),(0x0D,'SEC',1),(0x0E,'CLI',1),(0x0F,'SEI',1),(0x10,'SBA',1),(0x11,'CBA',1),
 (0x16,'TAB',1),(0x17,'TBA',1),(0x18,'XGDX',2),(0x19,'DAA',2),(0x1A,'SLP',4),(0x1B,'ABA',1),
 (0x30,'TSX',1),(0x31,'INS',1),(0x32,'PULA',3),(0x33,'PULB',3),(0x34,'DES',1),(0x35,'TXS',1),(0x36,'PSHA',4),(0x37,'PSHB',4),
 (0x38,'PULX',4),(0x39,'RTS',5),(0x3A,'ABX',1),(0x3B,'RTI',10),(0x3C,'PSHX',5),(0x3D,'MUL',7),(0x3E,'WAI',9),(0x3F,'SWI',12)]
for o,m,c in inh: op(o,m,'inh',c)
for i,m in enumerate(['BRA','BRN','BHI','BLS','BCC','BCS','BNE','BEQ','BVC','BVS','BPL','BMI','BGE','BLT','BGT','BLE']): op(0x20+i,m,'rel',3)
un = ['NEG',None,None,'COM','LSR',None,'ROR','ASR','ASL','ROL','DEC',None,'INC','TST','JMP','CLR']
for i,m in enumerate(un):
    if m:
        if m!='JMP': op(0x40+i,m+'A','inh',1); op(0x50+i,m+'B','inh',1)
        op(0x60+i,m,'idx',6 if m!='JMP' else 3); op(0x70+i,m,'ext',6 if m!='JMP' else 3)
for o,m in [(0x61,'AIM'),(0x62,'OIM'),(0x65,'EIM'),(0x6B,'TIM')]: op(o,m,'imidx',7 if m!='TIM' else 5)
for o,m in [(0x71,'AIM'),(0x72,'OIM'),(0x75,'EIM'),(0x7B,'TIM')]: op(o,m,'imdir',6 if m!='TIM' else 4)
ALU8=('SUB','CMP','SBC','AND','BIT','LDA','STA','EOR','ADC','ORA','ADD')
acc = ['SUB','CMP','SBC','SUBD','AND','BIT','LDA','STA','EOR','ADC','ORA','ADD','CPX','JSR','LDS','STS']
for base,r in [(0x80,'A'),(0xC0,'B')]:
    for i,m in enumerate(acc):
        if base==0xC0: m={'SUBD':'ADDD','CPX':'LDD','JSR':'STD','LDS':'LDX','STS':'STX'}.get(m,m)
        name = m+r if m in ALU8 else m
        w16 = m in ('SUBD','ADDD','CPX','LDD','LDS','LDX')
        for j,mode in enumerate(['imm','dir','idx','ext']):
            o=base+i+0x10*j
            if mode=='imm':
                if m in ('STA','STD','STS','STX','JSR'): continue
                op(o,name,'imm16' if w16 else 'imm8',3 if w16 else 2)
            else:
                cyc={'dir':3,'idx':4,'ext':4}[mode]+(1 if w16 or m in('STD','STS','STX') else 0)
                if m=='JSR': cyc={'dir':5,'idx':5,'ext':6}[mode]
                op(o,name,mode,cyc)
op(0x8D,'BSR','rel',5)
SIZE={'inh':1,'rel':2,'imm8':2,'imm16':3,'dir':2,'idx':2,'ext':3,'imdir':3,'imidx':3}
def disasm(rd, a):
    o=rd(a)
    if o not in T: return f'FCB ${o:02X}',1
    m,mode,_=T[o]; n=SIZE[mode]
    b1=rd((a+1)&0xFFFF); w=(b1<<8)|rd((a+2)&0xFFFF)
    s={'inh':'','rel':f'${(a+2+(b1-256 if b1>127 else b1))&0xFFFF:04X}','imm8':f'#${b1:02X}','imm16':f'#${w:04X}',
       'dir':f'<${b1:02X}','idx':f'{b1},X','ext':f'${w:04X}','imdir':f'#${b1:02X},<${rd((a+2)&0xFFFF):02X}',
       'imidx':f'#${b1:02X},{rd((a+2)&0xFFFF)},X'}[mode]
    return f'{m} {s}'.strip(),n

UNARY=('NEG','COM','LSR','ROR','ASR','ASL','ROL','DEC','INC','TST','CLR')
class CPU:
    def __init__(s, rd, wr):
        s.rd=rd; s.wr=wr; s.a=s.b=0; s.x=0; s.sp=0; s.pc=0; s.cc=0xD0; s.cycles=0; s.wai=False
    def r16(s,a): return (s.rd(a&0xFFFF)<<8)|s.rd((a+1)&0xFFFF)
    def w16(s,a,v): s.wr(a&0xFFFF,(v>>8)&0xFF); s.wr((a+1)&0xFFFF,v&0xFF)
    def push(s,v): s.wr(s.sp,v&0xFF); s.sp=(s.sp-1)&0xFFFF
    def pull(s): s.sp=(s.sp+1)&0xFFFF; return s.rd(s.sp)
    def push16(s,v): s.push(v); s.push(v>>8)
    def pull16(s): h=s.pull(); return (h<<8)|s.pull()
    def setnz8(s,v): s.cc=(s.cc&~0x0C)|(0x08 if v&0x80 else 0)|(0x04 if not v&0xFF else 0)
    def setnz16(s,v): s.cc=(s.cc&~0x0C)|(0x08 if v&0x8000 else 0)|(0x04 if not v&0xFFFF else 0)
    def flag(s,bit,c):
        if c: s.cc|=bit
        else: s.cc&=~bit
    def C(s): return s.cc&1
    def nxv(s): s.flag(0x02,((s.cc>>3)^s.cc)&1)
    def interrupt(s, vec):
        if s.wai!='wai':
            s.push16(s.pc); s.push16(s.x); s.push(s.a); s.push(s.b); s.push(s.cc)
        s.cc|=0x10; s.pc=s.r16(vec); s.wai=False; s.cycles+=12
    def add8(s,a,b,c=0):
        r=a+b+c; s.flag(0x20,(a^b^r)&0x10); s.flag(0x02,(~(a^b))&(a^r)&0x80); s.flag(0x01,r>0xFF); r&=0xFF; s.setnz8(r); return r
    def sub8(s,a,b,c=0):
        r=a-b-c; s.flag(0x02,(a^b)&(a^r)&0x80); s.flag(0x01,r<0); r&=0xFF; s.setnz8(r); return r
    def step(s):
        if s.wai: s.cycles+=4; return
        pc=s.pc; rd=s.rd; o=rd(pc)
        if o not in T: raise RuntimeError(f'illegal opcode {o:02X} at {pc:04X}')
        m,mode,cyc=T[o]; s.cycles+=cyc
        s.pc=(pc+SIZE[mode])&0xFFFF
        b1=rd((pc+1)&0xFFFF); ea=None
        if mode=='dir': ea=b1
        elif mode=='ext': ea=(b1<<8)|rd((pc+2)&0xFFFF)
        elif mode=='idx': ea=(s.x+b1)&0xFFFF
        elif mode in ('imm8','imm16'): ea=(pc+1)&0xFFFF
        elif mode=='imdir': ea=rd((pc+2)&0xFFFF)
        elif mode=='imidx': ea=(s.x+rd((pc+2)&0xFFFF))&0xFFFF
        if mode=='rel':
            d=b1-256 if b1&0x80 else b1; cc=s.cc; N=(cc>>3)&1; Z=(cc>>2)&1; V=(cc>>1)&1; C=cc&1
            t={'BRA':1,'BRN':0,'BHI':not(C or Z),'BLS':C or Z,'BCC':not C,'BCS':C,'BNE':not Z,'BEQ':Z,'BVC':not V,'BVS':V,
               'BPL':not N,'BMI':N,'BGE':N==V,'BLT':N!=V,'BGT':(not Z) and N==V,'BLE':Z or N!=V,'BSR':1}[m]
            if m=='BSR': s.push16(s.pc)
            if t: s.pc=(s.pc+d)&0xFFFF
            return
        if m in ('AIM','OIM','EIM','TIM'):
            v=rd(ea); r={'AIM':b1&v,'OIM':b1|v,'EIM':b1^v,'TIM':b1&v}[m]
            s.setnz8(r); s.flag(0x02,0)
            if m!='TIM': s.wr(ea,r)
            return
        if m[:3] in ALU8 and len(m)==4 and m[3] in 'AB':
            base=m[:3]; reg=m[3]; av=s.a if reg=='A' else s.b
            if base=='STA': s.wr(ea,av); s.setnz8(av); s.flag(0x02,0); return
            v=rd(ea)
            if base=='SUB': r=s.sub8(av,v)
            elif base=='CMP': s.sub8(av,v); return
            elif base=='SBC': r=s.sub8(av,v,s.C())
            elif base=='BIT': s.setnz8(av&v); s.flag(0x02,0); return
            elif base=='ADC': r=s.add8(av,v,s.C())
            elif base=='ADD': r=s.add8(av,v)
            else:
                r={'AND':av&v,'LDA':v,'EOR':av^v,'ORA':av|v}[base]; s.setnz8(r); s.flag(0x02,0)
            if reg=='A': s.a=r
            else: s.b=r
            return
        D=(s.a<<8)|s.b
        if m in ('SUBD','ADDD','CPX','LDD','LDS','LDX'):
            v=s.r16(ea)
            if m in ('SUBD','CPX'):
                a=D if m=='SUBD' else s.x; r=a-v
                s.flag(0x02,(a^v)&(a^r)&0x8000); s.flag(0x01,r<0); r&=0xFFFF; s.setnz16(r)
                if m=='SUBD': s.a=r>>8; s.b=r&0xFF
            elif m=='ADDD':
                r=D+v; s.flag(0x02,(~(D^v))&(D^r)&0x8000); s.flag(0x01,r>0xFFFF); r&=0xFFFF; s.setnz16(r); s.a=r>>8; s.b=r&0xFF
            else:
                s.setnz16(v); s.flag(0x02,0)
                if m=='LDD': s.a=v>>8; s.b=v&0xFF
                elif m=='LDS': s.sp=v
                else: s.x=v
            return
        if m in ('STD','STS','STX'):
            v={'STD':D,'STS':s.sp,'STX':s.x}[m]; s.w16(ea,v); s.setnz16(v); s.flag(0x02,0); return
        if m=='JSR': s.push16(s.pc); s.pc=ea; return
        if m=='JMP': s.pc=ea; return
        u=m; tgt=None
        if mode=='inh' and m[-1] in 'AB' and m[:-1] in UNARY: u=m[:-1]; tgt=m[-1]
        if u in UNARY:
            v=(s.a if tgt=='A' else s.b) if tgt else rd(ea)
            c=s.C()
            if u=='NEG': r=s.sub8(0,v)
            elif u=='COM': r=(~v)&0xFF; s.setnz8(r); s.flag(0x02,0); s.flag(0x01,1)
            elif u=='LSR': r=v>>1; s.flag(0x01,v&1); s.setnz8(r); s.nxv()
            elif u=='ROR': r=(v>>1)|(c<<7); s.flag(0x01,v&1); s.setnz8(r); s.nxv()
            elif u=='ASR': r=(v>>1)|(v&0x80); s.flag(0x01,v&1); s.setnz8(r); s.nxv()
            elif u=='ASL': r=(v<<1)&0xFF; s.flag(0x01,v&0x80); s.setnz8(r); s.nxv()
            elif u=='ROL': r=((v<<1)|c)&0xFF; s.flag(0x01,v&0x80); s.setnz8(r); s.nxv()
            elif u=='DEC': r=(v-1)&0xFF; s.flag(0x02,v==0x80); s.setnz8(r)
            elif u=='INC': r=(v+1)&0xFF; s.flag(0x02,v==0x7F); s.setnz8(r)
            elif u=='TST': s.setnz8(v); s.flag(0x02,0); s.flag(0x01,0); return
            else: r=0; s.cc=(s.cc&~0x0F)|0x04
            if tgt=='A': s.a=r
            elif tgt=='B': s.b=r
            else: s.wr(ea,r)
            return
        f=getattr(s,'i_'+m,None)
        if f is None: raise RuntimeError(f'unimplemented {m} at {pc:04X}')
        f(D)
    def setD(s,v): s.a=(v>>8)&0xFF; s.b=v&0xFF
    def i_NOP(s,D): pass
    def i_LSRD(s,D): s.flag(1,D&1); r=D>>1; s.setD(r); s.setnz16(r); s.flag(2,s.C())
    def i_ASLD(s,D): s.flag(1,D&0x8000); r=(D<<1)&0xFFFF; s.setD(r); s.setnz16(r); s.nxv()
    def i_TAP(s,D): s.cc=s.a|0xC0
    def i_TPA(s,D): s.a=s.cc|0xC0
    def i_INX(s,D): s.x=(s.x+1)&0xFFFF; s.flag(4,s.x==0)
    def i_DEX(s,D): s.x=(s.x-1)&0xFFFF; s.flag(4,s.x==0)
    def i_CLV(s,D): s.cc&=~2
    def i_SEV(s,D): s.cc|=2
    def i_CLC(s,D): s.cc&=~1
    def i_SEC(s,D): s.cc|=1
    def i_CLI(s,D): s.cc&=~0x10
    def i_SEI(s,D): s.cc|=0x10
    def i_SBA(s,D): s.a=s.sub8(s.a,s.b)
    def i_CBA(s,D): s.sub8(s.a,s.b)
    def i_TAB(s,D): s.b=s.a; s.setnz8(s.b); s.flag(2,0)
    def i_TBA(s,D): s.a=s.b; s.setnz8(s.a); s.flag(2,0)
    def i_XGDX(s,D): s.setD(s.x); s.x=D
    def i_DAA(s,D):
        a=s.a; cf=0; lo=a&15; hi=a>>4; c=s.C(); h=(s.cc>>5)&1
        if h or lo>9: cf|=6
        if c or hi>9 or (hi>8 and lo>9): cf|=0x60
        r=a+cf; s.a=r&0xFF; s.setnz8(s.a); s.flag(1,c or r>0xFF)
    def i_SLP(s,D): s.wai=True
    def i_ABA(s,D): s.a=s.add8(s.a,s.b)
    def i_TSX(s,D): s.x=(s.sp+1)&0xFFFF
    def i_TXS(s,D): s.sp=(s.x-1)&0xFFFF
    def i_INS(s,D): s.sp=(s.sp+1)&0xFFFF
    def i_DES(s,D): s.sp=(s.sp-1)&0xFFFF
    def i_PULA(s,D): s.a=s.pull()
    def i_PULB(s,D): s.b=s.pull()
    def i_PSHA(s,D): s.push(s.a)
    def i_PSHB(s,D): s.push(s.b)
    def i_PULX(s,D): s.x=s.pull16()
    def i_PSHX(s,D): s.push16(s.x)
    def i_RTS(s,D): s.pc=s.pull16()
    def i_ABX(s,D): s.x=(s.x+s.b)&0xFFFF
    def i_RTI(s,D): s.cc=s.pull()|0xC0; s.b=s.pull(); s.a=s.pull(); s.x=s.pull16(); s.pc=s.pull16()
    def i_MUL(s,D): r=s.a*s.b; s.setD(r); s.flag(1,r&0x80)
    def i_WAI(s,D): s.push16(s.pc); s.push16(s.x); s.push(s.a); s.push(s.b); s.push(s.cc); s.wai='wai'
    def i_SWI(s,D): s.interrupt(0xFFFA)
# HD63701 undocumented $12/$13 (MAME asx1/asx2): X += [S+1]
op(0x12,'ASX1','inh',4); op(0x13,'ASX2','inh',4)
def _asx(s,D): s.x=(s.x+s.rd((s.sp+1)&0xFFFF))&0xFFFF
CPU.i_ASX1=_asx; CPU.i_ASX2=_asx
