/* Runtime for the recompiled Pac-Land object routines (tools/recomp6809.py).
 * Arcade memory map (main CPU):
 *   0000-37FF RAM (R6), 3800/3A00 scroll, 3C00 bank select, 4000-5FFF banked ROM,
 *   6800-6BFF MCU shared RAM (M6), 8000-FFFF fixed ROM (ROM6).
 * Builds both for the Mega Drive (SGDK types) and for the host test (stdint). */
#ifndef PACLAND_RECOMP_RT_H
#define PACLAND_RECOMP_RT_H
#ifdef RC_HOST
#include <stdint.h>
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef int16_t s16; typedef uint32_t u32;
#else
#include <genesis.h>
#endif

extern u8 R6[0x4000] __attribute__((aligned(4))); /* definition has the same alignment */
extern u8 M6[0x400] __attribute__((aligned(4)));
extern const u8 *ROM6;          /* 128 KiB main CPU region image (fixed ROM at 8000, banks at 10000) */
extern u8 rc_bank;
/* gfN..gfH carry flags across calls for routines that return or read flags;
 * inside a routine the flags are locals fN..fH (tools/recomp6809.py). */
extern u8 gfN, gfZ, gfV, gfC, gfH, fI, fF, fE;
#if defined(RC_GLOBAL_REGS) && !defined(RC_HOST)
/* Cartridge: the 6809 registers live permanently in 68000 callee-saved
 * registers (GCC global register variables). Code that does not define
 * RC_GLOBAL_REGS preserves them through the normal calling convention. */
register u16 rX asm("a3");
register u16 rY asm("a4");
register u16 rU asm("a5");
register u16 rS asm("d5");
register u8 rA asm("d6");
register u8 rB asm("d7");
/* Arcade RAM base in a2 (set by RC_SAVE), so every RAM access in generated
 * code is one d16(a2) / d8(a2,Rn.w) operand instead of an absolute address. */
register u8 *rR6 asm("a2");
#ifdef RC_R6_REG
#define R6 rR6
#endif
#else
extern u8 rA, rB;
extern u16 rX, rY, rU, rS;
#endif
extern u16 rc_scroll[2];
extern u32 rc_io_count;

#define D ((u16)((rA << 8) | rB))
#define SETD(v) do { u16 _v = (v); rA = (u8)(_v >> 8); rB = (u8)_v; } while (0)
#define GETCC() ((u8)((fE << 7) | (fF << 6) | (fH << 5) | (fI << 4) | (fN << 3) | (fZ << 2) | (fV << 1) | fC))
#define SETCC(v) do { u8 _c = (v); fE = _c >> 7; fF = (_c >> 6) & 1; fH = (_c >> 5) & 1; fI = (_c >> 4) & 1; \
    fN = (_c >> 3) & 1; fZ = (_c >> 2) & 1; fV = (_c >> 1) & 1; fC = _c & 1; } while (0)

u8 rd8(u16 a);
void wr8(u16 a, u8 v);
#ifdef RC_HOST
#define ROMB ROM6
#else
extern const u8 arcade_main[0x20000];
#define ROMB arcade_main          /* direct array: no pointer load per access */
#endif
#define RC_INLINE static inline __attribute__((always_inline))
RC_INLINE u8 RD8(u16 a) { if (a < 0x3800) return R6[a]; if (a >= 0x8000) return ROMB[a]; return rd8(a); }
RC_INLINE void WR8(u16 a, u8 v) { if (a < 0x3800) R6[a] = v; else wr8(a, v); }
RC_INLINE u16 rd16(u16 a) { return (u16)((rd8(a) << 8) | rd8((u16)(a + 1))); }
RC_INLINE void wr16(u16 a, u16 v) { wr8(a, (u8)(v >> 8)); wr8((u16)(a + 1), (u8)v); }
RC_INLINE u16 RD16(u16 a) {
    if (a < 0x37FF) return (u16)((R6[a] << 8) | R6[a + 1]);
    if (a >= 0x8000 && a != 0xFFFF) return (u16)((ROMB[a] << 8) | ROMB[a + 1]);
    return rd16(a);
}
RC_INLINE void WR16(u16 a, u16 v) { if (a < 0x37FF) { R6[a] = (u8)(v >> 8); R6[a + 1] = (u8)v; } else wr16(a, v); }
/* Profile-specialised access sites (see tools/recomp6809.py tag_accesses). */
#define RD8R(a) (R6[(u16)(a) & 0x3FFF])
#define WR8R(a, v) (R6[(u16)(a) & 0x3FFF] = (u8)(v))
/* 16-bit access to arcade RAM at an even address known at compile time (or
 * an even runtime offset such as a slot base + 0Ch): one 68000 word access on
 * the cartridge (R6 is word aligned and both CPUs are big-endian), two byte
 * accesses on the little-endian host. */
#ifdef RC_HOST
#define R6W(c) ((u16)(R6[c] << 8 | R6[(c) + 1]))
#define R6W_SET(c, v) do { u16 _w = (u16)(v); R6[c] = (u8)(_w >> 8); R6[(c) + 1] = (u8)_w; } while (0)
#define PW16(p) ((u16)((p)[0] << 8 | (p)[1]))
#define PW16_SET(p,v) do {u16 _pv=(u16)(v);(p)[0]=(u8)(_pv>>8);(p)[1]=(u8)_pv;} while(0)
#else
typedef u16 __attribute__((may_alias)) rc_u16a;
#define R6W(c) (*(rc_u16a *)&R6[c])
#define R6W_SET(c, v) (*(rc_u16a *)&R6[c] = (u16)(v))
#define PW16(p) (*(const rc_u16a *)(p))
#define PW16_SET(p,v) (*(rc_u16a *)(p)=(u16)(v))
#endif
#define RD16R(a) ((u16)((R6[(u16)(a) & 0x3FFF] << 8) | R6[(u16)((a) + 1) & 0x3FFF]))
#define WR16R(a, v) do { u16 _a = (u16)(a), _v = (u16)(v); R6[_a & 0x3FFF] = (u8)(_v >> 8); R6[(u16)(_a + 1) & 0x3FFF] = (u8)_v; } while (0)
/* Register + constant offset (|offset| < 128) RAM-only accesses. The
 * cartridge uses one sign-extended word-index access; the profile guarantees
 * the address is inside arcade RAM (0000-37FF), so no mask is needed. */
#if defined(RC_HOST) || defined(RC_PROFILE)
#define RD8X(r, o) RD8R((u16)((r) + (o)))
#define WR8X(r, o, v) WR8R((u16)((r) + (o)), v)
#define RD16X(r, o) RD16R((u16)((r) + (o)))
#define WR16X(r, o, v) WR16R((u16)((r) + (o)), v)
#else
#define R6P(r, o) (R6 + (s16)(r) + (o))
#define RD8X(r, o) (*R6P(r, o))
#define WR8X(r, o, v) (*R6P(r, o) = (u8)(v))
#define RD16X(r, o) ((u16)(R6P(r, o)[0] << 8 | R6P(r, o)[1]))
#define WR16X(r, o, v) do { u16 _v = (u16)(v); u8 *_p = R6P(r, o); _p[0] = (u8)(_v >> 8); _p[1] = (u8)_v; } while (0)
#endif
#define RD8O(a) (ROMB[(u16)(a)])
#define RD16O(a) ((u16)((ROMB[(u16)(a)] << 8) | ROMB[(u16)((a) + 1)]))
#ifdef RC_PROFILE
extern u8 rc_prof[0x80000];
#define RC_PM(id, a) (rc_prof[id] |= (u16)(a) < 0x3800 ? 1 : (u16)(a) >= 0x8000 ? 2 : 4)
#define RD8S(id, a) (RC_PM(id, a), RD8(a))
#define WR8S(id, a, v) do { RC_PM(id, a); WR8(a, v); } while (0)
#define RD16S(id, a) (RC_PM(id, a), RC_PM(id, (u16)((a) + 1)), RD16(a))
#define WR16S(id, a, v) do { RC_PM(id, a); RC_PM(id, (u16)((a) + 1)); WR16(a, v); } while (0)
#endif
/* The 6809 system stack is always in work RAM (S starts at 3700). */
#ifdef RC_STACK_IMAGE
#define PUSHRET(s, v) do { R6[(u16)(s)] = (u8)((v) >> 8); R6[(u16)((s) + 1)] = (u8)(v); } while (0)
#else
#define PUSHRET(s, v) ((void)0)   /* no routine reads return addresses (recomp_diff, all rounds) */
#endif

void rc_call(u16 addr);          /* generated dispatch switch */
void rc_unknown(u16 addr);
void rc_trap(u16 addr);
void rc_dp(u8 v);
void rc_ret_check(u16 addr);

/* Object-system driver (src/recomp_rt.c): native equivalents of the loops at
 * 8010/80DA (round set-up) and 8230 (per frame), skipping type 00, the player,
 * which remains native. */
void rc_new_game(u8 lives);
void rc_bench_call(u16 addr);
void rc_setup(u16 round, u8 reinit);
void rc_frame(void);
void rc_demo_frame(void);
void rc_input(u8 left, u8 right, u8 jump);
void rc_death_start(void);
u8 rc_death_tick(void);
void rc_irq(void);
extern u16 rc_last_trap;
extern u8 rc_hide_player;
extern u8 rc_native_loop;
extern u8 rc_arcade_video;
extern u32 rc_yield_count;
void rc_task_start(void (*f)(void));
u8 rc_task_resume(void);
void rc_round_end_start(void);
void rc_round_start(u16 round);
extern u8 rc_round_phase;
extern u8 gate_kind[50];
extern u16 gate_lo[50], gate_width[50];
extern u32 gate_word[50];
#endif
