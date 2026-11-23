// Private build-only presentation experiments; no normal-source runtime branches.
import fs from 'node:fs';
export function instrument(sources,mode){
 const adaptive=mode==='adaptive',overlap=mode==='overlap';if(overlap||adaptive)mode='lease';
 if(mode==='records'){
  const file='src/blast_plan.s',from=' move.b #1,25(%a1)\n move.b #1,57(%a1)\n move.b #1,89(%a1)';
  const text=sources[file].replaceAll('\r\n','\n');
  if(!text.includes(from))throw Error('Missing legacy descriptor handoff');
  sources[file]=text.replace(from,[25,57,89].map((offset,i)=>` cmpi.b #4,${offset}(%a1)\n bne .Lretain_${i}\n move.b #3,${offset}(%a1)\n.Lretain_${i}:`).join('\n'));
  return;
 }
 sources['main.c']=sources['main.c'].replaceAll('\r\n','\n');
 const change=(from,to)=>{if(!sources['main.c'].includes(from))throw Error('Presentation injection missing: '+from);sources['main.c']=sources['main.c'].replace(from,to);};
 change('if(g_display_pal||elapsed==1)renderer_frame();','renderer_frame();');
 if(mode==='draw')return;
 if(!['irq','lease'].includes(mode))throw Error('Unknown presentation mode '+mode);
 if(process.env.PL_MUSIC==='c')throw Error('Presentation lab currently requires the Z80 player');
 {
  const file='src/music.c';let music=sources[file].replaceAll('\r\n','\n');
  const from='void music_vint(void) {\n    Z80_requestBus(TRUE);\n    ZMB[MB_GO] = (u8)vtimer;\n    Z80_releaseBus();\n}';
  if(!music.includes(from))throw Error('Missing sound interrupt handoff');
  music=music.replace(from,'void music_vint(void) {\n    bool owned=Z80_getAndRequestBus(TRUE);\n    ZMB[MB_GO] = (u8)vtimer;\n    if(!owned)Z80_releaseBus();\n}');
  const at=music.indexOf('#else   /* PL_MUSIC_C');if(at<0)throw Error('Missing sound backend boundary');
  let z80=music.slice(0,at).replaceAll('Z80_requestBus(TRUE);','presentation_sound_request();');
  z80=z80.replace('void music_init(void) {',`/* An interrupt must not release a request while its main-thread caller
 * is still waiting for the Z80 acknowledgement. Once acknowledged, both
 * interrupt clients preserve the existing owner. */
static void presentation_sound_request(void){
 u16 mask=SYS_getInterruptMaskLevel();SYS_setInterruptMaskLevel(7);
 Z80_requestBus(TRUE);SYS_setInterruptMaskLevel(mask);
}
void music_init(void) {`);
  sources[file]=z80+music.slice(at);
 }
 sources['src/presentation.c']=fs.readFileSync(mode==='lease'?'tools/presentation-lab/packet_lease.c':'tools/presentation-lab/packet.c','utf8');
 if(overlap||adaptive){
  if(overlap)sources['src/presentation.c']=sources['src/presentation.c'].replace('presentation_try_late();presentation_wait();return 1;','presentation_try_late();return 1;');
  const file='src/renderer.c';sources[file]='void presentation_wait(void);\n'+sources[file];
  for(const name of ['renderer_frame','renderer_load','renderer_menu','renderer_pause']){
   const from=`void ${name}(void) {`;if(!sources[file].includes(from))throw Error('Missing source lease boundary '+name);
   sources[file]=sources[file].replace(from,from+'\n presentation_wait();');
  }
 }
 if(adaptive){
  const runtime=sources['build/arcade_logic.c']?'build/arcade_logic.c':'src/recomp_rt.c';
  for(const name of ['rc_setup','rc_new_game','rc_round_start','rc_death_tick','rc_round_end_start']){
   const re=new RegExp('((?:void|u8)\\s+)'+name+'(\\s*\\()');
   if(!re.test(sources[runtime]))throw Error('Missing cold ABI boundary '+name);
   sources[runtime]=sources[runtime].replace(re,'$1'+name+'_impl$2');
  }
  sources['src/rc_cold_bridge.s']=fs.readFileSync('tools/presentation-lab/cold_bridge.s','utf8');
  let p=sources['src/presentation.c'];
  const wait='void presentation_wait(void){while(ready)asm volatile("nop");}',frame='u16 presentation_frame(void){if(!presentation_seal())return 0;presentation_try_late();presentation_wait();return 1;}';
  if(!p.includes(wait)||!p.includes(frame))throw Error('Missing adaptive scheduler boundaries');
  const implementation=fs.readFileSync('tools/presentation-lab/adaptive.c','utf8').replace('presentation_adaptive_wait','presentation_wait').replace('presentation_adaptive_frame','presentation_frame');
  p=p.replace(wait,'void presentation_wait(void);').replace(frame,implementation);
  sources['src/presentation.c']=p;
  sources['src/renderer.c']='void presentation_reset_mode(void);\n'+sources['src/renderer.c'];
  for(const name of ['renderer_load','renderer_menu','renderer_pause'])sources['src/renderer.c']=sources['src/renderer.c'].replace(`void ${name}(void) {\n presentation_wait();`,`void ${name}(void) {\n presentation_wait();presentation_reset_mode();`);
  change('void boot_screen(void);','void boot_screen(void);\nvoid presentation_work_begin(void);void presentation_clock_init(void);u32 presentation_clock_elapsed(void);\nextern volatile u32 g_pipe_last_elapsed,g_pipe_phase;extern volatile u16 g_pipe_last_denominator;');
  change('phase=0,last_vtimer=0,last_logic_vtimer;','phase=0,last_vtimer=0;');
  change(' last_logic_vtimer=vtimer;',' presentation_clock_init();');
  sources['main.c']=sources['main.c'].replaceAll('last_logic_vtimer=vtimer;','presentation_clock_init();presentation_work_begin();');
  change('u32 now=vtimer,elapsed=now-last_logic_vtimer;last_logic_vtimer=now;','u32 elapsed=presentation_clock_elapsed();\n  presentation_work_begin();');
  change('held=pad;edges|=fresh;phase+=20000*elapsed;','g_pipe_last_elapsed=elapsed;g_pipe_last_denominator=denominator;\n   held=pad;edges|=fresh;phase+=20000*elapsed;g_pipe_phase=phase;');
 }
 if(process.env.PL_PIPE_VERIFY_LEASE==='1'){
  if(mode!=='lease')throw Error('Lease verification needs queue ownership');
  let p=sources['src/presentation.c'];
  const verify=`
volatile u32 g_pipe_lease_checks,g_pipe_lease_mutations;
static u32 lease_hash;static u16 lease_check;
static u32 source_hash(void){
 u16 i;u32 h=0x13579BDF;
 for(i=0;i<front.count;i++){
  DMAOpInfo *op=front.ops+i;
  u32 a=((op->regAddrHAddrL&0x7F0000)<<1)|((op->regAddrMStep&0xFF0000)>>7)|((op->regAddrHAddrL&255)<<1);
  if(a>=0xFF0000){u16 n=(op->regLenL&255)|((op->regLenH&255)<<8);const u16 *src=(const u16*)a;
   while(n--){h=(h<<5)|(h>>27);h^=*src++;}
  }
 }return h;
}
`;
  p=p.replace('u16 presentation_seal(void){',verify+'\nu16 presentation_seal(void){');
  p=p.replace('asm volatile("":::"memory");ready=1;', 'lease_hash=source_hash();lease_check=1;asm volatile("":::"memory");ready=1;');
  const check='if(lease_check){g_pipe_lease_checks++;if(source_hash()!=lease_hash)g_pipe_lease_mutations++;lease_check=0;}';
  if(adaptive)p=p.replace('/* ADAPTIVE_AFTER_WAIT */',check);
  else p=p.replace('void presentation_wait(void){while(ready)asm volatile("nop");}', 'void presentation_wait(void){while(ready)asm volatile("nop");'+check+'}');
  sources['src/presentation.c']=p;
 }
 if(process.env.PL_PRESENT_PROBE==='1')sources['tests/presentation_probe.c']=(mode==='lease'?'#define PACKET_LATE 1\n':'')+fs.readFileSync('tools/presentation-lab/probe.c','utf8');
 change('void boot_screen(void);','void boot_screen(void);\nvoid presentation_vint(void);u16 presentation_frame(void);');
 change('static void game_vint(void){','static void game_vint(void){\n presentation_vint();');
 change('  last_vtimer=vtimer;','  u16 packet_presented=0;\n  last_vtimer=vtimer;\n  if(!g_display_pal&&g_state==1&&!g_starting&&!g_ending){packet_presented=presentation_frame();}\n  if(!packet_presented){');
 change('#ifdef PL_BLAST\n  if(last_presented_render!=g_render_count)', '}\n#ifdef PL_BLAST\n  if(packet_presented)last_presented_render=g_render_count;\n  if(last_presented_render!=g_render_count)');
}
