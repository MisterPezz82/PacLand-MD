#ifndef RC_HOST
#pragma GCC optimize ("O2")
#endif
#include "blast.h"
volatile u16 g_blast_native,g_blast_fallback,g_blast_entities;
#ifndef RC_HOST
extern u8 object_fast_entries[];
#define DRAW_DIRTY(o) (object_fast_entries[(u16)((o)-(R6+0x2400))+25]=1)
static void own_car(u8 *o){u8 *f=object_fast_entries+(u16)(o-(R6+0x2400));if(!f[31]){f[31]=f[63]=f[95]=1;f[25]=f[57]=f[89]=1;}}
#else
#define DRAW_DIRTY(o) ((void)0)
#define own_car(o) ((void)0)
#endif
/* Compact, aligned contact state is loaded once for a whole platform group.
 * Work on distant segments ends at the horizontal test, without any writes to
 * arcade scratch RAM. Commit meaningful player/contact results in slot order. */
typedef struct {u16 x,y,cap_y,vy;u8 width,height;} Contact;
/* 8452 treats this byte as its pending-spawn gate. Terrain, narrow contact
 * and angle conversion are its producers in the arcade engine. Preserve
 * that one observable effect explicitly, independently of virtual registers. */
#define SPAWN_GATE R6[0x2016]
static u16 word(const u8 *p){return (u16)(p[0]<<8|p[1]);}
static void put(u8 *p,u16 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void contact_load(Contact *p){
 p->x=PW16(R6+0x240C);p->y=word(R6+0x2409);p->cap_y=word(R6+0x2429);
 p->vy=word(R6+0x240F);p->width=R6[0x2408];p->height=R6[0x2407];
}
/* Broad phase belongs to the group update. Most segments can never touch the
 * player this tick; don't enter the narrow-phase function for those segments. */
static inline u16 nearby(const Contact *p,const u8 *o){
 u16 x=PW16(o+12),d=x>=p->x?x-p->x:p->x-x;
 return d<=(u8)(o[8]+p->width)&&!(p->y>>8);
}
/* Return 1 landing, 2 side, 0 none. Narrow matches the car's C0AD entry. */
static u16 contact(Contact *p,const u8 *o,u16 narrow){
 u16 ox=PW16(o+12),dx=ox>=p->x?ox-p->x:p->x-ox;
 u8 dy,delta,overlap,side;
 if(dx>255||(u8)(o[8]+p->width)<dx)return 0;
 if(!((u16)(word(o+9)-224)&0x8000)||(p->y>>8))return 0;
 delta=(u8)(o[10]-(u8)p->y);dy=o[10]<(u8)p->y?(u8)-delta:delta;
 if(dy&128)return 0;
 SPAWN_GATE=dy;
 overlap=(u8)(o[7]+p->height+delta);
 if(narrow!=2&&!(overlap&128)&&((u8)(overlap-15)&128)&&o[8]>=dx&&
    ((u16)(p->vy-word(o+15))&0x8000)){
  p->y=(u16)(p->y+(s16)(s8)overlap);p->cap_y=(u16)(p->cap_y+(s16)(s8)overlap);
  R6[0x20E9]=o[0];R6[0x20EA]=o[1];R6[0x20E8]|=1;
  put(R6+0x2409,p->y);put(R6+0x2429,p->cap_y);return 1;
 }
 side=(u8)((u8)(o[7]+p->height)>>1);if(narrow==1)side=(u8)((side>>2)+(side>>1));
 if(side<dy)return 0;
 side=(u8)((u8)(o[8]+p->width)>>1);if(narrow==1)side=(u8)((side>>2)+(side>>1));
 side++;if(side<dx)return 0;side=(u8)(side-dx);
 if(!((u16)(ox-p->x)&0x8000))side=(u8)-side;
 R6[0x2013]=side;R6[0x20E6]=o[0];R6[0x20E7]=o[1];R6[0x20E5]|=1;return 2;
}
static void move_y(u8 *o){
 u16 f=(u16)(o[11]+o[16]);s8 v=(s8)(u8)(o[15]+(f>>8));
 o[11]=(u8)f;put(o+9,(u16)(word(o+9)+(s16)v));
}
static void move_x(u8 *o){
 u16 f=(u16)(o[14]+o[18]);s8 v=(s8)(u8)(o[17]+(f>>8));
 o[14]=(u8)f;PW16_SET(o+12,(u16)(PW16(o+12)+(s16)v));
}
/* Native ordinary player movement, terrain, riding and repeat boots jumps.
 * Special launches and death retain a whole reference actor. Declining a
 * state never runs a partial native frame before its reference fallback. */
static void player_velocity_native(u8 *o,u16 air){
 u16 v=word(o+17),cap=word(R6+0x20EB),mid=word(R6+0x20ED),low=word(R6+0x20EF);
 u16 step=word(R6+0x20F5),acc=word(R6+0x20F3),mag;
 if(air){step>>=1;acc>>=1;}
 s16 press=(R6[0x2095]&1)-(R6[0x2093]&1),held=(R6[0x2096]&1)-(R6[0x2094]&1);
 if(press>0){v=(u16)(v+acc);if(!((u16)(v-cap)&0x8000))v=cap;}
 else if(press<0){v=(u16)(v-acc);if((u16)(v+cap)&0x8000)v=(u16)-cap;}
 else if(held>0){
  if(!((u16)(v-mid)&0x8000)||((u16)(v-low)&0x8000))v=(u16)(v+(((u16)(v-low)&0x8000)?step:-step));
 }else if(held<0){
  mag=(u16)-v;
  if(!((u16)(mag-mid)&0x8000)||((u16)(mag-low)&0x8000))v=(u16)(v+(((u16)(v+low)&0x8000)?step:-step));
 }else{
  mag=(v&0x8000)?(u16)-v:v;
  if((u16)(mag-(air?16:word(R6+0x20F1)))&0x8000)v=0;
  else{step=word(R6+0x20F7);if(air)step>>=1;v=(u16)(v+((v&0x8000)?step:-step));}
 }
 put(o+17,v);
 if(!R6[0x2219])R6[0x20FD]=R6[0x2115]=R6[0x2222]=0;
}
static u16 terrain_at(u16 x,u8 sample,u16 *at){
 u16 h=x>>1,q;u8 attr,n,d;
 SPAWN_GATE=(u8)x;
 /* Domain of actual level coordinates; the original's unusual high-input
  * restoring path is retained too, for wrapping edge probes. */
 if(x<0x9000){extern const u8 rc_div9[1152];q=rc_div9[h>>4];attr=(u8)(h-q*144);q=(q&127)<<4;}
 else{
  u8 a=(u8)(x>>8),b=(u8)x,i;
  for(i=0;i<7;i++){u8 c=b>>7,top=a>>7;b<<=1;a=(u8)(a<<1|c);if(top||a>=144){a-=144;b++;}}
  attr=a;q=(b&127)<<4;
 }
 n=R6[(0x3001+q)&0x3FFF];*at=(u16)(word(ROM6+(u16)(0xD4E8+(s8)(u8)(n<<1)))+(attr>>2));
 n=ROM6[(u16)(0xD4C5+(s8)n)];d=(u8)(sample-ROM6[*at]);
 if(sample<=ROM6[*at])return 1;
 while(--n){*at+=36;d=(u8)(sample-ROM6[*at]);if(sample<=ROM6[*at])return d>=248;}
 return 0;
}
static u16 player_terrain(u8 *o,u16 oldx){
 u16 y=word(o+9),x=PW16(o+12),at;u8 left,right,stage=R6[0x2074];s8 half=(s8)o[8]>>1;
 if(y>=224)return 0;
 if((R6[0x20E5]&2)&&(R6[0x20E7]==23||R6[0x20E7]==24))return 0;
 if((R6[0x20E8]&2)&&R6[0x20EA]==43)return 0;
 put(R6+0x2003,y);left=(u8)terrain_at((u16)(x-half),(u8)y,&at);R6[0x2005]=left;
 put(R6+0x2003,y);right=(u8)terrain_at((u16)(x+half),(u8)y,&at);
 if(right!=left){
  if(right<left)x=x<oldx?oldx:(u16)(x+2);
  else x=x>oldx?oldx:(u16)(x-2);
  PW16_SET(o+12,x);
 }
 if(!(o[15]&128)){
  if(stage>=6&&stage<18){u16 sample=(u16)(y+(o[7]>>1));put(R6+0x2003,sample);
   if(terrain_at(x,(u8)sample,&at)){put(o+15,0xFE00);put(o+28,0xFFE0);}
  }
  return 0;
 }
 {u16 sample=(u16)(y+(s16)(s8)(u8)-o[7]);put(R6+0x2003,sample);
  if(!terrain_at(x,(u8)sample,&at))return 0;
 }
 put(o+9,(u8)(ROM6[at]+o[7]));put(o+15,0xFC00);put(o+28,0xFFE0);return 1;
}
static void air_animation(u8 *o){
 u16 vx=word(o+17),speed=(vx&0x8000)?(u16)-vx:vx,vy=word(o+15),limit=(u16)(word(R6+0x20EF)-16);u8 code;
 if(speed<limit)code=!((u16)(vy-256)&0x8000)?0x48:!((u16)(vy-0xFF00)&0x8000)?0x4C:0x50;
 else if(!((u16)(vy-768)&0x8000))code=0x54;
 else if(!((u16)(vy-256)&0x8000))code=0x58;
 else if(!((u16)(vy-0xFF00)&0x8000))code=0x5C;
 else{u8 held=R6[(vx&0x8000)?0x2094:0x2096];code=!((u16)(vy-0xFD00)&0x8000)?(held?0x60:0x68):(held?0x64:0x6C);}
 o[4]=code;
 if(R6[0x2222]&&(R6W(0x222A)>=64||!(R6[0x222B]&8)))o[5]=0x1F;
 else o[5]=(word(R6+0x20E1)?1:0)+(R6[0x20FD]?2:0);
 if(speed>=limit){if(vx&0x8000)o[6]&=0xFE;else o[6]|=1;}
 PW16_SET(o+36,R6[0x2115]?0xF83E:0x3C00);
}
static void ground_animation(u8 *o){
 u16 vx=word(o+17),speed=(vx&0x8000)?(u16)-vx:vx,phase,table,limit=(u16)(word(R6+0x20EF)+32),period;u8 index;
 if(!speed){R6[0x2200]=0;put(R6+0x2201,0);o[4]=4;}
 else{
  if(speed<limit){table=0x8E2E;period=1024;}else{table=0x8E37;period=1280;}
  phase=(u16)(word(R6+0x2201)+speed);
  while(phase>=period){phase-=period;R6[0x2200]++;}put(R6+0x2201,phase);
  if(R6[(vx&0x8000)?0x2096:0x2094]){M6[0x29C]=1;index=8;}else index=R6[0x2200]&7;
  o[4]=ROM6[table+index];
 }
 limit=(u16)(word(R6+0x20EF)-16);
 if(R6[0x2222]&&(R6W(0x222A)>=64||!(R6[0x222B]&8)))o[5]=0x1F;
 else o[5]=(word(R6+0x20E1)?1:0)+(R6[0x20FD]?2:0);
 if(speed>=limit){if(vx&0x8000)o[6]&=0xFE;else o[6]|=1;}
 PW16_SET(o+36,R6[0x2115]?0xF83E:0x3C00);
}
static u8 *platform(void){u16 id=R6[0x20E9];return R6+(id>=64?0x2400+(id-64)*32:0x2800+id*32);}
static void player_jump_native(u8 *o){
 u16 speed=word(o+17),min=word(R6+0x20EF),v,product;
 if(speed&0x8000)speed=(u16)-speed;if(speed<min)speed=min;if(o[3]==5)speed>>=1;
 v=(u16)(speed*3);if(word(R6+0x20E1))v=(u16)(v+(v>>2));put(o+15,v);
 product=(u16)((u16)((s16)speed>>3)*22);put(R6+0x2024,(u16)((u8)((s16)speed>>3)*22));put(o+28,(u16)-(product>>4));
 if(o[3]==3){u8 *p=platform();put(o+15,(u16)(v+((s16)word(p+15)>>1)));put(o+17,(u16)(word(o+17)+((s16)word(p+17)>>1)));}
 o[3]=4;M6[0x28E]=1;
}
static void player_post(u8 *o,u16 oldx){
 u16 table=(u16)(0x9047+(s8)(u8)((u8)(o[4]-4)>>1)),x=PW16(o+12),y=word(o+9),distance;u8 stage=R6[0x2074],water=0;
 o[38]=(o[6]&1)|128;put(o+41,(u16)(y+ROM6[table]));
 PW16_SET(o+44,(u16)(x+(s16)(s8)((o[6]&1)?ROM6[table+1]:(u8)-ROM6[table+1])));
 if(!R6[0x2220]){
  distance=(u16)(x-oldx);if(distance&0x8000)distance=(u16)-distance;
  distance=(u16)(word(R6+0x2203)+distance);put(R6+0x2203,distance);
  while(!((u16)(distance-29)&0x8000)){
   distance-=29;put(R6+0x2203,distance);R6W_SET(0x2070,0);R6W_SET(0x2072,0x10);blast_distance_score();
   distance=word(R6+0x2203);
  }
 }
 if(stage>=13&&stage<18){if((u16)(y-56)&0x8000){put(o+9,40);water=1;}}
 else if(stage>=18&&stage<23){if((u16)(y-48)&0x8000){put(o+9,40);water=1;}}
 else if(stage>=23&&stage<29){
  if(stage==26){if((u16)(y-70)&0x8000){put(o+9,70);water=1;}}
  else if(y&0x8000){put(o+9,0);R6[0x2063]=1;R6[0x2212]=2;}
 }else if(y&0x8000){R6[0x2063]=1;R6[0x2212]=2;}
 if(water){u16 i;u8 *slot=R6+0x2800;R6[0x2063]=1;R6[0x2212]=1;
  for(i=R6[0x20DD];i;i--,slot+=32)if(slot[1]==23)slot[3]=0;
 }
}
static u16 player_air(u8 *o){
 u16 v,oldx,state=o[3];
 u32 cap;
 if(state>5)return 0;
 if(state==3){if(!(R6[0x20E8]&1))state=4;}
 else if(R6[0x20E8]&1)state=2;
 /* Special launch controllers retain their entire reference update. */
 if((state==2||state==3)&&R6[0x2097]&&
    (R6[0x20EA]==43||(R6[0x20EA]>=27&&R6[0x20EA]<30)))return 0;
 cap=*(const u32 *)(o+36);oldx=PW16(o+12);put(R6+0x20FB,oldx);R6[0x20E5]<<=1;R6[0x20E8]<<=1;
 v=word(R6+0x20E1);if(v)put(R6+0x20E1,(u16)(v-1));
 v=R6W(0x222A);if(v){R6W_SET(0x222A,(u16)(v-1));if(v==1)R6[0x2222]=0;}
 o[3]=(u8)state;
 if(state==0||state==2){
  put(o+15,0xFE00);put(o+28,0xFFE0);
  if(!R6[(o[17]&128)?0x2094:0x2096])put(o+17,0);
  o[3]=(u8)(state==0?1:3);state=o[3];
 }
 if(state==4||state==5){
  if(state==4)R6[0x2097]=0;
  o[3]=5;put(o+15,(u16)(word(o+15)+word(o+28)));player_velocity_native(o,1);move_y(o);move_x(o);
  if(player_terrain(o,oldx))o[3]=0;
  else if(R6[0x2097]&&R6[0x20FD]&&((u16)(word(o+9)-184)&0x8000))player_jump_native(o);
  air_animation(o);
 }else{
  u16 landed;player_velocity_native(o,0);move_y(o);move_x(o);
  if(state==3){u8 *p=platform();u16 f=(u16)(o[11]+p[16]);o[11]=(u8)f;put(o+9,(u16)(word(o+9)+(s16)(s8)(u8)(p[15]+(f>>8))));
   f=(u16)(o[14]+p[18]);o[14]=(u8)f;PW16_SET(o+12,(u16)(PW16(o+12)+(s16)(s8)(u8)(p[17]+(f>>8))));
  }
  landed=player_terrain(o,oldx);
  if(state==3){if(landed)o[3]=0;if(R6[0x2097])player_jump_native(o);}
  else if(!landed)o[3]=4;else if(R6[0x2097])player_jump_native(o);
  ground_animation(o);
 }
 player_post(o,oldx);DRAW_DIRTY(o);if(cap!=*(const u32 *)(o+36))DRAW_DIRTY(o+32);return 1;
}
static void bridge(u8 *base){
 Contact p;u8 *o;u16 i,n=(u16)(word(base+19)+1);put(base+19,n);
 if(!(n&127))for(i=0,o=base;i<12;i++,o+=32)if(o[3]<2){
  u16 v=word(ROM6+0xB157+i*2);put(o+15,(n&256)?(u16)-v:v);
 }
 contact_load(&p);
 for(i=0,o=base;i<12;i++,o+=32){
  u16 hit;
  if(o[3]<2){
   if(!o[3])o[3]=1;
   if(R6[0x2074]==0x19&&!o[30]&&!--o[31])o[3]=2;
   move_y(o);hit=nearby(&p,o)?contact(&p,o,0):0;
   if(hit==1&&!(R6[0x2002]&31))o[3]=2;
  }else if(o[3]<4){
   if(o[3]==2){put(o+15,0xFFC0);o[31]=0;o[3]=3;}
   move_y(o);if(o[9]&128)o[3]=4;
   o[31]++;
   if(!(o[31]&3)){u16 a=(o[31]&28)>>1;o[4]=ROM6[0xB1EE + a];o[6]=ROM6[0xB1EF+a];DRAW_DIRTY(o);}
   if(nearby(&p,o))contact(&p,o,0);
  }
 }
}
static void logs(u8 *base){
 Contact p;u16 j,i;contact_load(&p);
 for(j=0;j<2;j++,base+=96){
  u8 *o=base;u16 y,v;move_y(o);y=word(o+9);
  if((y&0x8000)||(u8)y<o[22]){v=R6W(0x34E8);goto velocity;}
  if((u8)y>=o[21]){v=(u16)-R6W(0x34E8);goto velocity;}
  goto positions;
velocity:put(o+15,v);put(o+47,v);put(o+79,v);
positions:put(o+41,y);put(o+73,y);
  if(!((u16)(y-8)&0x8000))for(i=0;i<3;i++)if(nearby(&p,o+i*32)&&contact(&p,o+i*32,0)==2&&!(p.vy&0x8000)){
   p.vy=(u16)-p.vy;put(R6+0x240F,p.vy);
  }
  v=(u16)(word(o+19)+word(o+17));put(o+19,v);
  {u8 code=(u8)(0x56-((v>>8)&6));if(o[4]!=code){DRAW_DIRTY(o);DRAW_DIRTY(o+32);DRAW_DIRTY(o+64);o[4]=o[36]=o[68]=code;}}
 }
}
static u16 car_component(u16 speed,u8 angle){
 u16 scale=ROM6[0x9531+(angle&7)],v;
 v=(u16)((((u32)speed*scale)>>8)<<1);return (angle&8)?(u16)-v:v;
}
static void car_direction(u8 *o,u8 angle){
 u16 speed=word(o+27),vx;o[29]=angle;
 put(o+15,car_component(speed,(u8)(angle+4)));vx=car_component(speed,angle);put(o+17,vx);
 /* 94F3 falls through into 9504 once more after storing X velocity. Its
  * unused result still produces the spawn gate, using X velocity's high
  * byte as the final angle. Keep the observable effect, not CPU scratch. */
 SPAWN_GATE=(u8)(((u16)(u8)speed*ROM6[0x9531+((vx>>8)&7)])>>8);
}
static u16 car(u8 *o){
 Contact p;u16 d,v;u8 tick=R6[0x2002];
 if(o[3]>9||o[3]==1||(o[19]&0xF0)!=0x40)return 0;
 own_car(o);contact_load(&p);M6[0x293]=1;
 if(!o[3]){
  u8 colour=(u8)(o[1]*3+3);o[68]=(u8)(ROM6[0x9C55+(s8)(u8)(o[1]-1)]+11);o[5]=o[37]=o[69]=colour;put(o+7,0x1010);
  o[6]=o[20]?12:13;o[38]=o[70]=o[20]?128:129;o[3]=2;DRAW_DIRTY(o);DRAW_DIRTY(o+32);DRAW_DIRTY(o+64);
 }
 if(o[3]==2){
  v=ROM6[0x97FA+(o[19]&3)];put(o+9,v);o[21]=(u8)v;
  put(o+27,word(R6+0x34B0+(u16)(o[1]-1)*2));car_direction(o,o[20]?12:4);o[3]=3;
 }
 if(o[3]==3){
  v=(u16)((word(o+9)-p.y)<<1);d=(u16)(PW16(o+12)-p.x);if(d&0x8000)d=(u16)-d;
  if((u16)(d-v)&0x8000)o[3]=(o[19]&12)?4:8;
 }else{
  if(o[3]==4){car_direction(o,o[20]?10:6);o[3]=5;}
  if(o[3]==5){
   u16 at;u8 delta;terrain_at(PW16(o+12),0,&at);
   if(ROM6[at])delta=(u8)(o[10]-ROM6[at]-16);else delta=(u8)(o[10]-(u8)p.y);
   if(o[1]!=3)delta-=16;if(delta&128)o[3]=6;
  }else{
   if(o[3]==6){car_direction(o,o[20]?14:2);o[3]=7;}
   if(o[3]==7){if(!((u8)(o[10]-o[21])&128))o[3]=8;}
   else if(o[3]==8){car_direction(o,o[20]?12:4);o[3]=9;}
  }
 }
 move_y(o);move_x(o);
 if(!R6[0x2222]&&nearby(&p,o)&&contact(&p,o,1)==2){R6[0x2063]=1;R6[0x2212]=0;}
 if(!(tick&7)){o[4]=(tick&8)?0xB0:0xAC;DRAW_DIRTY(o);}
 if(!(tick&15)){
  u8 a=(u8)(ROM6[0x9C55+(s8)(u8)(o[1]-1)]+5);
  if(!(tick&16))a+=(o[19]&8)?3:1;o[36]=a;DRAW_DIRTY(o+32);
 }
 put(o+41,(u16)(word(o+9)+7));PW16_SET(o+44,PW16(o+12));
 put(o+73,(u16)(word(o+9)+19));PW16_SET(o+76,(u16)(PW16(o+12)+((o[6]&1)?-4:4)));
 d=(u16)(PW16(o+12)-p.x+(R6[0x208E]?32:-32));if(d&0x8000)d=(u16)-d;
 if(d>=288){o[2]=0;PW16_SET(o+12,0);PW16_SET(o+44,0);PW16_SET(o+76,0);}
 else if((u16)(word(o+9)-24)&0x8000)blast_car_retire(o);
 return 1;
}
static void fruit_reset(u8 *o){
 u8 trip=R6[0x2101]>>2,index=trip>=8?4:trip>=4?3:trip>=2?2:trip,code=ROM6[0xA1D5+index];
 o[2]=0;o[4]=code;o[5]=47;PW16_SET(R6+0x222C,(u16)(code<<8|45));o[6]=128;PW16_SET(o+12,0);put(o+7,0x0808);
}
static u16 fruit(u8 *o){
 u8 kind=o[19],state=o[3];u16 v;Contact p;
 if(kind==17&&state<=1){
  if(!state){o[31]=16;o[3]++;}
  if(!--o[31]){o[2]=0;PW16_SET(o+12,0);}else PW16_SET(o+4,(o[31]&8)?0xE434:0xE534);return 1;
 }
 if(kind==16&&state==1){if(!--o[31])fruit_reset(o);return 1;}
 if(kind>=8&&kind<=10&&state<=3){
  if(!state){if(kind==10)o[20]=36;else{v=kind==8?0x2020:0x2222;PW16_SET(o+20,v);}o[5]=47;PW16_SET(o+30,512);o[3]=1;state=1;}
  if(state==1){v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);
   if(!v)fruit_reset(o);
   else if((R6[0x20E8]&2)&&(R6[0x20EA]==o[20]||R6[0x20EA]==o[21])){o[3]=2;PW16_SET(o+30,256);}
  }else{
   if(state==2){PW16_SET(o+4,R6W(0x222C));o[3]++;}
   v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);if(!v){o[19]=17;o[3]=0;}
  }
  goto pickup;
 }
 if(kind>1||state>3||(!kind&&state==1))return 0;
 if(!kind){
  if(state==0){PW16_SET(o+30,512);o[3]=2;}
  if(state!=3){PW16_SET(o+4,R6W(0x222C));o[3]++;}
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);if(!v){o[19]=0x11;o[3]=0;}
 }else if(state<2){
  if(!state){o[4]=R6[0x222C];o[5]=0x2F;PW16_SET(o+30,512);o[3]++;}
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);if(!v){fruit_reset(o);goto pickup;}
  v=(u16)(PW16(R6+0x240C)-PW16(o+12));if(v&0x8000)v=(u16)-v;
  if((u16)(v-64)&0x8000){PW16_SET(o+30,256);o[3]=2;}
 }else{
  if(state==2){PW16_SET(o+4,R6W(0x222C));o[3]++;}
  v=(u16)(PW16(o+30)-1);PW16_SET(o+30,v);if(!v){o[19]=0x11;o[3]=0;}
 }
pickup:if(o[5]==0x2D){contact_load(&p);if(nearby(&p,o)&&contact(&p,o,2)){M6[0x29D]=1;o[19]=0x10;o[3]=0;}}
 return 1;
}
u16 blast_actor(u8 *o){
 if(!o[1])return player_air(o);
 if(o[1]==32){bridge(o);return 1;}
 if(o[1]==30){logs(o);return 1;}
 if(o[1]>=1&&o[1]<=5)return car(o);
 if(o[1]==12)return fruit(o);
 return 0;
}
u16 blast_fruit_run(u8 *o,u32 remaining){
 u16 n=0;
 while(n<remaining&&o[1]==12){if(o[2]&&!fruit(o))break;o+=32;n++;}
 return n;
}
u16 blast_player_run(u8 *o,u32 n){(void)n;return player_air(o);}
u16 blast_car_run(u8 *o,u32 n){(void)n;return car(o);}
u16 blast_bridge_run(u8 *o,u32 n){(void)n;bridge(o);return 1;}
u16 blast_logs_run(u8 *o,u32 n){(void)n;logs(o);return 1;}

BlastEntity blast_entities[96];
u16 blast_entity_count;
static u16 plan_primary,plan_level;
static u8 plan_dirty=1;
extern void (*const rc_update_fn[50])(void);
#ifndef RC_HOST
_Static_assert(sizeof(BlastEntity)==24,"native entity assembly layout");
#endif
void blast_reset(void){plan_dirty=1;}
void blast_plan_prepare(void){
 u16 pass,i;
 if(!plan_dirty&&plan_primary==R6[0x20DC]&&plan_level==R6[0x20DD])return;
 blast_entity_count=0;
 for(pass=0;pass<2;pass++){
  u16 n=R6[pass?0x20DD:0x20DC];u8 *o=R6+(pass?0x2800:0x2400);
  for(i=0;i<n;i++,o+=32){
   u16 gate=word(ROM6+0x82C0+2*o[1]),w,bias=0;BlastEntity *e;
   if(gate==0x82B1)continue;
   if(gate==0x826B)w=1;else if(gate==0x826D)w=2;
   else if(gate==0x8272){w=592;bias=432;}else if(gate==0x8287){w=432;bias=216;}
   else if(gate==0x829C){w=64;bias=32;}else{rc_trap(gate);return;}
   /* These two particle families return immediately while inactive. Make
    * that an activation condition instead of entering the reference ABI. */
   if(w==1&&(o[1]==20||o[1]==22))w=2;
   if(blast_entity_count>=96){rc_trap(0xB1A5);return;}
   e=blast_entities+blast_entity_count++;e->slot=o;e->type=o[1];e->left=n-i;e->width=w;e->bias=bias;e->legacy=rc_update_fn[o[1]];e->span=0;
   e->native=!o[1]?blast_player_run:o[1]<=5?blast_car_run:o[1]==12?blast_fruit_run:o[1]==30?blast_logs_run:o[1]==32?blast_bridge_run:0;
  }
 }
 /* Static-solid runs have exact union bounds. Reject up to eight commands
  * with one test, without changing any per-actor gate or update order inside
  * an intersecting run. Only these families leave their own X unchanged. */
 for(i=0;i<blast_entity_count;){
  BlastEntity *e=blast_entities+i;u16 j=i,low=0xFFFF,high=0;
  while(j<blast_entity_count&&j<i+8){
   BlastEntity *part=blast_entities+j;u16 x;
   if((part->type!=18&&part->type!=19)||part->width!=64)break;
   x=PW16(part->slot+12);if(x<low)low=x;if(x>high)high=x;j++;
  }
  if(j>i+1&&(u16)(high-low)<0xFFC0){e->span=(u8)(j-i);e->span_left=(u16)(low-32);e->span_width=(u16)(high-low+64);e->width|=0x8000;}
  i=j>i?j:i+1;
 }
 plan_primary=R6[0x20DC];plan_level=R6[0x20DD];plan_dirty=0;g_blast_entities=blast_entity_count;
}

#ifdef RC_HOST
void blast_frame(void){
 u16 i,native=0,fallback=0;blast_plan_prepare();
 for(i=0;i<blast_entity_count;i++){
  const BlastEntity *e=blast_entities+i;u8 *o=e->slot;u16 d=0,span;
  u16 width=e->width&0x7FFF;
  if((e->width&0x8000)&&(u16)(PW16(R6+0x240C)-e->span_left)>=e->span_width){i+=e->span-1;continue;}
  if(width>2){d=(u16)(PW16(o+12)-PW16(R6+0x240C));if((u16)(d+e->bias)>=width)continue;}
  else if(width==2&&!o[2])continue;
  if(e->native&&(span=e->native(o,blast_entity_count-i))){native+=span;i+=span-1;continue;}
  blast_reference_actor(o,width>2?(u8)d:e->left);fallback++;
 }
 g_blast_native=native;g_blast_fallback=fallback;blast_reference_finish();
}
#endif
