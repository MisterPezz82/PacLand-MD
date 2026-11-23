#ifndef PACLAND_INPUT_H
#define PACLAND_INPUT_H
#include <genesis.h>
void input_init(void);
void input_vint(void);
void input_take(u16 *held,u16 *pressed);
#endif
