#ifndef PACLAND_MUSIC_H
#define PACLAND_MUSIC_H
#include <genesis.h>
#include "musdrv.h"
/* Arcade music and sound effects on the YM2612 and PSG, driven by the
 * original game's sound requests (6A85-6AA4 in the shared RAM). The player
 * runs on the Z80 (src/z80/musdrv.c); src/music.c loads it and passes the
 * requests through. */
typedef struct { u8 fbalg, carriers; u8 mul[4]; u8 tl[4]; } MusPatch;   /* as the Z80 table */
void music_init(void);
void music_frame(void);      /* once per frame, after the vertical blank DMA */
void music_stop_all(void);
void music_vint(void);       /* from the vertical interrupt (Z80 player) */
#endif
