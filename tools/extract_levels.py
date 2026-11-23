"""Recover all 32 original round segment/object definitions, without guesses.

World parent set addresses verified against disassembly D014-D0EF,
D2B3-D4A7, 8052-8094, 8559-85AA. Native rendering still requires game logic.
"""
import argparse
import json
from pathlib import Path
from extract_arcade import ROOT, load_regions

RULES = dict(round_segments=0x1dd00, fg_pointers=0xdd0d, bg_pointers=0xdd55,
             segment_classes=0xdcea, object_bank=0x1e000,
             object_pointer_field=4, secret_pointer_field=8,
             collision_counts=0xd4c5, collision_pointers=0xd4e8,
             fg_widths=[288,352,288],bg_widths=[256,288,288])

def word(data, offset): return int.from_bytes(data[offset:offset+2],'big')
def bank7(pointer): return RULES['object_bank']+(pointer & 0x1fff)
def packed(pointer): return 0x10000+pointer

def records(data,pointer,max_records):
    offset=bank7(pointer);result=[]
    for i in range(max_records):
        row=data[offset+4*i:offset+4*i+4]
        if row[0]==255:return result
        if len(row)!=4:raise ValueError('Truncated object data')
        result.append({'type':row[0],'parameters':list(row[1:]),'rom_offset':offset+4*i})
    raise ValueError('Missing object terminator')

def extract(data):
    rounds=[]
    for index in range(32):
        start=0x1c000+(word(data,RULES['round_segments']+index*2)&0x1fff)
        ids=[];pos=start
        while data[pos]!=255:
            ids.append(data[pos]);pos+=1
            if len(ids)>32:raise ValueError('Bad segment count')
        objtable=bank7(word(data,RULES['object_bank']+RULES['object_pointer_field']))
        secret=bank7(word(data,RULES['object_bank']+RULES['secret_pointer_field']))
        rounds.append(dict(round=index+1,trip=index//4+1,journey='return' if index%4==3 else 'outward',
                           segment_ids=ids,segments_offset=start,
                           start_segment=data[pos+1],end_segment=data[pos+2],
                           world_start=data[pos+1]*288,world_end=data[pos+2]*288,
                           palette_bank=(index&12)>>2,
                           objects=records(data,word(data,objtable+index*2),57),
                           interactions=records(data,word(data,secret+index*2),128),
                           gameplay_status='not implemented',map_status='ROM decoded; validate against reference RAM'))
    return rounds

def segment(data, ident, layer, round_index=0):
    cls=data[RULES['segment_classes']+ident]
    n=RULES[layer+'_widths'][cls]//8
    effective=35 if layer=='fg' and round_index>=16 and round_index%16==10 and 0<ident<32 else ident
    ptr=word(data,RULES[layer+'_pointers']+2*effective)
    cells=[]
    for col in range(n):
        descriptor=packed(ptr+(col//4)*12)
        column=[]
        for block in range(6):
            p=word(data,descriptor+2*block)+(col%4)*8
            for row in range(4):
                address=packed(p+2*row)
                column.append(word(data,address))
        cells.append(column)
    return cells

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--rom',required=True,type=Path)
    p.add_argument('--out',type=Path,default=ROOT/'res/generated/levels');a=p.parse_args()
    data=load_regions(a.rom)[0]['maincpu'];a.out.mkdir(parents=True,exist_ok=True)
    rounds=extract(data)
    (a.out/'rules.json').write_text(json.dumps(RULES,indent=2))
    (a.out/'manifest.json').write_text(json.dumps(rounds,indent=2))
    unique=sorted({s for r in rounds for s in r['segment_ids']})
    segments={str(i):{layer:segment(data,i,layer) for layer in ['fg','bg']} for i in unique}
    segments['35']={'fg':segment(data,35,'fg'),'bg':segment(data,35,'bg')}
    (a.out/'segments.json').write_text(json.dumps(segments,separators=(',',':')))
    print('Recovered',len(rounds),'round definitions;',len(unique),'segment IDs;',sum(len(r['objects']) for r in rounds),'object records')

if __name__=='__main__':main()
