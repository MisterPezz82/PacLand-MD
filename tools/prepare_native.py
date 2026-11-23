"""Compile ROM-derived maps, palettes and player animation to SGDK resources."""
from collections import Counter
from itertools import combinations
import json
from pathlib import Path
from extract_arcade import ROOT, palettes
from extract_levels import word, RULES
from convert_palettes import cram, rgb, distance, nearest
from convert_graphics import pack_tile, sprite_tiles, correct_sprite
import palette_pack

def cell_info(word_,layer):
    attr=word_&255;code=(word_>>8)+((attr&1)<<8)
    group=((attr&(30 if layer=='fg' else 62))>>1)+((code&(480 if layer=='fg' else 448))>>1)
    return code,group,attr

def compile_resources():
    base=ROOT/'res/generated';out=base/'native';out.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((base/'levels/manifest.json').read_text())
    segments=json.loads((base/'levels/segments.json').read_text())
    prom=(base/'arcade/proms.bin').read_bytes();program=(base/'arcade/maincpu.bin').read_bytes();pals=palettes(prom)
    art={k:json.loads((base/f'arcade/{k}.json').read_text()) for k in ['fg_chars','bg_chars','sprites']}
    declarations=['#include <genesis.h>','#include "native.h"'];stages=[];stats=[];player_shared={}
    def array(name,values,kind='u16'):
        declarations.append(f'const {kind} {name}[] = {{'+','.join(hex(v) for v in values)+'};')
    for stage in manifest:
        n=stage['round']-1;pal=pals[stage['palette_bank']];columns={};groups=Counter()
        for layer in ['fg','bg']:
            columns[layer]=sum((segments[str(35 if layer=='fg' and n==26 and 0<i<32 else i)][layer] for i in stage['segment_ids']),[])
            for w,count in Counter(w for c in columns[layer] for w in c).items():
                code,g,a=cell_info(w,layer);lut=0x800 if layer=='fg' else 0xc00
                # background black is pen 0: transparent, the backdrop (black)
                # shows through, so black needs no slot in a background line
                colors=frozenset(cram(pal[prom[lut+4*g+p]]) for p in range(4)
                                 if (layer=='bg' and cram(pal[prom[lut+4*g+p]])!=0) or (layer=='fg' and prom[lut+4*g+p]&127!=127))
                groups[colors]+=count
        # three scenery lines from the tile colour sets (tools/palette_pack.py)
        banks=palette_pack.pack_cached(list(groups.items()),out/'palette_pack_cache.json')
        # The BREAK TIME lettering/background must share one exact palette.
        # Repair the cached frequency packing with the least costly eviction;
        # this keeps its ochre fill from splitting into two unrelated shades.
        sign = frozenset(cram(pal[prom[0xC00 + 146 * 4 + pen]]) for pen in range(4)) - {0}
        if any(cell_info(w, 'bg')[1] == 146 for c in columns['bg'] for w in c):
            if not any(sign <= bank for bank in banks):
                D = palette_pack._dist_table({c for cs in groups for c in cs})
                trials = []
                for i, bank in enumerate(banks):
                    merged = bank | sign
                    for removed in combinations(sorted(bank - sign), max(0, len(merged) - 15)):
                        trial = [set(b) for b in banks]; trial[i] = merged - set(removed)
                        trials.append((palette_pack.cost_of(trial, list(groups.items()), D), i, removed, trial))
                assert trials, 'No palette can hold BREAK TIME colours'
                banks = min(trials, key=lambda t: t[:3])[3]
        hw=[[0]+sorted(b)+[0]*(15-len(b)) for b in banks]
        player_colors=sorted({cram(pal[prom[0x1000+p]]) for p in range(16) if prom[0x1000+p]&127!=127})
        hw.append([0]+player_colors+[0]*(14-len(player_colors))+[0xeee])
        patterns=[bytes(32)];lookup={patterns[0]:0};attrs={};errors=0
        for layer in ['fg','bg']:
            for w in sorted({w for c in columns[layer] for w in c}):
                code,g,a=cell_info(w,layer);lut=0x800 if layer=='fg' else 0xc00
                colors=[pal[prom[lut+4*g+p]] for p in range(4)]
                used_pens={p for row in art['fg_chars' if layer=='fg' else 'bg_chars'][code] for p in row}
                def error(bank):   # as tools/palette_pack.py: colours the line lacks cost extra
                    # Unused pens cannot affect this tile. Including them can
                    # choose an approximate sky/ground colour merely to fit a
                    # fourth colour that the character never draws.
                    return sum(min(distance(c,rgb(v)) for v in hw[bank][1:])+(0 if cram(c) in hw[bank][1:] else palette_pack.MISS) for p,c in enumerate(colors) if p in used_pens and ((layer=='bg' and cram(c)!=0) or (layer=='fg' and prom[lut+4*g+p]&127!=127)))
                bank=min(range(3),key=lambda i:(error(i),i));errors+=int(error(bank)>0)
                indices=[0 if (layer=='fg' and prom[lut+4*g+p]&127==127) or (layer=='bg' and cram(c)==0) else 1+nearest(c,hw[bank][1:]) for p,c in enumerate(colors)]
                pixels=[[indices[p] for p in row] for row in art['fg_chars' if layer=='fg' else 'bg_chars'][code]]
                packed=pack_tile(pixels)
                if packed not in lookup:lookup[packed]=len(patterns);patterns.append(packed)
                attrs[layer,w]=(lookup[packed]+128)|(bank<<13)|((a&64)<<5)|((a&128)<<5)|(0x8000 if layer=='fg' and a&32 else 0)
        mapnames=[]
        for layer in ['fg','bg']:
            name=f'{layer}_{n}';array(name,[attrs[layer,w] for col in columns[layer] for w in col]);mapnames.append(name)
        background_count=len(patterns)
        assert background_count+128<1400
        raw=b''.join(patterns);array(f'tiles_{n}',[int.from_bytes(raw[i:i+4],'big') for i in range(0,len(raw),4)],'u32')
        # Pac-Man frames depend only on the palette bank: one copy per bank.
        bank_id=stage['palette_bank'];patterns=[]
        player_starts=[]
        # 4 is idle, 8..68 walking/running, 72..108 airborne. Both proportions.
        for corrected in [False,True]:
            player_starts.append(len(patterns)+16)  # offsets into player_tiles_<bank>, +16 as before
            for frame in range(0,112,4):
                px=[[0]*32 for _ in range(32)]
                for y in range(32):
                    for x in range(32):
                        pen=art['sprites'][frame+(y//16)*2+x//16][y%16][x%16]
                        color=prom[0x1000+pen]
                        px[y][x]=0 if color&127==127 else 1+nearest(pal[color],hw[3][1:])
                if corrected:px=correct_sprite(px)
                packed=sprite_tiles(px)
                patterns.extend(packed[i:i+32] for i in range(0,len(packed),32))
        raw=b''.join(patterns)
        if raw not in player_shared:
            name=f'player_tiles_{bank_id}';array(name,[int.from_bytes(raw[i:i+4],'big') for i in range(0,len(raw),4)],'u32')
            player_shared[raw]=name
        player_name=player_shared[raw]
        patterns=[None]*(background_count+len(patterns))  # keep the reported tile total comparable
        array(f'palette_{n}',sum(hw,[]));array(f'ids_{n}',stage['segment_ids'],'u8')
        cumulative=[]
        for layer in ['fg','bg']:
            pos=[0]
            for ident in stage['segment_ids']:pos.append(pos[-1]+len(segments[str(ident)][layer])*8)
            array(f'{layer}_offset_{n}',pos);cumulative.append(f'{layer}_offset_{n}')
        stages.append('{'+','.join([f'tiles_{n}',f'palette_{n}',*mapnames,f'ids_{n}',*cumulative,
                       str(background_count),str(len(columns['fg'])),str(len(columns['bg'])),str(len(stage['segment_ids'])),
                       str(stage['world_start']),str(stage['world_end']),str(player_starts[0]),str(player_starts[1]),
                       player_name])+'}')
        stats.append(dict(round=n+1,tiles=len(patterns),vram_pattern_bytes=background_count*32,approximated_tile_variants=errors,fg_columns=len(columns['fg']),bg_columns=len(columns['bg'])))
    profiles=[];counts=[]
    for ident in range(35):
        count=program[RULES['collision_counts']+ident];ptr=word(program,RULES['collision_pointers']+2*ident)
        assert 1<=count<=4
        counts.append(count)
        profiles.extend(program[ptr:ptr+36*count]);profiles.extend([0]*(36*(4-count)))
    array('collision_profiles',profiles,'u8');array('collision_counts',counts,'u8')
    declarations.append('const Stage stages[32]={'+','.join(stages)+'};')
    (out/'data.c').write_text('\n'.join(declarations))
    (out/'stats.json').write_text(json.dumps(stats,indent=2))
    print('Prepared 32 native scenes. Peak tiles:',max(s['tiles'] for s in stats))

if __name__=='__main__':compile_resources()
