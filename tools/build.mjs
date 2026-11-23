import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {call} from './romdev.mjs';
import {execFileSync} from 'node:child_process';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
// Adaptive presentation with the Namco pause window is mainstream.
// PL_BLAST=0 retains the older engine for explicit comparison builds only.
process.env.PL_BLAST ??= '1';
if(process.env.PL_BLAST==='1'){
 process.env.PL_BLAST_SHORT ??= '1';
 process.env.PL_BLAST_WINDOW ??= '1';
 if(process.env.PL_MUSIC!=='c')process.env.PL_PRESENT_PIPELINE ??= 'adaptive';
}
const outDir=path.resolve(root,process.env.PL_OUTPUT_DIR||'dist');
if(outDir===path.join(root,'dist')&&((process.env.PL_PRESENT_PIPELINE&&process.env.PL_PRESENT_PIPELINE!=='adaptive')||process.env.PL_PIPE_VERIFY_LEASE==='1'||process.env.PL_PRESENT_PROBE==='1'))throw Error('Presentation controls/probes require a separate PL_OUTPUT_DIR');
if(outDir===path.join(root,'dist')&&(process.env.PL_BLAST!=='1'||process.env.PL_BLAST_PROBE==='1'||process.env.PL_BLAST_LAB==='1'))
 throw Error('Legacy/lab/probe builds require a separate PL_OUTPUT_DIR');
if(process.env.PL_BLAST_WINDOW==='1'&&process.env.PL_BLAST!=='1')throw Error('Early-blank experiment requires PL_BLAST');
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_boot.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_fairy.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_door.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_sprite_extras.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_region_art.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_player_variants.py')],{stdio:'inherit'});
execFileSync(process.env.PYTHON || 'python',[path.join(root,'tools/prepare_overlay_masks.py')],{stdio:'inherit'});
// the Z80 program first (build/z80_bin.c)
execFileSync(process.execPath,[path.join(root,'tools/build_z80.mjs')],{stdio:['ignore','ignore','inherit'],env:process.env});
const files=['src/pause.c','main.c','src/input.c','src/save.c','src/boot.c','src/options.c','build/boot_data.c','build/fairy_data.c','build/door_data.c','build/sprite_extras.c','build/region_art.c','build/player_variants.c','build/overlay_masks.c','src/game.c','src/simulation.c','src/renderer.c','src/audio.c','src/objects.c','src/recomp_rt.c','src/slot_pass.s','src/fast_draw.s',
 'src/recomp_objects.c','src/hud.c','src/title.c','src/music.c','build/z80_bin.c','res/generated/audio/music_data.c','res/generated/native/data.c','res/generated/native/recomp_data.c','res/generated/native/hud_data.c','res/generated/native/title_data.c'];
if(process.env.PL_BLAST==='1'){
 execFileSync(process.env.PYTHON||'python',[path.join(root,'tools/prepare_blast_atlas.py')],{stdio:'inherit'});
 if(process.env.PL_BLAST_SHORT==='1'){execFileSync(process.execPath,[path.join(root,'tools/compile_blast.mjs')],{stdio:'inherit'});files.push('build/blast_native.s');}else files.push('src/blast.c');
 files.push('build/blast_atlas.c','src/blast_plan.s');
}
if(process.env.PL_MERGED_RT!=='0'){
 // Give GCC visibility across the native/translated boundary. Keep the
 // absolute RAM symbol for C entry wrappers, and register-relative inline
 // accessors for translated calls. Generated combined source stays private.
 let runtime=fs.readFileSync(path.join(root,'src/recomp_rt.c'),'utf8');
 runtime=runtime.replace('#define RC_GLOBAL_REGS','#define RC_R6_REG\n#define RC_GLOBAL_REGS');
 runtime=runtime.replace('#include "recomp_rt.h"','#include "recomp_rt.h"\n#ifndef RC_HOST\n#undef R6\n#endif');
 // Force the small native primitives across calls: this also lets GCC remove
 // emulated stack/register writes immediately overwritten by their callers.
 for(const name of ['C010','C020','C031','C068','C0AD','C0B4','9142','A3F2','8E40','8EA2','D1BD','D266']){
  const definition=new RegExp('void f_'+name+'\\(void\\)\\s*\\{');
  if(!definition.test(runtime))throw Error('Native inline definition not found: '+name);
  runtime=runtime.replace(definition,'__attribute__((always_inline)) void f_'+name+'(void){');
 }

 runtime=runtime.replaceAll('__attribute__((noinline)) c0ad_near','__attribute__((always_inline)) c0ad_near');
 const combined=runtime+'\n#ifndef RC_HOST\n#define R6 rR6\n#endif\n'+fs.readFileSync(path.join(root,'src/recomp_objects.c'),'utf8');
 fs.writeFileSync(path.join(root,'build/arcade_logic.c'),combined);
 files.splice(files.indexOf('src/recomp_rt.c'),1);files.splice(files.indexOf('src/recomp_objects.c'),1);files.push('build/arcade_logic.c');
}
if(process.env.PL_BLAST_PROBE==='1'){if(process.env.PL_BLAST_WINDOW!=='1')throw Error('Window probe requires PL_BLAST_WINDOW');files.push('tests/vblank_window.c');}
const sources=Object.fromEntries(files.map(n=>[n,fs.readFileSync(path.join(root,n),'utf8').replace(/^\uFEFF/,'')]));
// Diagnostic builds stay separate from the playable development cartridge.
if(process.env.PL_BLAST==='1'){
 // The runtime already uses cell_hash. Its old binary-search tables and
 // disabled duplicate block-pool exports have no remaining C consumers.
 // Keep the private generated resource intact for offline tooling.
 const file='res/generated/native/recomp_data.c';
 for(const name of ['cell_keys','cell_index','cell_round_start','sprite_map','block_pool','block_keys','block_index','block_round_start']){
  const re=new RegExp('const (?:u8|u16|u32) '+name+'\\[[^;]+;');
  if(!re.test(sources[file]))throw Error('Missing obsolete table: '+name);
  sources[file]=sources[file].replace(re,'/* omitted unused '+name+' */');
 }
}
if(process.env.PL_BLAST==='1')sources['src/fast_draw.s']='.set BLAST_NATIVE,1\n'+sources['src/fast_draw.s'];
if(process.env.PL_BLAST==='1')sources['src/slot_pass.s']='.set BLAST_NATIVE,1\n'+sources['src/slot_pass.s'];
if(process.env.PL_BLAST_LAB==='1'){
 if(outDir===path.join(root,'dist'))throw Error('Blast lab requires a separate PL_OUTPUT_DIR');
 const {instrument}=await import('./blast_lab.mjs');instrument(sources);
}
if(process.env.PL_PRESENT_PIPELINE){const {instrument}=await import('./presentation_lab.mjs');instrument(sources,process.env.PL_PRESENT_PIPELINE);}
fs.mkdirSync(outDir,{recursive:true});
const result=await call('build',{output:'romWithDebug',platform:'genesis',sources,
 includes:{...Object.fromEntries(['pause.h','save.h','input.h','game.h','native.h','simulation.h','recomp_rt.h','recomp_data.h','hud.h','title.h','music.h','musdrv.h','options.h','blast.h'].map(n=>[n,fs.readFileSync(path.join(root,'include',n),'utf8')])),
 'build_config.h':(process.env.PL_BLAST==='1'?'#define PL_BLAST 1\n':'')+(process.env.PL_BLAST_WINDOW==='1'?'#define PL_BLAST_WINDOW 1\n':'')+(process.env.PL_MUSIC==='c'?'#define PL_MUSIC_C 1\n':'/* default build */\n')+(process.env.PL_PROFILE?'#define PL_PROFILE 1\n':'')+(process.env.PL_C_SLOT_PASS?'#define PL_C_SLOT_PASS 1\n':'')+(process.env.PL_BLAST_LAB==='1'?'#include <genesis.h>\nextern volatile u16 g_blast_mask;\nextern volatile u32 g_blast_loops;\n':''),
 'musdrv_core.h':fs.readFileSync(path.join(root,'src/z80/musdrv.c'),'utf8'),
 },outputPath:path.join(outDir,'Pac-Land-MD.bin'),
 resolveSymbols:[...(process.env.PL_PRESENT_PIPELINE==='adaptive'?['g_pipe_adaptive','g_pipe_force','g_pipe_work_last','g_pipe_work_max','g_pipe_busy_frames','g_pipe_light_frames','g_pipe_switches']:[]),...(['lease','overlap','adaptive'].includes(process.env.PL_PRESENT_PIPELINE)?['g_pipe_late','g_pipe_duplicates','g_pipe_allocs','g_pipe_allocfail']:[]),...(['irq','lease','overlap','adaptive'].includes(process.env.PL_PRESENT_PIPELINE)?['g_pipe_commits','g_pipe_fallbacks','g_pipe_published','g_pipe_entry','g_pipe_exit','g_pipe_overruns','g_pipe_max_bytes','g_pipe_max_ops']:[]),...(process.env.PL_BLAST==='1'?['g_present_count']:[]),...(process.env.PL_BLAST_WINDOW==='1'?['g_blast_window_calls','g_blast_window_overruns','g_blast_window_max_queue','g_blast_window_max_exit']:[]),'g_render_count','g_display_pal','opt_region','opt_lives','opt_random','opt_seed','opt_attract',...(process.env.PL_PROFILE?['g_profile','g_type_profile','g_render_profile']:[]),'R6','rc_last_trap','g_cpu_lines','g_lag_frames','g_bench_mode','g_bench_count','g_bench_frames','g_object_sprites','g_object_misses','g_object_deferred','g_round','g_loaded_round','g_state','g_player_x','g_player_y','g_camera_fg','g_camera_bg','g_ticks','g_lives','g_lag_at','mus_log','mus_log_n','mus_log_on','mus_dbg_fmfirst','mus_dbg_mask', 'mus_dbg_on','mus_dbg_wave','mus_dbg_psg','mus_dbg_freq','mus_dbg_custom','mus_dbg_patch','g_starting','g_ending','g_demo','g_control_mode','g_corrected','player'],
 options:['-Os','-fno-jump-tables']});
fs.writeFileSync(path.join(outDir,'build-result.json'),JSON.stringify(result,null,2));
if(result.ok && result.binaryPath){
 const rom=fs.readFileSync(result.binaryPath);
 const field=(offset,length,text)=>rom.write(text.padEnd(length,' ').slice(0,length),offset,length,'ascii');
 field(0x100,16,'SEGA MEGA DRIVE');field(0x110,16,'(C)2026 DEVBUILD');
 field(0x120,48,'PAC-LAND MD - INCOMPLETE DEVELOPMENT BUILD');
 field(0x150,48,'PAC-LAND MD - INCOMPLETE DEVELOPMENT BUILD');
 field(0x180,14,'GM 00000000-00');field(0x190,16,'J');
 rom.writeUInt32BE(0,0x1a0);rom.writeUInt32BE(rom.length-1,0x1a4);
 field(0x1b0,2,'RA');rom.writeUInt16BE(0xF820,0x1b2);rom.writeUInt32BE(0x200001,0x1b4);rom.writeUInt32BE(0x2001FF,0x1b8);field(0x1f0,16,'JUE');
 let sum=0;for(let i=0x200;i<rom.length;i+=2)sum=(sum+rom.readUInt16BE(i))&65535;
 rom.writeUInt16BE(sum,0x18e);fs.writeFileSync(result.binaryPath,rom);
}
console.log(JSON.stringify(result,(k,v)=>['mapText','debugText','log','binaryBase64'].includes(k)?undefined:v,2));
if(result.ok===false)process.exitCode=1;
