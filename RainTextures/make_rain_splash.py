"""The rain splash sheet, drawn from scratch: textures\\Weather\\RainDropSplash01.blp for the 1.12 client.

The game's sheet is 256 x 256: four rows, each a four-frame splash that grows over its frames, with the point of
impact at the lower middle of each 64 x 64 cell (about x 32, y 46) and the spray rising above it. Only that layout
was taken from it, so the client plays these frames where it played its own. Every pixel here comes from the
formulas below, nothing is read from the game, so the file can be shared freely.

Inspired by WoW Forever's rain, which hits the ground bright and quick, with a glitter of light off wet stone.
Four variants, one per row, each the same kind of impact (a sharp point and a glint, droplets thrown up, a ripple
running out) with its own droplets, at four sizes from the drop's own size up to twice its diameter, so the drops
landing around you vary. Brighter and crisper than the game's, with 8-bit alpha where the game's is 1-bit,
near-white with a cool tint like the drop's.

Usage: python3 make_rain_splash.py <out.blp> [preview.png]   (needs numpy and Pillow)
"""
import math, struct, sys
import numpy as np
from PIL import Image

SS = 4                      # drawn at 4x and averaged down, so every edge is smooth
CELL = 64
GX, GY = 32.0, 47.0         # the point of impact in a cell, as the game's sheet has it
TINT = (232, 238, 247)      # near-white, a little cooler than the drop's streak so it reads as water, not light

def canvas():
    n = CELL * SS
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    return (x + 0.5) / SS, (y + 0.5) / SS, np.zeros((n, n), np.float32)

def blob(x, y, a, cx, cy, r, strength, stretch=1.0):
    """A soft round drop: a Gaussian of radius r, optionally drawn out along y (a falling or rising drop)."""
    d2 = ((x - cx) / r) ** 2 + ((y - cy) / (r * stretch)) ** 2
    np.maximum(a, strength * np.exp(-d2 * 1.6), out=a)

def ring(x, y, a, rx, ry, width, strength, front=1.0, back=0.55):
    """A ring on the ground, seen from above at a slant: an ellipse whose near half (below the centre) is brighter."""
    d = np.sqrt(((x - GX) / max(rx, 0.1)) ** 2 + ((y - GY) / max(ry, 0.1)) ** 2) - 1.0
    edge = np.exp(-(d * min(rx, ry * 2.5) / max(width, 0.3)) ** 2)
    side = np.where(y >= GY, front, back)
    np.maximum(a, strength * edge * side, out=a)

def disc(x, y, a, rx, ry, strength):
    """A filled ellipse on the ground with a soft edge: the flash of impact, the sheet of water inside a ring."""
    d = np.sqrt(((x - GX) / max(rx, 0.1)) ** 2 + ((y - GY) / max(ry, 0.1)) ** 2)
    np.maximum(a, strength * np.clip(1.3 - d, 0, 1) ** 1.5, out=a)

def crown(x, y, a, rx, ry, height, strength):
    """The crown: a thin wall of water standing on the ring, brightest at its rim, fading toward the ground."""
    ang = np.arctan2((y - GY) / max(ry, 0.1), (x - GX) / max(rx, 0.1))
    ex = GX + rx * np.cos(ang)
    near = np.abs(x - ex) < 1.6
    rise = (GY + ry * np.abs(np.sin(ang))) - y                 # height above the ring's near edge at this x
    wall = near * np.clip(rise / max(height, 0.1), 0, 1) * (rise <= height + 0.8)
    np.maximum(a, strength * wall * (0.45 + 0.55 * np.clip(rise / max(height, 0.1), 0, 1)), out=a)

def glint(x, y, a, cx, cy, size, strength):
    """A point of light off wet ground: a bright core and a thin four-point star, the glitter of a wet road."""
    dx, dy = x - cx, y - cy
    core = np.exp(-(dx * dx + dy * dy) / (0.7 * size) ** 2)
    arms = np.exp(-(dy / 0.45) ** 2) * np.exp(-np.abs(dx) / size) + np.exp(-(dx / 0.45) ** 2) * np.exp(-np.abs(dy) / (size * 0.8))
    np.maximum(a, strength * np.clip(core + 0.6 * arms, 0, 1), out=a)

SIZES = (1.0, 1.33, 1.67, 2.0)    # the four variants span the drop's size up to twice its diameter

def frame(row, k, rng):
    """Forever's rain hits the ground bright and quick: a sharp point of impact, a few droplets thrown up, a thin
    ripple that runs out and fades, and a glint off the wet stone. All four rows are that impact, each with its own
    droplets, at four sizes from the drop's own size to twice its diameter; the client picks a row for each drop,
    so the sizes come out varied. Each starts bright and tight and is mostly gone by its last frame."""
    x, y, a = canvas()
    z = SIZES[row]
    t = (k + 1) / 4.0                                        # 0.25, 0.5, 0.75, 1.0 through the splash
    fade = (1.0 - t) ** 1.2 * 1.25 + 0.12                    # bright at first, nearly gone at the end
    if k == 0:                                               # the moment of impact: a sharp point with a glint
        disc(x, y, a, 2.6 * z, 1.0 * z, 1.0)
        glint(x, y, a, GX + 0.5, GY - 0.8 * z, 1.2 * z, 0.9)
    elif k == 1:
        glint(x, y, a, GX + 0.5, GY - 0.8 * z, 1.6 * z, 0.55)
    rx = (2.5 + 10 * t ** 0.75) * z                          # the ripple runs out and thins
    ring(x, y, a, rx, rx * 0.30, (0.8 + 0.4 * t) * min(z, 1.6), min(1.0, fade))
    if k >= 2:
        ring(x, y, a, rx * 0.55, rx * 0.55 * 0.30, 0.7 * min(z, 1.6), 0.5 * fade)
    n = 5 + row                                              # each variant throws its own few droplets
    for i in range(n):
        ang = 2 * math.pi * (i + 0.6 * rng.random()) / n
        v = 0.75 + 0.5 * rng.random()
        r = (3 + 8 * t) * v * z
        dx, dy = math.cos(ang) * r, math.sin(ang) * r * 0.32
        h = (18 * t - 16 * t * t) * v * z                    # up, then falling back: a short parabola
        blob(x, y, a, GX + dx, GY + dy - h, (0.9 + 0.25 * rng.random()) * min(z, 1.7), min(1.0, 1.1 * fade),
             stretch=1.0 + 0.6 * (1 - t))
    a = a.reshape(CELL, SS, CELL, SS).mean(axis=(1, 3))     # 4x down to the cell
    img = np.zeros((CELL, CELL, 4), np.uint8)
    img[..., 0], img[..., 1], img[..., 2] = TINT
    img[..., 3] = np.clip(a * 255, 0, 255).astype(np.uint8)
    return img

def sheet():
    rng = np.random.default_rng(1859)                        # fixed, so the file is the same on every run
    out = np.zeros((CELL * 4, CELL * 4, 4), np.uint8)
    for row in range(4):
        for k in range(4):
            out[row * CELL:(row + 1) * CELL, k * CELL:(k + 1) * CELL] = frame(row, k, rng)
    return out

# The same BLP2 writer as make_rain_drop.py: palette compression, 8-bit alpha, full mip chain.
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

img = sheet()
print("splash:", write_blp2_pal8(img, sys.argv[1] if len(sys.argv) > 1 else "RainDropSplash01.blp"))
if len(sys.argv) > 2:                                        # a preview on dark ground, 3x, with the cell grid
    bg = np.zeros((256, 256, 3), np.float32); bg[...] = (28, 34, 44)
    al = img[..., 3:4].astype(np.float32) / 255
    rgb = bg * (1 - al) + img[..., :3].astype(np.float32) * al
    for i in range(1, 4):
        rgb[i * 64, :, :] = (70, 80, 95); rgb[:, i * 64, :] = (70, 80, 95)
    Image.fromarray(rgb.astype(np.uint8)).resize((768, 768), Image.NEAREST).save(sys.argv[2])
