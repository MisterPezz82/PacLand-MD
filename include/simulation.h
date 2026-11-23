#ifndef PACLAND_SIMULATION_H
#define PACLAND_SIMULATION_H
/* C int is 32 bits on both the m68k target and the host test compiler. */
typedef struct {
 int x,y; /* 24.8; Y increases upward, matching the arcade objects. */
 short vx,vy,gravity;
 unsigned short animation;
 unsigned char grounded,facing_left;
} Player;
short player_velocity(short velocity,unsigned char left,unsigned char right,
                      unsigned char left_edge,unsigned char right_edge,unsigned char airborne);
void player_jump(Player *p);
int camera_axis(int x,const unsigned short *offsets,unsigned short count);
#endif
