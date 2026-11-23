#ifndef PACLAND_RECOMP_DATA_H
#define PACLAND_RECOMP_DATA_H
#include <genesis.h>
extern const u8 arcade_main[0x20000];
extern const u32 arcade_sprites[512 * 32];
extern const u8 sprite_lut[1024];
extern const u16 sprite_pal[32][16];
extern const u8 sprite_map[32][256];
extern const u8 group_line[32][64];        /* palette line per round and colour group */
extern const u8 group_pens[32][1024];      /* pen in that line per group * 16 + arcade pen */
extern const u32 cell_pool[];          /* pre-converted 16x16 cells, 4 tiles each */
extern const u16 cell_keys[];          /* per round, sorted: code | group << 9 */
extern const u16 cell_index[];         /* -> cell_pool cell */
extern const u16 cell_round_start[33];
extern const u32 cell_hash[];          /* per round: key << 16 | cell, 0xFFFFFFFF empty */
extern const u16 cell_hash_start[32], cell_hash_mask[32];
extern const u32 block_pool[];         /* pre-converted multi-cell entries, VDP tile order */
extern const u32 block_keys[];         /* per round, sorted: code | size << 9 | group << 11 */
extern const u16 block_index[];        /* -> block_pool offset in 128-byte units */
extern const u16 block_round_start[33];
#endif
