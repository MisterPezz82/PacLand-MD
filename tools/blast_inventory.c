/* Offline sprite census. Uses the retained arcade engine, not a shipping
 * runtime or hand-maintained assumptions about each round's common frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_rt.h"
static u8 rom[0x20000];
static u32 frequency[131072];
static u16 be(const u8 *p){return (u16)(p[0]<<8|p[1]);}
static void observe(void){
 int pass,i,centre=(int)be(R6+0x240C)+(R6[0x208E]?-32:32);
 for(pass=0;pass<2;pass++){
  u8 *o=R6+(pass?0x2800:0x2400);int n=R6[pass?0x20DD:0x20DC];
  for(i=0;i<n;i++,o+=32){
   u16 group=o[5]&63,fl=o[6],size=(fl>>2)&3,code=(o[4]|((fl&128)<<1))&~size;
   int dx=(int)be(o+12)-centre;
   if(o[9]||dx < -224||dx>224||group==47)continue;
   if(o==R6+0x2400&&size==3&&!(fl&2)&&code<112&&group<4)continue;
   frequency[code|(size<<9)|(group<<11)]++;
  }
 }
}
int main(int argc,char **argv){
 FILE *f,*out;int round,frame;
 if(argc!=3)return 2;f=fopen(argv[1],"rb");if(!f)return 2;
 if(fread(rom,1,sizeof rom,f)!=sizeof rom)return 2;fclose(f);ROM6=rom;rc_arcade_video=0;
 out=fopen(argv[2],"wb");if(!out)return 2;
 for(round=0;round<32;round++){
  u32 k,count=0,types=0;int i;
  memset(R6,0,sizeof R6);memset(M6,0,sizeof M6);memset(frequency,0,sizeof frequency);
  rc_new_game(9);rc_setup((u16)round,0);
  for(i=0;i<R6[0x20DD];i++){u8 type=R6[0x2801+i*32];if(type==30)types|=1;if(type==32)types|=2;}
  observe();
  for(frame=0;frame<2400;frame++){
   rc_input((round&3)==3,(round&3)!=3,frame%23<6);rc_irq();rc_frame();observe();
   if(R6[0x2063]){
    int t;rc_death_start();
    for(t=0;t<250;t++){u8 done;rc_irq();done=rc_death_tick();observe();if(done)break;}
    rc_setup((u16)round,1);
   }
  }
  for(k=0;k<131072;k++)if(frequency[k])count++;
  fwrite(&count,4,1,out);fwrite(&types,4,1,out);
  for(k=0;k<131072;k++)if(frequency[k]){fwrite(&k,4,1,out);fwrite(frequency+k,4,1,out);}
 }
 fclose(out);return 0;
}
