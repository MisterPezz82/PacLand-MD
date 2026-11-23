/* Included by the laboratory source injector. Times the complete update and
 * draw, subtracting the graphics ownership fence. One NTSC field is roughly
 * 1280 SDK subticks; rollback noise is covered by the threshold separation. */
volatile u16 g_pipe_adaptive,g_pipe_force,g_pipe_work_last,g_pipe_work_max;
volatile u32 g_pipe_busy_frames,g_pipe_light_frames,g_pipe_switches;
static u32 work_start,wait_ticks;
static u16 quiet_frames,mode_age;
static u32 logic_clock;
volatile u32 g_pipe_clock_delta,g_pipe_clock_now,g_pipe_last_elapsed,g_pipe_phase;
volatile u16 g_pipe_last_denominator;
void presentation_clock_init(void){logic_clock=vtimer;}
u32 presentation_clock_elapsed(void){
 u32 now=vtimer,elapsed;
 while((s32)(now-logic_clock)<0)now=vtimer;
 elapsed=now-logic_clock;
 if(!elapsed)logic_clock++;else logic_clock=now;
 g_pipe_clock_delta=elapsed;g_pipe_clock_now=now;
 return elapsed;
}
void presentation_work_begin(void){work_start=getSubTick();wait_ticks=0;}
void presentation_reset_mode(void){
 if(g_pipe_adaptive){g_pipe_adaptive=0;g_pipe_switches++;}
 quiet_frames=mode_age=0;
}
void presentation_adaptive_wait(void){
 if(ready){u32 start=getSubTick();while(ready)asm volatile("nop");wait_ticks+=getSubTick()-start;}
 /* ADAPTIVE_AFTER_WAIT */
}
u16 presentation_adaptive_frame(void){
 u32 elapsed=getSubTick()-work_start;
 u16 work,wanted=g_pipe_adaptive;
 elapsed=elapsed>=wait_ticks?elapsed-wait_ticks:65535;
 work=elapsed>65535?65535:(u16)elapsed;g_pipe_work_last=work;
 if(work>g_pipe_work_max)g_pipe_work_max=work;
 if(g_pipe_force)wanted=g_pipe_force==2;
 else if(!wanted){if(work>=900)wanted=1;}
 else{
  if(mode_age<65535)mode_age++;
  if(work<=700){if(quiet_frames<24)quiet_frames++;}else quiet_frames=0;
  if(mode_age>=30&&quiet_frames>=24)wanted=0;
 }
 if(wanted!=g_pipe_adaptive){g_pipe_adaptive=wanted;g_pipe_switches++;quiet_frames=mode_age=0;}
 if(!presentation_seal()){presentation_reset_mode();return 0;}
 /* Predictable next-blank publication keeps the speculative logic clock
  * bounded. Do not opportunistically publish twice around the NTSC rollback. */
 if(wanted){g_pipe_busy_frames++;return 1;}
 g_pipe_light_frames++;presentation_wait();return 1;
}
