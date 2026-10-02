"""BLP2 palettised textures with 8-bit alpha (compression 1, alpha depth 8): the format of the client's loading bar
(Interface\\Glues\\LoadingBar). read() gives an RGBA picture of the top mip; write() makes one with a full mip chain.
  python3 blp_pal.py in.blp out.png      (decode)
  python3 blp_pal.py in.png out.blp      (encode)
"""
import struct, sys
from PIL import Image

HEADER = 148            # magic, type, compression, alpha depth, alpha type, has mips, w, h, 16 offsets, 16 sizes


def read(data):
    magic, typ, comp, adepth, atype, mips, w, h = struct.unpack('<4sIBBBBII', data[:20])
    if magic != b'BLP2' or comp != 1:
        raise ValueError('not a palettised BLP2')
    off = struct.unpack('<16I', data[20:84])[0]
    pal = data[HEADER:HEADER + 1024]
    idx = data[off:off + w * h]
    rgb = bytearray()
    for i in idx:
        b, g, r, _ = pal[i * 4:i * 4 + 4]
        rgb += bytes((r, g, b))
    im = Image.frombytes('RGB', (w, h), bytes(rgb)).convert('RGBA')
    if adepth == 8:
        im.putalpha(Image.frombytes('L', (w, h), data[off + w * h:off + 2 * w * h]))
    elif adepth == 0:
        im.putalpha(255)
    else:
        raise ValueError(f'alpha depth {adepth} not handled')
    return im


def write(im, path):
    im = im.convert('RGBA')
    w, h = im.size
    if w & (w - 1) or h & (h - 1):
        raise ValueError('sides must be powers of two')
    q = im.convert('RGB').quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = (q.getpalette() or [])[:768]
    pal += [0] * (768 - len(pal))
    palette = b''.join(bytes((pal[i + 2], pal[i + 1], pal[i], 0)) for i in range(0, 768, 3))
    chunks, size = [], (w, h)
    level = im
    while True:
        lq = level.convert('RGB').quantize(palette=q, dither=Image.Dither.NONE)
        chunks.append(lq.tobytes() + level.getchannel('A').tobytes())
        if size == (1, 1) or len(chunks) == 16:
            break
        size = (max(1, size[0] // 2), max(1, size[1] // 2))
        level = im.resize(size, Image.LANCZOS)
    offsets, sizes, pos = [], [], HEADER + 1024
    for c in chunks:
        offsets.append(pos); sizes.append(len(c)); pos += len(c)
    offsets += [0] * (16 - len(offsets)); sizes += [0] * (16 - len(sizes))
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sIBBBBII', b'BLP2', 1, 1, 8, 8, 1, w, h))
        f.write(struct.pack('<16I', *offsets) + struct.pack('<16I', *sizes))
        f.write(palette)
        for c in chunks:
            f.write(c)


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    if src.lower().endswith('.blp'):
        read(open(src, 'rb').read()).save(dst)
    else:
        write(Image.open(src), dst)
