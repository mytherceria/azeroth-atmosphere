"""Packs the two rain textures into one patch archive for the 1.12 client (MPQ format 1, zlib, with a listfile).

Usage: STORMLIB=/path/to/libstorm.so python3 make_patch_mpq.py <out.MPQ> <RainDrop01.blp> <RainDropSplash01.blp>
StormLib (https://github.com/ladislav-zezula/StormLib, MIT) does the writing; this script only calls it.
"""
import ctypes, os, sys
out, drop, splash = sys.argv[1:4]
lib = ctypes.CDLL(os.environ.get("STORMLIB", "libstorm.so"))
lib.SFileCreateArchive.restype = ctypes.c_bool
lib.SFileAddFileEx.restype = ctypes.c_bool
lib.SFileCloseArchive.restype = ctypes.c_bool
MPQ_CREATE_LISTFILE, MPQ_CREATE_ARCHIVE_V1 = 0x00100000, 0x00000000
MPQ_FILE_COMPRESS, MPQ_FILE_REPLACEEXISTING = 0x00000200, 0x80000000
ZLIB, NEXT_SAME = 0x02, 0xFFFFFFFF
if os.path.exists(out):
    sys.exit(f"{out} exists; not overwriting")
h = ctypes.c_void_p()
if not lib.SFileCreateArchive(out.encode(), MPQ_CREATE_LISTFILE | MPQ_CREATE_ARCHIVE_V1, 16, ctypes.byref(h)):
    sys.exit("could not create the archive")
for src, name in ((drop, b"Textures\\Weather\\RainDrop01.blp"), (splash, b"Textures\\Weather\\RainDropSplash01.blp")):
    if not lib.SFileAddFileEx(h, src.encode(), name, MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, ZLIB, NEXT_SAME):
        lib.SFileCloseArchive(h); os.remove(out); sys.exit(f"could not add {src}")
lib.SFileCloseArchive(h)
print(out, os.path.getsize(out), "bytes")
