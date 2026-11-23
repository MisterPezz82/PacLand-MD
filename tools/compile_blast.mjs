// Pure native kernel has an explicit wide ABI at its assembly boundary. Its
// internal int arithmetic can use the 68000's word size independently of SGDK.
import fs from 'node:fs';import path from 'node:path';import {createRequire}from'node:module';import{pathToFileURL}from'node:url';
const req=createRequire(path.resolve('package.json'));
const modulePath=path.join(path.dirname(req.resolve('romdevtools/package.json')),'src/toolchains/m68k-elf-gcc/gcc.js');
const {runCc1m68k}=await import(pathToFileURL(modulePath).href);
const platform=`typedef unsigned char u8;typedef signed char s8;typedef unsigned short u16;typedef signed short s16;typedef unsigned long u32;
extern u8 R6[0x4000] __attribute__((aligned(4))),M6[0x400];extern const u8 *ROM6;
typedef u16 __attribute__((may_alias)) word_alias;
#define PW16(p) (*(const word_alias *)(p))
#define PW16_SET(p,v) (*(word_alias *)(p)=(u16)(v))
#define R6W(c) PW16(R6+(c))
#define R6W_SET(c,v) PW16_SET(R6+(c),v)
void rc_trap(u16);
void blast_distance_score(void);
void blast_fail(u32);
#define rc_trap(v) blast_fail((u32)(v))
`;
const header=fs.readFileSync('include/blast.h','utf8').replace('#include "recomp_rt.h"',platform);
const r=await runCc1m68k({source:fs.readFileSync('src/blast.c','utf8'),headers:{'blast.h':header},options:['-O2','-fno-jump-tables','-fomit-frame-pointer',...(process.env.PL_BLAST_SHORT==='1'?['-mshort']:[])]});
fs.writeFileSync('build/blast-compile.log',r.log||'');
if(r.exitCode||!r.asmSource)throw Error(r.log);
// Expose already-emitted local function labels for accurate PC attribution.
// This changes the ELF/map symbols only, not the generated instructions.
let asm=r.asmSource;
const local=[...asm.matchAll(/\.type\s+(\w+), @function/g)].map(m=>m[1]).filter(n=>!new RegExp(String.raw`\.globl\s+${n}\b`).test(asm));
for(const name of local){const renamed='blast_kernel_'+name;asm=asm.replace(new RegExp(String.raw`\b${name}\b`,'g'),renamed)+'\n.globl '+renamed+'\n';}
if(!asm.includes('blast_kernel_contact:'))throw Error('Native profiling labels were not emitted');
fs.writeFileSync('build/blast_native.s',asm);console.log('Compiled native actor kernel; short int:',process.env.PL_BLAST_SHORT==='1');
