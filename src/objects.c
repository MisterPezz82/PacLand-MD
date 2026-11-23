/* Native sprite builder for the recompiled arcade objects.
 *
 * This is a translation of the arcade sprite-list builder C1FB/C2E7 that
 * emits Mega Drive sprites directly instead of the arcade's three sprite RAM
 * arrays (the recompiled C1FB is still used by the host differential tests).
 * Rules kept from the original:
 *   - object slots are visited in order: static list (2400, count <$DC), then
 *     the round list (2800, count <$DD);
 *   - the arcade's visibility window is (X - centre) + A0h in [0, 140h), where
 *     centre = Pac-Man X + 20h ahead in the travel direction (<$8E), and its
 *     Y high byte (+09) is zero. The native renderer broadens this coarse
 *     window to +/- E0h, then checks actual 320-pixel screen bounds below:
 *     the arcade window clips the right edge prematurely on return trips;
 *   - objects with attribute bit 40h go to the 8 entries 0..7 (when full they
 *     fall back to the normal list), others to the 32 entries 8..39; when the
 *     normal list is full the round list stops;
 *   - entry code/colour come from +04/+05, flags (flip, size, code bit 8)
 *     from +06, Y = +0A + size offset, X = (X - centre) + size offset, with
 *     the size offset table at C32F;
 *   - later entries are drawn over earlier ones (MAME draw order).
 * Pac-Man (slot 2400) is drawn by the native renderer and skipped here.
 *
 * Positions are relative to Pac-Man: native X = object X - Pac-Man X +
 * (entry X offset - Pac-Man's offset 199) + native Pac-Man left edge. */
#pragma GCC optimize ("O2")
#include "game.h"
#include "native.h"
#include "recomp_rt.h"
#include "recomp_data.h"
#include "options.h"
#include "build_config.h"
#define frame_no object_frame_no
#define list object_draw_list
#define fast_entries object_fast_entries
#define fast_variant object_fast_variant
#define fast_failed_misses object_fast_failed_misses
#define prepare_entry object_prepare_entry

#define WAYS 4
#define MAX_SLOTS 300
/* Queue a VRAM tile upload without VDP_loadTileData's layers (about a third
 * of the cost). Sources never cross a 128 KB bank: pool cells are 128-byte
 * aligned and staging buffers are in work RAM. */
#define TILE_DMA(src, tile, n) DMA_queueDmaFast(DMA_VRAM, (void *)(src), (u16)((tile) << 5), (u16)((n) << 4), 2)
#define MAX_MISSES 1       /* cells converted by the CPU per frame (rare: not in ROM) */
#define MAX_ROM_LOADS 12   /* 1536 bytes; packed common blocks use one DMA */
static u16 cache_base, cache_sets;
static u16 cache_key[MAX_SLOTS];
static u16 cache_used[MAX_SLOTS];
#define HINTS 1024
static u16 hint[HINTS];        /* hash(code, colour) -> cache way + 1 that last held it */
#define HINT(code, group) (((code) ^ ((group) << 4)) & (HINTS - 1))
u16 frame_no;
/* Multi-cell entries (2x1, 1x2, 2x2 cells) are cached as one block of up to
 * 16 tiles laid out for a single VDP sprite (column-major), so a 32x32 ghost
 * car is one sprite and one cache lookup instead of four. */
#define BWAYS 8
#define MAX_BSLOTS 64
static u16 bcache_base, bcache_sets;
static u32 bcache_key[MAX_BSLOTS];
static u16 bcache_used[MAX_BSLOTS];
static u8 bhint[HINTS];
#define BKEY(code, s, group) ((u32)(code) | (u32)(s) << 9 | (u32)(group) << 11)
static u32 staging[MAX_MISSES][32];
static u32 bstaging[4][32];     /* cells of the one block converted in a frame */
static u16 bstaging_frame;      /* frame whose block conversion owns bstaging */
#define OVL_KEY 0x20000000UL     /* front-tree overlay block (see overlay_pass) */
#define OVL_MAX_WAYS 4           /* overlay blocks resident at once: bodies keep the rest */
static u16 ovl_ways;
#define BLUTS 4
static u8 bluts[BLUTS][256];   /* byte (two pens) -> two MD pens, per colour group */
static u16 blut_keys[BLUTS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
static u8 blut_next;
static void finfo_init(void);
u16 g_object_sprites, g_object_misses, g_object_deferred, g_object_rom_loads;
static u16 rom_loads;
static u8 fairy_palette;
static void fast_entries_reset(void);
#ifdef PL_BLAST
static void native_owners(void);
#endif
static u8 regional_sprite;
extern const u16 fairy_pal[32][16],fairy_cell_start[32];
extern const u32 fairy_cells[];
extern const u16 queen_cell_start[32],queen_alt_cell_start[32];
extern const u32 queen_cells[];
extern const u16 frightened_cells[32][40];
extern const u32 frightened_pool[];
extern const u16 frightened_blocks[32][10];
extern const u32 frightened_block_pool[];
extern const u16 bridge_blocks[32][4];
extern const u32 bridge_pool[];
extern const u16 door_scene_pal[32][64];
void objects_scene_palette(u8 scene) {
    fairy_palette=scene==2;
    if(scene==1)PAL_setColors(0,door_scene_pal[g_round],64,DMA_QUEUE);
    else{
        PAL_setColors(0,stages[g_round].palette,48,DMA_QUEUE);
        PAL_setPalette(PAL3,scene==2?fairy_pal[g_round]:sprite_pal[g_round],DMA_QUEUE);
    }
}
#define CELL_KEY(code, group) ((u16)((code)|((group)<<9)|((regional_sprite||(fairy_palette&&((group)==0x34||(group)==0x32||(group)==0x33)))?0x8000:0)))
#ifdef PL_BLAST
u16 object_candidate_offsets[48],object_candidate_count;
#define cand object_candidate_offsets
#define ncand object_candidate_count
#define CAND_SET(i,s) (cand[i]=(u16)((s)-(R6+0x2400)))
#define CAND_SLOT(i) (R6+0x2400+cand[i])
#else
static const u8 *cand[48];
static u8 ncand;
#define CAND_SET(i,s) (cand[i]=(s))
#define CAND_SLOT(i) cand[i]
#endif
static u8 cand_age, cand_count;

/* Pre-converted cell for this round from ROM (prepare_recomp.py), or 0. */
static const u32 *rom_cell(u16 code, u16 group) {
    if(regional_sprite)return 0;
    if(fairy_palette&&group==0x34&&code>=0x1E0&&code<0x1F0)
        return fairy_cells+((u32)(fairy_cell_start[g_round]+code-0x1E0)<<5);
    if(fairy_palette&&(group==0x32||group==0x33)&&code>=0x1D0&&code<0x1E0)
        return queen_cells+((u32)((group==0x32?queen_cell_start[g_round]:queen_alt_cell_start[g_round])+code-0x1D0)<<5);
    /* cell_hash_mask: low 12 bits = table size - 1, top 4 = 16 - log2(size) */
    u16 key = (u16)(code | (group << 9)), m = cell_hash_mask[g_round], mask = m & 0x0FFF;
    const u32 *t = cell_hash + cell_hash_start[g_round];
    u16 h = (u16)((u16)(key * 40503u) >> (m >> 12));      /* as cell_hash() in prepare_recomp.py */
    for (;;) {
        u32 e = t[h];
        if ((u16)(e >> 16) == key) return cell_pool + ((u32)(u16)e << 5);
        if (e == 0xFFFFFFFF) {
            if(code>=0xAC&&code<0xB4&&group>=6&&group<=18&&(group-6)%3==0)
                return frightened_pool+((u32)frightened_cells[g_round][((group-6)/3)*8+code-0xAC]<<5);
            return 0;
        }
        h = (u16)((h + 1) & mask);
    }
}

static const s8 size_y[4] = { -15, -15, -23, -23 };   /* C32F: 241, 241, 233, 233 */
static const s16 size_x[4] = { 207, 199, 207, 199 };
/* The Japanese program uses colour group 17h for its cap (8E7B), whereas
 * the World program writes group 00h (8E94). Keep gameplay RAM unchanged
 * while selecting the matching palette for the optional Japanese graphics. */
static u16 display_colour(const u8 *slot){
    u16 col=slot[5]&63;
    if(opt_region&&japan_art_available&&slot==R6+0x2420&&!col&&slot[4]==0x3C&&(slot[6]&0x80))return 0x17;
    return col;
}

#ifdef PL_BLAST
static void convert(u32 *dst,u16 code,u16 group);
typedef struct {u32 key;u16 tile;} ResidentFrame;
extern const ResidentFrame blast_atlas[];
extern const u16 blast_atlas_start[32],blast_atlas_count[32],blast_atlas_tiles[32];
extern const u8 blast_atlas_lookup[32][128];
static const ResidentFrame *resident_frames;
static const u8 *resident_lookup;
static u16 resident_first,resident_end;
u16 g_blast_resident_tiles;
static u16 resident_frame(u16 code,u8 size,u16 group){
 u32 key;u16 h,n,fold;
 if(!resident_end||(fairy_palette&&(group==0x34||group==0x32||group==0x33)))return 0;
 key=(u32)code|((u32)size<<9)|((u32)group<<11)|(regional_sprite?0x40000000:0);
 fold=(u16)key^(u16)(key>>16);h=(u16)(fold*40503u)>>9;
 while((n=resident_lookup[h])){
  --n;if(resident_frames[n].key==key)return resident_first+(resident_frames[n].tile&0x7FFF);
  h=(h+1)&127;
 }
 return 0;
}
static u16 resident_load(u16 first){
 u16 i;resident_first=first;resident_end=0;g_blast_resident_tiles=0;
 resident_frames=blast_atlas+blast_atlas_start[g_round];resident_lookup=blast_atlas_lookup[g_round];
 for(i=0;i<blast_atlas_count[g_round];i++)if(resident_frames[i].tile&0x8000){
  u32 key=resident_frames[i].key;u16 code=key&511,group=(key>>11)&63,size=(key>>9)&3;
  u16 cx,cy,H=(1+(size>>1))*2,base=first+(resident_frames[i].tile&0x7FFF);
  regional_sprite=(key&0x40000000)!=0;
  for(cx=0;cx<=((size&1)?1:0);cx++)for(cy=0;cy<1+(size>>1);cy++){
   const u32 *st=rom_cell(code+2*cy+cx,group);u16 t=base+cx*H*2+cy*2;
   if(!st){convert(bstaging[0],code+2*cy+cx,group);st=bstaging[0];}
   VDP_loadTileData(st,t,2,CPU);VDP_loadTileData(st+16,t+H,2,CPU);
  }
 }
 regional_sprite=0;resident_end=first+blast_atlas_tiles[g_round];
 g_blast_resident_tiles=blast_atlas_tiles[g_round];return resident_end;
}
#endif

void objects_load(u16 first_free_tile) {
    u16 i, slots;
    fast_entries_reset();
#ifdef PL_BLAST
    native_owners();
#endif
    fairy_palette=0;
#ifdef PL_BLAST
    first_free_tile=resident_load(first_free_tile);
#endif
    {   /* split the free VRAM: blocks get up to half, in 16-tile slots */
        u16 total = 1400 - first_free_tile, bslots = (u16)(total * 3 / 5 / 16);
        if (bslots > MAX_BSLOTS) bslots = MAX_BSLOTS;
        bcache_sets = bslots / BWAYS;                 /* any count: set = hash % sets on a miss */
        if (!bcache_sets) bcache_sets = 1;
        bcache_base = first_free_tile;
        first_free_tile += bcache_sets * BWAYS * 16;
        for (i = 0; i < MAX_BSLOTS; i++) { bcache_key[i] = 0xFFFFFFFF; bcache_used[i] = 0; }
        ovl_ways = 0;
        for (i = 0; i < HINTS; i++) bhint[i] = 0;
    }
    cache_base = first_free_tile;
    slots = (1400 - first_free_tile) / 4;
    if (slots > MAX_SLOTS) slots = MAX_SLOTS;
    cache_sets = slots / WAYS;                       /* any count: set = hash % sets on a miss */
    if (!cache_sets) cache_sets = 1;
    for (i = 0; i < MAX_SLOTS; i++) { cache_key[i] = 0xFFFF; cache_used[i] = 0; }
    for (i = 0; i < HINTS; i++) hint[i] = 0;
    { u8 k; for (k = 0; k < BLUTS; k++) blut_keys[k] = 0xFFFF; }
    finfo_init();
    cand_age = 0; cand_count = 0xFF;
    PAL_setPalette(PAL3, sprite_pal[g_round], CPU);
}

/* Pixel conversion through a byte lookup table (two pixels per entry), rebuilt
 * only when the colour group or round changes: no per-pixel long shifts. */
static void convert(u32 *dst, u16 code, u16 group) {
    u16 key = (u16)(group | (g_round << 8)), i;
    const u8 *src = (const u8 *)((regional_sprite?japan_sprites:arcade_sprites) + code * 32);
    u8 *out = (u8 *)dst, *blut = 0, k;
    for (k = 0; k < BLUTS; k++) if (blut_keys[k] == key) { blut = bluts[k]; break; }
    if (!blut) {
        u8 map[16], hi, lo; u16 p;
        const u8 *gp = group_pens[g_round] + group * 16;
        for (p = 0; p < 16; p++) map[p] = gp[p];
        k = blut_next; blut_next = (u8)((blut_next + 1) & (BLUTS - 1));
        blut = bluts[k]; blut_keys[k] = key;
        for (hi = 0; hi < 16; hi++) {
            u8 v = (u8)(map[hi] << 4), *row = blut + (hi << 4);
            for (lo = 0; lo < 16; lo++) row[lo] = v | map[lo];
        }
    }
    for (i = 0; i < 128; i += 4) {
        out[i] = blut[src[i]]; out[i + 1] = blut[src[i + 1]];
        out[i + 2] = blut[src[i + 2]]; out[i + 3] = blut[src[i + 3]];
    }
}

/* VRAM tile of the cell, 0 when it could not be loaded this frame. */
static u16 cell_slow(u16 code, u16 group, u16 *misses);
/* Fast path: the way that last held this code, checked against the full key. */
static inline u16 cell(u16 code, u16 group, u16 *misses) {
#ifdef PL_BLAST
    {u16 fixed=resident_frame(code,0,group);if(fixed)return fixed;}
#endif
    u16 h = hint[HINT(code, group)];
    if (h && cache_key[h - 1] == CELL_KEY(code,group)) { cache_used[h - 1] = frame_no; return cache_base + ((h - 1) << 2); }
    return cell_slow(code, group, misses);
}
static u16 cell_slow(u16 code, u16 group, u16 *misses) {
    u16 key = CELL_KEY(code,group);
    u16 set = (u16)((u16)(code ^ (code >> 5) ^ (group << 3) ^ (group >> 2)) % cache_sets);
    u16 base = set * WAYS, w, victim = base;
    for (w = base; w < base + WAYS; w++)
        if (cache_key[w] == key) { cache_used[w] = frame_no; hint[HINT(code, group)] = w + 1; return cache_base + w * 4; }
    if (*misses >= MAX_MISSES || rom_loads >= MAX_ROM_LOADS) { g_object_deferred++; return 0; }
    for (w = base; w < base + WAYS; w++) {          /* empty way, else least recently used */
        if (cache_key[w] == 0xFFFF) { victim = w; break; }
        if ((u16)(frame_no - cache_used[w]) > (u16)(frame_no - cache_used[victim])) victim = w;
    }
    if (cache_used[victim] == frame_no && cache_key[victim] != 0xFFFF) { g_object_deferred++; return 0; }
    {
        const u32 *src = rom_cell(code, group);
        if (src) rom_loads++;
        else { src = staging[*misses]; convert(staging[*misses], code, group); (*misses)++; }
        TILE_DMA(src, cache_base + victim * 4, 4);
    }
    cache_key[victim] = key; cache_used[victim] = frame_no; hint[HINT(code, group)] = victim + 1;
    return cache_base + victim * 4;
}


/* VRAM tile of a multi-cell block (size bits s: 1 = two cells wide, 2 = two
 * tall), 0 when it could not be loaded this frame. */
static u16 block(u16 code, u8 s, u16 group, u16 *misses) {
#ifdef PL_BLAST
    {u16 fixed=resident_frame(code,s,group);if(fixed)return fixed;}
#endif
    u32 key = BKEY(code, s, group);
    if(fairy_palette&&(group==0x34||group==0x32||group==0x33))key|=0x80000000;
    if(regional_sprite)key|=0x40000000;
    u16 hh = (u16)((code ^ (group << 4) ^ (s << 8)) & (HINTS - 1)), h = bhint[hh];
    u16 set, base, w, victim, cells, H, cx, cy;
    if (h && bcache_key[h - 1] == key) { bcache_used[h - 1] = frame_no; return bcache_base + ((h - 1) << 4); }
    set = (u16)((u16)(code ^ (code >> 5) ^ (group << 3) ^ (group >> 2) ^ (s << 2)) % bcache_sets);
    base = set * BWAYS; victim = base;
    for (w = base; w < base + BWAYS; w++)
        if (bcache_key[w] == key) { bcache_used[w] = frame_no; bhint[hh] = (u8)(w + 1); return bcache_base + (w << 4); }
    cells = (u16)((1 + (s & 1)) * (1 + (s >> 1)));
    /* cells come from the ROM pool (DMA only) or are converted by the CPU;
     * a block needing conversions is built only when nothing else was
     * converted this frame (then it uses its own staging buffers) */
    {
        const u32 *src[4]; u16 conv = 0;
        if (rom_loads + cells > MAX_ROM_LOADS) { g_object_deferred++; return 0; }
        for (w = base; w < base + BWAYS; w++) {
            if (bcache_key[w] == 0xFFFFFFFF) { victim = w; break; }
            if ((u16)(frame_no - bcache_used[w]) > (u16)(frame_no - bcache_used[victim])) victim = w;
        }
        if (bcache_used[victim] == frame_no && bcache_key[victim] != 0xFFFFFFFF) { g_object_deferred++; return 0; }
        if(s==3&&(code==0xAC||code==0xB0)&&!regional_sprite){
            u16 g=group==6?0:group==9?2:group==12?4:group==15?6:group==18?8:0xFFFF;
            if(g!=0xFFFF){
                u16 n=frightened_blocks[g_round][g+(code==0xB0)];
                TILE_DMA(frightened_block_pool+((u32)n<<7),bcache_base+(victim<<4),16);
                rom_loads+=4;if(bcache_key[victim]&OVL_KEY&&bcache_key[victim]!=0xFFFFFFFF)ovl_ways--;bcache_key[victim]=key;bcache_used[victim]=frame_no;bhint[hh]=(u8)(victim+1);
                return bcache_base+(victim<<4);
            }
        }
        /* These repeated bridge pieces dominate the return-trip log field.
         * Their packed ROM form also avoids four half-cell DMA commands. */
        if(s==2&&group==0x19&&(code&0xFFFA)==0x40&&!regional_sprite){
            u16 n=(u16)(((code>>1)&2)|(code&1));
            TILE_DMA(bridge_pool+((u32)bridge_blocks[g_round][n]<<6),bcache_base+(victim<<4),8);
            rom_loads+=2;if(bcache_key[victim]&OVL_KEY&&bcache_key[victim]!=0xFFFFFFFF)ovl_ways--;bcache_key[victim]=key;bcache_used[victim]=frame_no;bhint[hh]=(u8)(victim+1);
            return bcache_base+(victim<<4);
        }
        for (cy = 0; cy <= (u16)(s >> 1); cy++) for (cx = 0; cx <= (u16)(s & 1); cx++) {
            const u32 *st = rom_cell(code + (cy << 1) + cx, group);
            src[cy * 2 + cx] = st; if (!st) conv++;
        }
        if (conv && *misses) { g_object_deferred++; return 0; }
        /* VDP sprites use complete tile columns. Interleave the two-tile
         * columns from each 16x16 cell, so tall blocks need one sprite.
         * Sources remain in the shared ROM cell pool; no duplicate art. */
        H = (u16)((1 + (s >> 1)) << 1);                 /* tiles per full column */
        for (cy = 0; cy <= (u16)(s >> 1); cy++) for (cx = 0; cx <= (u16)(s & 1); cx++) {
            const u32 *st = src[cy * 2 + cx];
            u16 t = bcache_base + (victim << 4) + cx * H * 2 + cy * 2;
            if (st) rom_loads++;
            else { st = bstaging[cy * 2 + cx]; convert(bstaging[cy * 2 + cx], code + (cy << 1) + cx, group); }
            if(s&2){TILE_DMA(st,t,2);TILE_DMA(st+16,t+H,2);}
            else TILE_DMA(st,t,4);
        }
        if (conv) { *misses += MAX_MISSES; bstaging_frame = frame_no; } /* no further conversions this frame */
        if(bcache_key[victim]&OVL_KEY&&bcache_key[victim]!=0xFFFFFFFF)ovl_ways--;bcache_key[victim] = key; bcache_used[victim] = frame_no; bhint[hh] = (u8)(victim + 1);
        return bcache_base + (victim << 4);
    }
}

#ifdef PL_BLAST
/* Scene slots and native render records have the same 32-byte stride. A
 * compact offset addresses both; no pointer subtraction in the SAT writer. */
typedef struct { u16 offset; s16 dx; } Entry;
#define ENTRY_SLOT(e) (R6+0x2400+(e)->offset)
#define ENTRY_SET(e,s) ((e)->offset=(u16)((s)-(R6+0x2400)))
_Static_assert(sizeof(Entry)==4,"native draw command stride");
#else
typedef struct { const u8 *slot; s16 dx; u16 pad; } Entry;
#define ENTRY_SLOT(e) ((e)->slot)
#define ENTRY_SET(e,s) ((e)->slot=(s))
#endif
Entry list[40]; /* fixed-capacity, non-reentrant VDP command list */
typedef struct {
 u32 appearance,marker;const void *owner;u16 *used;
 u16 attr,xspan;
 u8 xoff,yc,wm1,hm1,yspan,mode,block,rows,size,variant;
 s8 rowt;u8 pad;
} FastEntry;
_Static_assert(sizeof(FastEntry)==32,"sprite command stride must match arcade slots");
FastEntry fast_entries[96];
u16 fast_variant,fast_failed_misses;
u16 g_object_fast_hits,g_object_fast_disable;
static void fast_entries_reset(void){u16 i;for(i=0;i<96;i++)fast_entries[i].variant=0xFF;}
#ifdef PL_BLAST
void objects_native_invalidate(void){u16 i;for(i=0;i<96;i++)fast_entries[i].variant=0xFF;}
void objects_native_suspend(void){u16 i;for(i=0;i<96;i++){fast_entries[i].variant=0xFF;fast_entries[i].pad=0;}}
static void native_owners(void){
 u16 i,pass;for(i=0;i<96;i++)fast_entries[i].pad=0;
 for(pass=0;pass<2;pass++){
  u16 count=R6[pass?0x20DD:0x20DC];const u8 *o=R6+(pass?0x2800:0x2400);
  for(i=0;i<count;i++,o+=32){
   u16 start=(u16)(o-(R6+0x2400))>>5,n=0,j;
   if(!o[1]){if(start+1<96)fast_entries[start+1].pad=1;}
   else if(o[1]==30)n=6;else if(o[1]==32)n=12;
   for(j=0;j<n&&start+j<96;j++)fast_entries[start+j].pad=1;
  }
 }
}
#endif

/* Per flags byte (+06): size bits, code bit 8 and size mask, flip attribute,
 * VDP size, x/y offsets (C32F table) and on-screen test spans. */
typedef struct {
    u16 xspan, codebit, mask, attr;
    u8 xoff, wm1, hm1, yspan, yc, s, size, pad;
} FlagInfo;
_Static_assert(sizeof(FlagInfo)==16,"flag lookup uses a compact power-of-two stride");
static FlagInfo finfo[256];
static void finfo_init(void) {
    u16 fl;
    for (fl = 0; fl < 256; fl++) {
        FlagInfo *f = &finfo[fl];
        u8 s = (fl >> 2) & 3; u16 w = (s & 1) ? 32 : 16, h = (s & 2) ? 32 : 16;
        f->s = s; f->codebit = (u16)((fl & 0x80) << 1); f->mask = (u16)~s;
        f->xoff = size_x[s]; f->yc = (u8)(265 - h - size_y[s]);
        f->wm1 = w - 1; f->xspan = (u16)(318 + w); f->hm1 = h - 1; f->yspan = (u16)(222 + h);
        f->attr = (u16)TILE_ATTR(PAL0, 0, (fl & 2) != 0, fl & 1);   /* line from group_line[] */
        f->size = (u8)SPRITE_SIZE(w >> 3, h >> 3);
    }
}

/* Reuse the complete decoded draw commands while the ordered object list and
 * graphics stay the same. Positions and visibility are still tested every
 * frame, and every cache owner is checked before its VRAM tile is reused. */
u16 __attribute__((noinline)) prepare_entry(FastEntry *f,const u8 *slot,s16 dx,s16 xoff,u16 *misses){
 const FlagInfo *fi=&finfo[slot[6]];u16 col=display_colour(slot),code,tile,attr,owner;s16 sx,sy;
 f->appearance=*(const u32 *)(slot+4);f->variant=(u8)fast_variant;f->mode=0;
 if(col==0x2F)return 1;
 f->mode=1;f->xoff=fi->xoff;f->yc=fi->yc;f->wm1=fi->wm1;f->xspan=fi->xspan;f->hm1=fi->hm1;f->yspan=fi->yspan;
 sx=(s16)(dx+xoff+f->xoff);sy=(s16)((u8)(f->yc-slot[10])-32);
 if((u16)(sx+f->wm1)>f->xspan||(u16)(sy+f->hm1)>f->yspan)return 1;
 regional_sprite=(opt_region&&japan_art_available&&(slot==R6+0x2400||slot==R6+0x2420));
 code=(slot[4]|fi->codebit)&fi->mask;
 attr=fi->attr|((u16)group_line[g_round][col]<<13);
 if(fairy_palette&&(col==0x34||col==0x32||col==0x33))attr=fi->attr|((u16)PAL3<<13);
 tile=fi->s?block(code,fi->s,col,misses):cell(code,col,misses);
 if(!tile)return 0;
 f->block=fi->s!=0;f->rows=1;f->size=fi->size;
#ifdef PL_BLAST
 f->attr=(u16)(attr|tile);f->rowt=0;
 if(tile>=resident_first&&tile<resident_end){f->mode=f->pad?4:3;return 1;}
#endif
 if(fi->s){owner=(tile-bcache_base)>>4;f->owner=bcache_key+owner;f->used=bcache_used+owner;f->marker=bcache_key[owner];}
 else{owner=(tile-cache_base)>>2;f->owner=cache_key+owner;f->used=cache_used+owner;f->marker=cache_key[owner];}
 f->attr=(u16)(attr|tile);f->rowt=0;
 f->mode=2;return 1;
}
static u16 __attribute__((noinline)) fast_draw(u16 first,u16 lo,u16 hi,s16 xoff){
 const Entry *e=list+8+lo,*stop=list+8;
 VDPSprite *spr=&vdpSpriteCache[first];u16 index=first,part,misses=0;
 fast_failed_misses=0;
 for(part=0;part<2;part++){
  if(part){e=list+hi;stop=list;}
  while(e!=stop){
   const u8 *slot;FastEntry *f; s16 sx,sy;
   --e;slot=ENTRY_SLOT(e);
   f=(FastEntry *)((u8 *)fast_entries+(u16)(slot-(R6+0x2400)));
   if(
#ifdef PL_BLAST
      (f->mode!=4&&f->appearance!=*(const u32 *)(slot+4))||
#else
      f->appearance!=*(const u32 *)(slot+4)||
#endif
      f->variant!=fast_variant){
    if(!prepare_entry(f,slot,e->dx,xoff,&misses)){fast_failed_misses=misses;return 0;}
   }
   if(!f->mode)continue;
   sx=(s16)(e->dx+xoff+f->xoff);
   sy=(s16)((u8)(f->yc-slot[10])-32);
   if((u16)(sx+f->wm1)>f->xspan||(u16)(sy+f->hm1)>f->yspan)continue;
   if(
#ifdef PL_BLAST
      f->mode<3&&
#endif
      (f->mode!=2||(f->block?*(const u32 *)f->owner!=f->marker:*(const u16 *)f->owner!=(u16)f->marker))){
    if(!prepare_entry(f,slot,e->dx,xoff,&misses)){fast_failed_misses=misses;return 0;}
   }
   if(index+f->rows>78){fast_failed_misses=misses;return 0;}
#ifdef PL_BLAST
   if(f->mode<3)
#endif
   *f->used=frame_no;
   spr->y=sy+128;spr->size=f->size;spr->link=(u8)(index+1);
   spr->attribut=f->attr;spr->x=sx+128;
   spr++;index++;
   if(f->rows==2){
    spr->y=sy+144;spr->size=f->size;spr->link=(u8)(index+1);
    spr->attribut=(u16)(f->attr+f->rowt);spr->x=sx+128;spr++;index++;
   }
  }
 }
 g_object_misses=misses;return index+1;
}

#ifdef PL_BLAST
/* Behavioural reference for the compact collector, also used by diagnostic
 * full/C writers. The native writer receives the same ordered commands. */
u32 object_collect_c(u32 centre){
 u16 hi=0,lo=0,pass;
    for (pass = 0; pass < 2; pass++) {
        const u8 *slot = R6 + 0x2400; u16 ci=0;
        u16 count = pass ? ncand : R6[0x20DC];
        for (; count; count--, slot += 0x20) {
            s16 d;
            if (pass) slot = CAND_SLOT(ci++);
            if (slot[0x09]) continue;
            d = (s16)(PW16(slot + 0x0C) - centre);
            if ((u16)(d + 0xE0) >= 0x1C0) continue;
            if (slot == R6 + 0x2400 && g_pac_native) continue;   /* Pac-Man: native art this frame */
            if ((slot[0x06] & 0x40) && hi < 8) { ENTRY_SET(&list[hi],slot); list[hi].dx = d; hi++; continue; }
            ENTRY_SET(&list[8 + lo],slot); list[8 + lo].dx = d; lo++;
            if (lo == 32) { pass = 2; break; }
        }
    }
 return ((u32)hi<<16)|lo;
}
u32 object_collect_68k(u32 centre);
#endif

#ifdef PL_BLAST
/* Front-scenery overlays. The arcade draws sprite pixels whose colour-table
 * entry is opaque below 80h, or F0h-FEh, over the high-priority foreground
 * (MAME pacland transmask). Elsewhere those pixels are ordinary sprite pixels.
 * A ghost that overlaps a visible high-priority foreground tile gets a second,
 * high-priority VDP sprite holding only those pens, linked immediately in
 * front of its own body. Its other pixels stay low priority, so the VDP hides
 * them behind the trees. Unoccluded pixels are identical, so nothing changes
 * in the open, and no per-pixel work happens per frame: overlay blocks are
 * converted once and then live in the ordinary block cache. */
#define OVL_BLANK 128
static s16 ovl_round=-1,ovl_first,ovl_last;
static u32 ovl_empty[16];
u16 g_overlay_sprites,g_overlay_disable,g_overlay_loads;
extern const u8 sprite_lut[1024];
static void ovl_prepare(void){
 const Stage *s=&stages[g_round];u16 c,r;
 ovl_first=0x7FFF;ovl_last=-1;
 for(c=0;c<s->fg_columns;c++){const u16 *q=s->fg+c*24;
  for(r=0;r<24;r++)if((q[r]&0x8000)&&(q[r]&0x7FF)!=OVL_BLANK){if((s16)c<ovl_first)ovl_first=c;ovl_last=c;break;}}
 for(c=0;c<16;c++)ovl_empty[c]=0xFFFFFFFF;
 ovl_round=g_round;
}
static u8 ovl_covered(s16 sx,s16 sy,u16 w,u16 h){
 const Stage *s=&stages[g_round];
 s16 x0=(s16)(g_camera_fg+sx)>>3,x1=(s16)(g_camera_fg+sx+w-1)>>3,r0=(sy>>3)-3,r1=((sy+(s16)h-1)>>3)-3,c,r;
 if(x1<ovl_first||x0>ovl_last)return 0;
 if(r0<0)r0=0;
 if(r1>23)r1=23;
 if(x0<0)x0=0;
 if(x1>=(s16)s->fg_columns)x1=s->fg_columns-1;
 for(c=x0;c<=x1;c++){const u16 *q=s->fg+c*24;
  for(r=r0;r<=r1;r++){u16 v=q[r];if((v&0x8000)&&(v&0x7FF)!=OVL_BLANK)return 1;}}
 return 0;
}
/* Ghost colour groups (walkers, vehicles, edible purple and flash). Log and
 * scenery groups are excluded: overlaying them costs frames on busy rounds. */
#define OVL_GROUP(g) (((g)>=4&&(g)<=0x12)||(g)==0x39||(g)==0x3A)
extern const u16 ovl_mask_count,ovl_mask_keys[];
extern volatile u16 g_pipe_work_last __attribute__((weak));
extern const u32 ovl_masks[];
/* Index of the precomputed mask for a 32x32 ghost block, or -1. */
static s16 ovl_mask_find(u16 code,u16 group){
 u16 key=(u16)(code|(group<<9));s16 lo=0,hi=(s16)ovl_mask_count-1;
 while(lo<=hi){s16 mid=(lo+hi)>>1;u16 k=ovl_mask_keys[mid];if(k==key)return mid;if(k<key)lo=mid+1;else hi=mid-1;}
 return -1;
}
/* Overlay block tile, 0 when not loaded this frame, 0xFFFF when it has no
 * front pixels at all. One conversion per frame, own staging buffers. */
static u16 oblock(u16 code,u8 s,u16 group,u16 *budget){
 u32 key=BKEY(code,s,group)|OVL_KEY;u16 e=(u16)(key^(key>>9)^(key>>17))&15,set,base,w,victim,H,cx,cy,i;
 if(ovl_empty[e]==key)return 0xFFFF;
 set=(u16)((u16)(code^(code>>5)^(group<<3)^(group>>2)^(s<<2))%bcache_sets);base=set*BWAYS;victim=base;
 for(w=base;w<base+BWAYS;w++)if(bcache_key[w]==key){bcache_used[w]=frame_no;return bcache_base+(w<<4);}
 if(!*budget||bstaging_frame==frame_no)return 0; /* staging busy: next frame */
 if(rom_loads+4>MAX_ROM_LOADS)return 0;            /* frame's transfer budget spent */
 /* The adaptive presenter publishes one transfer packet per frame of at most
  * 2048 bytes; a larger queue falls back to a late frame. Leave room for this
  * 512-byte block and the sprite table (up to 640 bytes) queued after objects. */
 if(DMA_getQueueTransferSize()+512+640>2048)return 0;
 /* Only on frames with headroom: the adaptive presenter's measured work for
  * the previous frame (1280 sub-ticks per NTSC field, busy from 900). A busy
  * moment just shows the outline a few frames later, never a late frame. */
 if(&g_pipe_work_last&&g_pipe_work_last>=800)return 0;
 if(ovl_ways>=OVL_MAX_WAYS){        /* at the cap: only replace another overlay */
  u16 found=0;
  for(w=base;w<base+BWAYS;w++)if(bcache_key[w]!=0xFFFFFFFF&&(bcache_key[w]&OVL_KEY)&&
     (!found||(u16)(frame_no-bcache_used[w])>(u16)(frame_no-bcache_used[victim]))){victim=w;found=1;}
  if(!found)return 0;
 }else for(w=base;w<base+BWAYS;w++){
  if(bcache_key[w]==0xFFFFFFFF){victim=w;break;}
  if((u16)(frame_no-bcache_used[w])>(u16)(frame_no-bcache_used[victim]))victim=w;
 }
 /* never displace a block drawn in the last eight frames: bodies win */
 if(bcache_key[victim]!=0xFFFFFFFF&&(u16)(frame_no-bcache_used[victim])<8)return 0;
 (*budget)--;
 /* Masks are precomputed by tools/prepare_overlay_masks.py: outer edge, dark
  * line art and the arcade's front-layer pens. Copy (or convert) each cell
  * and AND it with the mask; no per-pixel work on the 68000. */
 {s16 mi=s==3?ovl_mask_find(code,group):-1;const u32 *mk;
  if(mi<0){ovl_empty[e]=key;return 0xFFFF;}
  mk=ovl_masks+((u32)(u16)mi<<7);
  for(cy=0;cy<2;cy++)for(cx=0;cx<2;cx++){
   u32 *st=bstaging[cy*2+cx];const u32 *src=rom_cell(code+(cy<<1)+cx,group),*m=mk+((cy*2+cx)<<5);
   if(!src){convert(st,code+(cy<<1)+cx,group);src=st;}
   for(i=0;i<32;i++)st[i]=src[i]&m[i];
  }
 }
 H=(u16)((1+(s>>1))<<1);
 for(cy=0;cy<=(u16)(s>>1);cy++)for(cx=0;cx<=(u16)(s&1);cx++){
  const u32 *st=bstaging[cy*2+cx];u16 t=bcache_base+(victim<<4)+cx*H*2+cy*2;
  if(s&2){TILE_DMA(st,t,2);TILE_DMA(st+16,t+H,2);}else TILE_DMA(st,t,4);
 }
 g_overlay_loads++;bstaging_frame=frame_no;rom_loads+=4;
 if(bcache_key[victim]&OVL_KEY&&bcache_key[victim]!=0xFFFFFFFF)ovl_ways--;bcache_key[victim]=key;bcache_used[victim]=frame_no;ovl_ways++;
 return bcache_base+(victim<<4);
}
/* Walks the same ordered commands and visibility tests as the writer, so it
 * knows which SAT entry holds each object. Overlays are appended after the
 * bodies and linked in front of their own body: arcade order is preserved. */
static u16 overlay_pass(u16 first,u16 next,u16 lo,u16 hi,s16 xoff){
 const Entry *e=list+8+lo,*stop=list+8;u16 index=first,part,extra=next,budget=1;
 VDPSprite *sc=vdpSpriteCache;
 g_overlay_sprites=0;
 if(g_overlay_disable==1||fairy_palette)return next;
 if(ovl_round!=g_round)ovl_prepare();
 if(ovl_last<0||(s16)((g_camera_fg+320)>>3)<ovl_first||(s16)(g_camera_fg>>3)>ovl_last)return next;
 for(part=0;part<2;part++){
  if(part){e=list+hi;stop=list;}
  while(e!=stop){
   const u8 *slot;FastEntry *f;s16 sx,sy;
   --e;slot=ENTRY_SLOT(e);f=(FastEntry *)((u8 *)fast_entries+(u16)(slot-(R6+0x2400)));
   if(!f->mode)continue;
   sx=(s16)(e->dx+xoff+f->xoff);sy=(s16)((u8)(f->yc-slot[10])-32);
   if((u16)(sx+f->wm1)>f->xspan||(u16)(sy+f->hm1)>f->yspan)continue;
   if(f->rows==1&&f->block&&index&&extra<78){
    u16 col=display_colour(slot);
    if(OVL_GROUP(col)&&!(opt_region&&japan_art_available&&(slot==R6+0x2400||slot==R6+0x2420))
       &&ovl_covered(sx,sy,f->wm1+1,f->hm1+1)){
     const FlagInfo *fi=&finfo[slot[6]];u16 code=(u16)((slot[4]|fi->codebit)&fi->mask),tile;
     regional_sprite=0;
     tile=oblock(code,fi->s,col,&budget);
     if(tile&&tile!=0xFFFF){
      VDPSprite *o=&sc[extra];*o=sc[index];
      o->attribut=(u16)((sc[index].attribut&0x7800)|0x8000|tile);
      o->link=(u8)index;sc[index-1].link=(u8)extra;extra++;
     }
    }
   }
   index+=f->rows;
  }
 }
 if(extra!=next){sc[next-1].link=(u8)extra;g_overlay_sprites=extra-next;}
 return extra;
}
#endif
u16 object_fast_draw_68k(u16 first,u16 lo,u16 hi,s16 xoff);
u16 objects_draw(u16 first_index, s16 pac_x) {
    u16 hi = 0, lo = 0, misses = 0, index = first_index, list_no, pass;
    u16 px = R6W(0x240C);
    u16 centre = (u16)(px + (R6[0x208E] ? -0x20 : 0x20));
    u16 variant=(opt_region&&japan_art_available?1:0)|(fairy_palette?2:0);
    frame_no++; rom_loads = 0;
    /* C1FB: visibility, priority split and list limits */
    /* The level-object list (2800) is long and mostly far away: its
     * candidates (idle slots, and those within 140h of the centre, wider
     * than the A0h test) are
     * collected every 8 frames and only they are tested in between. Display
     * only: the arcade logic is not affected. */
    if (!cand_age-- || cand_count != R6[0x20DD]) {
        const u8 *slot = R6 + 0x2800; u16 count = R6[0x20DD];
        cand_count = (u8)count; ncand = 0; cand_age = 7;
        for (; count; count--, slot += 0x20)
            if (ncand < 48 && (!PW16(slot + 0x0C) ||                 /* idle: may be spawned any frame */
                               (u16)((s16)(PW16(slot + 0x0C) - centre) + 0x140) < 0x280)) CAND_SET(ncand++,slot);
    }
#ifdef PL_BLAST
    {u32 counts=g_object_fast_disable?object_collect_c(centre):object_collect_68k(centre);
     hi=(u16)(counts>>16);lo=(u16)counts;}
#else
    for (pass = 0; pass < 2; pass++) {
        const u8 *slot = R6 + 0x2400, *const *cp = cand;
        u16 count = pass ? ncand : R6[0x20DC];
        for (; count; count--, slot += 0x20) {
            s16 d;
            if (pass) slot = *cp++;
            if (slot[0x09]) continue;
            d = (s16)(PW16(slot + 0x0C) - centre);
            if ((u16)(d + 0xE0) >= 0x1C0) continue;
            if (slot == R6 + 0x2400 && g_pac_native) continue;   /* Pac-Man: native art this frame */
            if ((slot[0x06] & 0x40) && hi < 8) { ENTRY_SET(&list[hi],slot); list[hi].dx = d; hi++; continue; }
            ENTRY_SET(&list[8 + lo],slot); list[8 + lo].dx = d; lo++;
            if (lo == 32) { pass = 2; break; }
        }
    }
#endif
    fast_variant=variant;
    if(g_object_fast_disable!=1){
        s16 xoff=(s16)(centre-px)-199+pac_x;
        u16 next=g_object_fast_disable==2?fast_draw(first_index,lo,hi,xoff):object_fast_draw_68k(first_index,lo,hi,xoff);
        if(next){g_object_fast_hits++;
#ifdef PL_BLAST
         next=overlay_pass(first_index,next-1,lo,hi,xoff)+1;
#endif
         g_object_sprites=next-1-first_index;g_object_rom_loads=rom_loads;return next-1;}
        misses=fast_failed_misses;
    }
    /* draw: later arcade entries are in front -> emitted first. Sprites are
     * written straight into SGDK's sprite table (x/y offset 128). Everything
     * that depends only on the flags byte comes from finfo[]. */
    {
        s16 xoff = (s16)(centre - px) - 199 + pac_x;
        VDPSprite *spr = &vdpSpriteCache[index];
        const Entry *e = &list[8 + lo], *stop = &list[8];
        u8 part;
        for (part = 0; part < 2; part++) {
            if (part) { e = &list[hi]; stop = list; }
            while (e != stop) {
                const u8 *slot; const FlagInfo *fi;
                s16 sx, sy; u16 code, tile, attr; u8 col;
                e--;
                slot = ENTRY_SLOT(e);
                col = (u8)display_colour(slot);
                if(col==0x2F)continue; /* transparent placeholders still count in the arcade list */
                fi = &finfo[slot[0x06]];
                sx = (s16)(e->dx + xoff + fi->xoff);
                if ((u16)(sx + fi->wm1) > fi->xspan) continue;          /* -w < sx < 320 */
                sy = (s16)((u8)(fi->yc - slot[0x0A]) - 32);
                if ((u16)(sy + fi->hm1) > fi->yspan) continue;          /* -h < sy < 224 */
                if (index >= 78) {part = 2; break; }
                regional_sprite=(opt_region&&japan_art_available&&(slot==R6+0x2400||slot==R6+0x2420));
                code = (u16)((slot[0x04] | fi->codebit) & fi->mask);
                attr = fi->attr | ((u16)group_line[g_round][col] << 13);
                if(fairy_palette&&(col==0x34||col==0x32||col==0x33))attr=fi->attr|((u16)PAL3<<13);
                if (!fi->s) {
                    tile = cell(code, col, &misses);
                    if (!tile) {continue;}
                } else {
                    tile = block(code, fi->s, col, &misses);
                    if (!tile) {
                        /* block cache full this frame: one sprite per cell */
                        u8 cy, cx, s = fi->s, hf = slot[0x06] & 1, vf = (slot[0x06] >> 1) & 1;
                        for (cy = 0; cy <= (s >> 1); cy++) for (cx = 0; cx <= (s & 1); cx++) {
                            s16 dx = sx + (cx << 4), dy = sy + (cy << 4);
                            u8 ox = (s & 1) ? (cx ^ hf) : cx, oy = (s & 2) ? (cy ^ vf) : cy;
                            if (dx <= -16 || dx >= 320 || dy <= -16 || dy >= 224 || index >= 78) continue;
                            tile = cell(code + (oy << 1) + ox, col, &misses);
                            if (!tile) continue;
                            spr->y = dy + 128; spr->size = SPRITE_SIZE(2, 2); spr->link = (u8)(index + 1);
                            spr->attribut = (u16)(attr | tile); spr->x = dx + 128;
                            spr++; index++;
                        }
                        continue;
                    }
                }
                spr->y = sy + 128; spr->size = fi->size; spr->link = (u8)(index + 1);
                spr->attribut = (u16)(attr | tile); spr->x = sx + 128;
                spr++; index++;
            }
        }
    }
    list_no = hi + lo; (void)list_no;
    g_object_sprites = index - first_index; g_object_misses = misses; g_object_rom_loads = rom_loads;
    return index;
}

