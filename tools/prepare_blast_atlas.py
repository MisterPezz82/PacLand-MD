"""Compile per-round resident sprite plans from a reference-engine census.

Only metadata is emitted: scene entry uploads existing ROM art. A reserved
streaming cache handles unseen frames and randomiser variants. Generated data
and census inputs remain private build artifacts.
"""
from pathlib import Path
import re,json,subprocess,struct,hashlib
from host_compiler import command as host_compiler
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'res/generated/native/recomp_data.c').read_text(encoding='utf-8')
def array(text,name):
 return [int(v,0)for v in re.search(r'\b'+name+r'\[[^=]*= \{([^}]+)',text)[1].split(',')]
art=array(source,'arcade_sprites')
program=(ROOT/'res/generated/arcade/maincpu.bin').read_bytes()
region_source=(ROOT/'build/region_art.c').read_text()
jp=array(region_source,'japan_sprites')
japanese_available=bool(re.search(r'japan_art_available\s*=\s*1\b',region_source))
if japanese_available and len(jp)!=512*32:raise ValueError('Japanese sprite sheet has the wrong size')
pens=[[int(v)for v in m[1].split(',')]for m in re.finditer(r'\{([^}]+)\}',re.search(r'group_pens\[32\]\[1024\] = \{(.*?)\};',source,re.S)[1])]
# Invalidate the offline census when its engine/harness/input changes.
inputs=['tools/blast_inventory.c','src/recomp_objects.c','src/recomp_rt.c','include/recomp_rt.h','res/generated/arcade/maincpu.bin']
stamp=hashlib.sha256(b''.join((ROOT/p).read_bytes()for p in inputs)).hexdigest()
census=ROOT/'build/blast-inventory.bin';stampfile=ROOT/'build/blast-inventory.sha256'
if not census.exists()or not stampfile.exists()or stampfile.read_text()!=stamp:
 exe=ROOT/'build/blast-inventory.exe'
 subprocess.run([*host_compiler(),'-DRC_HOST','-DRC_ORACLE','-O2','-w','-iquote','include','src/recomp_objects.c','src/recomp_rt.c','tools/blast_inventory.c','-o',str(exe)],cwd=ROOT,check=True)
 subprocess.run([str(exe),'res/generated/arcade/maincpu.bin',str(census)],cwd=ROOT,check=True)
 stampfile.write_text(stamp)
data=census.read_bytes();offset=0;frequencies=[];types=[]
for rnd in range(32):
 count,kind=struct.unpack_from('<II',data,offset);offset+=8
 frequencies.append(dict(struct.unpack_from('<II',data,offset+i*8)for i in range(count)));offset+=count*8;types.append(kind)
assert offset==len(data),'census has unexpected trailing data'
stages=(ROOT/'res/generated/native/data.c').read_text()
# Captured fields start after tiles_N; tile_count is field six here.
scenery=[int(m[2].split(',')[6])for m in re.finditer(r'\{tiles_(\d+),([^{}]+)\}',stages)]
assert len(scenery)==32
extra=array((ROOT/'build/door_data.c').read_text(),'door_tile_count')

def pixels(key,rnd):
 if key&0x40000000 and not japanese_available:raise ValueError('Japanese atlas key without Japanese artwork')
 code=key&511;shape=(key>>9)&3;group=(key>>11)&63;sheet=jp if key&0x40000000 else art
 words=[]
 for cx in range(1+(shape&1)):
  for lx in range(2):
   for cy in range(1+(shape>>1)):
    cell=code+2*cy+cx
    for raw in sheet[cell*32+lx*16:cell*32+lx*16+16]:
     v=0
     for shift in range(28,-1,-4):v=(v<<4)|pens[rnd][group*16+((raw>>shift)&15)]
     words.append(v)
 return shape,tuple(words)
records=[];starts=[];counts=[];tilecounts=[];lookups=[];reports=[]
for rnd in range(32):
 frequency={k:v for k,v in frequencies[rnd].items()if japanese_available or not k&0x40000000}
 required={0x13C}
 if japanese_available:required.add(0x40000000|0x13C|(23<<11))
 if rnd%4==3:
  for group in [6,9,12,15,18]:
   for code in [0xAC,0xB0]:required.add(code|(3<<9)|(group<<11))
  # Native cars own three records: chassis, head animation, and legs.
  # Cover every head phase/identity, including randomised ghost identities.
  for ghost in range(5):
   base=program[0x9C55+ghost];group=(ghost+1)*3+3
   for delta in [5,6,8,11]:required.add((0x100|base+delta)|(group<<11))
 if types[rnd]&2:
  for code in [0x40,0x41,0x44,0x45]:required.add(code|(2<<9)|(25<<11))
 if types[rnd]&1:
  for code in [0x150,0x152,0x154,0x156]:required.add(code|(1<<9)|(25<<11))
 keys=set(frequency)|required

 budget=min(636,1400-128-scenery[rnd]-extra[rnd]-144)
 assert budget>=0
 identities={k:pixels(k,rnd)for k in keys}
 ordered=sorted(keys,key=lambda k:(k not in required,-frequency.get(k,0)/max(1,len(identities[k][1])//8),k))
 unique={};selected=[];tiles=0
 for key in ordered:
  identity=identities[key];upload=identity not in unique;cost=len(identity[1])//8 if upload else 0
  if len(selected)>=96 or tiles+cost>budget:
   assert key not in required,(rnd+1,'required atlas frames exceed VRAM budget')
   continue
  if upload:unique[identity]=tiles;tiles+=cost
  selected.append((key,unique[identity]|(0x8000 if upload else 0)))
 selected.sort();starts.append(len(records));counts.append(len(selected));tilecounts.append(tiles);records.extend(selected)
 lookup=[0]*128
 for i,(key,_)in enumerate(selected):
  h=((((key^(key>>16))&65535)*40503)&65535)>>9
  while lookup[h]:h=(h+1)&127
  lookup[h]=i+1
 lookups.append(lookup)
 reports.append(dict(round=rnd+1,frames=len(selected),observedFrames=len(frequency),uniqueFrames=len(unique),tiles=tiles,bytes=tiles*32,dynamicTilesRemaining=1400-128-scenery[rnd]-extra[rnd]-tiles,keys=[hex(k)for k,_ in selected]))
lines=['/* Private reference-derived plans. No duplicated bitmap pool. */','#include <genesis.h>',
 'const struct {u32 key;u16 tile;} blast_atlas[]={' + ','.join('{'+hex(k)+','+str(t)+'}'for k,t in records)+'};']
for name,values in [('start',starts),('count',counts),('tiles',tilecounts)]:lines.append('const u16 blast_atlas_'+name+'[32]={'+','.join(map(str,values))+'};')
lines.append('const u8 blast_atlas_lookup[32][128]={'+','.join('{'+','.join(map(str,row))+'}'for row in lookups)+'};')
(ROOT/'build/blast_atlas.c').write_text('\n'.join(lines)+'\n')
(ROOT/'build/blast-atlas-report.json').write_text(json.dumps({'rounds':reports,'records':len(records),'metadataBytes':len(records)*6+32*128+32*6},indent=2))
print('Resident sprite plans:',len(records),'frame keys across 32 rounds;',min(tilecounts),'to',max(tilecounts),'tiles per scene')
