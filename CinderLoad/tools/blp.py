"""Writes a picture as a BLP2 texture the 1.12 client reads, the way its own loading screens are stored: DXT1, no
alpha, a full chain of smaller copies (mipmaps). ImageMagick does the DXT1 compression into a DDS file; this only
moves those blocks into a BLP2 header.

Usage as a module: write_blp(png_path, blp_path, width, height). The picture is resized to width x height exactly,
so a picture drawn at the screen's shape is squeezed into the texture and the game stretches it back.
"""
import os, struct, subprocess, tempfile


def write_blp(src, dst, width, height):
    with tempfile.TemporaryDirectory() as tmp:
        dds = os.path.join(tmp, 'tex.dds')
        subprocess.run(['magick', src, '-resize', f'{width}x{height}!', '-alpha', 'off',
                        '-define', 'dds:compression=dxt1',
                        '-define', 'dds:mipmaps=16', dds], check=True)
        data = open(dds, 'rb').read()
    if data[:4] != b'DDS ' or data[84:88] != b'DXT1':
        raise SystemExit('ImageMagick did not write a DXT1 DDS')
    h, w = struct.unpack_from('<II', data, 12)          # DDS_HEADER: height, then width
    mips = struct.unpack_from('<I', data, 28)[0]          # dwMipMapCount, the full-size picture included
    if (w, h) != (width, height):
        raise SystemExit(f'DDS is {w}x{h}, wanted {width}x{height}')
    mips = max(1, mips)
    offsets, sizes, pos, lw, lh = [], [], 128, w, h
    for _ in range(min(mips, 16)):
        n = max(1, (lw + 3) // 4) * max(1, (lh + 3) // 4) * 8
        offsets.append(pos); sizes.append(n); pos += n
        lw, lh = max(1, lw // 2), max(1, lh // 2)
    if pos > len(data):
        raise SystemExit('DDS shorter than its mipmaps')
    header_len = 4 + 4 + 4 + 8 + 64 + 64 + 1024          # 1172, as in the client's own files
    blob = b''.join(data[o:o + s] for o, s in zip(offsets, sizes))
    out_offsets, p = [], header_len
    for s in sizes:
        out_offsets.append(p); p += s
    out_offsets += [0] * (16 - len(out_offsets)); out_sizes = sizes + [0] * (16 - len(sizes))
    header = b'BLP2' + struct.pack('<IBBBBII', 1, 2, 0, 0, 1 if len(sizes) > 1 else 0, w, h)
    header += struct.pack('<16I', *out_offsets) + struct.pack('<16I', *out_sizes) + bytes(1024)
    assert len(header) == header_len
    with open(dst, 'wb') as f:
        f.write(header + blob)
    return len(sizes), os.path.getsize(dst)
