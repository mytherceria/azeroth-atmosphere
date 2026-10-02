"""Makes the wide loading screen test: a pattern drawn at the screen's own shape (circles that are round only when
the game shows it at that shape, arrows at all four edges), for the two continents' loading screens, packed into a
patch archive.

Usage: STORMLIB=/path/to/libstorm.so python3 make_test_screen.py <out dir> [screen width] [screen height]
       (default 5120 2160). Writes test-<continent>.png, the two BLPs and patch-U.MPQ into <out dir>.
"""
import ctypes, os, sys
from PIL import Image, ImageDraw, ImageFont
from blp import write_blp

TW, TH = 2048, 1024        # the texture: about 1.4 MB with its mipmaps, under the client's ~2 MB for loading screens
FONTS = os.path.join(os.environ.get('WOW_DIR', '.'), 'Interface', 'AddOns', 'ClearNames')   # WOW_DIR = the game folder
FONT = os.path.join(FONTS, 'AtkinsonHyperlegible-Bold.ttf')
TITLE = os.path.join(FONTS, 'FontinSans-SmallCaps.ttf')
SCREENS = {'Kalimdor': 'KALIMDOR', 'EasternKingdom': 'EASTERN KINGDOMS'}


def pattern_for(label, SW, SH):
    im = Image.new('RGB', (SW, SH))
    d = ImageDraw.Draw(im)
    for y in range(SH):                                   # dark blue to dark teal, top to bottom
        t = y / SH
        d.line([(0, y), (SW, y)], fill=(int(18 + 6 * t), int(24 + 20 * t), int(40 + 10 * t)))
    small = ImageFont.truetype(FONT, SH // 40)
    for i in range(1, 10):                                # a line every tenth, labelled
        x, y = SW * i // 10, SH * i // 10
        d.line([(x, 0), (x, SH)], fill=(60, 72, 96), width=3)
        d.line([(0, y), (SW, y)], fill=(60, 72, 96), width=3)
        d.text((x + 10, SH // 2 + 10), f'{i * 10}%', font=small, fill=(110, 125, 150))
    r = SH // 3                                           # round only at the right shape
    cx, cy = SW // 2, SH // 2
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(232, 180, 60), width=14)
    for qx, qy in ((0.12, 0.2), (0.88, 0.2), (0.12, 0.8), (0.88, 0.8)):
        x, y, rr = int(SW * qx), int(SH * qy), SH // 9
        d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=(120, 190, 255), width=10)
    a = SH // 14                                          # arrows to each edge
    d.polygon([(8, cy), (8 + a, cy - a), (8 + a, cy + a)], fill=(255, 255, 255))
    d.polygon([(SW - 8, cy), (SW - 8 - a, cy - a), (SW - 8 - a, cy + a)], fill=(255, 255, 255))
    d.polygon([(cx, 8), (cx - a, 8 + a), (cx + a, 8 + a)], fill=(255, 255, 255))
    d.polygon([(cx, SH - 8), (cx - a, SH - 8 - a), (cx + a, SH - 8 - a)], fill=(255, 255, 255))
    mid = ImageFont.truetype(FONT, SH // 22)
    d.text((16 + a, cy - SH // 44), 'LEFT EDGE', font=mid, fill=(255, 255, 255))
    d.text((SW - 16 - a, cy - SH // 44), 'RIGHT EDGE', font=mid, fill=(255, 255, 255), anchor='ra')
    title = ImageFont.truetype(TITLE, SH // 14)
    d.text((cx, SH * 0.08), 'CinderLoad: loading screen test', font=title, fill=(232, 180, 60), anchor='mt')
    big = ImageFont.truetype(FONT, SH // 9)
    d.text((cx, cy), label, font=big, fill=(255, 255, 255), anchor='mm')
    note = ImageFont.truetype(FONT, SH // 34)
    d.text((cx, cy + r + SH // 30), f'Drawn for {SW} x {SH}, stored as {TW} x {TH}. The gold circle is round and both side '
           'arrows touch the edges when the shape is right.', font=note, fill=(210, 215, 225), anchor='mt')
    return im


def pack(mpq_path, files):
    lib = ctypes.CDLL(os.environ.get('STORMLIB', 'libstorm.so'))
    lib.SFileCreateArchive.restype = ctypes.c_bool
    lib.SFileAddFileEx.restype = ctypes.c_bool
    lib.SFileCloseArchive.restype = ctypes.c_bool
    if os.path.exists(mpq_path):
        raise SystemExit(f'{mpq_path} exists; not overwriting')
    h = ctypes.c_void_p()
    if not lib.SFileCreateArchive(mpq_path.encode(), 0x00100000, 16, ctypes.byref(h)):   # format 1, with a listfile
        raise SystemExit('could not create the archive')
    for src, name in files:
        if not lib.SFileAddFileEx(h, src.encode(), name.encode(), 0x00000200 | 0x80000000, 0x02, 0xFFFFFFFF):  # zlib
            lib.SFileCloseArchive(h); os.remove(mpq_path); raise SystemExit(f'could not add {src}')
    lib.SFileCloseArchive(h)


if __name__ == '__main__':
    out = sys.argv[1]
    SW, SH = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (5120, 2160)
    os.makedirs(out, exist_ok=True)
    files = []
    for name, label in SCREENS.items():
        png = os.path.join(out, f'test-{name}.png')
        pattern_for(label, SW, SH).save(png)
        blp = os.path.join(out, f'LoadScreen{name}.blp')
        levels, size = write_blp(png, blp, TW, TH)
        print(f'{os.path.basename(blp)}: {TW}x{TH}, {levels} levels, {size} bytes')
        files.append((blp, f'Interface\\Glues\\LoadingScreens\\LoadScreen{name}.blp'))
    pack(os.path.join(out, 'patch-U.MPQ'), files)
    print('patch-U.MPQ', os.path.getsize(os.path.join(out, 'patch-U.MPQ')), 'bytes')
