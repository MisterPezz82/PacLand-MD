#ifndef PACLAND_BLAST_H
#define PACLAND_BLAST_H
#include "recomp_rt.h"
/* Ordinary native ABI: no emulated registers/flags/return values. The initial
 * bridge shares semantic state with the reference through R6; it is not yet
 * the final standalone world/player storage. Unsupported states are untouched. */
u16 blast_actor(u8 *object);
u16 blast_fruit_run(u8 *object,u32 count);
void blast_reset(void);
void blast_frame(void);
void blast_reference_actor(u8 *o,u16 b);
void blast_reference_finish(void);
void blast_distance_score(void);
void blast_car_retire(u8 *o);
typedef struct {u8 *slot;u16 width,bias;void (*legacy)(void);u16 (*native)(u8*,u32);u16 left;u8 type,span;u16 span_left,span_width;} BlastEntity;
extern BlastEntity blast_entities[96];
extern u16 blast_entity_count;
void blast_plan_prepare(void);
u16 blast_player_run(u8 *o,u32 count);
u16 blast_car_run(u8 *o,u32 count);
u16 blast_bridge_run(u8 *o,u32 count);
u16 blast_logs_run(u8 *o,u32 count);
extern volatile u16 g_blast_native,g_blast_fallback,g_blast_entities;
#endif
