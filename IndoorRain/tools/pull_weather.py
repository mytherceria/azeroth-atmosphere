"""Read SoundEntries.dbc from the RavenCraft client and pull the nine weather loops (rain, snow, sand x light/medium/heavy)
out of the archives, later archive wins, zero-length stubs skipped. StormLib via ctypes. Read-only on the client.
What it writes are the client's own sound files, byte for byte, so it refuses an output folder inside a git work tree,
where a commit could pick them up (README, License), as make_loops.sh and make_storm_assets.sh refuse theirs."""
import ctypes, os, stat, struct, sys, pathlib, re
import argparse
ap = argparse.ArgumentParser(description='Pull the nine weather loops out of the client archives, later archive wins.')
ap.add_argument('--data', required=True, help='the client Data folder')
ap.add_argument('--storm', required=True, help='StormLib shared library (libstorm.so or StormLib.dll)')
ap.add_argument('--out', required=True, help='output folder for the .wav files: a scratch folder outside any git work tree')
args = ap.parse_args()
def in_git_work_tree(path):
    """True when path lands inside a git work tree. Where it lands: os.path.realpath takes each part that exists as the
    kernel will (its symlinks, and a '..' after them) and each that does not as mkdir will make it. Then a .git (a
    folder, or the file a worktree has) is looked for at and above that on disk, not asked of git, whose failure (a
    moved main checkout, a repo it calls dubious, no git at all) must not read as outside (the third 0.14 review). A
    folder that cannot be looked in counts as inside."""
    d = pathlib.Path(os.path.realpath(path))
    for p in (d, *d.parents):
        try:
            os.lstat(p / '.git')
            return True
        except (FileNotFoundError, NotADirectoryError):
            continue
        except OSError:
            return True
    return False
def write_out(path, blob):
    """Writes one file only where its folder, as it is now, lies outside every git work tree, and never through a link
    at its own name: not a symlink (O_NOFOLLOW), which a program could plant in the output folder to carry the write
    wherever it points, and not a file that has another name (a hard link), which may lie in a work tree, where O_TRUNC
    and the write would land as well (the fourth 0.14 review). The file is opened without O_TRUNC and emptied only once
    it passes, so a file refused is left as it was; anything but a plain file is refused, and O_NONBLOCK keeps a FIFO at
    the name from holding the open. A file name read from the client's own tables could also take the path out of the
    folder, which the first check catches."""
    if in_git_work_tree(path.parent):
        print(f'refusing {path}: it lands inside a git work tree', file=sys.stderr)
        sys.exit(2)
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o644)
    except OSError as e:
        print(f'refusing {path}: {"it is a symbolic link, which would carry the write wherever it points" if os.path.islink(path) else e}', file=sys.stderr)
        sys.exit(2)
    info = os.fstat(fd)
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        os.close(fd)
        print(f'refusing {path}: {"it is not a plain file" if not stat.S_ISREG(info.st_mode) else "the file has another name (a hard link), which the write would reach as well"}',
              file=sys.stderr)
        sys.exit(2)
    os.ftruncate(fd, 0)
    with os.fdopen(fd, 'wb') as f:
        f.write(blob)
if in_git_work_tree(args.out):
    print(f'refusing {args.out}: it lands inside a git work tree, and these files are copies of Blizzard sounds; name a scratch folder outside any repo', file=sys.stderr)
    sys.exit(2)
LIB = args.storm
DATA = pathlib.Path(args.data); OUT = pathlib.Path(args.out); OUT.mkdir(parents=True, exist_ok=True)
st = ctypes.CDLL(LIB)
st.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]; st.SFileOpenArchive.restype = ctypes.c_bool
st.SFileHasFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p]; st.SFileHasFile.restype = ctypes.c_bool
st.SFileOpenFileEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]; st.SFileOpenFileEx.restype = ctypes.c_bool
st.SFileGetFileSize.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]; st.SFileGetFileSize.restype = ctypes.c_uint
st.SFileReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(ctypes.c_uint), ctypes.c_void_p]; st.SFileReadFile.restype = ctypes.c_bool
st.SFileCloseFile.argtypes = [ctypes.c_void_p]; st.SFileCloseArchive.argtypes = [ctypes.c_void_p]
def load_order():
    names = [p.name for p in DATA.iterdir() if p.suffix.lower() == '.mpq']
    def key(n):
        l = n.lower()
        if l == 'base.mpq': return (0, '')
        if l in ('dbc.mpq', 'fonts.mpq', 'interface.mpq', 'misc.mpq', 'model.mpq', 'sound.mpq', 'speech.mpq', 'terrain.mpq', 'texture.mpq', 'wmo.mpq', 'backup.mpq'): return (1, l)
        if l == 'patch.mpq': return (2, '')
        m = re.match(r'patch-(\d+)\.mpq', l)
        if m: return (3, int(m.group(1)))
        m = re.match(r'patch-([a-z])\.mpq', l)
        if m: return (4, m.group(1))
        return (5, l)
    return sorted(names, key=key)
archives = []
for n in load_order():
    h = ctypes.c_void_p()
    if st.SFileOpenArchive(str(DATA / n).encode(), 0, 0x100, ctypes.byref(h)): archives.append((n, h))
print('archives opened:', len(archives))
def read(name):
    best = None
    for n, h in archives:  # later wins
        if not st.SFileHasFile(h, name.encode()): continue
        f = ctypes.c_void_p()
        if not st.SFileOpenFileEx(h, name.encode(), 0, ctypes.byref(f)): continue
        hi = ctypes.c_uint(); size = st.SFileGetFileSize(f, ctypes.byref(hi))
        if size and size != 0xFFFFFFFF:
            buf = ctypes.create_string_buffer(size); got = ctypes.c_uint()
            if st.SFileReadFile(f, buf, size, ctypes.byref(got), None) and got.value == size: best = (n, buf.raw)
        st.SFileCloseFile(f)
    return best
dbc = read('DBFilesClient\\SoundEntries.dbc')
if not dbc: sys.exit('SoundEntries.dbc not found')
src, d = dbc; magic, nrec, nfields, recsize, strsize = struct.unpack_from('<4sIIII', d, 0)
print(f'SoundEntries.dbc from {src}: {nrec} records, {nfields} fields, {recsize} B/record')
strings = d[20 + nrec * recsize:]
def s(off): e = strings.index(b'\0', off); return strings[off:e].decode('latin-1')
want = {8533: 'rain_light', 8534: 'rain_medium', 8535: 'rain_heavy', 8536: 'snow_light', 8537: 'snow_medium', 8538: 'snow_heavy', 8556: 'sand_light', 8557: 'sand_medium', 8558: 'sand_heavy'}
found = {}
for i in range(nrec):
    rec = struct.unpack_from(f'<{nfields}I', d, 20 + i * recsize)
    if rec[0] in want:
        name = s(rec[2]); files = [s(rec[3 + k]) for k in range(10) if rec[3 + k]]; dirbase = s(rec[23]) if nfields > 23 else ''
        found[rec[0]] = (name, files, dirbase); print(f'  {rec[0]} {want[rec[0]]:12} name={name!r} dir={dirbase!r} files={files}')
for sid, (name, files, dirbase) in found.items():
    for fn in files[:1]:
        path = (dirbase.rstrip('\\') + '\\' + fn) if dirbase else fn
        got = read(path)
        if not got: print(f'  !! {path} not in any archive'); continue
        an, blob = got; out = OUT / f'{want[sid]}_{fn}'; write_out(out, blob); print(f'  pulled {path} from {an}: {len(blob)} B -> {out.name}')
