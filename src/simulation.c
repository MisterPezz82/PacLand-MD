#include "simulation.h"
/* Algorithm translation: 892E-8AFA. A held direction and a fresh button
 * press accelerate differently. Constants originate at 86C2-86E3. */
short player_velocity(short v,unsigned char left,unsigned char right,
                      unsigned char le,unsigned char re,unsigned char air) {
 int d=(int)re-(int)le;
 short a=air?154:308;
 if(d) {v+=d*a;if(v>704)v=704;if(v<-704)v=-704;}
 else {
  d=(int)right-(int)left;
  if(d) {
   short aligned=d*v;
   if(aligned>=616 || aligned<308)v+=d*(aligned<308?(air?11:22):-(air?11:22));
  } else {
   short magnitude=v<0?-v:v;
   if(magnitude<(air?16:264))v=0;
   else v+=v<0?(air?2:5):-(air?2:5);
  }
 }
 return v;
}
/* Baseline jump 8C93-8CEE; special object multipliers remain pending. */
void player_jump(Player *p) {
 int speed=p->vx<0?-p->vx:p->vx;
 if(speed<308)speed=308;
 p->vy=speed*3;p->gravity=-((speed*22)>>7);p->grounded=0;
}
/* D191-D23E: 288 world pixels per segment; independent plane ratios. */
/* n/288 == ((n>>5)*7282)>>16 exactly for 0 <= n < 2^18 (checked exhaustively):
 * a 16x16 multiply instead of a 32-bit software division on the 68000. */
static unsigned int div288(unsigned int n) {
 return ((unsigned int)(unsigned short)(n>>5)*7282u)>>16;
}
int camera_axis(int x,const unsigned short *offsets,unsigned short count) {
 /* 16-bit arithmetic only (mulu.w): no libgcc 32-bit multiply */
 unsigned short index,ratio,fraction,width;
 if(x<=0)return 0;
 index=(unsigned short)div288((unsigned int)x);
 if(index>=count)return offsets[count];
 fraction=(unsigned short)((unsigned short)x-(unsigned short)(index*288u));
 width=(unsigned short)(offsets[index+1]-offsets[index]);
 ratio=(unsigned short)div288((unsigned int)width*256u+144u);
 return offsets[index]+(int)(((unsigned long)fraction*ratio)>>8);
}
