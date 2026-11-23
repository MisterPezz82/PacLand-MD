#ifndef PACLAND_TITLE_H
#define PACLAND_TITLE_H
#include <genesis.h>
void title_start(void);   /* set up the screen and start the attract title loop */
void title_frame(void);   /* advance one frame */
u8 title_cycle_done(void);
extern const u16 title_pal[64];
extern const u16 title_tile_base, title_tile_count, title_frames;
extern const u32 title_tiles[];
extern const u16 title_stream[];
#endif
