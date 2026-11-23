"""Pac-Land sound MCU system: HD63701 at E=1.536 MHz, CUS30 shared RAM
(MCU 1000-13FF = main CPU 6800-6BFF), output-compare timer, vblank IRQ."""
import m6801, zipfile, os
ZIP=os.environ.get('PACLAND_ZIP') or [os.path.join(r,f) for r,_,fs in os.walk(os.path.expanduser('~/mnt')) for f in fs if f=='pacland.zip'][0]
E_HZ=1536000; FPS=60.606060; CYC_FRAME=int(E_HZ/FPS)
class MCU:
    def __init__(s):
        z=zipfile.ZipFile(ZIP); s.mem=bytearray(0x10000)
        e=z.read('pl1_7.3e'); s.mem[0x8000:0xA000]=e; s.mem[0xA000:0xC000]=e
        s.mem[0xF000:]=z.read('cus60-60a1.mcu')
        s.regs=bytearray(32); s.counter=0; s.ocr=0xFFFF; s.irq_mask=False; s.irq_line=False
        s.writes=[]   # (frame,cycle,addr,val) to shared RAM
        s.frame=0
        s.cpu=m6801.CPU(s.rd,s.wr); s.cpu.pc=s.cpu.r16(0xFFFE); s.cpu.cc=0xD0
        s.ocf_pending=False
    def rd(s,a):
        if a<0x20:
            if a==0x09: return (s.counter>>8)&0xFF
            if a==0x0A: return s.counter&0xFF
            if a==0x08:
                s.tcsr_read=True; return s.regs[8]
            if a in (2,3): return 0xFF
            return s.regs[a]
        if 0xD000<=a<0xD004: return 0xFF
        return s.mem[a]
    def wr(s,a,v):
        if a<0x20:
            if a==0x0B: s.regs[0x0B]=v; s.ocr=(v<<8)|(s.ocr&0xFF); s.clear_ocf(); return
            if a==0x0C: s.regs[0x0C]=v; s.ocr=(s.ocr&0xFF00)|v; s.clear_ocf(); return
            if a==0x09: s.counter=0xFFF8; return
            if a==0x08: s.regs[8]=(s.regs[8]&0xE0)|(v&0x1F); return
            s.regs[a]=v; return
        if 0x1000<=a<0x1400: s.mem[a]=v; s.writes.append((s.frame,s.cpu.cycles,a,v)); return
        if 0x2000<=a<0x4000: return
        if 0x4000<=a<0x8000:
            s.irq_mask = not (a-0x4000)&0x2000
            if not s.irq_mask: s.irq_line=False
            return
        if 0x8000<=a<0xC000 or a>=0xF000: return
        s.mem[a]=v
    def clear_ocf(s):
        if getattr(s,'tcsr_read',False): s.regs[8]&=~0x40; s.tcsr_read=False
    def run_frame(s):
        if s.irq_mask: s.irq_line=True
        end=s.cpu.cycles+CYC_FRAME; cpu=s.cpu
        while cpu.cycles<end:
            c0=cpu.cycles
            if not cpu.cc&0x10:
                if s.regs[8]&0x40 and s.regs[8]&0x08: cpu.interrupt(0xFFF4)
                elif s.irq_line: cpu.interrupt(0xFFF8)
            cpu.step()
            d=cpu.cycles-c0; old=s.counter; s.counter=(s.counter+d)&0xFFFF
            if (s.ocr-old-1)&0xFFFF < d: s.regs[8]|=0x40
            if old+d>0xFFFF: s.regs[8]|=0x20
        s.frame+=1
    def cus30(s): return bytes(s.mem[0x1100:0x1140])
