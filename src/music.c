#pragma GCC optimize ("O2")
/* Arcade music and sound effects.
 *
 * Normal build: the player runs on the Z80 (src/z80/player.s, hand-written);
 * this side loads it with the generated tables and, once per frame after the
 * vertical-blank DMA, passes the game's sound requests (6A85-6AA4) through
 * the mailbox (include/musdrv.h), takes back the requests the player has
 * finished or consumed, and copies its "playing" status to 6AA6-6AAF.
 *
 * PL_MUSIC_C build (tools/build.mjs with PL_MUSIC=c): the reference player in
 * C (src/z80/musdrv.c) runs here on the 68000, with its YM2612 writes queued
 * to src/z80/ymq.s. tests/z80_player.mjs compares the two write for write. */
#include "build_config.h"
#ifndef PL_MUSIC_C
#include "game.h"
#include "recomp_rt.h"
#include "music.h"

extern const u8 z80_bin[]; extern const u16 z80_bin_size;
extern const u8 mus_tables[]; extern const u8 mus_blob[];

volatile u8 mus_dbg_fmfirst, mus_dbg_mask, mus_dbg_on, mus_dbg_wave, mus_dbg_psg, mus_dbg_custom; volatile u32 mus_dbg_freq;
MusPatch mus_dbg_patch;
/* test hook: chip-write log of the last update (copied from Z80 RAM) */
u8 mus_log_on; u16 mus_log_n; u8 mus_log[Z_LOG_MAX * 3];

#define ZRAM ((vu8 *)0xA00000)
#define ZMB ((vu8 *)(0xA00000 + Z_MAILBOX))
static u8 mus_sent[MUS_TRACKS];
static u32 mus_req_word[9];
static u8 mus_dbg_active;

void music_init(void) {
    u16 i; u32 a = (u32)mus_blob;
    Z80_requestBus(TRUE);
    for (i = 0; i < 0x2000; i++) ZRAM[i] = 0;
    for (i = 0; i < z80_bin_size; i++) ZRAM[i] = z80_bin[i];
    for (i = 0; i < MUS_TABLES_SIZE; i++) ZRAM[Z_TABLES + i] = mus_tables[i];
    ZMB[MB_BASE] = (u8)(a >> 15); ZMB[MB_BASE + 1] = (u8)(a >> 23);
    ZMB[MB_BASE + 2] = (u8)(a & 0xFF); ZMB[MB_BASE + 3] = (u8)((a >> 8) & 0x7F);
    ZMB[MB_PAL] = IS_PAL_SYSTEM ? 1 : 0;
    Z80_startReset();
    Z80_releaseBus();
    for (i = 0; i < 64; i++) asm volatile ("nop");
    Z80_endReset();
    Z80_useBusProtection(Z_MAILBOX + MB_PROTECT);   /* SGDK raises it around its DMA flush */
    for (i = 0; i < MUS_TRACKS; i++) mus_sent[i] = 0;
    for (i = 0; i < 9; i++) mus_req_word[i] = 0;
}

static u16 mus_ym_fb(u32 freq) {
    u32 f0 = ((freq >> 4) * 59055u) >> 12; u16 blk = 0;
    while (f0 >= 2048 && blk < 7) { f0 >>= 1; blk++; }
    if (f0 > 2047) f0 = 2047;
    return (u16)(blk << 11 | f0);
}
static u16 mus_psg_per(u32 freq, u8 wave) {
    if (wave == 14) freq <<= 1; else if (wave == 15) freq <<= 2;
    if (!freq) return 1023;
    freq = 4887180u / (freq < 4778 ? 4778 : freq);
    return freq > 1023 ? 1023 : (u16)freq;
}

void music_frame(void) {
    const u32 *w = (const u32 *)&M6[0x284];
    u16 q, n;
    Z80_requestBus(TRUE);
    if (mus_log_on) {                 /* test hook: the Z80's log ring copied to ours (same layout) */
        u16 total = ZMB[MB_LOGN] | (ZMB[MB_LOGN + 1] << 8);
        while (mus_log_n != total) {
            u16 slot = (mus_log_n & (Z_LOG_MAX - 1)) * 3;
            mus_log[slot] = ZRAM[Z_LOG + slot]; mus_log[slot + 1] = ZRAM[Z_LOG + slot + 1]; mus_log[slot + 2] = ZRAM[Z_LOG + slot + 2];
            mus_log_n++;
        }
    }
    ZMB[MB_LOG] = mus_log_on;
    /* requests the driver finished or consumed: clear them, unless the game
     * has asked again since */
    for (q = 0; q < 4; q++) {
        u8 c = ZMB[MB_CLR + q];
        if (!c) continue;
        ZMB[MB_CLR + q] = 0;
        for (n = q * 8; c; n++, c >>= 1) if (c & 1) {
            if (M6[0x200 + MUS_FIRST + n] == mus_sent[n]) M6[0x200 + MUS_FIRST + n] = 0;
            mus_sent[n] = 0;
        }
    }
    {   u16 st = ZMB[MB_STAT] | (ZMB[MB_STAT + 1] << 8);
        for (q = 0; q < 10; q++, st >>= 1) M6[0x2A6 + q] = st & 1;
    }
    /* requests the game changed (words compared first: most frames none) */
    for (q = 0; q < 9; q++) if (w[q] != mus_req_word[q]) {
        s16 k;
        for (k = q * 4 - 1; k < q * 4 + 3; k++) {
            u8 r;
            if (k < 0 || k >= MUS_TRACKS) continue;
            r = M6[0x200 + MUS_FIRST + k];
            if (r != mus_sent[k]) { mus_sent[k] = r; ZMB[MB_REQ + k] = r; }
        }
        mus_req_word[q] = w[q];
    }
    if (mus_dbg_on | mus_dbg_mask | mus_dbg_custom | mus_dbg_fmfirst | mus_dbg_active) {
        const u8 *p = (const u8 *)&mus_dbg_patch;
        u16 fb = mus_ym_fb(mus_dbg_freq), per = mus_psg_per(mus_dbg_freq, mus_dbg_wave);
        ZMB[MB_DBG + 1] = mus_dbg_wave; ZMB[MB_DBG + 2] = mus_dbg_psg; ZMB[MB_DBG + 3] = mus_dbg_mask;
        ZMB[MB_DBG + 5] = mus_dbg_fmfirst;
        ZMB[MB_DBG + 6] = (u8)fb; ZMB[MB_DBG + 7] = (u8)(fb >> 8); ZMB[MB_DBG + 8] = (u8)per; ZMB[MB_DBG + 9] = (u8)(per >> 8);
        for (q = 0; q < 10; q++) ZMB[MB_DBG + 10 + q] = p[q];
        ZMB[MB_DBG + 4] = mus_dbg_custom; ZMB[MB_DBG] = mus_dbg_on;
        mus_dbg_active = mus_dbg_on | mus_dbg_mask | mus_dbg_custom | mus_dbg_fmfirst;
    }
    ZMB[MB_DONE] = (u8)vtimer;        /* this frame's DMA is over */
    Z80_releaseBus();
}

/* vertical interrupt: the tempo count to the Z80 player */
void music_vint(void) {
    Z80_requestBus(TRUE);
    ZMB[MB_GO] = (u8)vtimer;
    Z80_releaseBus();
}

void music_stop_all(void) {
    u16 i;
    for (i = MUS_FIRST; i < MUS_FIRST + MUS_TRACKS; i++) M6[0x200 + i] = 0;
    Z80_requestBus(TRUE);
    for (i = 0; i < MUS_TRACKS; i++) { ZMB[MB_REQ + i] = 0; mus_sent[i] = 0; }
    ZMB[MB_CMD] = 1;
    Z80_releaseBus();
    for (i = 0; i < 9; i++) mus_req_word[i] = 0;
}

#else   /* PL_MUSIC_C: the reference player on the 68000 */
#include "game.h"
#include "recomp_rt.h"
#include "music.h"
#include "musdrv_core.h"

/* test hooks (tools/sound calibration scripts): one voice, given waveform and
 * CUS30 frequency, on FM (or the PSG when mus_dbg_psg); voice mute mask;
 * custom FM patch; squares on FM first */
volatile u8 mus_dbg_fmfirst, mus_dbg_mask, mus_dbg_on, mus_dbg_wave, mus_dbg_psg, mus_dbg_custom; volatile u32 mus_dbg_freq;
MusPatch mus_dbg_patch;

static u8 mus_sent[MUS_TRACKS];          /* request levels as last passed on */
static u32 mus_req_word[9];              /* M6[284h..2A7h] as last seen */
static u8 mus_dbg_active;

extern const u8 z80_bin[]; extern const u16 z80_bin_size;
#define ZRAM ((vu8 *)0xA00000)
#define Q_START 0x1000
#define Q_END   0x1C00
#define Q_RD    0x1FF0
#define Q_WR    0x1FF2

/* hand this update's YM2612 writes to the Z80 queue (src/z80/ymq.s) */
static void ymq_flush(void) {
    u16 i = 0;
    while (i < ymq_n) {
        u16 rd, wr, room, n;
        Z80_requestBus(TRUE);
        rd = ZRAM[Q_RD] | (ZRAM[Q_RD + 1] << 8); wr = ZRAM[Q_WR] | (ZRAM[Q_WR + 1] << 8);
        room = (u16)((rd > wr ? rd - wr : (Q_END - Q_START) - (wr - rd)) / 3 - 1);   /* entries free */
        n = ymq_n - i; if (n > room) n = room;
        {   const u8 *q = ymq + i * 3; u16 k = n * 3, run;
            while (k) {                                   /* in runs up to the ring end */
                vu8 *z = ZRAM + wr;
                run = Q_END - wr; if (run > k) run = k;
                k -= run; wr += run; if (wr == Q_END) wr = Q_START;
                while (run--) *z++ = *q++;
            }
        }
        ZRAM[Q_WR] = (u8)wr; ZRAM[Q_WR + 1] = (u8)(wr >> 8);
        Z80_releaseBus();
        i += n;
        if (i < ymq_n) waitSubTick(200);    /* ring full (only after a long stall): let the Z80 drain */
    }
    ymq_n = 0;
}

void music_init(void) {
    u16 i;
    Z80_requestBus(TRUE);
    for (i = 0; i < 0x2000; i++) ZRAM[i] = 0;
    for (i = 0; i < z80_bin_size; i++) ZRAM[i] = z80_bin[i];
    ZRAM[Q_RD] = ZRAM[Q_WR] = (u8)Q_START; ZRAM[Q_RD + 1] = ZRAM[Q_WR + 1] = (u8)(Q_START >> 8);
    Z80_startReset();
    Z80_releaseBus();
    for (i = 0; i < 64; i++) asm volatile ("nop");
    Z80_endReset();
    drv_init();
    ymq_flush();
}

static u16 mus_ym_fb(u32 freq) {
    u32 f0 = ((freq >> 4) * 59055u) >> 12; u16 blk = 0;
    while (f0 >= 2048 && blk < 7) { f0 >>= 1; blk++; }
    if (f0 > 2047) f0 = 2047;
    return (u16)(blk << 11 | f0);
}
static u16 mus_psg_per(u32 freq, u8 wave) {
    if (wave == 14) freq <<= 1; else if (wave == 15) freq <<= 2;
    if (!freq) return 1023;
    freq = 4887180u / (freq < 4778 ? 4778 : freq);
    return freq > 1023 ? 1023 : (u16)freq;
}

void music_frame(void) {
    const u32 *w = (const u32 *)&M6[0x284];
    u16 q, n;
    /* requests the game changed (words compared first: most frames none) */
    for (q = 0; q < 9; q++) if (w[q] != mus_req_word[q]) {
        s16 k;
        for (k = q * 4 - 1; k < q * 4 + 3; k++) {
            u8 r;
            if (k < 0 || k >= MUS_TRACKS) continue;
            r = M6[0x200 + MUS_FIRST + k];
            if (r != mus_sent[k]) { mus_sent[k] = r; MB[MB_REQ + k] = r; drv_req_dirty = 1; }
        }
        mus_req_word[q] = w[q];
    }
    if (mus_dbg_on | mus_dbg_mask | mus_dbg_custom | mus_dbg_fmfirst | mus_dbg_active) {
        const u8 *p = (const u8 *)&mus_dbg_patch;
        u16 fb = mus_ym_fb(mus_dbg_freq), per = mus_psg_per(mus_dbg_freq, mus_dbg_wave);
        MB[MB_DBG + 1] = mus_dbg_wave; MB[MB_DBG + 2] = mus_dbg_psg; MB[MB_DBG + 3] = mus_dbg_mask;
        MB[MB_DBG + 5] = mus_dbg_fmfirst;
        MB[MB_DBG + 6] = (u8)fb; MB[MB_DBG + 7] = (u8)(fb >> 8); MB[MB_DBG + 8] = (u8)per; MB[MB_DBG + 9] = (u8)(per >> 8);
        for (q = 0; q < 10; q++) MB[MB_DBG + 10 + q] = p[q];
        MB[MB_DBG + 4] = mus_dbg_custom; MB[MB_DBG] = mus_dbg_on;
        mus_dbg_active = mus_dbg_on | mus_dbg_mask | mus_dbg_custom | mus_dbg_fmfirst;
    }
    update();
    if (ymq_n) ymq_flush();
    /* requests the player finished or consumed: clear them, unless the game
     * has asked again since */
    for (q = 0; q < 4; q++) {
        u8 c = MB[MB_CLR + q];
        if (!c) continue;
        MB[MB_CLR + q] = 0;
        for (n = q * 8; c; n++, c >>= 1) if (c & 1) {
            if (M6[0x200 + MUS_FIRST + n] == mus_sent[n]) M6[0x200 + MUS_FIRST + n] = 0;
            mus_sent[n] = 0;
        }
    }
    {   u16 st = MB[MB_STAT] | (MB[MB_STAT + 1] << 8);
        for (q = 0; q < 10; q++, st >>= 1) M6[0x2A6 + q] = st & 1;
    }
}

void music_stop_all(void) {
    u16 i;
    for (i = MUS_FIRST; i < MUS_FIRST + MUS_TRACKS; i++) M6[0x200 + i] = 0;
    for (i = 0; i < MUS_TRACKS; i++) { MB[MB_REQ + i] = 0; mus_sent[i] = 0; }
    for (i = 0; i < 9; i++) mus_req_word[i] = 0;
    MB[MB_CMD] = 1; drv_req_dirty = 1;
}

#endif
