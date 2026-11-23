#include "input.h"
volatile u16 g_input_held,g_input_pending,g_input_presses;
volatile u32 g_input_samples;

/* Standard three-button TH handshake, matching the SGDK pad protocol.
 * Poll in VInt so a late renderer cannot hide an entire press/release. */
static u16 sample(void){
 vu8 *port=(vu8 *)0xA10003;u16 value,owned=Z80_getAndRequestBus(TRUE);
 *port=0;asm volatile("nop\n\tnop");value=(u16)*port<<8;
 *port=0x40;asm volatile("nop\n\tnop");value|=*port;
 if(!owned)Z80_releaseBus();
 return (u16)((((value&0x3000)>>6)|(value&0x3F))^0xFF);
}
void input_init(void){
 JOY_setSupport(PORT_1,JOY_SUPPORT_OFF);JOY_setSupport(PORT_2,JOY_SUPPORT_OFF);
 *(vu8 *)0xA10009=0x40;
 g_input_held=sample();g_input_pending=g_input_presses=0;g_input_samples=0;
}
void input_vint(void){
 u16 held=sample(),press=held&~g_input_held;
 g_input_held=held;g_input_pending|=press;g_input_samples++;
 if(press)g_input_presses++;
}
void input_take(u16 *held,u16 *pressed){
 SYS_disableInts();*held=g_input_held;*pressed=g_input_pending;g_input_pending=0;SYS_enableInts();
}
