#pragma GCC optimize ("O2")
#include "game.h"
#include "recomp_rt.h"
#include "recomp_data.h"
#include "options.h"
#include "save.h"
volatile u16 g_round,g_state,g_control_mode,g_corrected=1,g_lives=3,g_loaded_round=0xffff;
volatile s16 g_player_x,g_player_y,g_camera_fg,g_camera_bg;
volatile u32 g_ticks;
volatile u16 g_lag_at[8];   /* g_ticks of the last 8 dropped frames (diagnostics) */
Player player;
static u16 jump_prev,left_prev,right_prev,death_ticks,clear_ticks;
static u8 arcade_reinit;
u16 g_cpu_lines,g_lag_frames;
/* Development benchmark (set by tests through the debugger): run g_bench_count
 * iterations of component g_bench_mode back to back and report elapsed VBlanks.
 * 1 full arcade tick, 2 objects (8230), 3 sprite builder (C1FB),
 * 4 native sprite drawing, 5 camera/scroll (D130). */
volatile u16 g_bench_mode,g_bench_count,g_bench_frames;
void game_bench(void){
 u16 i;u32 t=vtimer;
 for(i=0;i<g_bench_count;i++){
  switch(g_bench_mode){
   case 1:rc_irq();rc_frame();break;
   case 2:rc_bench_call(0x8230);break;
   case 3:rc_bench_call(0xC1FB);break;
   case 4:objects_draw(2,100);break;
   case 5:rc_bench_call(0xD130);break;
   case 6:break;
   case 8:rc_bench_call(0xEBF1);break;
   case 9:rc_bench_call(0xCE62);break;
   case 10:rc_native_loop=1;rc_irq();break;
   case 11:player.x+=256;renderer_frame();DMA_flushQueue();break;
   case 12:game_tick(0,0);break;
   case 7:objects_draw(2,-400);break;
  }
 }
 g_bench_frames=(u16)(vtimer-t);g_bench_mode=0;
}
/* Hybrid, session 3: all gameplay logic, including Pac-Man himself (player
 * routine 8731: walking, running, jumping, terrain, steps, logs, springboards,
 * water), runs as the original arcade code recompiled to C (src/recomp_objects.c)
 * against the arcade RAM image R6. The cartridge supplies the pad input in the
 * arcade's format, the camera, scenery and sprite rendering, and the menus. */
static u8 dying;
/* EA5D..EA6C / EAA2..EAB1: the in-game music requests and their status */
static void music_level_clear(void){M6[0x289]=M6[0x28A]=M6[0x28B]=0;M6[0x2A9]=M6[0x2AA]=M6[0x2AB]=0;}
static u16 freeze;           /* frames left of an arcade freeze (220E) */
/* cut-scene tasks running (round start / round end); exported for the tests */
volatile u8 g_starting,g_ending;
volatile u8 g_demo;
static u16 demo_ticks,demo_freeze;
#define starting g_starting
#define ending g_ending
u8 g_pac_native;
/* High score (BCD at 205D-2060) survives new games; the arcade's default is 80000. */
u8 game_high_score[4]={0x00,0x08,0x00,0x00};
#define high_score game_high_score
static void capture_high_score(void){
 u8 i;if(g_demo||g_loaded_round==0xFFFF)return;
 for(i=0;i<4;i++){if(R6[0x205D+i]>high_score[i]){for(;i<4;i++)high_score[i]=R6[0x205D+i];return;}if(R6[0x205D+i]<high_score[i])return;}
}
void game_new(void){
 u8 i;
 capture_high_score();
 ROM6=arcade_main;rc_hide_player=0;rc_arcade_video=0;rc_new_game((u8)opt_lives);arcade_reinit=0;options_new_game();
 for(i=0;i<4;i++)R6[0x205D+i]=high_score[i];
}
void game_leave(void){
 capture_high_score();audio_stop();save_commit();
 g_demo=0;starting=ending=dying=0;death_ticks=clear_ticks=freeze=0;
 g_state=0;renderer_menu();
}
static void sync_player(void){
 g_lives=R6[0x2100]; /* arcade counter also receives score/fairy extra lives */
 u16 x=(u16)(R6[0x240C]<<8|R6[0x240D]);s16 y=(s16)(R6[0x2409]<<8|R6[0x240A]);
 player.x=(int)x*256+R6[0x240E];player.y=(int)y*256;
 player.vx=(s16)(R6[0x2411]<<8|R6[0x2412]);player.facing_left=!(R6[0x2406]&1);
 g_player_x=(s16)x;g_player_y=y;
}
void game_start(u16 round) {
 g_round=round&31;death_ticks=clear_ticks=0;dying=ending=starting=0;freeze=0;g_state=1;
 if(arcade_reinit){rc_setup(g_round,1);arcade_reinit=0;R6[0x220E]=R6[0x220F]=0;}   /* E9F2 */
 else{
  /* the original round start (trip intro, set-up, walk-in) as a task: run
   * it to its first frame; game_tick continues it */
  rc_round_start(g_round);starting=1;
  if(!rc_task_resume())starting=0;
  if(rc_round_phase==2)rc_round_phase=3;    /* set-up already done: load once below */
 }
 if(!starting||rc_round_phase>=2)options_randomize_round();sync_player();renderer_load();
}
void game_demo_start(u16 round){
 u16 i;static const char label[]="DEMO";
 audio_stop();game_new();M6[0x18C]=0;
 g_demo=1;g_round=round&3;g_state=1;starting=ending=dying=0;
 clear_ticks=death_ticks=freeze=demo_ticks=demo_freeze=0;arcade_reinit=0;
 R6[0x2100]=1;R6[0x2214]=1;rc_setup(g_round,0);
 for(i=0;i<4;i++){u16 o=4*128+(29+i)*2;R6[o]=(u8)(0xCA+label[i]-'A');R6[o+1]=9;}
 sync_player();renderer_load();
}
void game_demo_tick(void){
 g_ticks++;rc_irq();
 if(demo_freeze)demo_freeze--;
 else{u16 n=R6W(0x220E);if(n){R6[0x220E]=R6[0x220F]=0;demo_freeze=(u8)n;}else rc_demo_frame();}
 sync_player();
 if(R6[0x2062]||R6[0x2063]||++demo_ticks>=1800){
  g_demo=0;g_state=0;audio_stop();renderer_menu();
 }
}
static u16 game_over_ticks;
/* GAME OVER in the middle of the frozen playfield, written into the arcade's
 * text layer like the arcade's own messages (drawn by hud_text). Letters are
 * characters CA.. (A..), space EE; colour 9 as HIGH SCORE. */
static void game_over_text(void){
 capture_high_score();save_commit();
 static const char t[]="GAME OVER";
 u16 i;   /* text rows hold columns 3..38 of the 288-pixel screen: centred from 16 */
 for(i=0;i<9;i++){u16 cell=(u16)((14*64+16+i)*2);R6[cell]=t[i]==' '?0xEE:(u8)(0xCA+t[i]-'A');R6[cell+1]=9;}
 game_over_ticks=0;
 M6[0x2A1]=1; /* EC B7..EC B9: the arcade's game-over sting */
}
/* Half an extra pixel/tick of launch lift for pool springboards. State6 lasts
 * exactly one actor update before the arcade changes it to flight state7.
 * Keep the original in-flight taps, ordinary jumps and boots untouched. */
void game_pool_assist(void){
 if(R6[0x2403]==6&&!R6[0x20FD]){
  s16 vy=(s16)((u16)R6[0x240F]<<8|R6[0x2410]);
  if(vy>0&&vy<0x7F00){vy+=0x0080;R6[0x240F]=(u16)vy>>8;R6[0x2410]=(u8)vy;}
 }
}
void game_tick(u16 pad,u16 fresh) {
 const Stage *s=&stages[g_round];u16 left,right,jump;
 g_ticks++;if(g_state==0)return;
 if(g_state==3){if((fresh&BUTTON_START)||++game_over_ticks>=480){game_over_ticks=0;audio_stop();g_state=0;renderer_menu();}return;}   /* 8 s, or Start */
 if((pad&BUTTON_START)&&(fresh&(BUTTON_UP|BUTTON_DOWN))){
  if(fresh&BUTTON_UP)game_start((g_round+1)&31);
  if(fresh&BUTTON_DOWN)game_start((g_round+31)&31);
  return;
 }
 if(death_ticks){if(--death_ticks==0){if(g_lives)game_start(g_round);else{g_state=3;audio_stop();game_over_text();}}return;}
 if(starting){
  rc_irq();
  if(!rc_task_resume())starting=0;
  if(rc_round_phase==2){
   /* after the trip intro. In the arcade its text (PLAYER 1 READY, TRIP n)
    * is overwritten by the scenery the game draws into the same layer; the
    * scenery is native here, so blank the playfield text rows instead. */
   u16 i;for(i=5*64;i<29*64;i++)R6[2*i]=0xEE;
   rc_round_phase=3;options_randomize_round();sync_player();renderer_load();
  }
  sync_player();if(starting)return;
 }
 if(ending){
  /* the original end-of-round sequence (EA59..EA75), one frame per tick */
  rc_irq();
  if(!rc_task_resume()){ending=0;clear_ticks=1;}
  sync_player();if(!clear_ticks)return;
 }
 if(clear_ticks){if(--clear_ticks==0)game_start(options_next_round(g_round));return;}
 if(dying){
  /* the original death sequence (EAB4..EAD5) runs until the player routine
   * signals completion; it also records the respawn checkpoint (EB9D) */
  rc_irq();
  if(rc_death_tick()||++dying>250){dying=0;if(R6[0x2100])R6[0x2100]--;arcade_reinit=1;death_ticks=1;}
  sync_player();return;
 }
 /* main loop E9FA..EA20: in-game music request, every frame unless the
  * round-start jingle is playing (6AA6): 6A8B while 2074 is 6..0Ch,
  * otherwise 6A89 (8E clear) or 6A8A (8E set, and 6A8B cleared) */
 if(!M6[0x2A6]){
  s8 a=(s8)R6[0x2074];
  if((s8)(a-6)>=0&&(s8)(a-0x0D)<0)M6[0x28B]=1;
  else if(!R6[0x208E])M6[0x289]=1;
  else{M6[0x28A]=1;M6[0x28B]=0;}
 }
 /* main loop EA23..EA2E: a non-zero 220E (set to 20h when a ghost is eaten,
  * 9289) freezes the game for its low byte in frames (DE60), then is
  * cleared; the ghost-eat test (9249) refuses while it is set */
 if(freeze){freeze--;rc_irq();sync_player();return;}
 {u16 w=R6W(0x220E);
  if(w){R6[0x220E]=R6[0x220F]=0;freeze=(u16)(((u8)w?(u8)w:256)-1);rc_irq();sync_player();return;}}
 /* both layouts at once: D-pad left/right, or the arcade's own buttons on
  * A (left) and C (right); B jumps */
 left=pad&(BUTTON_LEFT|BUTTON_A);right=pad&(BUTTON_RIGHT|BUTTON_C);jump=pad&BUTTON_B;
 if(g_control_mode==1||g_control_mode==3){left=pad&BUTTON_LEFT;right=pad&BUTTON_RIGHT;}
 if(g_control_mode==2){left=pad&BUTTON_A;right=pad&BUTTON_C;}
 if(g_control_mode==3)jump=pad&(BUTTON_A|BUTTON_B|BUTTON_C);
 rc_input(left!=0,right!=0,jump!=0);
 /* A release and another tap can both happen during a long draw. Keep the
  * VInt-recorded edge even when rc_input's last simulated state was held. */
 if(left&fresh)R6[0x2093]|=1;
 if(right&fresh)R6[0x2095]|=1;
 if(jump&fresh)R6[0x2097]|=1;
 rc_irq();game_pool_assist();rc_frame();sync_player();
 jump_prev=left_prev=right_prev=0;(void)jump_prev;(void)left_prev;(void)right_prev;
 if(R6[0x2063]){music_level_clear();rc_death_start();dying=1;return;}         /* caught, pit or water (2063) */
 if(R6[0x2062]){capture_high_score();save_commit();music_level_clear();rc_round_end_start();ending=1;return;}      /* round end (D16D sets 2062) */
 if((s->end>s->start && g_player_x>=s->end+48) || (s->end<s->start && g_player_x<=s->end-48))clear_ticks=90;
}
