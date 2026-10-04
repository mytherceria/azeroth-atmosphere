r"""Builds one screen shape's loading screens: Data/CinderLoad/LoadingScreens-<shape>.MPQ, every loading screen the
client names in LoadingScreens.dbc, at the shape of the screen. Each is the client's own picture at the 4:3 it was
drawn for, centred, with its sides filled by a blurred, darkened, wider copy of itself, eased together at the
seams; the continents' two screens can be the test pattern instead (--test-continents).

Usage: STORMLIB=/path/to/libstorm.so python3 make_pack.py <client Data dir> <out dir> <shape> <screen w> <screen h>
       [--test-continents] [--art <map file>] [--bar <dir>] [--fire-bars <dir> --bar-map <file>] [--reveal]
       [--wallpapers <file>]
   e.g. ... make_pack.py ~/Games/RavenCraft-fogtest/Data out 21x9 5120 2160 --test-continents

The loading bar's fire for each screen (CinderLoad.dll picks the bar by the screen's picture; both options or neither):
  --fire-bars <dir>  one folder per fire family, named for it in letters and digits (fel, arcane, aqua, ...), each
                     holding that family's Loading-BarFill.blp and Loading-BarBorder.blp, drawn as the --bar ones are.
                     They go in as Interface\Glues\LoadingBar\Loading-BarFill-<family>.blp and -BarBorder-<family>.blp,
                     beside the --bar ones, which stay the orange bar the game shows without the DLL.
  --bar-map <file>   the art kit's choice of family for each screen: lines '<loading screen> <family>', where the screen
                     is its file (LoadScreenDeadmines.blp) or its name (loadscreendeadmines), 'loading' is the default
                     screen (Interface\Glues\loading, for a map with none of its own) and 'default <family>' covers
                     every screen not listed; '#' starts a comment. Packed as CinderLoad\bars.txt. A family the map
                     names with no folder (orange, say) packs no textures: its screens keep the orange bar.
"""
import concurrent.futures as cf, ctypes, glob, io, os, re, struct, sys, tempfile
from PIL import Image, ImageFilter
from blp import write_blp

TW, TH = 2048, 1024        # about 1.4 MB with mipmaps, under the client's ~2 MB for a loading screen
BASE = ['base.MPQ', 'dbc.MPQ', 'fonts.MPQ', 'interface.MPQ', 'misc.MPQ', 'model.MPQ', 'sound.MPQ', 'speech.MPQ',
        'terrain.MPQ', 'texture.MPQ', 'wmo.MPQ', 'backup.MPQ']


class Client:
    """The newest copy of a file across the client's archives, in the order the client loads them."""
    def __init__(self, data):
        self.L = L = ctypes.CDLL(os.environ.get('STORMLIB', 'libstorm.so'))
        L.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        L.SFileOpenFileEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
        L.SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
        L.SFileGetFileSize.restype = ctypes.c_uint
        L.SFileReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint),
                                    ctypes.c_void_p]
        L.SFileCloseFile.argtypes = [ctypes.c_void_p]
        patches = [os.path.basename(p) for p in glob.glob(os.path.join(data, 'patch*.[mM][pP][qQ]'))]
        patches = [p for p in patches if p.lower() != 'patch-~.mpq']          # never our own output
        patches.sort(key=lambda n: (0 if n.lower() == 'patch.mpq' else 1, n.lower()))
        self.handles = []
        for name in [b for b in BASE if os.path.exists(os.path.join(data, b))] + patches:
            h = ctypes.c_void_p()
            if L.SFileOpenArchive(os.path.join(data, name).encode(), 0, 0x100, ctypes.byref(h)):   # read only
                self.handles.append(h)

    def read(self, path):
        best = None
        for h in self.handles:
            f = ctypes.c_void_p()
            if not self.L.SFileOpenFileEx(h, path.encode(), 0, ctypes.byref(f)):
                continue
            hi = ctypes.c_uint(0)
            n = self.L.SFileGetFileSize(f, ctypes.byref(hi))
            if n and n != 0xFFFFFFFF:                       # a zero-length copy is a stub, not the file
                buf = ctypes.create_string_buffer(n)
                got = ctypes.c_uint(0)
                self.L.SFileReadFile(f, buf, n, ctypes.byref(got), None)
                if got.value == n:
                    best = buf.raw
            self.L.SFileCloseFile(f)
        return best


def loading_screens(client):
    d = client.read('DBFilesClient\\LoadingScreens.dbc')
    _, n, fields, size, _ = struct.unpack('<4s4I', d[:20])
    strings = d[20 + n * size:]
    files = set()
    for i in range(n):
        off = struct.unpack_from('<I', d, 20 + i * size + 8)[0]          # the third field: the file name
        files.add(strings[off:strings.index(b'\0', off)].decode('latin-1'))
    return sorted(files, key=str.lower)


def widen(art, sw, sh):
    """The picture at its 4:3, centred on a sw x sh canvas whose sides are a blurred, darker, wider copy of it."""
    art = art.convert('RGB')
    w43 = round(sh * 4 / 3)
    middle = art.resize((w43, sh), Image.LANCZOS)
    small = art.resize((sw // 8, round(sw * 3 / 4) // 8), Image.BILINEAR).filter(ImageFilter.GaussianBlur(sh / 160))
    top = (small.height - sh // 8) // 2
    back = small.crop((0, top, sw // 8, top + sh // 8)).resize((sw, sh), Image.BILINEAR)
    back = Image.eval(back, lambda v: int(v * 0.55))
    if w43 >= sw:
        return middle.crop(((w43 - sw) // 2, 0, (w43 - sw) // 2 + sw, sh))
    mask = Image.new('L', (w43, sh), 255)
    ease = max(1, w43 // 16)
    for x in range(ease):
        v = int(255 * (x + 1) / ease)
        mask.paste(v, (x, 0, x + 1, sh))
        mask.paste(v, (w43 - 1 - x, 0, w43 - x, sh))
    back.paste(middle, ((sw - w43) // 2, 0), mask)
    return back


BAR_BAND = False   # set by --bar: the screens get a dark band where the bar's fire runs
REVEAL = False     # set by --reveal: the bar's fill is a painting of the burning log, uncovered as the bar fills
                   # (CinderLoad.dll reveals it when the pack holds CinderLoad\reveal.txt); the screens then get pure
                   # black under the whole log, so the log is matte black wherever the fire has not reached


def dark_band(im):
    """Darkens the strip under CinderLoad's full-width bar where the fill runs (the whole width, 0.052 of the height,
    centred 0.045 above the bottom). The log covers it, so it is seen only through the cracks the fire has not reached
    yet, which then look dark instead of showing the picture. The game draws nothing there before the fill."""
    w, h = im.size
    x0, x1 = 0, w
    if REVEAL:                                          # the whole log, 0.09 of the height from the bottom, pure black
        y0, y1 = int(h * (1 - 0.09)) - 2, h
        band = Image.new(im.mode, (x1 - x0, y1 - y0))
        im = im.copy()
        im.paste(band, (x0, y0))
        return im
    y0, y1 = int(h * (1 - 0.078)), int(h * (1 - 0.012))
    band = im.crop((x0, y0, x1, y1)).point(lambda v: int(v * 0.06))
    im = im.copy()
    im.paste(band, (x0, y0))
    return im


def build_one(job):
    path, raw, sw, sh, label, out = job
    if label and os.path.isfile(label):                 # a designed screen: cover the shape, centred
        art = Image.open(label).convert('RGB')
        k = max(sw / art.width, sh / art.height)
        art = art.resize((round(art.width * k), round(art.height * k)), Image.LANCZOS)
        x, y = (art.width - sw) // 2, (art.height - sh) // 2
        im = art.crop((x, y, x + sw, y + sh))
    elif label:
        from make_test_screen import pattern_for
        im = pattern_for(label, sw, sh)
    else:
        im = widen(Image.open(io.BytesIO(raw)), sw, sh)
    if BAR_BAND:
        im = dark_band(im)
    name = path.split('\\')[-1]
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, 'in.png')
        im.save(png)
        blp = os.path.join(out, name)
        write_blp(png, blp, TW, TH)
    return path, blp


FAMILIES_MAX, SCREENS_MAX, NAME_MAX = 8, 256, 47     # what CinderLoad.dll reads of bars.txt: no more than this


def screen_name(s):
    """A screen as bars.txt names it: the file's base name, no extension, lower case (as the DLL compares it)."""
    s = s.replace('/', '\\').split('\\')[-1]
    return (s.rsplit('.', 1)[0] if '.' in s else s).lower()


def fire_bars(bar_dir, map_file, screens, work):
    """The fire bars' files for the pack, [(source, name in the archive)], from the family folders and the art kit's
    map; bars.txt is written into work. Refuses what the DLL would skip, so a slip shows here and not in the game."""
    chosen, default = {}, None
    for n, line in enumerate(open(map_file, encoding='utf-8').read().splitlines(), 1):
        words = line.split('#', 1)[0].split()
        if not words:
            continue
        if len(words) != 2:
            raise SystemExit(f'{map_file}:{n}: want "<screen> <family>", got {line!r}')
        name, family = screen_name(words[0]), words[1].lower()
        if not re.fullmatch(r'[a-z0-9]{1,15}', family):
            raise SystemExit(f'{map_file}:{n}: a family is 1 to 15 letters and digits, not {words[1]!r}')
        if not (1 <= len(name) <= NAME_MAX and name.isascii() and name.isprintable()):
            raise SystemExit(f'{map_file}:{n}: a screen name of 1 to {NAME_MAX} plain characters, not {words[0]!r}')
        if name == 'default':
            if default:
                raise SystemExit(f'{map_file}:{n}: a second default line')
            default = family
        elif name in chosen:
            raise SystemExit(f'{map_file}:{n}: {name} is named twice')
        else:
            chosen[name] = family
    families = sorted(set(chosen.values()) | ({default} if default else set()))
    if len(families) > FAMILIES_MAX or len(chosen) > SCREENS_MAX:
        raise SystemExit(f'{map_file}: {len(families)} families and {len(chosen)} screens; '
                         f'CinderLoad reads at most {FAMILIES_MAX} and {SCREENS_MAX}')
    known = {screen_name(p) for p in screens} | {'loading'}
    for name in sorted(set(chosen) - known):
        print(f'  fire bars: {name} is not a loading screen of this client; its line is packed all the same')
    if not default:
        print('  fire bars: no default line, so every screen the map does not list keeps the orange bar')
    files, packed = [], []
    folders = {d.lower(): d for d in os.listdir(bar_dir) if os.path.isdir(os.path.join(bar_dir, d))}
    for family in families:
        if family not in folders:
            print(f'  fire bars: no folder for {family}, so its screens keep the orange bar')
            continue
        for part in ('Fill', 'Border'):
            src = os.path.join(bar_dir, folders[family], f'Loading-Bar{part}.blp')
            if not os.path.isfile(src):
                raise SystemExit(f'{src} is missing: a family needs both its textures')
            files.append((src, f'Interface\\Glues\\LoadingBar\\Loading-Bar{part}-{family}.blp'))
        packed.append(family)
    for d in sorted(set(folders) - set(families)):
        print(f'  fire bars: the folder {folders[d]} is not a family the map names; not packed')
    bars = os.path.join(work, 'bars.txt')
    with open(bars, 'w', encoding='ascii', newline='\n') as f:
        f.write('# CinderLoad: the fire of each loading screen\'s bar, "<screen> <family>"; "default" is every screen\n'
                '# not listed. A family whose two textures are not in this archive keeps the orange bar. (make_pack.py)\n')
        for name in sorted(chosen):
            f.write(f'{name} {chosen[name]}\n')
        if default:
            f.write(f'default {default}\n')
    files.append((bars, 'CinderLoad\\bars.txt'))
    print(f'  fire bars: {len(chosen)} screens{" and a default" if default else ""}; '
          f'families packed: {", ".join(packed) or "none"}')
    return files


def pack(mpq_path, files):
    lib = ctypes.CDLL(os.environ.get('STORMLIB', 'libstorm.so'))
    lib.SFileCreateArchive.restype = ctypes.c_bool
    lib.SFileAddFileEx.restype = ctypes.c_bool
    lib.SFileCloseArchive.restype = ctypes.c_bool
    if os.path.exists(mpq_path):
        raise SystemExit(f'{mpq_path} exists; not overwriting')
    h = ctypes.c_void_p()
    if not lib.SFileCreateArchive(mpq_path.encode(), 0x00100000, max(64, len(files) * 2), ctypes.byref(h)):
        raise SystemExit('could not create the archive')
    for src, name in files:
        if not lib.SFileAddFileEx(h, src.encode(), name.encode(), 0x00000200 | 0x80000000, 0x02, 0xFFFFFFFF):
            lib.SFileCloseArchive(h); os.remove(mpq_path); raise SystemExit(f'could not add {src}')
    lib.SFileCloseArchive(h)


if __name__ == '__main__':
    data, out, shape, sw, sh = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
    test = '--test-continents' in sys.argv
    BAR_BAND = '--bar' in sys.argv                     # module level: build_one sees it
    REVEAL = '--reveal' in sys.argv
    art = {}                                            # --art <file>: lines '<loadscreen>.blp <picture>'
    if '--art' in sys.argv:
        for line in open(sys.argv[sys.argv.index('--art') + 1]).read().splitlines():
            if line.strip() and not line.startswith('#'):
                k, v = line.split(None, 1)
                art[k.lower()] = os.path.expanduser(v.strip())
    client = Client(data)
    blps = os.path.join(out, 'blp-' + shape)
    os.makedirs(blps, exist_ok=True)
    jobs, missing = [], []
    labels = {'loadscreenkalimdor.blp': 'KALIMDOR', 'loadscreeneasternkingdom.blp': 'EASTERN KINGDOMS'}
    for path in loading_screens(client):
        raw = client.read(path)
        base = path.split('\\')[-1].lower()
        label = art.get(base) or (labels.get(base) if test else None)
        if raw is None and not label:
            missing.append(path)
            continue
        jobs.append((path, raw, sw, sh, label, blps))
    if '--wallpapers' in sys.argv:                      # one picture per line: CinderWall01.blp, 02, ... in that order,
        pics = [l.strip() for l in open(sys.argv[sys.argv.index('--wallpapers') + 1]).read().splitlines()
                if l.strip() and not l.startswith('#')]       # drawn at random by CinderLoad.dll on the continents
        if not 0 < len(pics) <= 64:
            raise SystemExit('--wallpapers: 1 to 64 pictures')
        for i, pic in enumerate(pics, 1):
            pic = os.path.expanduser(pic)
            if not os.path.isfile(pic):
                raise SystemExit(f'--wallpapers: no such picture {pic}')
            jobs.append((f'Interface\\Glues\\LoadingScreens\\CinderWall{i:02d}.blp', None, sw, sh, pic, blps))
    fire = []                                           # the bar's fire for each screen (see the usage above), checked
    if ('--fire-bars' in sys.argv) != ('--bar-map' in sys.argv):          # before the screens take their minutes
        raise SystemExit('--fire-bars and --bar-map go together')
    if '--fire-bars' in sys.argv:
        fire = fire_bars(sys.argv[sys.argv.index('--fire-bars') + 1], sys.argv[sys.argv.index('--bar-map') + 1],
                         [j[0] for j in jobs] + missing, blps)
    # Threads, not processes: the heavy part (DXT1) is ImageMagick in its own process anyway, and a process pool
    # hung under Python 3.14's forkserver default.
    files = []
    with cf.ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) // 2)) as ex:
        for path, blp in ex.map(build_one, jobs):
            files.append((blp, path))
            print(f'  {len(files)}/{len(jobs)} {path.split(chr(92))[-1]}', flush=True)
    if '--bar' in sys.argv:                             # --bar <dir>: the loading bar's Loading-Bar*.blp, as they are
        bar = sys.argv[sys.argv.index('--bar') + 1]
        for n in sorted(os.listdir(bar)):
            if n.lower().startswith('loading-bar') and n.lower().endswith('.blp'):
                files.append((os.path.join(bar, n), 'Interface\\Glues\\LoadingBar\\' + n))
    files += fire
    if REVEAL:
        note = os.path.join(blps, 'reveal.txt')
        with open(note, 'w') as f:
            f.write('The bar textures in this pack are made to be revealed, not stretched (CinderLoad 0.1.5 and later).\n')
        files.append((note, 'CinderLoad\\reveal.txt'))
    mpq = os.path.join(out, 'Data', 'CinderLoad', f'LoadingScreens-{shape}.MPQ')
    os.makedirs(os.path.dirname(mpq), exist_ok=True)
    pack(mpq, files)
    print(f'{len(files)} loading screens for {shape} ({sw}x{sh}) in {mpq}: {os.path.getsize(mpq) // 1048576} MB')
    for m in missing:
        print('  not in the client, skipped:', m)
