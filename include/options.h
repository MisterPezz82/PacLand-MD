#ifndef PACLAND_OPTIONS_H
#define PACLAND_OPTIONS_H
#include <genesis.h>
extern volatile u16 opt_trip;
extern volatile u16 opt_region,opt_lives,opt_random,opt_seed,opt_attract;
extern volatile u16 opt_shuffle_trips,opt_shuffle_enemies,opt_shuffle_items;
void options_sync_random(void);
extern const u16 japan_art_available;
extern const u32 *const japan_player_tiles[32];
extern const u32 japan_sprites[];
void options_open(void);
void options_trip_open(void);
void options_frame(u16 fresh);
void options_new_game(void);
u16 options_first_round(void);
u16 options_next_round(u16 round);
void options_randomize_round(void);
#endif
