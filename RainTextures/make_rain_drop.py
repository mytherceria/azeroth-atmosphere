"""The raindrop texture, drawn from scratch: a soft streak, near-white with a cool tint, its alpha rising from the
tail (top) to the head (bottom). Written as BLP2, palettized, 8-bit alpha, full mip chain, the format the 1.12 client
reads for textures\\Weather\\RainDrop01.blp. Nothing here is read from the game: every pixel comes from the formulas
below, so the file can be shared freely.

The game's splash sheet (RainDropSplash01.blp) is NOT replaced: a softer splash was tried, but it was made from
Blizzard's own sheet, so it does not go in a public pack. The splash stays the game's own.

Usage: python3 make_rain_drop.py <out.blp>   (needs numpy and Pillow)
"""
import struct, sys
import numpy as np
from PIL import Image

def write_blp2_pal8(img_rgba, path):
    """BLP2, compression 1 (palette), alpha depth 8, full mip chain."""
    im=Image.fromarray(img_rgba,'RGBA'); w,h=im.size
    q=im.convert('RGB').quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    pal=q.getpalette()[:768]; pal+= [0]*(768-len(pal))
    palette=bytearray()
    for i in range(256): palette+=bytes([pal[i*3+2],pal[i*3+1],pal[i*3],0])   # BGRA
    mips=[]; cw,ch=w,h; cur=im
    while True:
        qi=cur.convert('RGB').quantize(palette=q, dither=Image.Dither.NONE)
        mips.append(bytes(np.array(qi,dtype=np.uint8).tobytes())+bytes(np.array(cur.getchannel('A'),dtype=np.uint8).tobytes()))
        if cw==1 and ch==1: break
        cw,ch=max(1,cw//2),max(1,ch//2); cur=im.resize((cw,ch),Image.LANCZOS)
        if len(mips)==16: break
    header=b'BLP2'+struct.pack('<I',1)+bytes([1,8,8,1])+struct.pack('<II',w,h)
    offs=[];sizes=[];pos=148+1024
    for m in mips: offs.append(pos); sizes.append(len(m)); pos+=len(m)
    offs+= [0]*(16-len(offs)); sizes+= [0]*(16-len(sizes))
    data=header+struct.pack('<16I',*offs)+struct.pack('<16I',*sizes)+bytes(palette)+b''.join(mips)
    open(path,'wb').write(data); return len(data), len(mips)

w, h = 16, 128
img = np.zeros((h, w, 4), dtype=np.uint8)
x = np.arange(w)
across = np.exp(-((x - 9.0) / 1.6) ** 2)          # the game's own streak sits at columns 7 to 10, so this lines up with it
for y in range(8, h):
    along = min(1.0, (y - 8) / 6.0, (h - 1 - y) / 6.0) * 0.9
    img[y, :, 3] = np.clip(255 * across * along, 0, 255)
img[..., 0] = 225; img[..., 1] = 232; img[..., 2] = 242
print("drop:", write_blp2_pal8(img, sys.argv[1] if len(sys.argv) > 1 else "RainDrop01.blp"))
