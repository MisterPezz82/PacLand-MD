#ifndef PACLAND_NATIVE_H
#define PACLAND_NATIVE_H
#include <genesis.h>
typedef struct {
 const u32 *tiles;
 const u16 *palette,*fg,*bg;
 const u8 *segments;
 const u16 *fg_offsets,*bg_offsets;
 u16 tile_count,fg_columns,bg_columns,segment_count;
 u16 start,end,player_original,player_corrected;
 const u32 *player_tiles; /* Pac-Man frames for this palette bank */
} Stage;
/* tile_count is the number of resident scenery tiles. */
extern const Stage stages[32];
extern const u8 collision_profiles[],collision_counts[];
#endif
