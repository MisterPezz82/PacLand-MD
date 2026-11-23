#include "game.h"
#include "options.h"
#include "recomp_rt.h"
#include "recomp_data.h"
#include "music.h"
#include "save.h"
#include "hud.h"
extern const TileSet font_default;
static void put16(u8 *p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}

volatile u16 opt_trip=1;
volatile u16 opt_region,opt_lives=3,opt_random,opt_seed=0x1984,opt_attract=1;
volatile u16 opt_shuffle_trips,opt_shuffle_enemies,opt_shuffle_items;
static u16 cursor,order[32],rng;
static u8 page,drawn_page=255,draw_full;
static u16 drawn_cursor,preview_key=0xFFFF;
static char drawn_value[10][17];
extern const u8 mus_blob[];
static u8 sound_ids[32],sound_count,sound_cursor;
static u16 random16(void){rng=(u16)(rng*25173u+13849u);return rng;}
void options_sync_random(void){opt_random=!(opt_shuffle_trips||opt_shuffle_enemies||opt_shuffle_items)?0:opt_shuffle_trips&&!opt_shuffle_enemies&&!opt_shuffle_items?1:opt_shuffle_trips&&opt_shuffle_enemies&&opt_shuffle_items?2:3;}
static void preset(u16 n){opt_shuffle_trips=n!=0;opt_shuffle_enemies=opt_shuffle_items=n==2;options_sync_random();}

void options_new_game(void){
 u16 i,j,trips[8];rng=opt_seed;
 for(i=0;i<8;i++)trips[i]=i;
 if(opt_shuffle_trips)for(i=7;i;i--){u16 t;j=random16()%(i+1);t=trips[i];trips[i]=trips[j];trips[j]=t;}
 if(opt_shuffle_trips)for(i=0;i<8;i++)if(trips[i]==opt_trip-1){u16 t=trips[0];trips[0]=trips[i];trips[i]=t;break;}
 for(i=0;i<32;i++)order[i]=trips[i>>2]*4+(i&3);
}
u16 options_first_round(void){return opt_shuffle_trips?order[0]:(opt_trip-1)*4;}
u16 options_next_round(u16 round){
 u16 i;if(!opt_shuffle_trips)return round==31?16:round+1;
 for(i=0;i<32;i++)if(order[i]==round)return order[(i+1)&31];
 return order[0];
}
void options_randomize_round(void){
 u16 i;u8 *s;
 if(!(opt_shuffle_enemies||opt_shuffle_items)||g_demo)return;
 rng=(u16)(opt_seed^(g_round*40503u));
 /* Ghost handlers all share the same slot layout. Change identities, never
  * slot IDs, contact records, platforms or the scripts that finish a trip. */
 if(opt_shuffle_enemies)for(i=0;i<5;i++)R6[0x2441+i*96]=(u8)(1+random16()%5);
 if(!opt_shuffle_items)return;
 rng=(u16)(opt_seed^(g_round*40503u)^0xA731); /* independent item choices */
 /* Vary fruit from the arcade's own five-frame fruit table. */
 R6[0x222C]=ROM6[0xA1D5+random16()%5];R6[0x222D]=0x2D;
 for(s=R6+0x2620;s<R6+0x2700;s+=32)if(s[1]==0x0C)s[4]=R6[0x222C];
 /* Hidden item triggers move to another ground contact point in their own
  * segment. Read the existing collision profile so none move below terrain. */
 for(i=0,s=R6+0x2800;i<R6[0x20DD];i++,s+=32)if(s[1]==0x30){
  u16 old=PW16(s+12),seg=old/288,local=(u16)(16+random16()%16*16),ident;
  const Stage *st=&stages[g_round];
  if(seg>=st->segment_count)continue;
  ident=st->segments[seg];if(ident>=35)continue;
  {u8 h=collision_profiles[ident*144+local/8];if(h>=224)continue;put16(s+12,seg*288+local);put16(s+9,(u16)(h+8));}
 }
}

static void preview(void){
 u16 key=(opt_region<<1)|g_corrected;if(!draw_full&&key==preview_key)return;preview_key=key;
 const Stage *s=&stages[0];u16 stride=g_corrected?20:16;
 const u32 *tiles=(opt_region&&japan_art_available)?japan_player_tiles[0]:s->player_tiles;
 u16 off=g_corrected?s->player_corrected:s->player_original;
 VDP_loadTileData(tiles+off*8,128,stride,CPU);PAL_setPalette(PAL1,sprite_pal[0],CPU);
 VDP_setSpriteFull(0,264,28,SPRITE_SIZE(4,4),TILE_ATTR_FULL(PAL1,1,0,1,128),g_corrected?1:0);
 if(g_corrected)VDP_setSpriteFull(1,256,28,SPRITE_SIZE(1,4),TILE_ATTR_FULL(PAL1,1,0,1,144),0);
 VDP_updateSprites(g_corrected?2:1,CPU);
}
static void row(u16 n,const char *name,const char *value){
 u16 y=6+n*2,i;char field[17];
 if(draw_full||n==drawn_cursor||n==cursor)VDP_drawText(n==cursor?">":" ",2,y);
 if(draw_full)VDP_drawText(name,4,y);
 if(draw_full||strcmp(drawn_value[n],value)){
  for(i=0;i<16;i++)field[i]=' ';field[16]=0;
  for(i=0;value[i]&&i<16;i++)field[i]=value[i];
  VDP_drawText(field,23,y);strcpy(drawn_value[n],value);
 }
}
extern const u16 fairy_pal[32][16],fairy_cell_start[32];
extern const u32 fairy_cells[];
static void trip_header(void){
 u16 i,x,y;char digits[9];u32 pixels[8];
 PAL_setColor(47,0x00E);PAL_setColor(63,0x0EE);
 PAL_setPalette(PAL1,fairy_pal[0],CPU);
 VDP_loadTileData(fairy_cells+((u32)(fairy_cell_start[0]+2)<<5),128,4,CPU);
 for(y=0;y<8;y++){u32 row=0;for(x=0;x<8;x++){
  const u8 *glyph=hud_font+(0x1EC<<4);
  row=(row<<4)|(((glyph[y*2+(x>>2)]>>(6-2*(x&3)))&3)?15:0);
 }pixels[y]=row;}VDP_loadTileData(pixels,132,1,CPU);
 VDP_setTextPalette(PAL2);VDP_drawText("1UP",7,0);VDP_drawText("HIGH SCORE",17,0);
 VDP_setTextPalette(PAL0);VDP_drawText("00",9,1);
 for(i=0;i<4;i++){digits[i*2]='0'+(game_high_score[i]>>4);digits[i*2+1]='0'+(game_high_score[i]&15);}digits[8]=0;
 for(i=0;i<6&&digits[i]=='0';i++);VDP_drawText(digits+i,25-(8-i),1);
 for(i=0;i+1<opt_lives&&i<8;i++)VDP_setTileMapXY(BG_A,TILE_ATTR_FULL(PAL3,1,0,0,132),2+i,3);
}
static void trip_marker(void){
 VDP_setSpriteFull(0,40+(opt_trip-1)*56,160,SPRITE_SIZE(2,2),TILE_ATTR_FULL(PAL1,1,0,0,128),0);
 VDP_updateSprites(1,CPU);
}
static void draw(void){
 char num[8];const char *controls[]={"BOTH","D-PAD + B","ARCADE A/C + B","D-PAD + A/B/C"};
 draw_full=page!=drawn_page;
 if(draw_full){VDP_setEnable(FALSE);VDP_clearPlane(BG_A,TRUE);}
 if(page==3){
  u16 i;char digit[2]={0,0};
  if(draw_full){
   trip_header();VDP_drawText("TO SELECT TRIP NUMBER",10,7);
   VDP_drawText("PUSH RIGHT OR LEFT BUTTON",8,10);
   VDP_drawText("TRIP NUMBER",14,14);
   VDP_drawText("TO START PUSH JUMP BUTTON",8,25);
   VDP_drawText("UP: BACK",16,27);
  }
  for(i=0;i<5;i++){digit[0]='1'+i;VDP_setTextPalette(opt_trip==i+1?PAL2:PAL0);VDP_drawText(digit,5+i*7,17);}
  VDP_setTextPalette(PAL0);trip_marker();
  goto done;
 }
 if(page==2){
  u16 id=sound_ids[sound_cursor],off=(id-MUS_FIRST)*TR_SIZE;
  const char *name=id==0xA1?"GAME OVER":id==0x89?"OUTWARD MUSIC":id==0x8A?"RETURN MUSIC":(mus_blob[off+TR_LOOP]!=255||mus_blob[off+TR_LOOP+1]!=255)?"LOOPING MUSIC":"ONE-SHOT SOUND";
  if(draw_full)VDP_drawText("PAC-LAND SOUND TEST",4,3);
  VDP_drawText("TRACK",8,8);intToStr(sound_cursor+1,num,2);VDP_drawText(num,15,8);VDP_drawText("OF",19,8);intToStr(sound_count,num,2);VDP_drawText(num,23,8);
  VDP_clearTextArea(0,11,40,1);VDP_drawText(name,(40-strlen(name))/2,11);intToHex(id,num,2);VDP_drawText("ID",17,14);VDP_drawText(num,20,14);
  VDP_drawText("LEFT/RIGHT: SELECT",9,20);VDP_drawText("A: PLAY     C: STOP",9,22);VDP_drawText("B: OPTIONS   START: PLAY GAME",6,26);preview();goto done;
 }
 if(page==1){
  if(draw_full)VDP_drawText("RANDOMISER SETTINGS",4,3);
  row(0,"SHUFFLE TRIPS",opt_shuffle_trips?"ON":"OFF");
  row(1,"SHUFFLE ENEMIES",opt_shuffle_enemies?"ON":"OFF");
  row(2,"SHUFFLE ITEMS",opt_shuffle_items?"ON":"OFF");
  intToHex(opt_seed,num,4);row(3,"SEED",num);row(4,"BACK","");
  if(draw_full)VDP_drawText("SEED REPEATS THE REMIX CHOICES",4,20);
  if(draw_full)VDP_drawText("A/C TOGGLE   LEFT/RIGHT CHANGE",4,25);
  if(draw_full)VDP_drawText("B BACK   START PLAY",10,26);preview();goto done;
 }
 if(draw_full)VDP_drawText("PAC-LAND OPTIONS",4,3);
 row(0,"CHARACTER",opt_region?"JAPAN":"USA");
 row(1,"PIXEL ASPECT",g_corrected?"WIDE":"ARCADE");
 intToStr(opt_lives,num,1);row(2,"STARTING LIVES",num);
 row(3,"CONTROLS",controls[g_control_mode]);
 row(4,"RANDOMISER",opt_random==3?"CUSTOM":opt_random==2?"FULL":opt_random?"TRIPS":"OFF");
 intToHex(opt_seed,num,4);row(5,"SEED",num);
 row(6,"ATTRACT DEMO",opt_attract?"ON":"OFF");
 row(7,"START GAME","");row(8,"SOUND TEST","");row(9,"BACK TO TITLE","");
 if(draw_full)VDP_drawText("UP/DOWN SELECT   LEFT/RIGHT CHANGE",3,25);
 if(draw_full)VDP_drawText("A/C CHANGE   B BACK   START PLAY",4,26);
 if(draw_full||drawn_cursor!=cursor){
  VDP_clearTextArea(0,1,40,1);
  if(cursor==4)VDP_drawText("A: RANDOMISER SETTINGS",4,1);
  else if(g_save_status==2)VDP_drawText("SAVING UNAVAILABLE",4,1);
 }
 preview();
done:
 drawn_page=page;drawn_cursor=cursor;
 if(draw_full)VDP_setEnable(TRUE);
}
static void open_page(u8 initial){
 u16 i;
 audio_stop();g_demo=0;g_state=4;cursor=0;page=initial;drawn_page=255;preview_key=0xFFFF;
 DMA_clearQueue(); /* discard title scroll/SAT queued earlier in this tick */
 sound_count=0;sound_cursor=0;for(i=0;i<MUS_TRACKS;i++)if(mus_blob[i*TR_SIZE+TR_NV])sound_ids[sound_count++]=(u8)(MUS_FIRST+i);
 VDP_setEnable(FALSE);VDP_setWindowOff();VDP_clearPlane(BG_A,TRUE);VDP_clearPlane(BG_B,TRUE);
 VDP_setScrollingMode(HSCROLL_PLANE,VSCROLL_PLANE);VDP_setHorizontalScroll(BG_A,0);VDP_setHorizontalScroll(BG_B,0);
 renderer_font();PAL_setColors(0,palette_black,64,CPU);PAL_setColor(15,0xEEE);
 VDP_setTextPalette(PAL0);VDP_setTextPriority(TRUE);draw();VDP_setEnable(TRUE);
}
void options_open(void){open_page(0);}
void options_trip_open(void){open_page(3);}
static void start(void){u16 round;audio_stop();save_commit();VDP_setTextPriority(FALSE);game_new();round=options_first_round();R6[0x2236]=(u8)round;game_start(round);}
void options_frame(u16 fresh){
 s16 d=(fresh&BUTTON_LEFT)?-1:1;
 if(page==3){
  if(fresh&BUTTON_UP){save_commit();VDP_setTextPriority(FALSE);g_state=0;renderer_menu();return;}
  if(fresh&(BUTTON_START|BUTTON_B|(g_control_mode==3?(BUTTON_A|BUTTON_C):0))){start();return;}
  if(fresh&(BUTTON_LEFT|BUTTON_A))opt_trip=opt_trip==1?5:opt_trip-1;
  if(fresh&(BUTTON_RIGHT|BUTTON_C))opt_trip=opt_trip==5?1:opt_trip+1;
  if(fresh)draw();return;
 }
 if(fresh&BUTTON_START){page=3;draw();return;}
 if(page==2){
  if(fresh&BUTTON_B){audio_stop();page=0;cursor=8;draw();return;}
  if(fresh&BUTTON_C)audio_stop();
  if(fresh&BUTTON_LEFT)sound_cursor=sound_cursor?sound_cursor-1:sound_count-1;
  if(fresh&BUTTON_RIGHT)sound_cursor=(sound_cursor+1)%sound_count;
  if(fresh&BUTTON_A){audio_stop();M6[0x200+sound_ids[sound_cursor]]=1;}
  if(fresh)draw();return;
 }
 if(page==1){
  if(fresh&BUTTON_B){page=0;cursor=4;draw();return;}
  if(fresh&BUTTON_UP)cursor=cursor?cursor-1:4;
  if(fresh&BUTTON_DOWN)cursor=(cursor+1)%5;
  if(fresh&(BUTTON_LEFT|BUTTON_RIGHT|BUTTON_A|BUTTON_C)){
   if(cursor==0)opt_shuffle_trips^=1;
   else if(cursor==1)opt_shuffle_enemies^=1;
   else if(cursor==2)opt_shuffle_items^=1;
   else if(cursor==3)opt_seed+=(fresh&(BUTTON_A|BUTTON_C))?(u16)(vtimer|1):d;
   else{page=0;cursor=4;}
   options_sync_random();
  }
  if(fresh)draw();return;
 }
 if(fresh&BUTTON_B){save_commit();VDP_setTextPriority(FALSE);g_state=0;renderer_menu();return;}
 if(fresh&BUTTON_UP)cursor=cursor?cursor-1:9;
 if(fresh&BUTTON_DOWN)cursor=(cursor+1)%10;
 if(fresh&(BUTTON_LEFT|BUTTON_RIGHT|BUTTON_A|BUTTON_C))switch(cursor){
  case 0:if(japan_art_available)opt_region^=1;break;
  case 1:g_corrected^=1;break;
  case 2:opt_lives=(opt_lives+8+d)%9+1;break;
  case 3:g_control_mode=(g_control_mode+4+d)%4;break;
  case 4:if(fresh&BUTTON_A){page=1;cursor=0;}else preset(opt_random==3?(d>0?0:2):(opt_random+3+d)%3);break;
  case 5:opt_seed+=(fresh&(BUTTON_A|BUTTON_C))?(u16)vtimer:d;break;
  case 6:opt_attract^=1;break;
  case 7:page=3;draw();return;
  case 8:page=2;break;
  case 9:save_commit();VDP_setTextPriority(FALSE);g_state=0;renderer_menu();return;
 }
 if(fresh)draw();
}
