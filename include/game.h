#ifndef PACLAND_GAME_H
#define PACLAND_GAME_H
#include "native.h"
#include "simulation.h"
extern volatile u16 g_round,g_state,g_control_mode,g_corrected,g_lives,g_loaded_round;
extern volatile s16 g_player_x,g_player_y,g_camera_fg,g_camera_bg;
extern volatile u32 g_ticks;
extern volatile u16 g_lag_at[8];
extern Player player;
extern u8 game_high_score[4];
void game_new(void);
void game_bench(void);
extern u8 g_pac_native;
extern volatile u8 g_starting,g_ending;
extern volatile u8 g_demo;
void game_demo_start(u16 round);
void game_demo_tick(void);
extern volatile u16 g_bench_mode,g_bench_count,g_bench_frames;
void game_start(u16 round);
void game_leave(void);
void objects_load(u16 first_free_tile);
void objects_scene_palette(u8 scene);
u16 objects_draw(u16 first_index, s16 pac_x);
extern u16 g_object_sprites,g_object_misses,g_object_deferred,g_cpu_lines,g_lag_frames;
void game_tick(u16 pad,u16 fresh);
void renderer_load(void);
void renderer_frame(void);
void renderer_menu(void);
void renderer_pause(void);
void renderer_pause_cursor(u16 selected);
void renderer_font(void);
void audio_init(void);
void audio_start(void);
void audio_stop(void);
#endif
