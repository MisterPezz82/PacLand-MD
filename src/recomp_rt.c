/* Runtime + object-system driver for the recompiled Pac-Land routines. */
#define RC_GLOBAL_REGS
#include "recomp_rt.h"
#ifndef RC_HOST
#include "build_config.h"
#endif
#ifdef PL_PROFILE
u16 g_type_profile[50];
#endif
#ifdef RC_HOST
#include <stdio.h>
#include <stdlib.h>
#endif

u8 R6[0x4000] __attribute__((aligned(4)));   /* word access via R6W/PW16 */
#ifdef RC_PROFILE
u8 rc_prof[0x80000];
#endif
u8 M6[0x400] __attribute__((aligned(4)));   /* music.c reads the sound requests as words */
const u8 *ROM6;
u8 rc_bank;
u8 gfN, gfZ, gfV, gfC, gfH, fI = 1, fF = 1, fE;
#ifdef RC_HOST
u8 rA, rB;
u16 rX, rY, rU, rS = 0x3700;
#endif
u16 rc_scroll[2];
u32 rc_io_count;
u16 rc_last_trap;
u8 rc_arcade_video = 1;  /* 0 in the cartridge: native renderer draws scenery */

u8 rd8(u16 a) {
    if (a < 0x3800) return R6[a];
    if (a >= 0x8000) return ROM6[a];
    if (a >= 0x4000 && a < 0x6000) return ROM6[0x10000 + (u32)rc_bank * 0x2000 + (a - 0x4000)];
    if (a >= 0x6800 && a < 0x6C00) return M6[a - 0x6800];
    return 0;
}

void wr8(u16 a, u8 v) {
    if (a < 0x3800) { R6[a] = v; return; }
    if (a >= 0x6800 && a < 0x6C00) { M6[a - 0x6800] = v; return; }
    if (a >= 0x3C00 && a < 0x3E00) rc_bank = v & 7;
    else if (a >= 0x3800 && a < 0x3C00) rc_scroll[(a >> 9) & 1] = (u16)((a & 1) << 8 | v);
    rc_io_count++;
}

void rc_unknown(u16 a) { rc_last_trap = a;
#ifdef RC_HOST
    fprintf(stderr, "rc_unknown %04X\n", a); exit(3);
#endif
}
void rc_trap(u16 a) { rc_last_trap = a;
#ifdef RC_HOST
    fprintf(stderr, "rc_trap %04X\n", a); exit(4);
#endif
}
void rc_dp(u8 v) { if (v != 0x20) rc_trap(0xD000 | v); }
void rc_ret_check(u16 a) { (void)a; }


/* C031: position -> terrain block lookup. D / 90h by 7-step restoring
 * division (quotient bits into B, remainder in A, workspace <$13/<$14), then
 * U = column table entry + remainder/4 and A = block attribute. Hand-written
 * for speed; results are identical to the generated version (recomp_diff). */
#ifndef RC_HOST
#define R6 rR6
#endif
extern const u8 rc_div9[1152];
void f_C031(void) {
    u8 a = rA, b = rB, n; u16 d, u;
    R6[0x2015] = a; R6[0x2016] = b;
    if (a < 0x90) {
        /* For h<4800h, floor(h/144)=floor((h>>4)/9). The 1152-byte
         * quotient table avoids a multiply/shift on every terrain lookup. */
        u16 h = (u16)((a << 8 | b) >> 1), q = rc_div9[h>>4];
        b = (u8)((b & 1) << 7 | q); a = (u8)(h - q * 0x90);
    } else for (n = 7; n; n--) {
        u8 c = b >> 7, top = a >> 7;
        b = (u8)(b << 1); a = (u8)(a << 1 | c);
        if (top || a >= 0x90) { a = (u8)(a - 0x90); b++; }
    }
    R6[0x2013] = a; R6[0x2014] = b;
    d = (u16)((b & 0x7F) << 4);                       /* ANDB #$7F, LDA #$10, MUL */
    a = R6[(u16)(0x3001 + d) & 0x3FFF];
    R6[0x2014] = a;
    u = PW16(ROMB+(u16)(0xD4E8 + (s8)(u8)(a<<1)));
    u = (u16)(u + (s8)(u8)(R6[0x2013] >> 2));
    rU = u; rA = a; rB = (u8)d; rS += 2;
}

/* Task yield (DE46) from the bonus-event wait loop at F272: complete the wait. */
/* Object pointer for the hand-written routines: a2-based on the cartridge. */
#if defined(RC_HOST)
#define OBJ ((u8 *)R6 + (u16)rX)
#define RAMB ((u8 *)R6)
#else
#define OBJ ((u8 *)rR6 + (s16)rX)
#define RAMB ((u8 *)rR6)
#endif
#define W16(p) ((u16)((p)[0] << 8 | (p)[1]))
#define SW16(p, v) do { u16 v_ = (u16)(v); (p)[0] = (u8)(v_ >> 8); (p)[1] = (u8)v_; } while (0)
/* D191: Pac-Man's X divided by 90h (the C031 loop on 240C): 2013 = the
 * quotient bits, 2075 = column (low 7 bits), 2076/2077 = remainder * 2 with
 * the carry, 200F = 0 (loop counter). */
void f_D191(void) {
    u8 a = R6[0x240C], b = R6[0x240D], n, c;
    if (a < 0x90) {
        u16 h = (u16)((a << 8 | b) >> 1), q = (u16)(((u32)h * 7282u) >> 20);
        b = (u8)((b & 1) << 7 | q); a = (u8)(h - q * 0x90);
    } else for (n = 7; n; n--) {
        u8 cb = b >> 7, top = a >> 7;
        b = (u8)(b << 1); a = (u8)(a << 1 | cb);
        if (top || a >= 0x90) { a = (u8)(a - 0x90); b++; }
    }
    R6[0x200F] = 0; R6[0x2013] = b;
    c = b >> 7; b = (u8)(b << 1);
    R6[0x2076] = a >> 7; a = (u8)(a << 1 | c);
    R6[0x2077] = a;
    rA = R6[0x2013] & 0x7F; R6[0x2075] = rA; rB = b; rS += 2;
}

/* C0AD: contact test between the object at X and Pac-Man (C068 is the same
 * test with 225F = 0; C0AD sets 225F = 1, which shrinks the side window).
 * Returns D = 1: Pac-Man lands on the object (his Y is moved by the
 * overlap, 20E8 bit 0, object id in 20E9/20EA); D = 100h: side contact
 * (push-back distance in 2013, 20E5 bit 0, id in 20E6/20E7); D = 0: none.
 * Every scratch write of the original (2013, 2015-2017, 2023) is kept. */
static void __attribute__((noinline)) c0ad_near(u8 *o, u8 r13);
/* C068: the item contact test has no landing branch. */
void f_C068(void){
 u8 *o=OBJ,*ram=RAMB,b;u16 ox=PW16(o+12),px=PW16(ram+0x240C),d=ox>=px?ox-px:px-ox;
 ram[0x225F]=0;
 if(d>>8)goto none;
 ram[0x2013]=(u8)d;
 if((u8)(o[8]+ram[0x2408])<(u8)d)goto none;
 if(!((u16)(W16(o+9)-0xE0)&0x8000)||ram[0x2409])goto none;
 b=(u8)(o[10]-ram[0x240A]);ram[0x2015]=b;if(o[10]<ram[0x240A])b=(u8)-b;
 if(b&0x80)goto none;ram[0x2016]=b;
 if((u8)((u8)(o[7]+ram[0x2407])>>1)<b)goto none;
 b=(u8)(((u8)(o[8]+ram[0x2408])>>1)+1);
 if(b<(u8)d)goto none;b-=(u8)d;ram[0x2017]=b;
 if(!((u16)(ox-px)&0x8000))b=(u8)-b;
 ram[0x2013]=b;ram[0x20E6]=o[0];ram[0x20E7]=o[1];ram[0x20E5]|=1;
 rA=1;rB=0;gfZ=0;rS+=2;return;
none:rA=rB=0;gfZ=1;rS+=2;
}
void f_C0AD(void) {
    /* most calls are for objects more than FFh away: keep that path short */
    u8 *o = OBJ, *ram = RAMB;
    u16 ox = PW16(o + 12), px = PW16(ram + 0x240C);   /* both even addresses */
    u16 d = ox >= px ? (u16)(ox - px) : (u16)(px - ox);
    ram[0x225F] = 1;
    if (d >> 8) { rA = 0; rB = 0; rS += 2; return; }
    c0ad_near(o, (u8)d);
}
/* C0B4 is the full landing/contact entry used by each return-trip log.
 * C0AD differs only by selecting the narrower side-contact window. */
void f_C0B4(void) {
    u8 *o=OBJ,*ram=RAMB;u16 ox=PW16(o+12),px=PW16(ram+0x240C);
    u16 d=ox>=px?(u16)(ox-px):(u16)(px-ox);
    ram[0x225F]=0;
    if(d>>8){rA=0;rB=0;rS+=2;return;}
    c0ad_near(o,(u8)d);
}
static void __attribute__((noinline)) c0ad_near(u8 *o, u8 r13) {
    u8 *ram = RAMB, b, r16;
    u16 d;
    ram[0x2013] = r13;
    if ((u8)(o[8] + ram[0x2408]) < r13) goto none;
    if (!((u16)(W16(o + 9) - 0xE0) & 0x8000)) goto none;
    if (ram[0x2409]) goto none;
    b = (u8)(o[10] - ram[0x240A]); ram[0x2015] = b;
    if (o[10] < ram[0x240A]) b = (u8)-b;
    if (b & 0x80) goto none;
    ram[0x2016] = r16 = b;
    b = (u8)(o[7] + ram[0x2407] + ram[0x2015]); ram[0x2017] = b;
    if (b & 0x80) goto side;
    if (!((u8)(b - 0x0F) & 0x80)) goto side;
    if (o[8] < r13) goto side;
    if (!((u16)(W16(ram + 0x240F) - W16(o + 15)) & 0x8000)) goto side;
    ram[0x20E9] = o[0]; ram[0x20EA] = o[1];
    d = (u16)(W16(ram + 0x2409) + (u16)(s16)(s8)ram[0x2017]); SW16(ram + 0x2409, d);
    d = (u16)(W16(ram + 0x2429) + (u16)(s16)(s8)ram[0x2017]); SW16(ram + 0x2429, d);
    ram[0x20E8] |= 1;
    rA = 0; rB = 1; rS += 2; return;
side:
    b = (u8)((u8)(o[7] + ram[0x2407]) >> 1);
    if(ram[0x225F]){b >>= 1; ram[0x2023] = b; b >>= 1; b = (u8)(b + ram[0x2023]);}
    if (b < r16) goto none;
    b = (u8)(b - r16);
    (void)b;
    b = (u8)((u8)(o[8] + ram[0x2408]) >> 1);
    if(ram[0x225F]){b >>= 1; ram[0x2023] = b; b >>= 1; b = (u8)(b + ram[0x2023]);}
    b = (u8)(b + 1);
    if (b < r13) goto none;
    b = (u8)(b - r13);
    ram[0x2017] = b;
    if (!((u16)(W16(o + 12) - W16(ram + 0x240C)) & 0x8000)) b = (u8)-b;
    ram[0x2013] = b;
    ram[0x20E6] = o[0]; ram[0x20E7] = o[1];
    ram[0x20E5] |= 1;
    rA = 1; rB = 0; rS += 2; return;
none:
    rA = 0; rB = 0; rS += 2;
}

/* Hand-written object routines (NATIVE in tools/recomp6809.py): the same
 * memory writes and final A/B/X as the 6809 code; no caller reads their flags
 * (interprocedural flag analysis). Checked by tests/recomp_diff.py. */
void f_CB64(void);
/* C010 / C020: 8.8 fixed-point Y / X update: frac += velocity fraction,
 * position += sign-extended velocity integer + carry. */
#define FIXED_ADD(FR, VF, VI, POS) do { \
    u8 *o_ = OBJ; \
    u16 s_ = (u16)(o_[FR] + o_[VF]); \
    u8 b_ = (u8)(o_[VI] + (s_ >> 8)); \
    u16 d_ = (u16)(W16(o_ + POS) + (u16)(s16)(s8)b_); \
    o_[FR] = (u8)s_; SW16(o_ + POS, d_); \
    rA = (u8)(d_ >> 8); rB = (u8)d_; rS += 2; } while (0)
void f_C010(void) { FIXED_ADD(11, 16, 15, 9); }
void f_C020(void) { FIXED_ADD(14, 18, 17, 12); }
/* Frightened-car update: preserve the arcade state handlers and scratch
 * results, but keep its common animation/three-slot attachment work native. */
void f_9C7E(void);void f_9CBC(void);void f_9CEA(void);void f_9D2F(void);void f_9D40(void);void f_9D72(void);void f_9D83(void);void f_9D91(void);void f_9DA2(void);
void f_9BF8(void){
 static void (*const states[10])(void)={f_9C7E,0,f_9CBC,f_9CEA,f_9D2F,f_9D40,f_9D72,f_9D83,f_9D91,f_9DA2};
 u8 *o=OBJ,b;u16 v;
 M6[0x293]=1;rA=(u8)(o[3]<<1);rY=0x9C09;rS-=2;
 if(rA<20&&states[rA>>1])states[rA>>1]();else rc_call(W16(ROMB+rY+(s8)rA));
 o=OBJ;rB=b=R6[0x2002];
 if(!(b&7)){rA=(b&8)?0xB0:0xAC;o[4]=rA;}
 if(!(b&15)){
  rA=(u8)(o[1]-1);rY=0x9C55;rA=(u8)(ROMB[rY+(s8)rA]+5);
  if(!(b&16)){rB=o[19]&8;rA+=(rB?3:1);}
  o[36]=rA;
 }
 v=(u16)(W16(o+9)+7);SW16(o+41,v);v=W16(o+12);SW16(o+44,v);
 v=(u16)(W16(o+9)+19);SW16(o+73,v);
 rB=(o[6]&1)?0xFC:4;v=(u16)(W16(o+12)+(s16)(s8)rB);SW16(o+76,v);
 rA=v>>8;rB=(u8)v;rS+=2;
}
void f_9CEA(void){
 u8 *o=OBJ;u16 v=(u16)((u16)(W16(o+9)-W16(RAMB+0x2409))<<1),d;
 SW16(RAMB+0x2013,v);d=(u16)(W16(o+12)-PW16(RAMB+0x240C));if(d&0x8000)d=(u16)-d;
 d=(u16)(d-v);rA=d>>8;rB=(u8)d;
 if(d&0x8000){rA=o[19];rA=(rA&12)?4:8;o[3]=rA;}
 rS-=2;FIXED_ADD(11,16,15,9);
 rS-=2;FIXED_ADD(14,18,17,12);
 rA=RAMB[0x2222];if(rA){rS+=2;return;}
 rS-=2;f_C0AD();
 if(rA){rA=1;RAMB[0x2063]=1;RAMB[0x2212]=0;}
 rS+=2;
}
/* Six persistent fruit slots otherwise pay checked RAM accesses and two
 * ROM-table dispatches per tick, including while hidden or far away. Keep
 * the common fruit states direct; special item states retain their handlers. */
void f_A198(void);void f_A207(void);void f_A260(void);
void f_A1DA(void){
 u8 *o=OBJ,kind=o[19],state=o[3];u16 v;
#if defined(RC_ORACLE) || defined(PL_BLAST)
 /* Oracle and prototype bypass the older hand-written fruit shortcut, retaining
  * the original state-table tail (notably A232's fall-through into A243). */
 rA=(u8)(kind<<1);rU=0xA1E3;rc_call(W16(ROMB+rU+(s8)rA));return;
#endif
 rA=(u8)(kind<<1);rU=0xA1E3;
 if(kind>1||state>2){rc_call(W16(ROMB+rU+(s8)rA));return;}
 rU=kind?0xA26B:0xA22A;
 if(!kind){
  if(state<2){PW16_SET(o+4,R6W(0x222C));if(!state){PW16_SET(o+30,512);o[3]=2;}else o[3]++;}
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);rA=v>>8;rB=(u8)v;
  if(!v){rA=0x11;o[19]=0x11;o[3]=0;}
 }else if(state<2){
  if(!state){o[4]=R6[0x222C];o[5]=0x2F;PW16_SET(o+30,512);o[3]++;}
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);rA=v>>8;rB=(u8)v;
  if(!v){rS-=2;f_A198();}
  else{
   v=(u16)(R6W(0x240C)-PW16(o+12));if(v&0x8000)v=(u16)-v;v-=64;rA=v>>8;rB=(u8)v;
   if(v&0x8000){PW16_SET(o+30,256);rA=2;rB=0;o[3]=2;}
  }
 }else{
  PW16_SET(o+4,R6W(0x222C));o[3]++;
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);rA=v>>8;rB=(u8)v;
  if(!v){rA=0x11;o[19]=0x11;o[3]=0;}
 }
 rB=o[5];
 if(rB==0x2D){rS-=2;f_C068();if(rA){M6[0x29D]=1;rA=0x10;OBJ[19]=0x10;OBJ[3]=0;}}
 rS+=2;
}
/* The twelve-log bridge: keep the arcade state machine and scratch writes,
 * but avoid repeating translated dispatch/stack arithmetic for every log. */
void f_B12A(void);void f_B13E(void);void f_B194(void);void f_B1C8(void);
void f_B192(void){OBJ[3]++;f_B194();}
void f_B194(void){
 u8 *o=OBJ;
 rA=R6[0x2074];
 if(rA==0x19){rA=o[30];if(!rA&&! --o[31]){rA=2;o[3]=2;}}
 rS-=2;f_C010();rS-=2;f_C0B4();
 if(rB){rB=R6[0x2002];if(!(rB&31)){rA=2;OBJ[3]=2;}}
 rS+=2;
}
void f_B1BC(void){u8 *o=OBJ;SW16(o+15,0xFFC0);o[31]=0;o[3]++;f_B1C8();}
void f_B1C8(void){
 u8 *o;u16 v;
 rS-=2;f_C010();o=OBJ;rA=o[9];if(rA&0x80){rA=4;o[3]=4;}
 rA=++o[31];
 if(!(rA&3)){rA=(rA&28)>>1;rY=0xB1EE;v=W16(ROMB+rY+rA);rA=v>>8;rB=(u8)v;o[4]=rA;o[6]=rB;}
 rS-=2;f_C0B4();rS+=2;
}
void f_B1FE(void){rS+=2;}
void f_B17F(void){
 rA=(u8)(OBJ[3]<<1);rU=0xB188;rS-=2;
 switch(rA){case 0:f_B192();break;case 2:f_B194();break;case 4:f_B1BC();break;case 6:f_B1C8();break;case 8:f_B1FE();break;default:rc_call(W16(ROMB+rU+(s8)rA));}
 rS+=2;
}
void f_B102(void){
 u8 *o=OBJ;u16 n=(u16)(W16(o+19)+1),count;SW16(o+19,n);rA=n>>8;rB=(u8)n;
 if(!(rB&127)){rU=rX;rY=0xB157;rB=12;R6[0x200F]=12;rA=o[19];rS-=2;if(rA&1)f_B13E();else f_B12A();}
 for(count=12;count;count--){
  o=OBJ;
  if(o[3]<=1){
   u16 frac,oy,px,ox,d;u8 vi;
   rU=0xB188;if(!o[3])o[3]++;
   if(R6[0x2074]==0x19&&!o[30]&&! --o[31])o[3]=2;
   frac=(u16)(o[11]+o[16]);vi=(u8)(o[15]+(frac>>8));
   oy=(u16)(W16(o+9)+(u16)(s16)(s8)vi);o[11]=(u8)frac;SW16(o+9,oy);
   px=PW16(R6+0x240C);ox=PW16(o+12);d=ox>=px?(u16)(ox-px):(u16)(px-ox);
   R6[0x225F]=0;rA=0;
   if(!(d>>8)){
    R6[0x2013]=(u8)d;
    if((u8)(o[8]+R6[0x2408])>=(u8)d){
     rS-=2;c0ad_near(o,(u8)d);
     if(rB&&!(R6[0x2002]&31)){rA=2;OBJ[3]=2;}
    }
   }
  }else {rB=(u8)count;rS-=3;f_B17F();rS++;}
  rX+=32;
 }
 rB=0;rS+=2;
}
/* Paired three-log platforms: copy their shared Y/velocity directly and
 * retain every arcade contact and phase write. */
void f_AEDA(void){
 u16 orig=rX,frac,y,v,i;u8 *o=OBJ,vi;
 frac=(u16)(o[11]+o[16]);vi=(u8)(o[15]+(frac>>8));
 y=(u16)(W16(o+9)+(u16)(s16)(s8)vi);o[11]=(u8)frac;SW16(o+9,y);
 if((y&0x8000)||(u8)y<o[22])v=R6W(0x34E8);
 else if((u8)y>=o[21])v=(u16)-R6W(0x34E8);
 else goto position;
 SW16(o+15,v);SW16(o+47,v);SW16(o+79,v);
position:
 SW16(o+41,y);SW16(o+73,y);
 for(i=0;i<3;i++,rX+=32){
  if((u16)(y-8)&0x8000)continue;
  rS-=2;f_C0B4();
  if(rA){v=W16(R6+0x240F);if(!(v&0x8000))SW16(R6+0x240F,(u16)-v);}
 }
 rX=orig;o=OBJ;v=(u16)(W16(o+19)+W16(o+17));SW16(o+19,v);
 rA=(u8)(0x56-((v>>8)&6));rB=(u8)v;o[4]=o[36]=o[68]=rA;rS+=2;
}
void f_AECE(void){u16 orig=rX;rS-=2;f_AEDA();rX+=96;rS-=2;f_AEDA();rX=orig;rS+=2;}
/* 9142: object retirement. Keeps the object while it is near Pac-Man
 * (|x - 20h - player x| < 120h, mirrored when facing left) or its state is
 * 11h, and while its Y >= 18h; a fallen object scores 200 (CB64) when the
 * 2074 condition allows; then clears the active flag and the X words of
 * its three slots. */
void f_9142(void) {
    u8 *o = OBJ, *ram = RAMB; u16 d;
    if (o[3] != 0x11) {
        if ((o[19] & 0xF0) == 0x60) {
            if (ram[0x2074] >= 0x0B) goto retire;
        } else {
            d = ram[0x208E] ? 0x0020 : 0xFFE0;
            d = (u16)(d + PW16(o + 12) - PW16(ram + 0x240C));
            if (d & 0x8000) d = (u16)-d;
            if (d >= 0x120) goto retire;
        }
    }
    d = (u16)(W16(o + 9) - 0x18);
    if (!(d & 0x8000)) { rA = (u8)(d >> 8); rB = (u8)d; rS += 2; return; }
    SW16(ram + 0x2070, 0); SW16(ram + 0x2072, 0); SW16(ram + 0x2074, 0x0200);
    rS -= 2; PUSHRET(rS, 0x9191); f_CB64();
    o = OBJ;
retire:
    o[2] = 0; PW16_SET(o + 12, 0); PW16_SET(o + 44, 0); PW16_SET(o + 76, 0);
    rA = 0; rB = 0; rS += 2;
}

u32 rc_yield_count;

/* Player animation and colours (8E40/8EA2). Final D is the cap descriptor;
 * keep World gameplay RAM intact; renderer selects regional cap colours. */
static void player_colour_tail(u8 *o,u16 limit,u16 speed){
 u16 cap;
 if(R6[0x2222] && (R6W(0x222A)>=64 || !(R6[0x222B]&8)))o[5]=0x1F;
 else o[5]=(W16(RAMB+0x20E1)?1:0)+(R6[0x20FD]?2:0);
 SW16(RAMB+0x2013,limit);
 if(speed>=limit){if(o[17]&0x80)o[6]&=0xFE;else o[6]|=1;}
 cap=R6[0x2115]?0xF83E:0x3C00;PW16_SET(o+36,cap);SETD(cap);rS+=2;
}
void f_8E40(void){
 u8 *o=OBJ;u16 speed=W16(o+17),limit=(u16)(W16(RAMB+0x20EF)-16);
 if(speed&0x8000)speed=(u16)-speed;player_colour_tail(o,limit,speed);
}
void f_8EA2(void){
 u8 *o=OBJ;u16 vx=W16(o+17),speed=vx,vy=W16(o+15),limit=(u16)(W16(RAMB+0x20EF)-16);u8 code;
 if(speed&0x8000)speed=(u16)-speed;
 if(speed<limit){
  if(!((u16)(vy-0x0100)&0x8000))code=0x48;
  else if(!((u16)(vy-0xFF00)&0x8000))code=0x4C;else code=0x50;
 }else{
  if(!((u16)(vy-0x0300)&0x8000))code=0x54;
  else if(!((u16)(vy-0x0100)&0x8000))code=0x58;
  else if(!((u16)(vy-0xFF00)&0x8000))code=0x5C;
  else{u8 held=R6[(vx&0x8000)?0x2094:0x2096];
   code=!((u16)(vy-0xFD00)&0x8000)?(held?0x60:0x68):(held?0x64:0x6C);}
 }
 o[4]=code;player_colour_tail(o,limit,speed);
}
/* Countdown shared by the three log/object variants. Both fields are even. */
void f_A198(void);
void f_A3F2(void){
 u8 *o=OBJ;u16 v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);SETD(v);
 if(!v){rS-=2;f_A198();rS+=2;return;}
 rA=R6[0x20E8];
 if(rA&2){rA=R6[0x20EA];if(rA==o[20]||rA==o[21]){o[3]=2;SETD(0x0100);PW16_SET(o+30,0x0100);}}
 rS+=2;
}
/* Camera segment scaling, preserving the original scratch words and its
 * zero-integer-component truncation, including unusual input values. */
static u16 camera_component(u16 u,u16 offset){
 u16 remainder=W16(RAMB+0x2076),partial=(u16)(((u32)remainder*ROMB[(u16)(u+1)])>>8),value;
 u8 whole=ROMB[u];SW16(RAMB+0x2003,partial);
 if(whole){RAMB[0x200F]=0;value=(u16)(partial+(u32)whole*remainder);}
 else value=(u8)partial;
 return (u16)(value+offset);
}
void f_D1BD(void){
 u8 *p;u16 x,y;rX=(u16)(0x3000+((u16)R6[0x2075]<<4));p=OBJ;
 R6[0x2074]=p[1];x=PW16(p+4);y=PW16(p+6);SW16(RAMB+0x2078,x);SW16(RAMB+0x207A,y);
 rU=(u16)(0xD1DE +(s8)(u8)(p[12]<<2));
 x=camera_component(rU,x);SW16(RAMB+0x2078,x);
 y=camera_component((u16)(rU+2),y);SW16(RAMB+0x207A,y);SETD(y);rS+=2;
}
/* Native camera-column bookkeeping; arcade video helpers remain available
 * for the host reference. The cartridge's native scenery path skips them. */
void f_D2B3(void);void f_D3A2(void);
void f_D266(void){
 u16 i,now,previous,d;
 for(i=0;i<4;i++){
  rX=(u16)(0x207C+i*4);now=R6W(rX);previous=R6W(rX+2);d=(u16)(now-previous);SETD(d);
  if((i&1)?now>previous:now<previous){
   if(rc_arcade_video){rS-=2;if(i<2)f_D2B3();else f_D3A2();}
  }else{R6W_SET(rX+2,now);SETD(now);}
 }
 rS+=2;
}

#ifndef RC_HOST
#undef R6
#endif

/* Cut-scene tasks. The original runs its sequences (round end F1DC, trip
 * intro BEF2, walk-in F0F1) as cooperative tasks that call DE46 once per
 * frame. On the cartridge such a sequence runs on its own 68000 stack and its
 * own 6809 stack (3600 down, the arcade's game-task stack area): DE46 swaps
 * back to the caller, and rc_task_resume() continues it next frame. Outside a
 * task (and on the host, where a task runs to completion at once) DE46 only
 * completes the 2219 wait as before. On the host the sequence then ends
 * early; the cartridge runs it frame by frame. */
static u8 in_task, task_done;
#if defined(RC_HOST)
void rc_task_start(void (*f)(void)) {
    rS = 0x3600; rS -= 2; WR16(rS, 0xFFFF); in_task = 1; f(); in_task = 0; task_done = 1;
}
u8 rc_task_resume(void) { return 0; }
void f_DE46(void) { R6[0x2219] = 0; rc_yield_count++; rS += 2; }
#else
static u32 task_stack[640];                  /* 2.5 KB: recompiled calls nest deeply */
static u32 main_sp, task_sp;
static void (*task_fn)(void);
void rc_swap(u32 *save, u32 sp);
__asm__(".globl rc_swap\nrc_swap:\n"
        "\tmove.l 4(%sp),%a0\n\tmove.l 8(%sp),%a1\n"
        "\tmovem.l %d2-%d7/%a2-%a6,-(%sp)\n\tmove.l %sp,(%a0)\n"
        "\tmove.l %a1,%sp\n\tmovem.l (%sp)+,%d2-%d7/%a2-%a6\n\trts\n");
static void task_entry(void) {
    rR6 = R6; rS = 0x3600; rS -= 2; WR16(rS, 0xFFFF);
    task_fn();
    task_done = 1; in_task = 0;
    for (;;) rc_swap(&task_sp, main_sp);
}
void rc_task_start(void (*f)(void)) {
    u32 *sp = task_stack + sizeof task_stack / 4;
    u16 i;
    *--sp = (u32)task_entry;
    for (i = 0; i < 11; i++) *--sp = 0;     /* d2-d7/a2-a6 popped by rc_swap */
    task_sp = (u32)sp; task_fn = f; task_done = 0;
}
/* Run the task until its next DE46 or its end; 1 while it is still running. */
u8 rc_task_resume(void) {
    if (task_done || !task_fn) return 0;
    in_task = 1;
    rc_swap(&main_sp, task_sp);
    in_task = 0;
    return !task_done;
}
void f_DE46(void) {
    rc_yield_count++;
    /* in a task the frame really passes, so waits on 2219 (the round timer,
     * counted down into the time bonus by CF61/CE70) end by themselves */
    if (in_task) rc_swap(&task_sp, main_sp); else R6[0x2219] = 0;
    rS += 2;
}
#endif

void f_8010(void); void f_80DA(void); void f_8230(void);

/* Entry from ordinary C code. On the cartridge the 6809 registers occupy
 * callee-saved 68000 registers (d5-d7/a3-a5) that the caller may be using:
 * save them around every entry into recompiled code. */
#if defined(RC_HOST)
#define RC_SAVE
#define RC_RESTORE
#else
#define RC_SAVE u32 _saved[7]; __asm__ volatile ("movem.l %%d5-%%d7/%%a2-%%a5,%0" : "=m" (_saved) : : "memory"); rR6 = R6;
#define RC_RESTORE __asm__ volatile ("movem.l %0,%%d5-%%d7/%%a2-%%a5" : : "m" (_saved) : "memory");
#endif

static void rc_jsr(void (*f)(void)) {
    RC_SAVE
    rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF); f();
    RC_RESTORE
}

void f_EBC8(void); void f_CFE9(void); void f_CE49(void); void f_D0F0(void);

static u8 bcd(u16 v) { return (u8)(((v / 10) % 10) << 4 | (v % 10)); }

/* New game: player-1 state block as initialised at E8C8..E909 (lives from
 * the default DIP setting, round 0, BCD round/trip counters at 1, score 0). */
void rc_new_game(u8 lives) {
    u16 i;
    for (i = 0; i < 0x3800; i++) R6[i] = 0;
    for (i = 0; i < sizeof M6; i++) M6[i] = 0;
    R6[0x2100] = lives; R6[0x2101] = 0; R6[0x210F] = 1; R6[0x2110] = 1; R6[0x2116] = 1; R6[0x2111] = 1;
    /* E91C/E91E enables the original movable-scenery secrets before setup.
     * A645 otherwise returns without marking the eligible hydrant/cactus. */
    R6[0x2214] = 1;
    R6[0x20E3] = 1;
    R6[0x2233] = 1; R6[0x2055] = 0; R6[0x2056] = 8;   /* upright cabinet, as captured in MAME */
    M6[0x18C] = 1;   /* 698C: game in play (the MCU sets it when a credit starts a game; 0 in the
                        demo). Without it CB64 adds no score and 8625/BD2A/F0FC take demo paths. */
}

/* Round start, following E975..E994: EBC8, CFE9, 8010 (or 80DA after a lost
 * life, E9BC..E9D4), CE49, D0F0. */
void rc_setup(u16 round, u8 reinit) {
    R6[0x2101] = (u8)round;
    R6[0x210F] = bcd((u16)(round + 1)); R6[0x2110] = bcd((u16)(round / 4 + 1));
    rc_jsr(f_EBC8); rc_jsr(f_CFE9);
    rc_jsr(reinit ? f_80DA : f_8010);
    rc_jsr(f_CE49); rc_jsr(f_D0F0);
}

void f_EBF1(void); void f_D130(void); void f_C1FB(void); void f_CE62(void); void f_C1DC(void);

/* Frame interrupt bookkeeping from the IRQ handler at DE6A (frame counter
 * 2001-2002, 1/60 s counter 2058 with seconds in 2057). CE02 (MCU input
 * latching) is not needed: the native player reads the pad itself. */
void rc_irq(void) {
    u16 t = (u16)((R6[0x2001] << 8 | R6[0x2002]) + 1);
    R6[0x2000] = 1; R6[0x2001] = t >> 8; R6[0x2002] = (u8)t;
    { u8 a = 0, b = (u8)(R6[0x2058] + 1); if (b == 0x3C) { a = 1; b = 0; } R6[0x2057] = a; R6[0x2058] = b; }
    if (rc_arcade_video) rc_jsr(f_C1DC);
}

/* Native equivalent of the object loop at 8230..826A (with the six gate
 * routines 826B..82B1 inlined). It performs the same register assignments and
 * the same stack writes as the original, so arcade RAM stays byte-identical
 * (checked by tests/recomp_diff.py against the interpreted original). */
extern void (*const rc_update_fn[50])(void);
/* Gate kinds from the table at 82C0, decoded once. */
enum { G_ALWAYS = 1, G_STATE, G_WIN_WIDE, G_WIN_MID, G_WIN_NEAR, G_NEVER };
u8 gate_kind[50];
u8 gate_mode[50]; /* native scan: zero means passive; other values retain the gate kind */
u16 gate_lo[50], gate_width[50];             /* player-relative X windows: run when (u16)(d - lo) < width */
u32 gate_word[50];                           /* the same, packed for slot_pass */
static void gates_init(void) {
    u16 t;
    for (t = 0; t < 50; t++) {
        u16 g = (u16)(ROMB[0x82C0 + 2 * t] << 8 | ROMB[0x82C1 + 2 * t]);
        gate_kind[t] = g == 0x826B ? G_ALWAYS : g == 0x826D ? G_STATE : g == 0x8272 ? G_WIN_WIDE :
                       g == 0x8287 ? G_WIN_MID : g == 0x829C ? G_WIN_NEAR : g == 0x82B1 ? G_NEVER : 0;
        if (!gate_kind[t]) rc_trap(g);
        gate_mode[t]=gate_kind[t]==G_NEVER?0:gate_kind[t];
        {   s16 lo = gate_kind[t] == G_WIN_WIDE ? -0x1B0 : gate_kind[t] == G_WIN_MID ? -0xD8 : -0x20;
            s16 hi = gate_kind[t] == G_WIN_WIDE ? 0xA0 : gate_kind[t] == G_WIN_MID ? 0xD8 : 0x20;
            gate_lo[t] = (u16)lo; gate_width[t] = (u16)(hi - lo); }
        gate_word[t] = gate_kind[t] == G_NEVER ? 0 : gate_kind[t] == G_ALWAYS ? 1 : gate_kind[t] == G_STATE ? 2 :
                       (u32)gate_lo[t] << 16 | gate_width[t];
    }
}

/* Stack bytes below S (3600-36FF) are scratch in this model, so the PSHS/JSR
 * pushes of the original loop are not needed: no update handler reads below
 * its own frame (no S-relative loads; every PULS matches a PSHS), so S is
 * moved exactly as the original but the bytes are only written when
 * RC_STACK_IMAGE is defined. tests/recomp_diff.py (stack page masked) is
 * identical for all 32 rounds either way. */
/* Gate test and dispatch in one loop, written so that GCC keeps it in the
 * eight 68000 registers left over by the global 6809 registers (an earlier
 * version spilled; a version with the scan in src/slot_scan.c paid a call
 * per runnable slot). gate_word: window kinds = lo << 16 | width; 0 never,
 * 1 always, 2 when +02 is set. Pac-Man's X is re-read after every handler
 * because handlers can move him. */
static void slot_pass(u16 base, u8 count, u16 ret) {
    u8 *slot = R6 + base, *end = slot + ((u16)count << 5);
    u16 px = R6W(0x240C);
    rX = base; rB = count;
    if (!count) return;
    (void)ret;
    do {
        u32 g = gate_word[slot[1]];
        u16 w = (u16)g, d = 0;
        if (w > 2) {
            d = (u16)(PW16(slot + 0x0C) - px);
            if ((u16)(d - (u16)(g >> 16)) >= w) continue;
        } else if (w != 1 && !(w == 2 && slot[2])) continue;
        {
            u16 s = rS, x = (u16)(slot - R6);
            u8 b = (u8)((u16)(end - slot) >> 5);
#ifdef RC_STACK_IMAGE
            R6[s - 2] = (u8)(x >> 8); R6[s - 1] = (u8)x; R6[s - 3] = b;          /* PSHS B,X */
            R6[s - 5] = (u8)(ret >> 8); R6[s - 4] = (u8)ret;                     /* JSR [A,Y] */
            R6[s - 7] = (u8)(x >> 8); R6[s - 6] = (u8)x;                         /* 82B2 PSHS X */
            R6[s - 9] = 0x82; R6[s - 8] = 0xBD;                                  /* JSR [A,Y] */
#endif
            rS = (u16)(s - 9);
            rX = x; rA = (u8)(slot[1] << 1); rB = w > 2 ? (u8)d : b; rY = 0x8324;
#ifdef PL_PROFILE
            {u8 type=slot[1];u32 tm=getSubTick();rc_update_fn[type]();g_type_profile[type]+=(u16)(getSubTick()-tm);}
#else
            rc_update_fn[slot[1]]();                                             /* returns with S = s-7 */
#endif
            rS = s;
            px = R6W(0x240C);
        }
    } while ((slot += 0x20) != end);
    rX = (u16)(end - R6); rB = 0;
}
#if !defined(RC_HOST) && !defined(PL_PROFILE) && !defined(RC_STACK_IMAGE) && !defined(PL_C_SLOT_PASS)
void rc_slot_pass(u16 base,u16 count);
#define PASS(base,count,ret) rc_slot_pass(base,count)
#else
#define PASS(base,count,ret) slot_pass(base,count,ret)
#endif
void f_841A(void); void f_85D5(void);
static void objects_tick(void) {
    static u8 ready;
    RC_SAVE
#ifdef PL_BLAST
    extern volatile u16 g_blast_native,g_blast_fallback;
    extern void objects_native_invalidate(void);
#ifndef RC_HOST
    objects_native_invalidate();
#endif
    g_blast_native=g_blast_fallback=0;
#endif
#ifdef PL_PROFILE
    {u16 i;for(i=0;i<50;i++)g_type_profile[i]=0;}
#endif
    if (!ready) { gates_init(); ready = 1; }
    rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF);                          /* JSR 8230 */
    PASS(0x2400, R6[0x20DC], 0x8242);
    PASS(0x2800, R6[0x20DD], 0x825C);
    rS -= 2; PUSHRET(rS, 0x8267); f_841A();
    rS -= 2; PUSHRET(rS, 0x826A); f_85D5();
    RC_RESTORE
}

#ifdef PL_BLAST
#include "blast.h"
void blast_reference_actor(u8 *o,u16 b){
 RC_SAVE
 rS=0x36F5;rX=(u16)(o-R6);rA=(u8)(o[1]<<1);rB=(u8)b;rY=0x8324;
 rc_update_fn[o[1]]();RC_RESTORE
}
void blast_reference_finish(void){
 RC_SAVE
 rX=(u16)(0x2800+((u16)R6[0x20DD]<<5));rB=0;rS=0x36FC;f_841A();
 rS=0x36FC;f_85D5();RC_RESTORE
}
void blast_distance_score(void){
 RC_SAVE
 rS=0x36F0;rX=0x2400;f_CB64();RC_RESTORE
}
void blast_car_retire(u8 *o){
 RC_SAVE
 rS=0x36F0;rX=(u16)(o-R6);f_9142();RC_RESTORE
}
void blast_fail(u32 code){rc_trap((u16)code);}
void blast_entity_pass(void);
static void blast_planned_tick(void){
 RC_SAVE
 blast_plan_prepare();rS=0x36FE;g_blast_native=g_blast_fallback=0;
 blast_entity_pass();
 rX=(u16)(0x2800+((u16)R6[0x20DD]<<5));rB=0;rS=0x36FC;f_841A();
 rS=0x36FC;f_85D5();RC_RESTORE
}
#endif

/* One gameplay tick, in the order of the original main loop at EA31..EA3D. */
u8 rc_hide_player;
/* Player input, as the IRQ routine CE02 shifts it in from the MCU each frame:
 * 2093/2095/2097 = left/right/jump pressed this frame (fresh), 2094/2096/2098
 * = held. The player routine tests bit 0 (and consumes 2097 on a jump). */
void rc_input(u8 left, u8 right, u8 jump) {
    static u8 last;
    u8 now = (u8)((left ? 1 : 0) | (right ? 2 : 0) | (jump ? 4 : 0)), fresh = now & ~last;
    last = now;
    R6[0x2093] = (u8)(R6[0x2093] << 1 | (fresh & 1)); R6[0x2094] = (u8)(R6[0x2094] << 1 | (now & 1));
    R6[0x2095] = (u8)(R6[0x2095] << 1 | (fresh >> 1 & 1)); R6[0x2096] = (u8)(R6[0x2096] << 1 | (now >> 1 & 1));
    R6[0x2097] = (u8)(R6[0x2097] << 1 | (fresh >> 2 & 1)); R6[0x2098] = (u8)(R6[0x2098] << 1 | (now >> 2 & 1));
}

void f_8731(void); void f_C6B7(void); void f_EB9D(void);
/* Death sequence of the main loop (EAB4..EAD5): player state 8, then only
 * the player routine runs until it signals 2213; then C6B7 and EB9D (EB9D
 * stores the respawn checkpoint X at 210B used by the re-init path 80DA). */
void rc_death_start(void) {
#if defined(PL_BLAST) && !defined(RC_HOST)
 extern void objects_native_suspend(void);objects_native_suspend();
#endif
 R6[0x2063] = 0; R6[0x2403] = 8;
}
u8 rc_death_tick(void) {
    RC_SAVE
    rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF); rX = 0x2400; f_8731();
    rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF); f_D130();
    RC_RESTORE
    if (!R6[0x2213]) return 0;
    R6[0x2213] = 0; rc_jsr(f_C6B7); rc_jsr(f_EB9D);
    return 1;
}

/* Round end, as the main loop at EA59..EA75: EBF8, then the end-of-round
 * sequence F1DC (walk to the goal, time bonus; trip end and home scenes in
 * rounds 3 and 4 of a trip), then C6B7. Runs as a task (see f_DE46). */
void f_EBF8(void); void f_F1DC(void);
static void round_end_seq(void) {
    rS -= 2; f_EBF8(); rS -= 2; f_F1DC(); rS -= 2; f_C6B7();
}
void rc_round_end_start(void) {
#if defined(PL_BLAST) && !defined(RC_HOST)
    extern void objects_native_suspend(void);objects_native_suspend();
#endif
    R6[0x2062] = R6[0x2063] = 0;
    rc_task_start(round_end_seq);
}

/* Round start, as the main loop at E975..E99A: EBC8; at the first round of a
 * trip the trip intro (C6C3 prints TRIP n, BEF2 runs it for A0h frames);
 * CFE9, 8010, CE49, D0F0 (set-up); one frame; F0F1 (Pac-Man walks in).
 * Runs as a task. rc_round_phase: 1 while the intro runs, 2 once the
 * set-up is done (the cartridge then reloads its scenery and objects). */
u8 rc_round_phase;
void f_C6C3(void); void f_BEF2(void); void f_F0F1(void);
static void round_start_seq(void) {
    rc_round_phase = 0;
    rS -= 2; f_EBC8();
    if (!(R6[0x2101] & 3)) {
        rc_round_phase = 1;
        rS -= 2; f_C6C3();
        R6[0x220E] = 0; R6[0x220F] = 0xA0;
        rS -= 2; f_BEF2();
    }
    rS -= 2; f_CFE9(); rS -= 2; f_8010(); rS -= 2; f_CE49(); rS -= 2; f_D0F0();
    rc_round_phase = 2;
    rS -= 2; f_DE46();
    rS -= 2; f_F0F1();
    R6[0x2234] = 1; R6[0x220E] = R6[0x220F] = 0;
}
void rc_round_start(u16 round) {
    R6[0x2101] = (u8)round;
    R6[0x210F] = bcd((u16)(round + 1)); R6[0x2110] = bcd((u16)(round / 4 + 1));
    R6[0x2062] = R6[0x2063] = 0;
    rc_task_start(round_start_seq);
}

u8 rc_native_loop = 1;
void f_CDEA(void);
/* Arcade demo controller and update order, E80D..E84A. Its pseudo-random
 * taps advance Pac-Man toward the exit, jumping/tapping once in 32 rolls. */
void rc_demo_frame(void) {
    u8 tap;
    {RC_SAVE
     rS=0x3700;rS-=2;WR16(rS,0xFFFF);f_CDEA();tap=(rA&31)==0;
     RC_RESTORE}
    R6[0x2093]=R6[0x2094]=R6[0x2095]=R6[0x2096]=0;
    R6[0x2097]=tap;R6[0x2098]=0;
    if(R6[0x208E]){R6[0x2093]=tap;R6[0x2094]=1;}
    else{R6[0x2095]=tap;R6[0x2096]=1;}
#ifdef PL_BLAST
#ifdef RC_HOST
    blast_frame();
#else
    blast_planned_tick();
#endif
#else
    if(rc_native_loop)objects_tick();else rc_jsr(f_8230);
#endif
    rc_jsr(f_D130);
    if(rc_arcade_video)rc_jsr(f_C1FB);
}
void rc_frame(void) {
#ifdef PL_BLAST
    {
#ifdef RC_HOST
    blast_frame();
#else
    blast_planned_tick();
#endif
    }
#else
    if (rc_native_loop) objects_tick(); else rc_jsr(f_8230);
#endif
    rc_jsr(f_EBF1); rc_jsr(f_D130);
    /* On the cartridge the sprite list is built natively from the object
     * slots (src/objects.c); the recompiled C1FB is kept for the host tests. */
    if (rc_arcade_video) rc_jsr(f_C1FB);
    rc_jsr(f_CE62);
}

void rc_bench_call(u16 addr) { RC_SAVE rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF); rc_call(addr); RC_RESTORE }
