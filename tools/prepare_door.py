"""Exact door masks using scenery pens unused in the visible neighbourhood.
Keep PAL3 and all player/ghost/door object colours intact on both trips.
"""
import json,re
from collections import Counter
from extract_arcade import ROOT,palettes
from convert_palettes import cram
from convert_graphics import pack_tile
from prepare_native import cell_info

def main():
 g=ROOT/'res/generated';data=(g/'native/data.c').read_text();recomp=(g/'native/recomp_data.c').read_text()
 stages=json.loads((g/'levels/manifest.json').read_text());segments=json.loads((g/'levels/segments.json').read_text())
 prom=(g/'arcade/proms.bin').read_bytes();pals=palettes(prom);art=json.loads((g/'arcade/bg_chars.json').read_text())
 arr=lambda n:[int(v,0)for v in re.search(rf'{n}\[\] = \{{([^}}]+)',data)[1].split(',')]
 rows_of=lambda name,size:[[int(v,0)for v in r.split(',')]for r in re.findall(r'\{([^}]+)\}',re.search(name+rf'\[32\]\[{size}\] = \{{(.*?)\}};',recomp,re.S)[1])]
 sp=rows_of('sprite_pal',16);gp=rows_of('group_pens',1024);gl=rows_of('group_line',64)
 out=['/* GENERATED from local arcade data; do not distribute. */','#include <genesis.h>']
 full=[];maps=[];tiles=[];counts=[];starts=[];report=[]
 for n,st in enumerate(stages):
  original=arr(f'palette_{n}');hw=original[:48]+sp[n];base_hw=hw.copy()
  if 34 not in st['segment_ids']:
   full.append(hw);maps.append('0');tiles.append('0');counts.append(0);starts.append(0);continue
  seg=st['segment_ids'].index(34);offsets=arr(f'bg_offset_{n}');first=offsets[seg]//8
  columns=segments['34']['bg'];words=[w for c in columns for w in c];base=arr(f'bg_{n}')[first*24:(first+len(columns))*24]
  pal=pals[st['palette_bank']];frequency=Counter(words);need={}
  for i,w in enumerate(words):
   code,group,_=cell_info(w,'bg');cs={cram(pal[prom[0xC00+group*4+p]])for row in art[code]for p in row}-{0}
   bank=(base[i]>>13)&3
   if not cs.issubset(set(hw[bank*16+1:bank*16+16])):need[w]=(group,cs)
  used=[{0}for _ in range(3)];patterns=arr(f'tiles_{n}')
  # More than one viewport on either side, including the parallax foreground.
  for layer in ('fg','bg'):
   off=arr(f'{layer}_offset_{n}');native_map=arr(f'{layer}_{n}');door=off[seg]//8
   lo=off[max(0,seg-2)]//8;hi=off[min(len(off)-1,seg+3)]//8
   for c in range(lo,hi):
    for y in range(24):
     if layer=='bg' and door<=c<door+36 and words[(c-door)*24+y]in need:continue
     attr=native_map[c*24+y];bank=(attr>>13)&3;tile=(attr&2047)-128
     if bank<3:
      for word in patterns[tile*8:tile*8+8]:used[bank].update((word>>shift)&15 for shift in range(0,32,4))
  # Preserve all pens of player variants, ghost parts, caps/umbrella, door,
  # platforms, cacti and fruit, even if a frozen fixture has hidden them.
  groups=list(range(4,19))+[0,1,2,3,23,0x25,0x15,0x1B,0x2D,0x34,0x1A,0x1C,0x3E,0x2E]
  for group in groups:
   line=gl[n][group]
   if line<3:used[line].update(gp[n][group*16:group*16+16])
  protected=[set(row)for row in used]
  assigned={};priority=lambda w:(0 if need[w][0]in(51,53,59)else 1 if need[w][0]==49 else 2,-frequency[w],w)
  for w in sorted(need,key=priority):
   cs=need[w][1];choices=[]
   for bank in range(3):
    row=hw[bank*16:bank*16+16];missing=sorted(cs-set(row[1:]));free=[i for i in range(1,16)if i not in used[bank]and row[i]not in cs]
    if len(free)>=len(missing):choices.append((len(missing),bank,missing,free))
   if not choices:continue
   _,bank,missing,free=min(choices)
   for colour,index in zip(missing,free):hw[bank*16+index]=colour
   row=hw[bank*16:bank*16+16];used[bank].update(row.index(c,1)for c in cs);assigned[w]=bank
  for bank in range(3):
   assert all(hw[bank*16+i]==base_hw[bank*16+i]for i in protected[bank])
  assert hw[48:]==sp[n]
  packed=[];lookup={};changed=0;mask_errors=0
  for i,w in enumerate(words):
   if w not in need:continue
   group,cs=need[w]
   if w not in assigned:
    if group in(51,53,59):mask_errors+=1
    continue
   bank=assigned[w];row=hw[bank*16:bank*16+16];code,_,attr=cell_info(w,'bg')
   mapping=[0 if not(c:=cram(pal[prom[0xC00+group*4+p]]))else row.index(c,1)if c in cs else 0 for p in range(4)]
   raw=pack_tile([[mapping[p]for p in r]for r in art[code]]);key=(bank,raw)
   if key not in lookup:lookup[key]=len(packed);packed.append(raw)
   tile_count=len(patterns)//8;base[i]=(128+tile_count+lookup[key])|(bank<<13)|((attr&64)<<5)|((attr&128)<<5);changed+=1
  assert not mask_errors,(n+1,'door masks cannot fit without changing protected colours',mask_errors)
  full.append(hw)
  def emit(typ,name,values):out.append(f'const {typ} {name}[] = '+chr(123)+','.join(hex(v)for v in values)+chr(125)+';')
  name=f'door_bg_{n}';emit('u16',name,base);maps.append(name)
  name=f'door_tiles_{n}';raw=b''.join(packed);emit('u32',name,[int.from_bytes(raw[i:i+4],'big')for i in range(0,len(raw),4)]);tiles.append(name)
  counts.append(len(packed));starts.append(first);report.append(dict(round=n+1,alternate_tiles=len(packed),corrected_cells=changed,mask_errors=mask_errors))
 out.append('const u16 door_scene_pal[32][64] = {'+','.join('{'+','.join(hex(v)for v in row)+'}'for row in full)+'};')
 out.append('const u16 *const door_bg[32] = {'+','.join(maps)+'};');out.append('const u32 *const door_tiles[32] = {'+','.join(tiles)+'};')
 out.append('const u16 door_tile_count[32] = {'+','.join(map(str,counts))+'};');out.append('const u16 door_bg_start[32] = {'+','.join(map(str,starts))+'};')
 (ROOT/'build/door_data.c').write_text('\n'.join(out)+'\n');(ROOT/'build/door_report.json').write_text(json.dumps(report,indent=2))
 print('Door palettes:',sum(counts),'extra tiles;',sum(r['corrected_cells']for r in report),'corrected cells; all mask and protected object colours exact')

if __name__=='__main__':main()
