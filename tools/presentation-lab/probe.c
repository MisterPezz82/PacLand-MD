/* Isolated test cartridge. Never resume normal gameplay after injection. */
#include <genesis.h>
extern volatile u32 g_render_count,g_pipe_commits;
extern volatile u16 g_pipe_entry,g_pipe_exit;
#ifdef PACKET_LATE
void presentation_try_late(void);
#endif
u16 presentation_seal(void);void presentation_wait(void);
volatile u16 g_packet_probe_line,g_packet_probe_fail,g_packet_probe_first,g_packet_probe_queue,g_packet_probe_bytes,g_packet_probe_frames;
static u16 front_source[32] __attribute__((aligned(128)));
static u16 back_source[32] __attribute__((aligned(128)));
void packet_probe(void){
 u16 pass,i;u32 before=vtimer;
 for(pass=0;pass<2;pass++){
  DMA_clearQueue();for(i=0;i<32;i++){front_source[i]=0x5A00+(pass<<8)+i;back_source[i]=0xA500+i;}
  for(i=0;i<32;i++)DMA_queueDmaFast(DMA_VRAM,front_source,(u16)(1200*32+i*64),32,2);
  while(GET_VCOUNTER>=224){}while(GET_VCOUNTER<g_packet_probe_line){}
  g_render_count++;
  if(!presentation_seal()){g_packet_probe_fail=1;return;}
  /* Deliberately change the SDK back queue after sealing. Its descriptor
   * and pending source must survive the interrupt's front-only flush. */
  DMA_queueDmaFast(DMA_VRAM,back_source,1000*32,32,2);
  #ifdef PACKET_LATE
  presentation_try_late();
  #endif
  presentation_wait();g_packet_probe_queue=DMA_getQueueSize();g_packet_probe_bytes=DMA_getQueueTransferSize();
  if(g_packet_probe_queue!=1||g_packet_probe_bytes!=64){g_packet_probe_fail=2;return;}
  if(!pass){*(vu32*)VDP_CTRL_PORT=VDP_READ_VRAM_ADDR(1200*32);g_packet_probe_first=*(vu16*)VDP_DATA_PORT;}
 }
 g_packet_probe_frames=(u16)(vtimer-before);
}
