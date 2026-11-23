#pragma GCC optimize ("O2")
#include "game.h"
#include "recomp_rt.h"
#include "hud.h"
#include "title.h"
#include "options.h"
#include "build_config.h"
extern const TileSet font_default;
#ifdef PL_PROFILE
u16 g_render_profile[4];
#endif
static s16 last_fg,last_bg;
static s16 cam_fg_axis;   /* Pac-Man's foreground-plane x this frame (computed once in camera()) */
static u16 last_animation=0xffff;
#ifdef PL_BLAST
/* Two complete native frames fit before HUD_BASE1440. Cache hits change the
 * sprite's tile address; they need neither CPU tile gathering nor an upload. */
static u16 player_frame_tags[2],player_frame_recent;
u16 g_player_tile,g_player_cache_hits,g_player_cache_misses;
#define PAC_FRAME_TILE g_player_tile
#else
#define PAC_FRAME_TILE 1400
#endif
static u32 player_variant_stage[20*8] __attribute__((aligned(4)));
extern const u32 player_variant_pool[];
extern const u16 *const player_variants[2][32][3];
static s16 scroll_a[28] __attribute__((aligned(4))),scroll_b[28] __attribute__((aligned(4)));
static u8 scroll_dirty=1;   /* force a full upload after a round load */
static TransferMethod column_tm=DMA_QUEUE;
static u8 fairy_view;
static u8 scene_view;
static u8 scene_dma_reload;
#ifdef PL_BLAST
static u16 blast_plane_a,blast_plane_b,blast_scroll;
#endif
volatile u32 g_render_count;
extern const u16 *const door_bg[32];
extern const u32 *const door_tiles[32];
extern const u16 door_tile_count[32],door_bg_start[32];
/* Map columns are stored contiguously (24 cells), so a new column is a single
 * transfer straight from ROM instead of 24 separate tilemap writes. */
static void column(VDPPlane plane,const u16 *map,u16 width,s16 world) {
 u16 c;if(world<0)c=0;else if(world>=width)c=width-1;else c=world;
 if(fairy_view){
  const Stage *s=&stages[g_round];u16 seg=s->segment_count-3;
  const u16 *off=plane==BG_A?s->fg_offsets:s->bg_offsets;
  u16 first=off[seg]>>3,last=(off[seg+1]>>3)-1;
  if(c<first)c=first+(first-c-1);else if(c>last)c=last-(c-last-1);
  if(c<first)c=first;else if(c>last)c=last;
 }
 const u16 *src=map+c*24;
 if(scene_view==1&&plane==BG_B&&door_bg[g_round]&&c>=door_bg_start[g_round]&&c<door_bg_start[g_round]+36)
  src=door_bg[g_round]+(c-door_bg_start[g_round])*24;
#ifdef PL_BLAST
 if(column_tm==DMA_QUEUE){
  u16 dest=(plane==BG_A?blast_plane_a:blast_plane_b)+384+((world&63)<<1);
  /* The SDK bank splitter advances the destination by contiguous bytes.
   * Columns stride128 bytes per word, so split explicitly at128KB instead. */
  u32 offset=(u32)src&0x1FFFF;
  if(offset>0x1FFD0){
   u16 words=(u16)((0x20000-offset)>>1);
   DMA_queueDmaFast(DMA_VRAM,(void*)src,dest,words,128);
   DMA_queueDmaFast(DMA_VRAM,(void*)(src+words),(u16)(dest+(words<<7)),24-words,128);
  }else DMA_queueDmaFast(DMA_VRAM,(void*)src,dest,24,128);
 }else VDP_setTileMapDataColumnFast(plane,(u16*)src,world&63,3,24,column_tm);
#else
 VDP_setTileMapDataColumnFast(plane,(u16*)src,world&63,3,24,column_tm);
#endif
}
static void stream(VDPPlane plane,const u16 *map,u16 width,s16 cam,s16 *last) {
 s16 col=cam>>3;
 if(*last==0x7fff || col-*last>16 || *last-col>16) {
  s16 i;column_tm=scene_dma_reload?DMA_QUEUE:CPU;for(i=col-1;i<=col+41;i++)column(plane,map,width,i);column_tm=DMA_QUEUE;
 } else {
  while(*last<col){(*last)++;
#ifndef PL_BLAST
   column(plane,map,width,*last+40);
#endif
   column(plane,map,width,*last+41);}
  while(*last>col){(*last)--;column(plane,map,width,*last-1);}
 }
 *last=col;
}
static void camera(void) {
 const Stage *s=&stages[g_round];u16 i;
 u8 fairy=(g_ending&&(g_round&3)==2&&R6[0x2220]&&
           (player.x>>8)>=R6W(0x221E)+288&&s->segments[s->segment_count-3]==32);
 /* Look ahead in the trip direction. Keeping the outward anchor on the
  * return leg exposes the normally offscreen pursuer behind Pac-Man. */
 s16 anchor=R6[0x208E]?192:128;
 s16 a=(cam_fg_axis=camera_axis(player.x>>8,s->fg_offsets,s->segment_count))-anchor;
 s16 b=camera_axis(player.x>>8,s->bg_offsets,s->segment_count)-anchor;
 if(a<0)a=0;
 if(b<0)b=0;
 if(a>s->fg_columns*8-320)a=s->fg_columns*8-320;
 if(b>s->bg_columns*8-320)b=s->bg_columns*8-320;
 /* The door's background is visible before its jump task, and behind the
  * player at the start of the return trip. Keep its corrected palette/map
  * for that whole visible interval, not only while 2220 is set. */
 {u16 first=door_bg_start[g_round]*8;
  u8 scene=fairy?2:(door_bg[g_round]&&b+320>first&&b<first+288)?1:0;
  if(scene!=scene_view||fairy!=fairy_view){scene_view=scene;fairy_view=fairy;scene_dma_reload=1;objects_scene_palette(scene);last_fg=last_bg=0x7fff;}
 }
 g_camera_fg=a;g_camera_bg=b;
 stream(BG_A,s->fg,s->fg_columns,a,&last_fg);stream(BG_B,s->bg,s->bg_columns,b,&last_bg);
 scene_dma_reload=0;
 /* Rows 0-2 and 27 (text) never scroll and stay 0; the rest are only
  * rewritten and uploaded when the camera moved. */
 if(-a!=scroll_a[3] || scroll_dirty){u32 v=(u16)-a;u32 *q=(u32*)&scroll_a[4];v|=v<<16;scroll_a[3]=(s16)-a;scroll_a[26]=(s16)-a;for(i=0;i<11;i++)*q++=v;
#ifdef PL_BLAST
  DMA_queueDmaFast(DMA_VRAM,scroll_a,blast_scroll,28,32);
#else
  VDP_setHorizontalScrollTile(BG_A,0,scroll_a,28,DMA_QUEUE);
#endif
 }
 if(-b!=scroll_b[3] || scroll_dirty){u32 v=(u16)-b;u32 *q=(u32*)&scroll_b[4];v|=v<<16;scroll_b[3]=(s16)-b;scroll_b[26]=(s16)-b;for(i=0;i<11;i++)*q++=v;
#ifdef PL_BLAST
  DMA_queueDmaFast(DMA_VRAM,scroll_b,blast_scroll+2,28,32);
#else
  VDP_setHorizontalScrollTile(BG_B,0,scroll_b,28,DMA_QUEUE);
#endif
 }
 scroll_dirty=0;
}
void renderer_load(void) {
#ifdef PL_BLAST
 extern void blast_reset(void);blast_reset();
#endif
 const Stage *s=&stages[g_round];u16 i,budget;char label[40];
 SYS_doVBlankProcess();VDP_setEnable(FALSE);
 budget=DMA_getMaxTransferSize();DMA_setMaxTransferSize(0);
 VDP_clearPlane(BG_A,TRUE);VDP_clearPlane(BG_B,TRUE);
 VDP_setWindowOff();VDP_setScrollingMode(HSCROLL_TILE,VSCROLL_PLANE);
#ifdef PL_BLAST
 blast_plane_a=VDP_getPlaneAddress(BG_A,0,0);blast_plane_b=VDP_getPlaneAddress(BG_B,0,0);blast_scroll=VDP_getHScrollTableAddress();
#endif
 for(i=0;i<4;i++)PAL_setPalette(i,s->palette+i*16,CPU);
 if(g_starting&&rc_round_phase<2){
  /* The trip card precedes arcade object initialisation. Keep its original
   * text on clean planes, without drawing objects left by the previous scene. */
  for(i=0;i<28;i++)scroll_a[i]=scroll_b[i]=0;
  VDP_setHorizontalScrollTile(BG_A,0,scroll_a,28,CPU);
  VDP_setHorizontalScrollTile(BG_B,0,scroll_b,28,CPU);
  VDP_setVerticalScroll(BG_A,0);VDP_setVerticalScroll(BG_B,0);
  hud_load();renderer_frame();SYS_doVBlankProcess();
  VDP_waitDMACompletion();VDP_waitFIFOEmpty();DMA_setMaxTransferSize(budget);
  VDP_waitVInt(); /* VBlank status stays set while display is disabled */
  VDP_setEnable(TRUE);return;
 }
 /* Only scenery tiles are resident; Pac-Man frames stream to 1400 and the
  * space between holds the arcade object sprite cache (src/objects.c). */
 VDP_loadTileData(s->tiles,128,s->tile_count,CPU);VDP_setTextPalette(PAL3);
 if(door_tile_count[g_round])VDP_loadTileData(door_tiles[g_round],128+s->tile_count,door_tile_count[g_round],CPU);
 objects_load(128+s->tile_count+door_tile_count[g_round]);
 scene_view=fairy_view=scene_dma_reload=0;
 last_fg=last_bg=0x7fff;last_animation=0xffff;scroll_dirty=1;camera();
#ifdef PL_BLAST
 player_frame_tags[0]=player_frame_tags[1]=0xFFFF;player_frame_recent=1;g_player_tile=1400;
#endif
 (void)label;
 hud_load();   /* rows 0, 1 and 27: the arcade's own HUD (src/hud.c) */
 /* Commit the first complete scene before revealing the display. */
 renderer_frame();
 SYS_doVBlankProcess();VDP_waitDMACompletion();VDP_waitFIFOEmpty();
 DMA_setMaxTransferSize(budget);
 VDP_waitVInt(); /* reveal at a physical frame boundary, even when blanked */
 g_loaded_round=g_round;VDP_setEnable(TRUE);
}
void renderer_frame(void) {
 const Stage *s=&stages[g_round];u16 frame=4,source,stride=g_corrected?20:16,colour=R6[0x2405]&63,region=opt_region&&japan_art_available; s16 x,y;u16 i,n;
 if(g_starting&&rc_round_phase<2){
  n=hud_text(0);hud_frame();
  if(!n){VDP_setSpriteFull(0,0,-32,SPRITE_SIZE(1,1),0,0);n=1;}
  VDP_setSpriteLink(n-1,0);VDP_updateSprites(n,DMA_QUEUE);
#ifdef PL_BLAST
  g_render_count++;
#endif
  return;
 }
 if(g_state!=1&&g_state!=3)return;   /* 3: game over, frozen playfield */
#ifdef PL_PROFILE
 u32 rt=getSubTick(),rn;
#endif
 camera();
#ifdef PL_PROFILE
 rn=getSubTick();g_render_profile[0]=(u16)(rn-rt);rt=rn;
#endif
 /* Pac-Man's animation frame and facing come from the arcade player slot
  * (set by the original player routine). Standard 32x32 frames are drawn
  * with the native original/corrected art; anything else (death frames,
  * special poses) is drawn by the arcade sprite path in src/objects.c. */
 {u8 fl=R6[0x2406];u16 code=(u16)(R6[0x2404]+((fl&0x80)<<1));
  g_pac_native=((fl>>2)&3)==3&&!(fl&2)&&code<112&&!R6[0x2409]&&colour<4;
  frame=code&~3;}
 if(g_pac_native&&(frame|(colour<<8)|(region<<10)|(g_corrected<<11))!=last_animation){
#ifdef PL_BLAST
  u16 key=frame|(colour<<8)|(region<<10)|(g_corrected<<11),victim;
  if(player_frame_tags[0]==key){g_player_tile=1400;g_player_cache_hits++;}
  else if(player_frame_tags[1]==key){g_player_tile=1420;g_player_cache_hits++;}
  else{
   victim=player_frame_recent^1;g_player_tile=1400+victim*20;g_player_cache_misses++;
#endif
  source=(g_corrected?s->player_corrected:s->player_original)-16+(frame/4)*stride;
  const u32 *tiles=region?japan_player_tiles[g_round]:s->player_tiles;
  if(colour){
   const u16 *ids=player_variants[region][g_round][colour-1]+source;u32 *dst=player_variant_stage;
   for(i=0;i<stride;i++){
    const u32 *src=player_variant_pool+((u32)ids[i]<<3);u16 k;
    for(k=0;k<8;k++)*dst++=*src++;
   }
   tiles=player_variant_stage;source=0;
  }
  VDP_loadTileData(tiles+source*8,PAC_FRAME_TILE,stride,DMA_QUEUE);
#ifdef PL_BLAST
   player_frame_tags[victim]=key;
  }
  player_frame_recent=g_player_tile==1400?0:1;
#endif
  last_animation=frame|(colour<<8)|(region<<10)|(g_corrected<<11);
 }
 x=cam_fg_axis-g_camera_fg-(g_corrected?18:16);y=224-(player.y>>8);
 if(g_corrected && !player.facing_left)x-=4;
 /* Arcade sprite order puts the door and other later slots in front of
  * Pac-Man. Emit native Pac-Man after those objects (earlier VDP sprites
  * win), just as the original sprite-list builder does. */
 n=hud_text(0);
#ifdef PL_PROFILE
 rn=getSubTick();g_render_profile[1]=(u16)(rn-rt);rt=rn;
#endif
 n=objects_draw(n,cam_fg_axis-g_camera_fg-16);
#ifdef PL_PROFILE
 rn=getSubTick();g_render_profile[2]=(u16)(rn-rt);rt=rn;
#endif
 if(g_corrected){
  /* A 36x32 source frame occupies a 32x32 + padded 8x32 metasprite. */
  if(g_pac_native)for(i=0;i<2;i++){
   s16 sx=x+(!player.facing_left?(i==0?8:0):(i==0?0:32));
   VDP_setSpriteFull(n,sx,y,SPRITE_SIZE(i==0?4:1,4),TILE_ATTR_FULL(PAL3,0,0,!player.facing_left,PAC_FRAME_TILE+(i?16:0)),n+1);
   n++;}
 }else if(g_pac_native){VDP_setSpriteFull(n,x,y,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL3,0,0,!player.facing_left,PAC_FRAME_TILE),n+1);n++;}
 hud_frame();
#ifdef PL_PROFILE
 rn=getSubTick();g_render_profile[3]=(u16)(rn-rt);
#endif
 if(!n){VDP_setSpriteFull(0,0,-32,SPRITE_SIZE(1,1),0,0);n=1;}
 VDP_setSpriteLink(n-1,0);VDP_updateSprites(n,DMA_QUEUE);
 g_render_count++;
}
void renderer_menu(void) {
 u16 i;VDP_setEnable(FALSE);VDP_setWindowOff();VDP_clearPlane(BG_A,TRUE);VDP_clearPlane(BG_B,TRUE);
 VDP_setScrollingMode(HSCROLL_PLANE,VSCROLL_PLANE);VDP_setHorizontalScroll(BG_A,0);VDP_setHorizontalScroll(BG_B,0);
 for(i=0;i<64;i++)PAL_setColor(i,0);
 PAL_setColor(63,0xeee);VDP_setTextPalette(PAL3);
 VDP_setSpriteFull(0,0,240,SPRITE_SIZE(1,1),0,0);VDP_updateSprites(1,CPU);
 title_start();   /* the arcade's own attract title (src/title.c) */
}
/* Pause uses otherwise-unused tiles16..50; gameplay scenery begins at128.
 * Plane-A priority covers the frozen sprites without changing arcade RAM. */
#define PAUSE_FILL 16
#define PAUSE_FONT 17
#define PAUSE_CURSOR 43
#define PAUSE_EDGE 44
static u16 pause_fg,pause_bg;
static void pause_cell(u16 x,u16 y,u16 tile){
 s16 col=(s16)x-(scroll_a[y]>>3);
 VDP_setTileMapXY(BG_A,TILE_ATTR_FULL(PAL3,1,0,0,tile),col&63,y);
}
static void pause_text(const char *text,u16 x,u16 y){
 while(*text){u16 tile=PAUSE_FILL;if(*text>='A'&&*text<='Z')tile=PAUSE_FONT+*text-'A';pause_cell(x++,y,tile);text++;}
}
void renderer_pause_cursor(u16 selected){
 pause_cell(10,12,selected==0?PAUSE_CURSOR:PAUSE_FILL);
 pause_cell(10,15,selected==1?PAUSE_CURSOR:PAUSE_FILL);
}
void renderer_pause(void) {
 u16 colours[64],i,x,y,lo=0xFFFF,hi=0;u32 pixels[8];
 SYS_doVBlankProcess();DMA_clearQueue();
 PAL_getColors(0,colours,64);
 for(i=0;i<64;i++){
  u16 c=colours[i],r=((c>>1)&7)*3/4,g=((c>>5)&7)*3/4,b=((c>>9)&7)*3/4;
  colours[i]=(r<<1)|(g<<5)|(b<<9);
 }
 for(i=1;i<16;i++){
  u16 c=colours[48+i],v=((c>>1)&7)+((c>>5)&7)+((c>>9)&7);
  if(v<lo){lo=v;pause_bg=i;}if(v>hi){hi=v;pause_fg=i;}
 }
 /* Decode the game's own 2-bit Namco A-Z font, without heap allocation. */
 for(i=0;i<26;i++){
  const u8 *src=hud_font+((0x1CA+i)<<4);
  for(y=0;y<8;y++){u32 row=0;for(x=0;x<8;x++){
   u16 pen=(src[y*2+(x>>2)]>>(6-2*(x&3)))&3;
   row=(row<<4)|(pen?pause_fg:pause_bg);
  }pixels[y]=row;}
  VDP_loadTileData(pixels,PAUSE_FONT+i,1,CPU);
 }
 for(i=0;i<10;i++){
  for(y=0;y<8;y++){u32 row=0;for(x=0;x<8;x++){
   u16 ink=i==1?(x==(y<4?y:7-y)+2):i==2?y==0:i==3?y==7:i==4?x==0:i==5?x==7:i==6?(x==0||y==0):i==7?(x==7||y==0):i==8?(x==0||y==7):i==9?(x==7||y==7):0;
   row=(row<<4)|(ink?pause_fg:pause_bg);
  }pixels[y]=row;}VDP_loadTileData(pixels,i==0?PAUSE_FILL:i==1?PAUSE_CURSOR:PAUSE_EDGE+i-2,1,CPU);
 }
 SYS_doVBlankProcess();PAL_setColors(0,colours,64,CPU);
 for(y=6;y<=23;y++)for(x=6;x<=33;x++)pause_cell(x,y,y==6?PAUSE_EDGE+(x==6?4:x==33?5:0):y==23?PAUSE_EDGE+(x==6?6:x==33?7:1):x==6?PAUSE_EDGE+2:x==33?PAUSE_EDGE+3:PAUSE_FILL);
 for(i=0;i<80;i++)vdpSpriteCache[i].attribut&=~0x8000;
 VDP_updateSprites(80,CPU);
 pause_text("PAUSED",17,8);pause_text("RESUME",12,12);pause_text("EXIT TO TITLE",12,15);
 pause_text("UP DOWN SELECT",13,18);pause_text("A C CONFIRM",14,20);pause_text("B START RESUME",13,21);
}
void renderer_font(void){
 extern const u32 hud_menu_font[768];
 VDP_loadFontData(hud_menu_font,FONT_LENGTH,CPU);
}
