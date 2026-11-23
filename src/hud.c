/* Arcade HUD rows on the Mega Drive.
 *
 * The original code writes the score, high score, credit, round and time
 * gauge into the arcade foreground video RAM (R6 0000-0FFF, 64x32 cells of
 * code, attribute). The arcade shows cells 3..38 of rows 2..29; rows 2, 3 and
 * 29 do not scroll and hold the HUD. They are copied here to plane A rows
 * 0, 1 and 27, columns 2..37 (the 36 arcade columns centred in 40), with the
 * arcade's own character graphics (hud_font, 2 bits per pixel) coloured with
 * the nearest entries of the round's sprite palette (PAL3). Converted
 * characters live in a small VRAM cache (window-plane area, unused).
 *
 * Arcade attribute: bit 0 = code bit 8, bits 1-4 = colour, 6/7 = flips;
 * colour group = ((attr & 1Eh) >> 1) + ((code & 1E0h) >> 1). */
#pragma GCC optimize ("O2")
#include "game.h"
#include "recomp_rt.h"
#include "recomp_data.h"
#include "hud.h"

#define HUD_BASE 1440            /* tiles 1440..1535: window name table area, window off */
#define HUD_SLOTS 96
static const u8 arow[3] = { 2, 3, 29 }, mrow[3] = { 0, 1, 27 };
static u16 shadow[3][36];        /* last drawn (code | attr << 9 ... ) per cell, 0xFFFF = unknown */
static u16 slot_key[HUD_SLOTS];  /* cached character: code (9 bits) ... */
static u8 slot_group[HUD_SLOTS]; /* ... and colour group */
static u8 slot_used;             /* next slot to recycle (round robin) */
static u8 penmap[256][4];        /* colour group -> PAL3 index per pen, 0xFF = not built */
static u32 last[3][18];          /* raw cell pairs as last seen */
static u8 hint[1024];            /* HHASH(code, group) -> likely slot */
#define HHASH(code, group) (((code) + (u16)(group) * 37) & 1023)
static u8 hud_full;              /* first frame after hud_load: draw everything */

/* Nearest PAL3 entry by weighted RGB distance. The round palette is split
 * into components once (hud_load) and squares come from a table: about 600
 * cycles per colour instead of 45 multiplications. */
static u8 pr[16], pg[16], pb[16];
static const u8 sq3[15] = { 147, 108, 75, 48, 27, 12, 3, 0, 3, 12, 27, 48, 75, 108, 147 };
static const u8 sq4[15] = { 196, 144, 100, 64, 36, 16, 4, 0, 4, 16, 36, 64, 100, 144, 196 };
static const u8 sq2[15] = { 98, 72, 50, 32, 18, 8, 2, 0, 2, 8, 18, 32, 50, 72, 98 };
static u8 nearest(u16 c) {
    u8 r = (u8)(((c >> 1) & 7) + 7), g = (u8)(((c >> 5) & 7) + 7), b = (u8)(((c >> 9) & 7) + 7);
    u8 i, best = 1; u16 bd = 0xFFFF;
    for (i = 1; i < 16; i++) {
        u16 d = (u16)(sq3[r - pr[i]] + sq4[g - pg[i]] + sq2[b - pb[i]]);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static const u8 *pens(u8 group) {
    u8 *m = penmap[group];
    if (m[0] == 0xFF) {
        const u16 *c = hud_colours + (((u16)hud_bank[g_round] << 10) | ((u16)group << 2));
        u8 p;
        for (p = 0; p < 4; p++) m[p] = (c[p] == 0xFFFF) ? 0 : nearest(c[p]);
    }
    return m;
}

static u8 cache_full;            /* all slots handed out: start again next frame (no slot
                                    still on screen is ever overwritten) */
static u8 conv_left = 255;        /* character conversions still allowed (text rows: 2/frame) */
static u16 char_tile(u16 code, u8 group) {
    u8 i; u16 h = (u16)HHASH(code, group); u32 tile[8]; const u8 *src, *m;
    i = hint[h];
    if (i < HUD_SLOTS && slot_key[i] == code && slot_group[i] == group) return HUD_BASE + i;
    /* no search of the other slots (96 compares cost ~5 scan lines): a hint
     * miss converts again, at worst duplicating a cached character */
    if (!conv_left) return 0;                     /* conversion budget used up */
    conv_left--;
    i = slot_used;
    if (slot_used == HUD_SLOTS - 1) { slot_used = 0; cache_full = 1; } else slot_used++;
    slot_key[i] = code; slot_group[i] = group; hint[h] = i;
    src = hud_font + (code << 4);
    m = pens(group);
    {   /* each source byte holds four 2-bit pens -> four colour nibbles */
        u8 k;
        for (k = 0; k < 8; k++) {
            u8 hb = src[2 * k], lb = src[2 * k + 1];
            u16 hw = (u16)(m[hb >> 6] << 12 | m[(hb >> 4) & 3] << 8 | m[(hb >> 2) & 3] << 4 | m[hb & 3]);
            u16 lw = (u16)(m[lb >> 6] << 12 | m[(lb >> 4) & 3] << 8 | m[(lb >> 2) & 3] << 4 | m[lb & 3]);
            tile[k] = (u32)hw << 16 | lw;
        }
    }
    VDP_loadTileData(tile, HUD_BASE + i, 1, CPU);
    return HUD_BASE + i;
}

void hud_load(void) {
    u16 i;
    for (i = 0; i < 16; i++) { u16 p = sprite_pal[g_round][i]; pr[i] = (p >> 1) & 7; pg[i] = (p >> 5) & 7; pb[i] = (p >> 9) & 7; }
    for (i = 0; i < 256; i++) penmap[i][0] = 0xFF;
    for (i = 0; i < HUD_SLOTS; i++) { slot_key[i] = 0xFFFF; slot_group[i] = 0; }
    slot_used = 0; hud_full = 1;
    hud_text_reset();
    for (i = 0; i < 1024; i++) hint[i] = 0xFF;
    { u16 *p = &shadow[0][0]; for (i = 0; i < 3 * 36; i++) p[i] = 0xFFFF; }
    { u32 *p = &last[0][0]; for (i = 0; i < 3 * 18; i++) p[i] = 0xA5A5A5A5u; }   /* force a full first pass */
}

/* One HUD row is checked per frame (a row changes at most a few times a
 * second, so the up-to-3-frame delay is invisible), two cells at a time with
 * word-aligned long reads (fine on the 68000); the redraw of a changed pair
 * is kept out of line so the compare loop stays in registers. The original
 * cell-by-cell scan of all three rows cost about 35 scan lines per frame. */
static __attribute__((noinline)) void redraw_pair(u16 r, u16 c, const u8 *cell) {
    u16 k;
    for (k = 0; k < 2; k++, c++, cell += 2) {
        u16 key = (u16)(cell[0] | cell[1] << 8);
        if (key != shadow[r][c]) {
            u8 attr = cell[1]; u16 code = (u16)(cell[0] | ((attr & 1) << 8));
            u8 group = (u8)(((attr & 0x1E) >> 1) + ((code & 0x1E0) >> 1));
            u16 tile = char_tile(code, group);
            shadow[r][c] = key;
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL3, 1, (attr >> 7) & 1, (attr >> 6) & 1, tile), 2 + c, mrow[r]);
        }
    }
}
static u16 hud_row;
static void cache_reset(void) {
    u16 i;
    for (i = 0; i < HUD_SLOTS; i++) slot_key[i] = 0xFFFF;
    for (i = 0; i < 1024; i++) hint[i] = 0xFF;
    { u16 *p = &shadow[0][0]; for (i = 0; i < 3 * 36; i++) p[i] = 0xFFFF; }
    { u32 *p = &last[0][0]; for (i = 0; i < 3 * 18; i++) p[i] = 0xA5A5A5A5u; }
    slot_used = 0; cache_full = 0; hud_full = 1;
    hud_text_reset();
}
void hud_frame(void) {
    u16 r = hud_row, rows = 1, budget = 3;   /* at most 3 changed pairs per frame (score roll-overs) */
    if (cache_full) cache_reset();
    if (hud_full) { hud_full = 0; r = 0; rows = 3; budget = 0xFFFF; }
    for (; rows; rows--, r = r == 2 ? 0 : r + 1) {
        u16 n = 18;
        const u32 *v = (const u32 *)(R6 + (arow[r] << 7) + 3 * 2);
        u32 *l = last[r];
        hud_row = r == 2 ? 0 : r + 1;
        do {
            if (*v != *l) {
                if (!budget) { hud_row = r; return; }     /* finish this row next frame */
                budget--; *l = *v; redraw_pair(r, (u16)(2 * (18 - n)), (const u8 *)v);
            }
            v++; l++;
        } while (--n);
    }
}

/* Playfield text: arcade foreground rows 4..28 hold only text while playing
 * (the scenery is native), e.g. the lives row (4) and the trip intro. Their
 * non-blank cells are drawn as 8x8 sprites with the same cached character
 * tiles. Half a row is re-checked per frame (long compares against the
 * last copy); rows 5..28 follow the arcade's foreground scroll (from 2215h). */
#define TXT_ROW0 4
#define TXT_ROWS 25
#define TXT_PER_ROW 10
static u32 tlast[TXT_ROWS][18];
static u8 tcount[TXT_ROWS], tcol[TXT_ROWS][TXT_PER_ROW];
static u16 tattr[TXT_ROWS][TXT_PER_ROW];
static u16 trow;
static u32 tmask;                /* rows with text */
static u8 tfull;                 /* scan every row once after a reset */
void hud_text_reset(void) {
    u16 i;
    u32 *p = &tlast[0][0];
    for (i = 0; i < TXT_ROWS * 18; i++) p[i] = 0xA5A5A5A5u;
    for (i = 0; i < TXT_ROWS; i++) tcount[i] = 0;
    trow = 0; tmask = 0; tfull = 1;
}
/* returns 0 when the conversion budget ran out (row redone next time) */
static u8 text_row(u16 t) {
    const u8 *v = R6 + ((TXT_ROW0 + t) << 7) + 3 * 2;
    u16 c, n = 0;
    for (c = 0; c < 36; c++, v += 2) {
        u8 code = v[0], attr = v[1];
        if (code == 0xEE || (!code && !attr)) continue;
        if (n == TXT_PER_ROW) break;
        {
            u16 full = (u16)(code | ((attr & 1) << 8));
            u8 group = (u8)(((attr & 0x1E) >> 1) + ((full & 0x1E0) >> 1));
            u16 tile = char_tile(full, group);
            if (!tile) return 0;
            tcol[t][n] = (u8)c;
            tattr[t][n] = TILE_ATTR_FULL(PAL3, 1, (attr >> 7) & 1, (attr >> 6) & 1, tile);
            n++;
        }
    }
    tcount[t] = (u8)n;
    return 1;
}
u16 hud_text(u16 index) {
    u16 t;
    /* half a row per frame (a text change shows within 50 frames); all rows
     * on the first frame after a reset */
    u8 full = tfull; u16 rows = full ? TXT_ROWS * 2 : 1;
    tfull = 0;
    for (; rows; rows--) {
        const u32 *v; u32 *l; u16 i, changed = 0, half;
        half = trow & 1; t = trow >> 1; trow = trow == TXT_ROWS * 2 - 1 ? 0 : trow + 1;
        v = (const u32 *)(R6 + ((TXT_ROW0 + t) << 7) + 3 * 2) + half * 9; l = tlast[t] + half * 9;
        for (i = 0; i < 9; i++) if (v[i] != l[i]) { l[i] = v[i]; changed = 1; }
        if (changed) {
            u8 ok;
            conv_left = full ? 255 : 2;
            ok = text_row(t);
            conv_left = 255;
            if (!ok) { l[0] = ~l[0]; continue; }      /* finish this row later */
            if (tcount[t]) tmask |= 1ul << t; else tmask &= ~(1ul << t);
        }
    }
    if (tmask) {
        VDPSprite *spr = &vdpSpriteCache[index];
        s16 sc = (s16)((((u16)(R6[0x2215]<<8|R6[0x2216])) + 0xFF58) & 0x1FF);
        u32 m = tmask;
        for (t = 0; m; t++, m >>= 1) {
            u16 n, i; s16 y, dx;
            if (!(m & 1)) continue;
            n = tcount[t]; y = (s16)(((TXT_ROW0 + t) - 2) << 3) + 128;
            dx = (t && !(g_state == 3 && TXT_ROW0 + t == 14)) ? -sc : 0;   /* GAME OVER (game.c) stays centred */
            for (i = 0; i < n && index < 76; i++) {
                s16 x = (s16)((tcol[t][i] << 3) + 16 + dx);
                if (x <= -8) x += 512;
                if (x >= 320) continue;
                spr->y = y; spr->size = SPRITE_SIZE(1, 1); spr->link = (u8)(index + 1);
                spr->attribut = tattr[t][i]; spr->x = x + 128;
                spr++; index++;
            }
        }
    }
    return index;
}
