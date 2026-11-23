/* Experimental command packets. The producer leases all DMA source storage
 * until its packet is consumed; only command descriptors are double-buffered.
 * Cold scenes/SDK asynchronous tasks stay on the ordinary SYS path. */
#include <genesis.h>
extern vu16 VBlankProcess;
extern volatile u32 g_render_count,g_present_count;
extern void flushQueue(u16 count);
#define PACKET_OPS 32
#define PACKET_BYTES 2048
typedef struct {DMAOpInfo op[PACKET_OPS];u32 sequence;u16 count,bytes;} Packet;
static Packet packets[2] __attribute__((aligned(4)));
static volatile u16 ready;
static u16 next_slot;
static u32 last_sequence;
volatile u32 g_pipe_commits,g_pipe_fallbacks,g_pipe_published;
volatile u16 g_pipe_entry,g_pipe_exit,g_pipe_overruns,g_pipe_max_bytes,g_pipe_max_ops,g_pipe_sealed_bytes;
/* Seal only completed queues. No interrupt can see the packet until ready
 * is published last. The normal producer does not write source RAM again
 * before presentation_wait returns. */
u16 presentation_seal(void){
 u16 n=DMA_getQueueSize(),bytes=DMA_getQueueTransferSize(),i;Packet *p;
 if(ready||!n||n>PACKET_OPS||bytes>PACKET_BYTES||(VBlankProcess&~PROCESS_DMA_TASK)){g_pipe_fallbacks++;return 0;}
 p=&packets[next_slot];for(i=0;i<n;i++)p->op[i]=dmaQueues[i];
 p->count=n;p->bytes=bytes;p->sequence=g_render_count;
 DMA_clearQueue();g_pipe_published++;g_pipe_sealed_bytes=bytes;
 asm volatile("":::"memory");ready=next_slot+1;next_slot^=1;
 return 1;
}
void presentation_wait(void){while(ready)asm volatile("nop");}
u16 presentation_frame(void){if(!presentation_seal())return 0;presentation_wait();return 1;}
void presentation_vint(void){
 DMAOpInfo *back;Packet *p;u16 autoInc,v;bool owned;
 if(!ready)return;
 v=GET_VCOUNTER;if(v<224||v>230)return;
 p=&packets[ready-1];g_pipe_entry=v;
 /* Preserve the SDK back queue, cached VDP increment and sound bus protocol.
  * This path never reads or clears a producer's unfinished queue. */
 autoInc=VDP_getAutoInc();back=dmaQueues;dmaQueues=p->op;
 Z80_enableBusProtection();owned=Z80_isBusTaken();if(!owned)Z80_requestBus(FALSE);
 flushQueue(p->count);
 if(!owned)Z80_releaseBus();Z80_disableBusProtection();dmaQueues=back;VDP_setAutoInc(autoInc);
 g_pipe_exit=GET_VCOUNTER;if(g_pipe_exit<224)g_pipe_overruns++;
 if(p->bytes>g_pipe_max_bytes)g_pipe_max_bytes=p->bytes;
 if(p->count>g_pipe_max_ops)g_pipe_max_ops=p->count;
 if(p->sequence!=last_sequence){last_sequence=p->sequence;g_present_count++;}
 g_pipe_commits++;asm volatile("":::"memory");ready=0;
}
