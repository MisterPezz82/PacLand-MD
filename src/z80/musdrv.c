/* Pac-Land sound driver for the Mega Drive's Z80 (SDCC, -mz80).
 *
 * Plays the arcade's sound requests from streams of CUS30 voice parameters
 * (tools/sound/music.py) on the YM2612 and PSG, so the 68000 only passes the
 * request bytes through the mailbox (src/music.c). Per 60 Hz update:
 * requests -> streams (up to 6 at once) -> 8 virtual voices, each owned by
 * the newest stream using it -> channels (6 FM, 3 PSG tone, PSG noise) ->
 * chip writes, only for what changed.
 *
 * Squares go to the PSG, which plays them exactly; other waveforms to FM
 * patches fitted to the arcade waveforms. When there are more tones than
 * channels (the 8-voice jingles), detuned copies are folded into their
 * partner's channel. Noise is clocked by PSG tone channel 2, as the CUS30
 * noise rate (33-66 kHz) is far above the PSG's own noise rates.
 *
 * ROM is read through the 68000 bank window only after the 68000 has said
 * this frame's DMA is done (MB_GO) and never while SGDK's bus protection
 * signal is up, so a Z80 access never meets a DMA transfer. */
#include "musdrv.h"

#ifdef __SDCC
typedef unsigned char u8;
typedef unsigned int u16;
typedef signed char s8;
#define MB ((volatile u8 *)Z_MAILBOX)
#define TB ((const u8 *)Z_TABLES)
#define YM0 (*(volatile u8 *)0x4000)
#define YM1 (*(volatile u8 *)0x4001)
#define YM2 (*(volatile u8 *)0x4002)
#define YM3 (*(volatile u8 *)0x4003)
#define BANKREG (*(volatile u8 *)0x6000)
#define PSG (*(volatile u8 *)0x7F11)
#define WIN ((const volatile u8 *)0x8000)
#else
/* The same player built for the 68000 (src/music.c includes this file): the
 * mailbox is plain RAM, the tables and streams are read in place, and the
 * caller holds the Z80 bus around update() for the YM2612 writes. */
static u8 drv_mb[0x80];
extern const u8 mus_tables[]; extern const u8 mus_blob[];
#define MB drv_mb
#define TB mus_tables
#define YM0 (*(volatile u8 *)0xA04000)
#define YM1 (*(volatile u8 *)0xA04001)
#define YM2 (*(volatile u8 *)0xA04002)
#define YM3 (*(volatile u8 *)0xA04003)
#ifdef PL_MUSIC_HOST_TEST
static u8 host_psg;
#define PSG host_psg
#else
#define PSG (*(volatile u8 *)0xC00011)
#endif
static const u8 *drv_win;
static u8 drv_req_dirty;          /* src/music.c changed a request byte */
#define WIN drv_win
#endif

volatile u8 ticks;                      /* vertical interrupts (crt0; unused, see main) */

#define NSRC 6
typedef struct {
    u8 on, req, seq, nv, flags;         /* req: track index 0..31 */
    u16 frame, nf, loop, bank, off;
    u8 loopoff[3], voices[8], partner[4];
    u8 vol[8], wave[8]; u16 fb[8], per[8];
} Src;
static Src src[NSRC];
static u8 seq_next;
static u8 req_prev[MUS_TRACKS];

/* virtual voices */
static u8 vvol[8], vwave[8]; static u16 vfb[8], vper[8];
static u8 vch[8], chv[10], vmerged[8], vown[8];
static u8 own_dirty, m_dirty, m_realloc, merges, top_psg;
/* chip caches */
static s8 fm_patch[6]; static u8 fm_tl[6][4]; static u16 fm_fbc[6]; static u8 fm_quiet[6];
static u16 psg_per[3]; static u8 psg_att[4]; static u8 noise_mode;
static u16 cur_bank;

static const u8 opofs[4] = { 0, 8, 4, 12 };
static const u8 ch_part[6] = { 0, 0, 0, 1, 1, 1 }, ch_c[6] = { 0, 1, 2, 0, 1, 2 };
/* carrier attenuation (0.75 dB steps) for an arcade amplitude 0..15 */
static const u8 amp_tl[16] = { 127, 35, 23, 16, 11, 8, 6, 4, 3, 2, 2, 1, 1, 0, 0, 0 };
static const u8 vol_psg[16] = { 15, 12, 9, 7, 6, 4, 4, 3, 2, 2, 1, 1, 1, 0, 0, 0 };   /* 2 dB steps */
static const u16 sq16[17] = { 0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225, 256 };
#define NOISE_ATT 1                     /* noise level against the squares (emulator) */

/* ---- chips ---- */
#ifdef __SDCC
#define psgw(v) (PSG = (v))
static void ym(u8 part, u8 reg, u8 val) {
    if (part) { while (YM0 & 0x80); YM2 = reg; while (YM0 & 0x80); YM3 = val; }
    else      { while (YM0 & 0x80); YM0 = reg; while (YM0 & 0x80); YM1 = val; }
}
#else
/* 68000 build: the writes are queued and handed to the Z80 once per update
 * (src/music.c, src/z80/ymq.s), which waits out the YM2612's busy time */
#define YMQ_MAX 512
static u8 ymq[YMQ_MAX * 3]; static u16 ymq_n;
/* test hook (tests/z80_player.mjs): every chip write logged, as the Z80
 * player logs them, to compare the two players */
u8 mus_log_on; u16 mus_log_n; u8 mus_log[512 * 3];   /* ring; mus_log_n counts every entry */
static void logw(u8 port, u8 reg, u8 val) {
    if (mus_log_on) { u8 *q = mus_log + (mus_log_n & 511) * 3; q[0] = port; q[1] = reg; q[2] = val; mus_log_n++; }
}
static void ym(u8 part, u8 reg, u8 val) {
    logw(part ? 2 : 0, reg, val);
    if (ymq_n < YMQ_MAX) { u8 *q = ymq + ymq_n * 3; q[0] = part ? 2 : 0; q[1] = reg; q[2] = val; ymq_n++; }
}
static inline void psgw(u8 v) { logw(0xFF, 0, v); PSG = v; }
#endif
static const u8 *patch_of(u8 w) {
    return MB[MB_DBG + 4] ? (const u8 *)&MB[MB_DBG + 10] : TB + ZT_PATCH + w * 10;
}
static u8 last_custom[10];
static void fm_silence(u8 ch) {
    u8 o, part = ch_part[ch], c = ch_c[ch]; const u8 *mp;
    if (fm_quiet[ch]) return;
    fm_quiet[ch] = 1;
    if (fm_patch[ch] < 0) return;
    mp = patch_of((u8)fm_patch[ch]);
    for (o = 0; o < 4; o++) if ((mp[1] >> o) & 1)
        if (fm_tl[ch][o] != 127) { ym(part, 0x40 + opofs[o] + c, 127); fm_tl[ch][o] = 127; }
}
static void fm_set(u8 ch, u8 wave, u8 amp, u16 fb) {
    u8 part = ch_part[ch], c = ch_c[ch], o; const u8 *mp = patch_of(wave);
    fm_quiet[ch] = 0;
    if (MB[MB_DBG + 4]) {               /* test hook: reprogram when the custom patch changes */
        for (o = 0; o < 10; o++) if (last_custom[o] != mp[o]) { last_custom[o] = mp[o]; fm_patch[ch] = -1; }
    }
    if (fm_patch[ch] != (s8)wave) {
        ym(part, 0xB0 + c, mp[0]); ym(part, 0xB4 + c, 0xC0);
        for (o = 0; o < 4; o++) {
            u8 r = opofs[o] + c;
            ym(part, 0x30 + r, mp[2 + o]);
            ym(part, 0x50 + r, 0x1F); ym(part, 0x60 + r, 0); ym(part, 0x70 + r, 0);
            ym(part, 0x80 + r, 0x0F); ym(part, 0x90 + r, 0);
            if (!((mp[1] >> o) & 1)) { ym(part, 0x40 + r, mp[6 + o]); fm_tl[ch][o] = mp[6 + o]; }
            else fm_tl[ch][o] = 0xFF;
        }
        fm_patch[ch] = (s8)wave;
        /* key off and on: all operators restart in phase, so the timbre does
         * not depend on what the channel played before */
        ym(0, 0x28, c | (part << 2)); ym(0, 0x28, 0xF0 | c | (part << 2));
    }
    for (o = 0; o < 4; o++) if ((mp[1] >> o) & 1) {
        u16 t = mp[6 + o] + amp_tl[amp]; u8 tl = t > 127 ? 127 : (u8)t;
        if (fm_tl[ch][o] != tl) { ym(part, 0x40 + opofs[o] + c, tl); fm_tl[ch][o] = tl; }
    }
    if (fm_fbc[ch] != fb) { ym(part, 0xA4 + c, (u8)(fb >> 8)); ym(part, 0xA0 + c, (u8)fb); fm_fbc[ch] = fb; }
}
static void psg_att_set(u8 p, u8 att) {
    if (psg_att[p] != att) { psgw(0x90 | (p << 5) | att); psg_att[p] = att; }
}
static void psg_tone(u8 p, u8 vol, u16 per) {
    if (psg_per[p] != per) { psgw(0x80 | (p << 5) | (per & 15)); psgw((u8)(per >> 4)); psg_per[p] = per; }
    psg_att_set(p, vol_psg[vol]);
}
static void psg_noise(u8 vol, u16 per) {
    u8 att;
    if (psg_per[2] != per) { psgw(0xC0 | (per & 15)); psgw((u8)(per >> 4)); psg_per[2] = per; }
    if (noise_mode != 0xE7) { psgw(0xE7); noise_mode = 0xE7; }
    att = vol_psg[vol] + NOISE_ATT; if (att > 15) att = 15;
    psg_att_set(3, att);
}
static void ch_silence(u8 k) {
    if (k < 6) fm_silence(k);
    else psg_att_set(k - 6, 15);
}

/* ---- ROM ---- */
static void set_bank(u16 b) {
    u8 i;
    if (b == cur_bank) return;
    cur_bank = b;
#ifdef __SDCC
    for (i = 0; i < 9; i++) { BANKREG = (u8)b; b >>= 1; }
#else
    (void)i; drv_win = mus_blob + ((u32)b << 15);
#endif
}
static u16 rb_bank, rb_off;
#ifdef __SDCC
static u8 rd(void) {
#else
static inline __attribute__((always_inline)) u8 rd(void) {
#endif
    u8 v;
    v = WIN[rb_off];
    if (++rb_off == 0x8000) { rb_off = 0; set_bank(++rb_bank); }
    return v;
}
/* blob address + 24-bit offset -> rb_bank / rb_off */
static void at(const u8 *o3) {
    u16 lo = ((u16)o3[1] << 8) | o3[2];
    u16 off = MB[MB_BASE + 2] | ((u16)MB[MB_BASE + 3] << 8);
    rb_bank = (MB[MB_BASE] | ((u16)MB[MB_BASE + 1] << 8)) + ((u16)o3[0] << 1) + (lo >> 15);
    off += lo & 0x7FFF;
    if (off >= 0x8000) { off -= 0x8000; rb_bank++; }
    rb_off = off;
    set_bank(rb_bank);
}
/* track entry ti of the blob's table */
static u8 thdr[TR_SIZE];
static void load_hdr(u8 ti) {
    u8 o3[3], i; u16 x = (u16)ti * TR_SIZE;
    o3[0] = 0; o3[1] = (u8)(x >> 8); o3[2] = (u8)x;
    while (MB[MB_PROTECT]) ;
    at(o3);
    for (i = 0; i < TR_SIZE; i++) thdr[i] = rd();
}

/* ---- voices ---- */
#ifdef __SDCC
static void voice_set(u8 v, u8 a, u8 w, u16 fb, u16 per) {
#else
static inline __attribute__((always_inline)) void voice_set(u8 v, u8 a, u8 w, u16 fb, u16 per) {
#endif
    u8 b = vvol[v];
    if (MB[MB_DBG + 3] & (1 << v)) a = 0;                       /* test hook: mute voices */
    if (a == b && (!(a & 15) || (w == vwave[v] && fb == vfb[v] && per == vper[v]))) return;
    m_dirty |= 1 << v;
    /* Release silent channels and reconsider folded partners when their
     * waveform changes. Otherwise a sound effect can leave a music voice
     * permanently folded into a silent or unrelated voice. */
    if (((a ^ b) & 0x80) || (!(a & 15) != !(b & 15)) ||
        w != vwave[v]) m_realloc = 1;
    if (a & 15) {                       /* active voices keep a compatible channel */
        u8 c = vch[v];
        if (vmerged[v] != 0xFF) ;
        else if (c == 0xFF || ((a ^ b) & 0x80)) m_realloc = 1;
        else if (c >= 6 && c < 9 && !TB[ZT_SQUARE + w]) m_realloc = 1;
    }
    vvol[v] = a; vwave[v] = w; vfb[v] = fb; vper[v] = per;
}


static void src_start(Src *s, u8 ti) {   /* track header in thdr */
    u8 k;
    s->req = ti; s->on = 1; s->seq = ++seq_next; s->frame = 0;
    s->nf = thdr[TR_FRAMES] | ((u16)thdr[TR_FRAMES + 1] << 8);
    s->loop = thdr[TR_LOOP] | ((u16)thdr[TR_LOOP + 1] << 8);
    s->nv = thdr[TR_NV]; s->flags = thdr[TR_FLAGS];
    for (k = 0; k < 3; k++) s->loopoff[k] = thdr[TR_LOOPOFF + k];
    for (k = 0; k < 8; k++) { s->voices[k] = thdr[TR_VOICES + k]; s->vol[k] = 0; s->wave[k] = 0; s->fb[k] = 0; s->per[k] = 0; }
    for (k = 0; k < 4; k++) s->partner[k] = thdr[TR_PARTNER + k];
    at(thdr + TR_OFF); s->bank = rb_bank; s->off = rb_off;
    own_dirty = 1;
}
static void req_clear(u8 n) {
    MB[MB_REQ + n] = 0; req_prev[n] = 0; MB[MB_CLR + (n >> 3)] |= 1 << (n & 7);
}
static void src_step(Src *s, u8 si) {
    u8 mask, k;
    if (s->frame >= s->nf) {
        if (s->loop != 0xFFFF && MB[MB_REQ + s->req]) { s->frame = s->loop; at(s->loopoff); s->bank = rb_bank; s->off = rb_off; }
        else { s->on = 0; req_clear(s->req); own_dirty = 1; return; }
    }
    rb_bank = s->bank; rb_off = s->off; set_bank(rb_bank);
    mask = rd();
    for (k = 0; mask; k++, mask >>= 1) if (mask & 1) {
        u8 fl = rd(), v;
        if (fl & 1) s->vol[k] = rd();
        if (fl & 2) s->wave[k] = rd();
        if (fl & 4) {
            u16 a = rd(); a = (a << 8) | rd(); s->fb[k] = a;
            a = rd(); a = (a << 8) | rd(); s->per[k] = a;
        }
        v = s->voices[k];
        if (vown[v] == si) voice_set(v, s->vol[k], s->wave[k], s->fb[k], s->per[k]);
    }
    s->bank = rb_bank; s->off = rb_off; s->frame++;
}

static void stop_all(void) {
    u8 i;
    for (i = 0; i < NSRC; i++) src[i].on = 0;
    for (i = 0; i < MUS_TRACKS; i++) { req_prev[i] = 0; }
    own_dirty = 1;
}

static void update(void) {
    u8 i, k, v, dirty, realloc;
    if (MB[MB_CMD]) { stop_all(); MB[MB_CMD] = 0; }
    /* requests: start on 0->1, stop on 1->0 */
#ifndef __SDCC
    if (drv_req_dirty) { drv_req_dirty = 0;
#endif
    for (i = 0; i < MUS_TRACKS; i++) {
        u8 r = MB[MB_REQ + i];
        if (r == req_prev[i]) continue;
        req_prev[i] = r;
        if (r) {
            load_hdr(i);
            if (!(thdr[TR_FRAMES] | thdr[TR_FRAMES + 1])) continue;
            for (k = 0; k < NSRC; k++) if (src[k].on && src[k].req == i) break;
            if (k == NSRC) {
                for (k = 0; k < NSRC; k++) if (!src[k].on) break;
                if (k == NSRC) {                /* all busy: the oldest, a non-looping one if any */
                    u8 old = 0xFF, j;
                    for (j = 0; j < NSRC; j++) {
                        if (old != 0xFF && (src[old].loop == 0xFFFF) && src[j].loop != 0xFFFF) continue;
                        if (old == 0xFF || (src[old].loop != 0xFFFF && src[j].loop == 0xFFFF) || (u8)(src[j].seq - src[old].seq) > 127) old = j;
                    }
                    k = old;
                }
            }
            src_start(&src[k], i);
            if (thdr[TR_FLAGS] & 1) req_clear(i);
        } else {
            for (k = 0; k < NSRC; k++) if (src[k].on && src[k].req == i && !(src[k].flags & 1)) { src[k].on = 0; own_dirty = 1; }
        }
    }
#ifndef __SDCC
    }
#endif
    m_dirty = 0; m_realloc = 0;
    for (k = 0; k < NSRC; k++) if (src[k].on) {
        while (MB[MB_PROTECT]) ;                /* DMA in progress: no 68000 bus access */
        src_step(&src[k], k);
    }
    if (own_dirty) {
        u8 ord[NSRC], na = 0, j; u16 st = 0;
        own_dirty = 0;
        m_realloc = 1; m_dirty = 0xFF;
        for (k = 0; k < NSRC; k++) if (src[k].on) {
            for (j = na; j && (u8)(src[ord[j - 1]].seq - src[k].seq) < 128; j--) ord[j] = ord[j - 1];
            ord[j] = k; na++;
        }
        for (v = 0; v < 8; v++) vown[v] = 0xFF;
        for (j = 0; j < na; j++) {
            Src *s = &src[ord[j]]; u8 q;
            if ((u8)(s->req + MUS_FIRST - 0x86) < 10) st |= 1 << (s->req + MUS_FIRST - 0x86);
            for (q = 0; q < s->nv; q++) vown[s->voices[q]] = ord[j];
        }
        MB[MB_STAT] = (u8)st; MB[MB_STAT + 1] = (u8)(st >> 8);
        for (v = 0; v < 8; v++) {
            u8 o = vown[v];
            if (o == 0xFF) voice_set(v, 0, vwave[v], vfb[v], vper[v]);
            else {
                Src *s = &src[o]; u8 q = 0;
                while (s->voices[q] != v) q++;
                voice_set(v, s->vol[q], s->wave[q], s->fb[q], s->per[q]);
            }
        }
    }
    dirty = m_dirty; realloc = m_realloc;
    if (MB[MB_DBG]) {                       /* test hook: one voice, given waveform and pitch */
        for (v = 0; v < 8; v++) vvol[v] = 0;
        vvol[0] = 15; vwave[0] = MB[MB_DBG + 1];
        vfb[0] = MB[MB_DBG + 6] | ((u16)MB[MB_DBG + 7] << 8);
        vper[0] = MB[MB_DBG + 8] | ((u16)MB[MB_DBG + 9] << 8);
        if (vch[0] != 0xFF && (vch[0] < 6) == (MB[MB_DBG + 2] != 0)) { chv[vch[0]] = 0xFF; vch[0] = 0xFF; }
        if (vch[0] == 0xFF) { u8 c = MB[MB_DBG + 2] ? 6 : 0; if (chv[c] != 0xFF) vch[chv[c]] = 0xFF; vch[0] = c; chv[c] = 0; }
        dirty = 0xFF; realloc = 1;
    }
    if (!dirty) return;
    if (realloc) {
        /* PSG tone channel 2 is the noise clock while a noise voice plays */
        u8 nz = 0, need = 0, sq = 0, u;
        for (v = 0; v < 8; v++) if ((vvol[v] & 0x8F) > 0x80) nz = 1;
        top_psg = nz ? 8 : 9;
        if (nz && chv[8] != 0xFF) { vch[chv[8]] = 0xFF; chv[8] = 0xFF; }
        /* more tones than channels: fold detuned partners, highest voices first */
        merges = 0;
        for (v = 0; v < 8; v++) {
            vmerged[v] = 0xFF;
            if ((vvol[v] & 15) && !(vvol[v] & 0x80)) { if (TB[ZT_SQUARE + vwave[v]]) sq++; else need++; }
        }
        if (sq > top_psg - 6) need += sq - (top_psg - 6);
        if (need > 6) {
            for (v = 7; v > 0 && need > 6; v--) {
                u8 o = vown[v]; if (o == 0xFF) continue;
                if (!(vvol[v] & 15) || (vvol[v] & 0x80)) continue;
                u = src[o].partner[v >> 1]; u = (v & 1) ? u >> 4 : u & 15;
                if (u < v && vown[u] == o && vmerged[u] == 0xFF && (vvol[u] & 15) && !(vvol[u] & 0x80) && vwave[u] == vwave[v]) {
                    vmerged[v] = u; need--; merges = 1;
                }
            }
        }
        /* keep a voice on its channel while it suits it */
        for (v = 0; v < 8; v++) {
            u8 vol = vvol[v] & 15, noise = vvol[v] & 0x80, c = vch[v];
            if (!vol || vmerged[v] != 0xFF) { if (c != 0xFF) { chv[c] = 0xFF; vch[v] = 0xFF; } continue; }
            if (c != 0xFF) {
                u8 ok = noise ? c == 9 : c < 6 || (c < top_psg && TB[ZT_SQUARE + vwave[v]]);
                if (ok) continue;
                chv[c] = 0xFF; vch[v] = 0xFF;
            }
        }
        /* Release every unsuitable channel before assigning any: a lower
         * voice must be able to use a channel released by a higher voice. */
        for (v = 0; v < 8; v++) {
            u8 c, noise = vvol[v] & 0x80;
            if (!(vvol[v] & 15) || vmerged[v] != 0xFF || vch[v] != 0xFF) continue;
            if (noise) c = chv[9] == 0xFF ? 9 : 0xFF;
            else {
                c = 0xFF;
                if (TB[ZT_SQUARE + vwave[v]] && !MB[MB_DBG + 5]) for (k = 6; k < top_psg; k++) if (chv[k] == 0xFF) { c = k; break; }
                if (c == 0xFF) for (k = 0; k < 6; k++) if (chv[k] == 0xFF) { c = k; break; }
            }
            if (c != 0xFF) { vch[v] = c; chv[c] = v; }
        }
        /* squares left without a channel: any free PSG tone channel */
        for (v = 0; v < 8; v++) {
            if (!(vvol[v] & 15) || vch[v] != 0xFF || (vvol[v] & 0x80) || vmerged[v] != 0xFF) continue;
            for (k = 6; k < top_psg; k++) if (chv[k] == 0xFF) { vch[v] = k; chv[k] = v; break; }
        }
    }
    /* write: every channel after a reassignment, else the changed voices' */
    {   u16 chd = 0;
        if (realloc) chd = 0x3FF;
        else for (v = 0; v < 8; v++) if (dirty & (1 << v)) {
            u8 o = vmerged[v] != 0xFF ? vmerged[v] : v;
            if (vch[o] != 0xFF) chd |= 1 << vch[o];
        }
        for (k = 0; k < 10; k++) {
            if (!(chd & (1 << k))) continue;
            v = chv[k];
            if (v == 0xFF) {
                if (k < 9) { if (k < top_psg) ch_silence(k); else psg_att_set(2, 15); }
                else psg_att_set(3, 15);
                continue;
            }
            {   u8 a = vvol[v] & 15, j;
                if (merges) {                   /* sqrt of the summed power */
                    u16 p = sq16[a];
                    for (j = 0; j < 8; j++) if (vmerged[j] == v) p += sq16[vvol[j] & 15];
                    if (p != sq16[a]) { a = 0; while (a < 15 && sq16[a + 1] <= p) a++; }
                }
                if (k < 6) fm_set(k, vwave[v], a, vfb[v]);
                else if (k < 9) psg_tone(k - 6, a, vper[v]);
                else psg_noise(a, vper[v]);
            }
        }
    }
}

static void drv_init(void) {
    u8 i;
    cur_bank = 0xFFFF;
    for (i = 0; i < 6; i++) { fm_patch[i] = -1; fm_quiet[i] = 0; fm_fbc[i] = 0xFFFF; }
    for (i = 0; i < 8; i++) { vch[i] = 0xFF; vmerged[i] = 0xFF; vown[i] = 0xFF; vvol[i] = 0; }
    for (i = 0; i < 10; i++) chv[i] = 0xFF;
    for (i = 0; i < 3; i++) psg_per[i] = 0xFFFF;
    for (i = 0; i < 4; i++) { psg_att[i] = 0xFF; psg_att_set(i, 15); }
    noise_mode = 0; top_psg = 9; own_dirty = 1;
    ym(0, 0x2B, 0); ym(0, 0x22, 0); ym(0, 0x27, 0);
    for (i = 0; i < 6; i++) {
        u8 o, part = ch_part[i], c = ch_c[i];
        ym(0, 0x28, c | (part << 2));
        for (o = 0; o < 16; o += 4) { ym(part, 0x40 + o + c, 127); ym(part, 0x80 + o + c, 0xFF); }
        for (o = 0; o < 4; o++) fm_tl[i][o] = 127;
        fm_quiet[i] = 1;
    }
}

#ifdef __SDCC
void main(void) {
    u8 last, pal_acc = 0;
    drv_init();
    /* timing: the 68000 passes its vertical interrupt count with each
     * "DMA done" (MB_GO), once per game loop, so the tempo follows the
     * 60 Hz count even when the game loop runs late, and the ROM is only
     * read once this frame's DMA is over. (The Z80's own vertical interrupt
     * is not used: it is lost whenever the 68000 holds the Z80 bus at the
     * start of the vertical blank.) */
    last = MB[MB_GO];
    for (;;) {
        u8 n, go;
        while ((go = MB[MB_GO]) == last) ;
        n = go - last; last = go;
        if (n > 8) n = 8;                   /* after a long stall (loading), do not rush */
        while (n--) {
            u8 v0 = *(volatile u8 *)0x7F08;
            if (MB[MB_PAL]) { pal_acc += 6; while (pal_acc >= 5) { pal_acc -= 5; update(); } }
            else update();
            MB[MB_ALIVE]++;
            {   u8 d = *(volatile u8 *)0x7F08 - v0; if (d < 200 && d > MB[MB_INFO + 1]) MB[MB_INFO + 1] = d; MB[MB_INFO + 7] = d; }
            if (n) MB[MB_INFO + 2]++;       /* statistics: updates run back to back */
        }
    }
}
#endif
