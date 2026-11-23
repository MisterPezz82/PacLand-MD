"""Preconvert frightened ghost cars and packed bridge pieces.

Power boots on the first return trip expose AC..B3 in frightened groups.
Keep the existing palette choices; only add their missing sprite cells.
"""
import json
import re
from extract_arcade import ROOT


def main():
    base=ROOT/'res/generated'
    text=(base/'native/recomp_data.c').read_text()
    rows=re.search(r'group_pens\[32\]\[1024\] = \{(.*?)\};',text,re.S)[1]
    maps=[[int(v) for v in r.split(',')] for r in re.findall(r'\{([^}]+)\}',rows)]
    art=json.loads((base/'arcade/sprites.json').read_text())
    pool=[];shared={};indices=[]
    for mapping in maps:
        row=[]
        for group in (6,9,12,15,18):
            for code in range(0xAC,0xB4):
                words=[]
                for tx,ty in ((0,0),(0,1),(1,0),(1,1)):
                    for y in range(8):
                        w=0
                        for x in range(8):w=(w<<4)|mapping[group*16+art[code][ty*8+y][tx*8+x]]
                        words.append(w)
                key=tuple(words)
                if key not in shared:shared[key]=len(pool)//32;pool+=words
                row.append(shared[key])
        indices.append(row)
    out=['/* GENERATED from local arcade sprites/palette mappings. Do not distribute. */','#include <genesis.h>']
    out.append('const u16 frightened_cells[32][40] = {'+','.join('{'+','.join(str(v) for v in r)+'}' for r in indices)+'};')
    out.append('const u32 frightened_pool[] __attribute__((aligned(128))) = {'+','.join(hex(v) for v in pool)+'};')
    car_pool=[];car_ids=[];shared_cars={}
    for mapping in maps:
        row=[]
        for group in (6,9,12,15,18):
            for code in (0xAC,0xB0):
                words=[]
                for tx in range(4):
                    for ty in range(4):
                        cell=art[code+(ty//2)*2+tx//2]
                        for y in range(8):
                            w=0
                            for x in range(8):w=(w<<4)|mapping[group*16+cell[(ty%2)*8+y][(tx%2)*8+x]]
                            words.append(w)
                key=tuple(words)
                if key not in shared_cars:shared_cars[key]=len(car_pool)//128;car_pool+=words
                row.append(shared_cars[key])
        car_ids.append(row)
    out.append('const u16 frightened_blocks[32][10] = {'+','.join('{'+','.join(str(v)for v in r)+'}'for r in car_ids)+'};')
    out.append('const u32 frightened_block_pool[] __attribute__((aligned(512))) = {'+','.join(hex(v)for v in car_pool)+'};')
    blocks=[];block_ids=[];shared_blocks={}
    for mapping in maps:
        row=[]
        for code in (0x40,0x41,0x44,0x45):
            words=[]
            for tx in range(2):
                for ty in range(4):
                    cell=art[code+(ty//2)*2]
                    for y in range(8):
                        w=0
                        for x in range(8):w=(w<<4)|mapping[0x19*16+cell[(ty%2)*8+y][tx*8+x]]
                        words.append(w)
            key=tuple(words)
            if key not in shared_blocks:shared_blocks[key]=len(blocks)//64;blocks+=words
            row.append(shared_blocks[key])
        block_ids.append(row)
    out.append('const u16 bridge_blocks[32][4] = {'+','.join('{'+','.join(str(v) for v in r)+'}' for r in block_ids)+'};')
    out.append('const u32 bridge_pool[] __attribute__((aligned(256))) = {'+','.join(hex(v) for v in blocks)+'};')
    (ROOT/'build/sprite_extras.c').write_text('\n'.join(out)+'\n')
    print(f'Frightened car extras: {len(pool)//32} unique cells, {len(pool)*4+32*40*2} bytes')
    print(f'Packed bridges: {len(blocks)//64} unique blocks, {len(blocks)*4+32*4*2} bytes')
    print(f'Packed frightened cars: {len(car_pool)//128} blocks, {len(car_pool)*4+640} bytes')


if __name__=='__main__':main()
