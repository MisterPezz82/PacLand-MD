"""Optional Japanese character artwork from the user's paclandj.zip."""
import json
import re
import zlib
from zipfile import ZipFile
from extract_arcade import ROOT,decode,palettes
from convert_palettes import nearest
from convert_graphics import correct_sprite,sprite_tiles


def load_art():
    rom=ROOT/'reference_roms/paclandj.zip'
    if not rom.exists():return None
    specs=[('pl1_9b.6f',0x80768A87),('pl1_8.6e',0x2B20E46D),('pl1_10b.7e',0xFFD9D66E),('pl1_11.7f',0xC59775D8)]
    with ZipFile(rom) as z:
        raw=[]
        for name,crc in specs:
            b=z.read(name)
            if len(b)!=0x4000 or zlib.crc32(b)!=crc:raise ValueError('Wrong Japanese graphics ROM: '+name)
            raw.append(b)
    return decode(b''.join(raw),True)

def main():
    out=ROOT/'build/region_art.c';art=load_art()
    if art is None:
        out.write_text('#include <genesis.h>\nconst u16 japan_art_available=0;\nconst u32 *const japan_player_tiles[32]={0};\nconst u32 japan_sprites[1] = {0};\n')
        return
    base=ROOT/'res/generated';prom=(base/'arcade/proms.bin').read_bytes();pals=palettes(prom)
    stages=json.loads((base/'levels/manifest.json').read_text());native=(base/'native/data.c').read_text()
    lines=['/* GENERATED from user-supplied Japanese graphics ROMs. Do not distribute. */','#include <genesis.h>','const u16 japan_art_available=1;']
    def array(name,values):lines.append('const u32 '+name+'[] = {'+','.join(hex(v) for v in values)+'};')
    packed=[]
    for cell in art:
        for tx,ty in ((0,0),(0,1),(1,0),(1,1)):
            for y in range(8):
                w=0
                for x in range(8):w=(w<<4)|cell[ty*8+y][tx*8+x]
                packed.append(w)
    array('japan_sprites',packed)
    seen={};names=[]
    for n,st in enumerate(stages):
        bank=st['palette_bank'];pal=pals[bank]
        hw=[int(v,0) for v in re.search(rf'palette_{n}\[\] = \{{([^}}]+)',native)[1].split(',')][48:64]
        data=bytearray()
        for wide in (False,True):
            for frame in range(0,112,4):
                px=[[0]*32 for _ in range(32)]
                for y in range(32):
                    for x in range(32):
                        c=prom[0x1000+art[frame+(y//16)*2+x//16][y%16][x%16]]
                        px[y][x]=0 if c&127==127 else 1+nearest(pal[c],hw[1:])
                if wide:px=correct_sprite(px)
                data+=sprite_tiles(px)
        raw=bytes(data)
        if raw not in seen:
            name=f'japan_player_{bank}';array(name,[int.from_bytes(raw[i:i+4],'big') for i in range(0,len(raw),4)]);seen[raw]=name
        names.append(seen[raw])
    lines.append('const u32 *const japan_player_tiles[32]={'+','.join(names)+'};')
    out.write_text('\n'.join(lines)+'\n')


if __name__=='__main__':main()
