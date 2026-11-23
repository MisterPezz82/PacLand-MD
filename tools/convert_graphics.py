"""Native 4bpp tile packing, exact deduplication and optional sprite correction.
This module consumes decoded source pixels; it does not resize world tile grids.
"""
from PIL import Image

def pack_tile(pixels):
    if len(pixels)!=8 or any(len(row)!=8 for row in pixels):raise ValueError('8x8 required')
    if any(not 0<=v<=15 for row in pixels for v in row):raise ValueError('4bpp indices required')
    return bytes((row[x]<<4)|row[x+1] for row in pixels for x in range(0,8,2))

def unpack_tile(data):
    if len(data)!=32:raise ValueError('32 bytes required')
    return [[(data[y*4+x//2]>>(4 if x%2==0 else 0))&15 for x in range(8)] for y in range(8)]

def sprite_tiles(pixels):
    h=len(pixels);w=len(pixels[0]);out=bytearray()
    # Hardware metasprite patterns are column-major, unlike plane maps.
    for tx in range(0,w,8):
        for ty in range(0,h,8):
            out.extend(pack_tile([[pixels[y][x] if y<h and x<w else 0
                                    for x in range(tx,tx+8)] for y in range(ty,ty+8)]))
    return bytes(out)

def correct_sprite(pixels, numerator=10, denominator=9):
    im=Image.new('P',(len(pixels[0]),len(pixels)))
    im.putdata([v for row in pixels for v in row])
    im=im.resize(((im.width*numerator+denominator//2)//denominator,im.height),Image.Resampling.NEAREST)
    return [[im.getpixel((x,y)) for x in range(im.width)] for y in range(im.height)]
