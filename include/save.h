#ifndef PACLAND_SAVE_H
#define PACLAND_SAVE_H
#include <genesis.h>
void save_load(void);
void save_commit(void);
extern volatile u16 g_save_status; /* 0=new/no record, 1=loaded/saved, 2=write unavailable */
#endif
