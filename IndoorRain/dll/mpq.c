/* mpq.c: reads one file out of one of the game client's MPQ archives into memory, for IndoorRain.dll.
 *
 * IndoorRain rebuilds its sounds at run time from the player's own client files (the rain, snow and
 * sandstorm loops, the Blasted Lands lightning bolts, CallLightning and the shaman wind casts), so the
 * addon ships no Blizzard audio. Those files live in the MPQs of the Data folder next to WoW.exe. They are
 * read here with plain file reads through the caller's callbacks (mpq_io), never through the client's own
 * archive code, whose file layer keeps a cache with no lock and belongs to the game's thread (see
 * sound_bytes in indoorrain.c).
 *
 * It reads what those files need and refuses everything else rather than guess: MPQ format 0 and 1 below
 * 4 GB, files that are not encrypted, sectors stored as they are or compressed with zlib alone (compression
 * mask 0x02). Every source file measured in the RavenCraft client (30 Sep 2026) is exactly that. An archive
 * is a file on the player's disk that anything could have written, so every offset, count and size is
 * checked against the archive's real size before it is used, every walk is bounded, and each sector must
 * inflate to exactly its own length. Memory comes from the caller (io->alloc), and no function here keeps
 * more than a few hundred bytes on the stack: the DLL is built without stack probes, so a thread's stack
 * only grows one 4 KB guard page at a time.
 *
 * The algorithms (Storm's crypt table, name hash and table cipher, the hash table walk, the sector layout)
 * are the ones ~/.local/bin/mpq-extract implements. tools/mpq_selftest.c checks this reader against it byte
 * for byte on every source file and feeds it truncated and corrupted archives.
 */
#include "mpq.h"
#include "inflate.h"

#define MPQ_MAX_OUT      (64u * 1024u * 1024u)   /* the largest source file is 14 MB */
#define MPQ_SCAN_END     0x100000u               /* the header is looked for every 512 bytes up to 1 MB in, as the Python reader does */
#define MPQ_MAX_ENTRIES  (1u << 20)              /* per table: the source archives' largest has 65536 entries, 16 times fewer */
#define MPQ_MAX_SHIFT    15u                     /* sectors of 512 << 15 = 16 MB at most; the source archives use 3 (4 KB) */
#define MPQ_MAX_PATH     1023
#define MPQ_CHUNK        256                     /* table entries per read: 4 KB */

/* block table flags */
#define MPQ_FL_COMPRESS     0x00000200u   /* each sector starts with a compression mask byte, unless it is stored whole */
#define MPQ_FL_SINGLE_UNIT  0x01000000u   /* one sector the size of the file, and no sector table */
#define MPQ_FL_SECTOR_CRC   0x04000000u   /* the sector table has one more entry: the end of a checksum block */
#define MPQ_FL_EXISTS       0x80000000u
/* Every other flag is refused: 0x100 implode, 0x10000 encrypted, 0x20000 key adjusted, 0x100000 patch file,
   0x2000000 delete marker (with a non-zero size; at size zero it is a stub like any other, see mpq_read_file),
   and bits no format version defines. */
#define MPQ_FL_ACCEPTED     (MPQ_FL_COMPRESS | MPQ_FL_SINGLE_UNIT | MPQ_FL_SECTOR_CRC | MPQ_FL_EXISTS)
#define MPQ_COMP_ZLIB       0x02u
#define MPQ_HASH_FREE       0xFFFFFFFFu   /* block index of a slot never used: every chain ends at one */
#define MPQ_HASH_DELETED    0xFFFFFFFEu   /* a slot whose file was removed: chains run on through it */

typedef struct mpq_work {
    unsigned crypt[0x500];                /* Storm's crypt table, built per call (1280 steps) so nothing is shared */
    unsigned char chunk[MPQ_CHUNK * 16];
    infl_state inflate;
} mpq_work;

typedef struct mpq_arc {
    const mpq_io *io; void *fh; mpq_work *w;
    unsigned long long base;    /* file offset of the MPQ header; the archive's offsets count from here */
    unsigned long long avail;   /* bytes from the header to the end of the file: the archive's real size */
    unsigned shift;             /* sector size = 1 << shift */
    unsigned hash_off, hash_count, block_off, block_count;
} mpq_arc;

static unsigned mpq_le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned mpq_le32(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
static int mpq_read_exact(const mpq_arc *a, unsigned long long off, void *buf, unsigned len)
{
    return a->io->read_at(a->io->ctx, a->fh, off, buf, len) == len;
}

/* ---- Storm's crypt table, name hash and table cipher ---- */
/* 0x500 values from a fixed sequence (x = (x * 125 + 3) mod 0x2AAAAB, from 0x100001), two steps per value */
static void mpq_crypt_init(unsigned *t)
{
    unsigned seed = 0x00100001u, i, j;
    for (i = 0; i < 0x100; i++)
        for (j = 0; j < 5; j++) {
            unsigned hi;
            seed = (seed * 125u + 3u) % 0x2AAAABu; hi = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            t[i + j * 0x100] = hi | (seed & 0xFFFFu);
        }
}
/* Type 0 picks the hash table slot, 1 and 2 are the two names that identify the file there, 3 makes the
   table keys. Case-insensitive, and '/' hashes as '\\' (StormLib's hash does the same), so either separator
   finds a file. */
static unsigned mpq_hash(const unsigned *t, const char *s, unsigned type)
{
    unsigned s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A'; else if (ch == '/') ch = '\\';
        s1 = t[(type << 8) + ch] ^ (s1 + s2);
        s2 = ch + s1 + s2 + (s2 << 5) + 3u;
    }
    return s1;
}
/* The hash and block tables are encrypted with a cipher that runs through the whole table (each DWORD's key
   depends on the one before it, decrypted), so a table can only be read front to back. It is, a chunk at a
   time, into the work area: no table is ever held whole, whatever size its header claims. */
typedef struct mpq_table {
    unsigned long long pos;   /* file offset of the next entry not read yet */
    unsigned left;            /* entries not read yet */
    unsigned have, next;      /* entries in the chunk, and the next one to hand out */
    unsigned key, seed;
} mpq_table;
static void mpq_table_start(mpq_table *t, const mpq_arc *a, unsigned off, unsigned count, const char *name)
{
    t->pos = a->base + off; t->left = count; t->have = 0; t->next = 0;
    t->key = mpq_hash(a->w->crypt, name, 3); t->seed = 0xEEEEEEEEu;
}
/* The next entry, decrypted, as four DWORDs. The callers never ask past the table's count. */
static int mpq_table_next(const mpq_arc *a, mpq_table *t, unsigned e[4])
{
    const unsigned *ct = a->w->crypt;
    const unsigned char *p;
    unsigned i;
    if (t->next == t->have) {
        unsigned n = t->left < MPQ_CHUNK ? t->left : MPQ_CHUNK;
        if (n == 0) return MPQ_E_FORMAT;
        if (!mpq_read_exact(a, t->pos, a->w->chunk, n * 16u)) return MPQ_E_IO;
        t->pos += n * 16u; t->left -= n; t->have = n; t->next = 0;
    }
    p = a->w->chunk + t->next++ * 16u;
    for (i = 0; i < 4; i++) {
        unsigned v = mpq_le32(p + i * 4u);
        t->seed += ct[0x400 + (t->key & 0xFFu)];
        v ^= t->key + t->seed;
        t->key = ((~t->key << 21) + 0x11111111u) | (t->key >> 11);
        t->seed = v + t->seed + (t->seed << 5) + 3u;
        e[i] = v;
    }
    return MPQ_OK;
}

/* ---- the archive ---- */
static int mpq_open_header(mpq_arc *a)
{
    unsigned char h[44];
    unsigned long long size = a->io->size(a->io->ctx, a->fh), off;
    unsigned hsize, version;
    /* The header starts on a 512-byte boundary in the first megabyte. A user-data block (MPQ\x1B) in front of
       it is simply looked past, as the Python reader does. */
    for (off = 0; ; off += 512) {
        if (off > MPQ_SCAN_END || off + 32 > size) return MPQ_E_FORMAT;
        if (!mpq_read_exact(a, off, h, 4)) return MPQ_E_IO;
        if (h[0] == 'M' && h[1] == 'P' && h[2] == 'Q' && h[3] == 0x1A) break;
    }
    a->base = off; a->avail = size - off;
    if (!mpq_read_exact(a, off, h, 32)) return MPQ_E_IO;
    hsize = mpq_le32(h + 4); version = mpq_le16(h + 12);
    if (version > 1) return MPQ_E_UNSUPPORTED;   /* format 2 and up came with later expansions */
    if (hsize < (version ? 44u : 32u)) return MPQ_E_FORMAT;
    if (version == 1) {
        /* Format 1 adds the high words of the two table offsets and the offset of a table of each file's high
           offset word: all zero below 4 GB (RavenCraft's patch.MPQ is format 1 with all three zero). Anything
           else is an archive this reader has no business in. */
        if (a->avail < 44) return MPQ_E_FORMAT;
        if (!mpq_read_exact(a, off + 32, h + 32, 12)) return MPQ_E_IO;
        if (mpq_le32(h + 32) || mpq_le32(h + 36) || mpq_le16(h + 40) || mpq_le16(h + 42)) return MPQ_E_UNSUPPORTED;
    }
    if (mpq_le16(h + 14) > MPQ_MAX_SHIFT) return MPQ_E_UNSUPPORTED;
    a->shift = 9u + mpq_le16(h + 14);
    a->hash_off = mpq_le32(h + 16); a->block_off = mpq_le32(h + 20);
    a->hash_count = mpq_le32(h + 24); a->block_count = mpq_le32(h + 28);
    /* Every MPQ writer makes the hash table a power of two in size, so that the slot is the name's hash masked
       by size - 1, which is also what the Python reader's modulo comes to. At any other size the two differ,
       and which one the writer meant would be a guess. */
    if (a->hash_count == 0 || (a->hash_count & (a->hash_count - 1u)) != 0 || a->hash_count > MPQ_MAX_ENTRIES) return MPQ_E_FORMAT;
    if (a->block_count > MPQ_MAX_ENTRIES) return MPQ_E_FORMAT;
    if ((unsigned long long)a->hash_off + (unsigned long long)a->hash_count * 16u > a->avail) return MPQ_E_FORMAT;
    if ((unsigned long long)a->block_off + (unsigned long long)a->block_count * 16u > a->avail) return MPQ_E_FORMAT;
    return MPQ_OK;
}
/* The block index of path. Its chain starts at the name's slot and runs on (around the end of the table to
   slot 0) until the file or a slot never used; like the Python reader, the first entry whose two names
   match is taken, whatever its locale (every source file's entry is locale 0, and the only one). The table only
   decrypts front to back, so it is read once from slot 0, noting on the way past what the part of the chain
   that wraps around to slot 0 would find. At most hash_count entries, whatever the table holds. */
static int mpq_find_block(const mpq_arc *a, const char *path, unsigned *blk)
{
    const unsigned *ct = a->w->crypt;
    unsigned start = mpq_hash(ct, path, 0) & (a->hash_count - 1u), n1 = mpq_hash(ct, path, 1), n2 = mpq_hash(ct, path, 2);
    unsigned i, e[4], wrap_blk = 0;
    int wrap = 0;   /* in the slots before start: 0 nothing yet, 1 the file came first, 2 a free slot came first */
    mpq_table t;
    int r;
    mpq_table_start(&t, a, a->hash_off, a->hash_count, "(hash table)");
    for (i = 0; i < a->hash_count; i++) {
        if ((r = mpq_table_next(a, &t, e)) != MPQ_OK) return r;
        if (i < start) {
            if (wrap == 0 && e[3] == MPQ_HASH_FREE) wrap = 2;
            else if (wrap == 0 && e[0] == n1 && e[1] == n2 && e[3] != MPQ_HASH_DELETED) { wrap = 1; wrap_blk = e[3]; }
            continue;
        }
        if (e[3] == MPQ_HASH_FREE) return MPQ_E_NOT_FOUND;
        if (e[0] == n1 && e[1] == n2 && e[3] != MPQ_HASH_DELETED) { *blk = e[3]; return MPQ_OK; }
    }
    if (wrap == 1) { *blk = wrap_blk; return MPQ_OK; }
    return MPQ_E_NOT_FOUND;   /* the chain ended in the slots before start, or the table is full and has no such file */
}
static int mpq_block_entry(const mpq_arc *a, unsigned blk, unsigned e[4])
{
    mpq_table t;
    unsigned i;
    int r;
    if (blk >= a->block_count) return MPQ_E_FORMAT;   /* the hash table points past the block table */
    mpq_table_start(&t, a, a->block_off, a->block_count, "(block table)");
    for (i = 0; i <= blk; i++) if ((r = mpq_table_next(a, &t, e)) != MPQ_OK) return r;
    return MPQ_OK;
}

/* ---- the file ---- */
/* One compressed sector: a mask byte naming the compression, then its data. Only zlib alone is taken; PKWARE,
   bzip2, LZMA, sparse, ADPCM or any mix is refused, never guessed at. */
static int mpq_inflate_sector(const mpq_arc *a, const unsigned char *in, unsigned len, unsigned char *out, unsigned want)
{
    if (in[0] != MPQ_COMP_ZLIB) return MPQ_E_UNSUPPORTED;
    return infl_zlib(&a->w->inflate, in + 1, len - 1u, out, want) == INFL_OK ? MPQ_OK : MPQ_E_DATA;
}
static int mpq_read_raw(const mpq_arc *a, unsigned long long start, unsigned size, unsigned char **out)
{
    unsigned char *buf = a->io->alloc(a->io->ctx, size);
    if (!buf) return MPQ_E_NOMEM;
    if (!mpq_read_exact(a, start, buf, size)) { a->io->release(a->io->ctx, buf); return MPQ_E_IO; }
    *out = buf;
    return MPQ_OK;
}
/* A single-unit file: one sector the size of the file, stored as it is or compressed, with no sector table. */
static int mpq_read_single(const mpq_arc *a, unsigned long long start, unsigned csize, unsigned fsize, unsigned flags, unsigned char **out)
{
    const mpq_io *io = a->io;
    unsigned char *in, *buf;
    int r;
    if (flags & MPQ_FL_SECTOR_CRC) return MPQ_E_UNSUPPORTED;        /* no sector table to say where a checksum block would end */
    if (csize == fsize) return mpq_read_raw(a, start, fsize, out);   /* compressing it did not help */
    if (!(flags & MPQ_FL_COMPRESS) || csize > fsize || csize < 2) return MPQ_E_CORRUPT;
    in = io->alloc(io->ctx, csize);
    if (!in) return MPQ_E_NOMEM;
    buf = io->alloc(io->ctx, fsize);
    if (!buf) { io->release(io->ctx, in); return MPQ_E_NOMEM; }
    r = mpq_read_exact(a, start, in, csize) ? mpq_inflate_sector(a, in, csize, buf, fsize) : MPQ_E_IO;
    io->release(io->ctx, in);
    if (r != MPQ_OK) { io->release(io->ctx, buf); return r; }
    *out = buf;
    return MPQ_OK;
}
/* A file in sectors of 1 << shift bytes (the last one shorter), each compressed or stored as it is, found
   through the sector table at the block's start: the offset of each sector and the end of the last (then the
   end of the checksum block, with MPQ_FL_SECTOR_CRC), all counted from the block's start. */
static int mpq_read_sectors(const mpq_arc *a, unsigned long long start, unsigned csize, unsigned fsize, unsigned flags, unsigned char **out)
{
    const mpq_io *io = a->io;
    unsigned sector = 1u << a->shift, nsec = (fsize + sector - 1u) >> a->shift;
    unsigned tsize = (nsec + 1u + (flags & MPQ_FL_SECTOR_CRC ? 1u : 0u)) * 4u, s, lo, hi, want;
    unsigned char *tbl, *in = 0, *buf = 0;
    int r = MPQ_OK;
    if (tsize > csize) return MPQ_E_CORRUPT;
    tbl = io->alloc(io->ctx, tsize);
    if (!tbl) return MPQ_E_NOMEM;
    /* The whole table is checked before any data is read: the first sector starts right after the table,
       each one ends after it starts and inside the block, and none is longer than its share of the file.
       The checksum block is only bounded, between the last sector and the block's end: nothing here reads
       it (the client's archives hold it empty, or as zeros in a compression this reader does not take). */
    if (!mpq_read_exact(a, start, tbl, tsize)) r = MPQ_E_IO;
    else if (mpq_le32(tbl) != tsize) r = MPQ_E_CORRUPT;
    for (s = 0; r == MPQ_OK && s < nsec; s++) {
        lo = mpq_le32(tbl + s * 4u); hi = mpq_le32(tbl + s * 4u + 4u);
        want = s + 1u < nsec ? sector : fsize - s * sector;
        if (hi <= lo || hi > csize || hi - lo > want) r = MPQ_E_CORRUPT;
    }
    if (r == MPQ_OK && (flags & MPQ_FL_SECTOR_CRC)) {
        lo = mpq_le32(tbl + nsec * 4u); hi = mpq_le32(tbl + nsec * 4u + 4u);
        if (hi < lo || hi > csize) r = MPQ_E_CORRUPT;
    }
    if (r == MPQ_OK) {   /* the output only once the table holds up; a compressed sector is shorter than its share */
        if (!(in = io->alloc(io->ctx, sector < fsize ? sector : fsize))) r = MPQ_E_NOMEM;
        else if (!(buf = io->alloc(io->ctx, fsize))) r = MPQ_E_NOMEM;
    }
    for (s = 0; r == MPQ_OK && s < nsec; s++) {
        unsigned char *to = buf + s * sector;
        lo = mpq_le32(tbl + s * 4u); hi = mpq_le32(tbl + s * 4u + 4u);
        want = s + 1u < nsec ? sector : fsize - s * sector;
        if (hi - lo == want) r = mpq_read_exact(a, start + lo, to, want) ? MPQ_OK : MPQ_E_IO;   /* stored: compressing it did not help */
        else r = mpq_read_exact(a, start + lo, in, hi - lo) ? mpq_inflate_sector(a, in, hi - lo, to, want) : MPQ_E_IO;
    }
    if (in) io->release(io->ctx, in);
    io->release(io->ctx, tbl);
    if (r != MPQ_OK) { if (buf) io->release(io->ctx, buf); return r; }
    *out = buf;
    return MPQ_OK;
}
/* The file of block entry e (offset, stored size, file size, flags). */
static int mpq_read_file(const mpq_arc *a, const unsigned e[4], unsigned char **out)
{
    unsigned csize = e[1], fsize = e[2], flags = e[3];
    unsigned long long start = a->base + e[0];
    if (!(flags & MPQ_FL_EXISTS)) return MPQ_E_NOT_FOUND;
    /* Size zero first, whatever else the flags say, since there is no data to misread: RavenCraft's patch.MPQ
       holds 5708 such entries flagged as delete markers (0x82000000), and patch-3.mpq three plain stubs. */
    if (fsize == 0) return MPQ_E_EMPTY;
    if (flags & ~MPQ_FL_ACCEPTED) return MPQ_E_UNSUPPORTED;
    if (fsize > MPQ_MAX_OUT) return MPQ_E_TOO_BIG;
    if ((unsigned long long)e[0] + csize > a->avail) return MPQ_E_CORRUPT;   /* the block runs past the end of the archive */
    if (flags & MPQ_FL_SINGLE_UNIT) return mpq_read_single(a, start, csize, fsize, flags, out);
    if (!(flags & MPQ_FL_COMPRESS)) {
        /* not compressed: the sectors are the file, back to back, with no table, so a checksum block would
           have no entry to say where it ends */
        if (flags & MPQ_FL_SECTOR_CRC) return MPQ_E_UNSUPPORTED;
        if (csize != fsize) return MPQ_E_CORRUPT;
        return mpq_read_raw(a, start, fsize, out);
    }
    return mpq_read_sectors(a, start, csize, fsize, flags, out);
}

int mpq_read(const mpq_io *io, const char *archive, const char *path, unsigned char **out, unsigned *out_len)
{
    mpq_arc a;
    unsigned e[4], blk = 0, n;
    int r;
    if (out) *out = 0;
    if (out_len) *out_len = 0;
    if (!io || !io->alloc || !io->release || !io->open || !io->read_at || !io->size || !io->close || !archive || !path || !out || !out_len) return MPQ_E_ARG;
    for (n = 0; path[n]; n++) if (n >= MPQ_MAX_PATH) return MPQ_E_ARG;
    if (n == 0) return MPQ_E_ARG;
    a.io = io;
    if (!(a.fh = io->open(io->ctx, archive))) return MPQ_E_NO_ARCHIVE;
    if (!(a.w = io->alloc(io->ctx, sizeof(mpq_work)))) { io->close(io->ctx, a.fh); return MPQ_E_NOMEM; }
    mpq_crypt_init(a.w->crypt);
    r = mpq_open_header(&a);
    if (r == MPQ_OK) r = mpq_find_block(&a, path, &blk);
    if (r == MPQ_OK) r = mpq_block_entry(&a, blk, e);
    if (r == MPQ_OK) r = mpq_read_file(&a, e, out);
    if (r == MPQ_OK) *out_len = e[2];
    io->release(io->ctx, a.w);
    io->close(io->ctx, a.fh);
    return r;
}

int mpq_read_first(const mpq_io *io, const char *const *archives, int n, const char *path, unsigned char **out, unsigned *out_len, int *which)
{
    int i, r, none = MPQ_E_NO_ARCHIVE;
    if (which) *which = -1;
    if (out) *out = 0;
    if (out_len) *out_len = 0;
    if (!io || !archives || n < 0 || !path || !out || !out_len) return MPQ_E_ARG;
    for (i = 0; i < n; i++) {
        r = mpq_read(io, archives[i], path, out, out_len);
        if (r == MPQ_OK) { if (which) *which = i; return MPQ_OK; }
        if (r == MPQ_E_NO_ARCHIVE) continue;
        if (r == MPQ_E_NOT_FOUND) { if (none == MPQ_E_NO_ARCHIVE) none = r; continue; }
        if (r == MPQ_E_EMPTY) { none = r; continue; }
        if (which) *which = i;   /* the first archive that holds the file could not give it: no older copy instead */
        return r;
    }
    return none;
}

const char *mpq_error_text(int code)
{
    switch (code) {
    case MPQ_OK:            return "ok";
    case MPQ_E_NOT_FOUND:   return "no such file in the archive";
    case MPQ_E_NO_ARCHIVE:  return "no such archive";
    case MPQ_E_EMPTY:       return "the file is stored with size zero";
    case MPQ_E_ARG:         return "bad argument";
    case MPQ_E_IO:          return "read error";
    case MPQ_E_FORMAT:      return "malformed archive header or tables";
    case MPQ_E_CORRUPT:     return "malformed block entry or sector table";
    case MPQ_E_DATA:        return "compressed data that does not inflate to its size";
    case MPQ_E_UNSUPPORTED: return "stored in a way this reader refuses";
    case MPQ_E_TOO_BIG:     return "file over 64 MB";
    case MPQ_E_NOMEM:       return "out of memory";
    }
    return "unknown error";
}
