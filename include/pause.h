#ifndef PACLAND_PAUSE_H
#define PACLAND_PAUSE_H
#include <genesis.h>
extern volatile u16 g_pause_cursor;
void pause_open(void);
void pause_frame(u16 pad,u16 fresh);
void pause_filter(u16 *pad,u16 *fresh);
#endif
