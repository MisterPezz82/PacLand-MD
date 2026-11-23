#include "game.h"
#include "options.h"
#include "save.h"
#define RECORD 48
static u8 current[RECORD];static u16 sequence,slot,valid;
volatile u16 g_save_status;
static u16 crc(const u8 *p){u16 c=0xFFFF,i,b;for(i=0;i<44;i++){c^=(u16)p[i]<<8;for(b=0;b<8;b++)c=(c&0x8000)?(u16)((c<<1)^0x1021):(u16)(c<<1);}return c;}
static u16 word(const u8 *p){return (u16)(p[0]<<8|p[1]);}
static u8 good(const u8 *p){
 u16 i;if(p[0]!='P'||p[1]!='L'||p[2]!='M'||p[3]!='D'||p[4]!=1||p[5]!=RECORD||p[46]!='O'||p[47]!='K'||crc(p)!=word(p+44))return 0;
 if(p[22]>5)return 0;
 if(p[8]>1||p[9]<1||p[9]>9||p[10]>3||p[11]>1||p[12]>1||p[13]>1||p[14]>1||p[15]>1)return 0;
 for(i=18;i<22;i++)if((p[i]&15)>9||(p[i]>>4)>9)return 0;return 1;
}
void save_load(void){
 u8 a[RECORD],b[RECORD],*chosen;u16 i,owned,ga,gb;
 SYS_disableInts();owned=Z80_getAndRequestBus(TRUE);SRAM_enableRO();
 for(i=0;i<RECORD;i++){a[i]=SRAM_readByte(i);b[i]=SRAM_readByte(64+i);}
 SRAM_disable();if(!owned)Z80_releaseBus();SYS_enableInts();
 ga=good(a);gb=good(b);valid=0;g_save_status=0;if(!ga&&!gb)return;
 slot=gb&&(!ga||(s16)(word(b+6)-word(a+6))>0)?1:0;chosen=slot?b:a;
 for(i=0;i<RECORD;i++)current[i]=chosen[i];sequence=word(chosen+6);valid=1;
 opt_trip=chosen[22]?chosen[22]:1;
 opt_region=chosen[8]&&japan_art_available;opt_lives=chosen[9];g_control_mode=chosen[10];g_corrected=chosen[11];opt_attract=chosen[12];
 opt_shuffle_trips=chosen[13];opt_shuffle_enemies=chosen[14];opt_shuffle_items=chosen[15];opt_seed=word(chosen+16);options_sync_random();
 for(i=0;i<4;i++)game_high_score[i]=chosen[18+i];g_save_status=1;
}
void save_commit(void){
 u8 next[RECORD],verify[RECORD];u16 i,c,owned,target=valid?(slot^1):0;
 for(i=0;i<RECORD;i++)next[i]=0;
 next[0]='P';next[1]='L';next[2]='M';next[3]='D';next[4]=1;next[5]=RECORD;
 c=sequence+1;next[6]=c>>8;next[7]=(u8)c;
 next[8]=(u8)opt_region;next[9]=(u8)opt_lives;next[10]=(u8)g_control_mode;next[11]=(u8)g_corrected;next[12]=(u8)opt_attract;
 next[13]=(u8)opt_shuffle_trips;next[14]=(u8)opt_shuffle_enemies;next[15]=(u8)opt_shuffle_items;next[16]=opt_seed>>8;next[17]=(u8)opt_seed;
 next[22]=(u8)opt_trip;
 for(i=0;i<4;i++)next[18+i]=game_high_score[i];
 if(valid){for(i=8;i<44&&next[i]==current[i];i++);if(i==44)return;}
 c=crc(next);next[44]=c>>8;next[45]=(u8)c;next[46]='O';next[47]='K';
 SYS_disableInts();owned=Z80_getAndRequestBus(TRUE);SRAM_enable();
 SRAM_writeByte(target*64+46,0);SRAM_writeByte(target*64+47,0);
 for(i=0;i<46;i++)SRAM_writeByte(target*64+i,next[i]);
 SRAM_writeByte(target*64+46,'O');SRAM_writeByte(target*64+47,'K');
 for(i=0;i<RECORD;i++)verify[i]=SRAM_readByte(target*64+i);
 SRAM_disable();if(!owned)Z80_releaseBus();SYS_enableInts();
 for(i=0;i<RECORD;i++)if(verify[i]!=next[i]){g_save_status=2;return;}
 for(i=0;i<RECORD;i++)current[i]=next[i];sequence=word(next+6);slot=target;valid=1;g_save_status=1;
}
