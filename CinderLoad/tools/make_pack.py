"""Builds one screen shape's loading screens: Data/CinderLoad/LoadingScreens-<shape>.MPQ, every loading screen the
client names in LoadingScreens.dbc, at the shape of the screen. Each is the client's own picture at the 4:3 it was
drawn for, centred, with its sides filled by a blurred, darkened, wider copy of itself, eased together at the
seams; the continents' two screens can be the test pattern instead (--test-continents).

Usage: STORMLIB=/path/to/libstorm.so python3 make_pack.py <client Data dir> <out dir> <shape> <screen w> <screen h>
       [--test-continents] [--art <map file>] [--bar <dir>]
   e.g. ... make_pack.py ~/Games/RavenCraft-fogtest/Data out 21x9 5120 2160 --test-continents
"""
import concurrent.futures as cf, ctypes, glob, io, os, struct, sys, tempfile
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
        patches = [p for p in patches if p.lower() != 'patch-u.mpq']          # never our own output
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
    name = path.split('\\')[-1]
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, 'in.png')
        im.save(png)
        blp = os.path.join(out, name)
        write_blp(png, blp, TW, TH)
    return path, blp


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
    mpq = os.path.join(out, 'Data', 'CinderLoad', f'LoadingScreens-{shape}.MPQ')
    os.makedirs(os.path.dirname(mpq), exist_ok=True)
    pack(mpq, files)
    print(f'{len(files)} loading screens for {shape} ({sw}x{sh}) in {mpq}: {os.path.getsize(mpq) // 1048576} MB')
    for m in missing:
        print('  not in the client, skipped:', m)
