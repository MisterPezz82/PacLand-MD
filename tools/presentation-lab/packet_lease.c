/* Experimental command packets. The producer leases all DMA source storage
 * until its packet is consumed; the SDK-compatible DMA command queues are swapped without copying.
 * Cold scenes/SDK asynchronous tasks stay on the ordinary SYS path. */
#include <genesis.h>
extern vu16 VBlankProcess;
extern volatile u32 g_render_count,g_present_count;
extern void flushQueue(u16 count);
#define PACKET_OPS 32
#define PACKET_BYTES 2048
typedef struct {DMAOpInfo *ops;u32 sequence;u16 count,bytes;} Packet;
static Packet front;
static DMAOpInfo *spare;
static volatile u16 ready;
static volatile u32 last_commit;
static u32 last_sequence;
volatile u32 g_pipe_late,g_pipe_duplicates,g_pipe_allocs,g_pipe_allocfail,g_pipe_deferrals;
volatile u32 g_pipe_commits,g_pipe_fallbacks,g_pipe_published;
volatile u16 g_pipe_entry,g_pipe_exit,g_pipe_overruns,g_pipe_max_bytes,g_pipe_max_ops,g_pipe_sealed_bytes;
/* Seal only completed queues. No interrupt can see the packet until ready
 * is published last. The normal producer does not write source RAM again
 * before presentation_wait returns. */
u16 presentation_seal(void){
 u16 n=DMA_getQueueSize(),bytes=DMA_getQueueTransferSize();
 if(ready||!n||n>PACKET_OPS||bytes>PACKET_BYTES||DMA_getMaxQueueSize()>128||(VBlankProcess&~PROCESS_DMA_TASK)){g_pipe_fallbacks++;return 0;}
 if(!spare){spare=MEM_alloc(128*sizeof(DMAOpInfo));if(!spare){g_pipe_allocfail++;g_pipe_fallbacks++;return 0;}g_pipe_allocs++;}
 front.ops=dmaQueues;front.count=n;front.bytes=bytes;front.sequence=g_render_count;
 dmaQueues=spare;spare=NULL;DMA_clearQueue();g_pipe_published++;g_pipe_sealed_bytes=bytes;
 asm volatile("":::"memory");ready=1;return 1;
}
void presentation_wait(void){while(ready)asm volatile("nop");}
void presentation_try_late(void);
u16 presentation_frame(void){if(!presentation_seal())return 0;presentation_try_late();presentation_wait();return 1;}
void presentation_vint(void){
 DMAOpInfo *back;Packet *p;u16 autoInc,v;bool owned;
 if(!ready)return;
 v=GET_VCOUNTER;if(v<224||v>230){g_pipe_deferrals++;return;}
 p=&front;g_pipe_entry=v;
 if(last_commit==vtimer)g_pipe_duplicates++;
 /* Preserve the SDK back queue, cached VDP increment and sound bus protocol.
  * This path never reads or clears a producer's unfinished queue. */
 autoInc=VDP_getAutoInc();back=dmaQueues;dmaQueues=p->ops;
 Z80_enableBusProtection();owned=Z80_isBusTaken();if(!owned)Z80_requestBus(FALSE);
 flushQueue(p->count);
 if(!owned)Z80_releaseBus();Z80_disableBusProtection();dmaQueues=back;VDP_setAutoInc(autoInc);
 g_pipe_exit=GET_VCOUNTER;if(g_pipe_exit<224)g_pipe_overruns++;
 if(p->bytes>g_pipe_max_bytes)g_pipe_max_bytes=p->bytes;
 if(p->count>g_pipe_max_ops)g_pipe_max_ops=p->count;
 if(p->sequence!=last_sequence){last_sequence=p->sequence;g_present_count++;}
 g_pipe_commits++;last_commit=vtimer;spare=p->ops;asm volatile("":::"memory");ready=0;
}

/* The same admitted 2KiB/32-op packet can use an already-started early
 * blank. The V-counter repeats229..234 on NTSC; the230 ceiling includes
 * that later occurrence, checked separately by the timing probe. */
void presentation_try_late(void){
 u16 v=GET_VCOUNTER,did=0;
 if(!ready||last_commit==vtimer||v<224||v>230)return;
 SYS_disableInts();v=GET_VCOUNTER;
 if(ready&&last_commit!=vtimer&&v>=224&&v<=230){presentation_vint();g_pipe_late++;did=1;}
 SYS_enableInts();
 /* If VInt was pending while the short commit ran, the callback just
  * advanced vtimer. Tag that physical blank rather than admitting twice. */
 if(did)last_commit=vtimer;
}
