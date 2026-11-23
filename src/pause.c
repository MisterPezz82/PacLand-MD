#include "game.h"
#include "pause.h"
volatile u16 g_pause_cursor;
static u16 blocked;
void pause_filter(u16 *pad,u16 *fresh){
 if(g_state==5)return;
 blocked&=*pad;*pad&=~blocked;*fresh&=~blocked;
}
static void cursor(void){renderer_pause_cursor(g_pause_cursor);}
void pause_open(void){
 g_state=5;g_pause_cursor=0;renderer_pause();cursor();
}
void pause_frame(u16 pad,u16 fresh){
 u16 resume=0,exit=0;
 if(fresh&(BUTTON_START|BUTTON_B))resume=1;
 else if(fresh&(BUTTON_A|BUTTON_C)){if(g_pause_cursor)exit=1;else resume=1;}
 else if(fresh&(BUTTON_UP|BUTTON_DOWN)){g_pause_cursor^=1;SYS_doVBlankProcess();cursor();}
 if(!resume&&!exit)return;
 blocked=pad&(BUTTON_A|BUTTON_B|BUTTON_C|BUTTON_START|BUTTON_UP|BUTTON_DOWN);
 VDP_setTextPriority(FALSE);
 if(exit)game_leave();
 else{g_state=1;renderer_load();renderer_frame();}
}
