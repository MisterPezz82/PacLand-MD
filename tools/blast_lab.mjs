// Build-only ablation switches. These sources are sent to the compiler;
// shipping source files and the normal cartridge remain unchanged.
export function instrument(sources){
 const replace=(file,from,to)=>{
  if(!sources[file]?.includes(from))throw Error('Blast lab injection missing: '+file+' '+from);
  sources[file]=sources[file].replace(from,to);
 };
 replace('main.c','volatile u16 g_display_pal;',
  'volatile u16 g_display_pal;\nvolatile u16 g_blast_mask;\nvolatile u32 g_blast_loops;');
 replace('main.c','last_vtimer=vtimer;SYS_doVBlankProcess();',
  'g_blast_loops++;last_vtimer=vtimer;SYS_doVBlankProcess();');
 replace('src/renderer.c','void renderer_frame(void) {',
  'void renderer_frame(void) {\n if(g_blast_mask&1)return;');
 replace('src/renderer.c','n=objects_draw(n,cam_fg_axis-g_camera_fg-16);',
  'if(!(g_blast_mask&4))n=objects_draw(n,cam_fg_axis-g_camera_fg-16);');
 replace('src/renderer.c','n=hud_text(0);','n=(g_blast_mask&8)?0:hud_text(0);');
 replace('src/renderer.c','hud_frame();','if(!(g_blast_mask&8))hud_frame();');
 const runtime=sources['build/arcade_logic.c']?'build/arcade_logic.c':'src/recomp_rt.c';
 replace(runtime,'void rc_frame(void) {','void rc_frame(void) {\n if(g_blast_mask&2)return;');
}
