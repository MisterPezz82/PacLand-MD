"""Mega Drive CRAM conversion and deterministic weighted palette reduction."""
from collections import Counter

def cram(rgb):
    r,g,b=(min(7,(v*7+127)//255) for v in rgb)
    return (b<<9)|(g<<5)|(r<<1)

def rgb(word):
    return tuple(((word>>shift)&7)*255//7 for shift in (1,5,9))

def distance(a,b):
    return sum(w*(x-y)**2 for w,x,y in zip((3,6,2),a,b))

def reduce_palette(colors, count=15):
    counts=Counter(cram(c) for c in colors)
    if not counts:return [0]*count
    # Frequency-weighted farthest sampling, then weighted medoid refinement.
    chosen=[max(counts,key=lambda c:(counts[c],-c))]
    while len(chosen)<min(count,len(counts)):
        chosen.append(max((c for c in counts if c not in chosen),
                          key=lambda c:(counts[c]*min(distance(rgb(c),rgb(v)) for v in chosen),-c)))
    for _ in range(4):
        buckets=[[] for _ in chosen]
        for c in counts:
            i=min(range(len(chosen)),key=lambda i:distance(rgb(c),rgb(chosen[i])))
            buckets[i].append(c)
        chosen=[min(bucket,key=lambda c:sum(counts[v]*distance(rgb(c),rgb(v)) for v in bucket))
                if bucket else chosen[i] for i,bucket in enumerate(buckets)]
    return chosen+[0]*(count-len(chosen))

def nearest(color, palette):
    return min(range(len(palette)),key=lambda i:distance(color,rgb(palette[i])))
