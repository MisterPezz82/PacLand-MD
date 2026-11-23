"""Native flash/power-boot frames using the arcade's actual colour groups.

Deduplicate tiles and frame maps so the additional outlines fit the 4 MiB ROM.
Both proportions and regional designs use the same per-round palette contract.
"""
import json,re
from extract_arcade import ROOT
from convert_graphics import correct_sprite,sprite_tiles
from prepare_region_art import load_art

def main():
    g=ROOT/'res/generated';text=(g/'native/recomp_data.c').read_text()
    rows=lambda name,n:[[int(v,0)for v in r.split(',')]for r in re.findall(r'\{([^}]+)\}',re.search(name+rf'\[32\]\[{n}\] = \{{(.*?)\}};',text,re.S)[1])]
    maps=rows('group_pens',1024);lines=rows('group_line',64)
    arts=[json.loads((g/'arcade/sprites.json').read_text()),load_art()]
    pool=[];tiles={};frame_maps={};out=['/* GENERATED from local arcade data; do not distribute. */','#include <genesis.h>'];tables=[]
    for region,art in enumerate(arts):
        rounds=[];converted={}
        for n in range(32):
            groups=[]
            for group in (1,2,3):
                if art is None:groups.append('0');continue
                assert lines[n][group]==3,(n,group,'native player requires PAL3')
                mapping=tuple(maps[n][group*16:group*16+16])
                if mapping not in converted:
                    ids=[]
                    for wide in (False,True):
                        for frame in range(0,112,4):
                            px=[[mapping[art[frame+(y//16)*2+x//16][y%16][x%16]]for x in range(32)]for y in range(32)]
                            if wide:px=correct_sprite(px)
                            raw=sprite_tiles(px)
                            for i in range(0,len(raw),32):
                                tile=raw[i:i+32]
                                if tile not in tiles:tiles[tile]=len(pool);pool.append(tile)
                                ids.append(tiles[tile])
                    key=tuple(ids)
                    if key not in frame_maps:
                        name=f'player_variant_{len(frame_maps)}';frame_maps[key]=name
                        out.append('const u16 '+name+'[] = {'+','.join(map(str,ids))+'};')
                    converted[mapping]=frame_maps[key]
                groups.append(converted[mapping])
            rounds.append('{'+','.join(groups)+'}')
        tables.append('{'+','.join(rounds)+'}')
    raw=b''.join(pool)
    out.append('const u32 player_variant_pool[] __attribute__((aligned(32))) = {'+','.join(hex(int.from_bytes(raw[i:i+4],'big'))for i in range(0,len(raw),4))+'};')
    out.append('const u16 *const player_variants[2][32][3] = {'+','.join(tables)+'};')
    (ROOT/'build/player_variants.c').write_text('\n'.join(out)+'\n')
    print('Player colour variants:',len(pool),'unique tiles,',len(frame_maps),'frame maps,',len(raw)+len(frame_maps)*2016,'bytes')

if __name__=='__main__':main()
