#ifndef PACLAND_HUD_H
#define PACLAND_HUD_H
#include <genesis.h>
/* The arcade's own HUD rows, drawn from arcade video RAM (src/hud.c). */
void hud_load(void);   /* after the round's scenery and PAL3 are loaded */
void hud_frame(void);  /* once per frame: uploads changed cells only */
u16 hud_text(u16 index);   /* playfield text as sprites from index; returns next index */
void hud_text_reset(void);
extern const u8 hud_font[512 * 16];
extern const u16 hud_colours[4 * 256 * 4];
extern const u8 hud_bank[32];
#endif
