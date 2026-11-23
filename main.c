#include "build_config.h"   /* PL_MUSIC_C: reference music build (tools/build.mjs) */
#include "game.h"
#include "title.h"
#include "music.h"
#include "options.h"
#include "input.h"
#include "save.h"
#include "pause.h"
void boot_screen(void);
volatile u16 g_display_pal;
#ifdef PL_BLAST
extern volatile u32 g_render_count;
volatile u32 g_present_count;
#endif
#ifdef PL_BLAST_WINDOW
volatile u16 g_blast_window_calls,g_blast_window_overruns,g_blast_window_max_queue,g_blast_window_max_exit;
#endif
#ifdef PL_PROFILE
volatile u16 g_profile[4];
#endif
/* Music: the player runs on the Z80 (src/music.c), which keeps the tempo
 * from the vertical interrupt count. The PL_MUSIC_C reference build runs it
 * on the 68000 from a horizontal interrupt in mid-screen instead (never in
 * vertical-blank time, which the DMA transfers need). */
#ifdef PL_MUSIC_C
HINTERRUPT_CALLBACK sound_hint(void) {
 static u8 acc;
 if(!IS_PAL_SYSTEM)music_frame();
 else{acc+=6;while(acc>=5){acc-=5;music_frame();}}
}
#endif
static void game_vint(void){
 input_vint();
#ifndef PL_MUSIC_C
 music_vint();
#endif
}
int main(bool hard) {
 u16 pad,fresh,held=0,edges=0,denominator,demo_round=0;
 u32 phase=0,last_vtimer=0,last_logic_vtimer;
#ifdef PL_BLAST
 u32 last_presented_render=0;
#endif
#ifdef PL_BLAST_WINDOW
 u32 last_dma_vtimer;
#endif
 (void)hard;
 VDP_setScreenWidth320();VDP_setScreenHeight224();VDP_setPlaneSize(64,32,TRUE);
 JOY_init();JOY_setSupport(PORT_1,JOY_SUPPORT_3BTN);   /* A/B/C/Start only: the 6-button read cost ~2 lines a frame */
 DMA_setMaxQueueSize(128); /* 86 scene columns plus palettes/sprites and cold block uploads */
 boot_screen();
 input_init(); /* the boot-skip press is already held, so is not a fresh game press */
 save_load();
 audio_init();
#ifdef PL_MUSIC_C
 VDP_setHIntCounter(159);SYS_setHIntCallback(sound_hint);VDP_setHInterrupt(TRUE);
#endif
 SYS_setVIntCallback(game_vint);
 g_state=0;renderer_menu();
 /* NTSC: one arcade tick per display frame (about 1% below arcade rate),
  * including catch-up after a late draw. PAL: 2000/33 Hz arcade ticks
  * spread over 50 Hz display frames. */
 g_display_pal=IS_PAL_SYSTEM?1:0;denominator=g_display_pal?16402:20000;
 last_logic_vtimer=vtimer;
#ifdef PL_BLAST_WINDOW
 last_dma_vtimer=vtimer;
#endif
 while(TRUE) {
  u32 now=vtimer,elapsed=now-last_logic_vtimer;last_logic_vtimer=now;
  /* Drawing can cross a display boundary. Account for that elapsed time:
   * one update per draw otherwise turns a modest overrun into half speed.
   * Long round loads are discontinuities, not gameplay time to catch up. */
  if(!elapsed)elapsed=1;
  else if(elapsed>3){
   if(g_state!=1||g_starting||g_ending)elapsed=1;
   else if(elapsed>8)elapsed=8;
  }
  input_take(&pad,&fresh);
  pause_filter(&pad,&fresh);
  if(g_state==5){pause_frame(pad,fresh);phase=0;edges=0;last_logic_vtimer=vtimer;}
  else if(g_state==1&&!g_demo&&(fresh&BUTTON_START)&&!(pad&(BUTTON_UP|BUTTON_DOWN))){pause_open();phase=0;edges=0;last_logic_vtimer=vtimer;}
  else if(g_state==4){options_frame(fresh);}
  else if(g_state==0) {
   title_frame();
   if(fresh&BUTTON_UP)g_corrected=0;
   if(fresh&BUTTON_DOWN)g_corrected=1;
   if(fresh&BUTTON_START){options_trip_open();}
   else if(fresh&BUTTON_B)options_open();
   else if(opt_attract&&title_cycle_done()){game_demo_start(demo_round);demo_round=(demo_round+1)&3;}
  } else if(g_demo){
   if(fresh&BUTTON_START){options_trip_open();}
   else{phase+=20000*elapsed;while(phase>=denominator&&g_demo){phase-=denominator;game_demo_tick();}}
   renderer_frame();
  } else {
#ifdef PL_PROFILE
   u32 pt=getSubTick(),pr;
#endif
   if(g_bench_mode)game_bench();
   held=pad;edges|=fresh;phase+=20000*elapsed;
   while(phase>=denominator){phase-=denominator;game_tick(held|edges,edges);edges=0;}
#ifdef PL_PROFILE
   pr=getSubTick();g_profile[0]=(u16)(pr-pt);pt=pr;
#endif
#ifdef PL_BLAST
   /* Recover simulation debt without adding another draw to the overrun.
    * Once caught up, ordinary frames prepare a draw again. This recovery
    * improved the NTSC prototype, but worsened PAL in the measured replay;
    * PAL retains ordinary drawing. */
   if(g_display_pal||elapsed==1)renderer_frame();
#else
   renderer_frame();
#endif
#ifdef PL_PROFILE
   pr=getSubTick();g_profile[1]=(u16)(pr-pt);
#endif
   {u16 v=GET_VCOUNTER;if(v<224&&v>g_cpu_lines)g_cpu_lines=v;}
   if(vtimer!=last_vtimer+1){g_lag_at[g_lag_frames&7]=(u16)g_ticks;g_lag_frames++;}
  }
  last_vtimer=vtimer;
#ifdef PL_BLAST_WINDOW
  {
   u16 v=GET_VCOUNTER,bytes=DMA_getQueueTransferSize();
   /* A slightly late frame can still fit in early VBlank. Keep large/cold
    * uploads on a fresh blank, and never publish twice in the same blank. */
   if(!g_display_pal&&g_state==1&&!g_starting&&!g_ending&&
      vtimer!=last_dma_vtimer&&v>=224&&v<=230&&bytes<=2048&&DMA_getQueueSize()<=32){
    u16 end;SYS_doVBlankProcessEx(ON_VBLANK);g_blast_window_calls++;
    end=GET_VCOUNTER;if(end<224)g_blast_window_overruns++;
    if(end>g_blast_window_max_exit)g_blast_window_max_exit=end;
    if(bytes>g_blast_window_max_queue)g_blast_window_max_queue=bytes;
   }else SYS_doVBlankProcess();
   last_dma_vtimer=vtimer;
  }
#else
  SYS_doVBlankProcess();
#endif
#ifdef PL_BLAST
  if(last_presented_render!=g_render_count){last_presented_render=g_render_count;g_present_count++;}
#endif
#ifndef PL_MUSIC_C
  music_frame();   /* sound requests to the Z80 player, after this frame's DMA */
#endif
 }
 return 0;
}
