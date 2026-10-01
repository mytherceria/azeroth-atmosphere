/* mpq_selftest.c: native Linux test of dll/mpq.c and dll/inflate.c, the MPQ reader and zlib inflater that
 * IndoorRain.dll uses to rebuild its sounds from the player's own client archives.
 *
 *   gcc -std=c99 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
 *       -fno-omit-frame-pointer -I../dll -o /tmp/mpq_selftest mpq_selftest.c ../dll/mpq.c ../dll/inflate.c -lz
 *   /tmp/mpq_selftest <client Data folder> <reference folder> <scratch folder>
 * (built outside the repo, and run with folders outside it: the references are the client's own files)
 *
 * 1. Every source file the sound recipes use, read with mpq_read from the archive the recipes name, must
 *    equal byte for byte ~/.local/bin/mpq-extract's output for the same archive and path, which must already
 *    be in <reference folder> as <archive>__<file name>, made with
 *        mpq-extract <Data>/<archive> '<path>' <reference folder>/<archive>__<file name>
 *    for the twenty in REAL below, and for wmo.MPQ's World\wmo\Lorderon\Undercity\Undercity_206.wmo and
 *    patch-3.mpq's Creature\OgreMage\OgreMage.m2 (part 2). The Data folder is only ever opened for reading.
 * 2. mpq_read_first over lists: the first archive that holds the file with a non-zero size wins; a missing
 *    archive, an absent file, a zero-length stub and a delete marker are skipped (real ones: RavenCraft's
 *    patch-3.mpq has three stubs, its patch.MPQ 5708 delete markers).
 * 3. The inflater against zlib's own deflate (stored, fixed and dynamic blocks; levels 0, 1, 6 and 9 with
 *    every strategy; small windows; every kind of flush), truncations, bit flips, and hand-built streams for
 *    each error it catches.
 * 4. Archives built here, good and malformed, written to <scratch folder>/synthetic: truncated, tables past
 *    the end, sector tables that go backwards, sectors claiming huge sizes, bad deflate data, refused flags
 *    and compressions. Then random corruption of good ones, and a failing read or allocation at every step
 *    of a good read: each must end in its error code, with every allocation released and every archive
 *    closed.
 * Run it under AddressSanitizer and UBSan (the build line above), so that any read or write out of bounds
 * stops it. Nothing here writes outside <scratch folder>, which may not lie inside a git work tree. zlib (-lz) only
 * makes test data; the DLL has none.
 * Prints FAIL lines, one EXACT line per real source file and a summary; exits 1 on any failure.
 */
#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <zlib.h>
#include "mpq.h"
#include "inflate.h"

static long g_checks, g_fails;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { if (++g_fails <= 60) { printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6; }
static unsigned xs(unsigned *s) { unsigned x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void put32(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static unsigned get32(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

/* ---- files out: where a file lands, and git work trees ----
 * What these tools write is made from the client's own sounds, and in a git work tree a commit could pick it up
 * (README, License), so nothing is written inside one, as the scripts in tools/ refuse theirs. A path is judged where
 * it lands (land_path): from the left, each part that exists as the kernel takes it (realpath: its symlinks, and a
 * '..' after them), each that does not as the plain folder it will be made, which a '..' steps back out of (the third
 * 0.14 review: a/new/../../repo, judged by a, the nearest part that existed, landed in repo once new was made). It lies
 * in a work tree when a .git (a folder, or the file a worktree has) is at or above that, looked for on disk, not asked
 * of git, whose failure (a moved main checkout, a repo it calls dubious) must not read as outside. Every file is opened
 * through create_out, which judges its folder again as it now is and refuses a link at the file's own name, a symlink
 * or a second name of a file (a hard link), so neither a link planted in the output folder nor a symlink in the way of
 * a subfolder carries a write into a work tree. In doubt (a path too long, a part that cannot be looked at) the
 * answer is "inside". land_path, in_git_work_tree and create_out are the same, word for word, in the three C tools in
 * tools/. */
static int land_path(const char *path, char *out, size_t cap)
{
    char buf[4096], real[4096], *part, *save = NULL; struct stat st; size_t n;
    if (!path[0] || snprintf(buf, sizeof buf, "%s", path) >= (int)sizeof buf) return 0;
    if (!realpath(buf[0] == '/' ? "/" : ".", real) || strlen(real) >= cap) return 0;
    strcpy(out, real);
    for (part = strtok_r(buf, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, "..")) { char *s = strrchr(out, '/'); if (s == out) out[1] = 0; else *s = 0; continue; }
        n = strlen(out);
        if (n + 1 + strlen(part) >= cap) return 0;
        if (n > 1) out[n++] = '/';
        strcpy(out + n, part);
        if (lstat(out, &st) == 0) { if (!realpath(out, real) || strlen(real) >= cap) return 0; strcpy(out, real); }
        else if (errno != ENOENT) return 0;
    }
    return 1;
}
static int in_git_work_tree(const char *path)
{
    char dir[4096], probe[4200]; struct stat st; char *slash;
    if (!land_path(path, dir, sizeof dir)) return 1;
    for (;;) {
        if (snprintf(probe, sizeof probe, "%s/.git", strcmp(dir, "/") ? dir : "") >= (int)sizeof probe) return 1;
        if (lstat(probe, &st) == 0 || (errno != ENOENT && errno != ENOTDIR)) return 1;
        if (!strcmp(dir, "/")) return 0;
        slash = strrchr(dir, '/');
        if (slash == dir) dir[1] = 0; else *slash = 0;
    }
}
/* A file to write, opened only in a folder that, as it now is, lies outside every git work tree, and never through a
   link at its own name: not a symlink (O_NOFOLLOW), and not a file that has another name (a hard link, st_nlink over
   1), which may lie in a work tree, where O_TRUNC and the write would land as well (the fourth 0.14 review). It is
   opened without O_TRUNC and emptied only once it passes, so a file refused is left as it was; anything but a plain
   file is refused, and O_NONBLOCK keeps a FIFO at the name from holding the open. NULL, and why on stderr, when
   refused or when it cannot be opened. */
static FILE *create_out(const char *path)
{
    char dir[4096]; const char *slash = strrchr(path, '/'); int fd; FILE *f; struct stat st;
    if (!slash) strcpy(dir, ".");
    else if (slash == path) strcpy(dir, "/");
    else if (snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path) >= (int)sizeof dir) return NULL;
    if (in_git_work_tree(dir)) { fprintf(stderr, "refusing to write %s: it lands inside a git work tree\n", path); return NULL; }
    if ((fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0644)) < 0) {
        if (errno == ELOOP) fprintf(stderr, "refusing to write %s: it is a symbolic link, which would carry the write wherever it points\n", path);
        return NULL;
    }
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    if (!S_ISREG(st.st_mode) || st.st_nlink != 1) {
        fprintf(stderr, "refusing to write %s: %s\n", path, !S_ISREG(st.st_mode) ? "it is not a plain file"
                : "the file has another name (a hard link), which the write would reach as well");
        close(fd);
        return NULL;
    }
    if (ftruncate(fd, 0) != 0 || !(f = fdopen(fd, "wb"))) { close(fd); return NULL; }
    return f;
}
static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    off_t sz;
    *n = 0;
    if (!f) return NULL;
    fseeko(f, 0, SEEK_END); sz = ftello(f); fseeko(f, 0, SEEK_SET);
    p = malloc(sz > 0 ? (size_t)sz : 1);
    if (p && sz > 0 && fread(p, 1, (size_t)sz, f) != (size_t)sz) { free(p); p = NULL; }
    fclose(f);
    if (p) *n = (size_t)sz;
    return p;
}
static int spit(const char *dir, const char *name, const void *p, size_t n)
{
    char path[4096];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!(f = create_out(path))) return 0;
    if (n && fwrite(p, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}
static const char *base_name(const char *p) { const char *b = p; for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }

/* ---- the io callbacks: stdio on one folder (read only), or one archive in memory; with counters, and a
   failing read or allocation on request ---- */
typedef struct { FILE *f; const unsigned char *mem; unsigned long long size; } t_handle;
typedef struct {
    const char *dir;
    const unsigned char *mem; unsigned long long mem_size; const char *mem_name;
    long live, handles;                 /* allocations and archives not given back yet */
    long allocs, reads;                 /* since the struct was cleared */
    long fail_alloc_at, fail_read_at;   /* 1-based: that allocation returns NULL, that read comes back short; 0 = never */
} t_io;
static void *t_alloc(void *ctx, unsigned size)
{
    t_io *t = ctx;
    void *p;
    t->allocs++;
    if (t->fail_alloc_at && t->allocs == t->fail_alloc_at) return NULL;
    if ((p = malloc(size ? size : 1))) t->live++;
    return p;
}
static void t_release(void *ctx, void *p) { t_io *t = ctx; if (p) { t->live--; free(p); } }
static void *t_open(void *ctx, const char *archive)
{
    t_io *t = ctx;
    t_handle *h;
    DIR *d;
    struct dirent *de;
    FILE *f = NULL;
    if (!archive || !*archive || strchr(archive, '/') || strstr(archive, "..")) return NULL;
    if (t->mem) {
        if (strcasecmp(archive, t->mem_name)) return NULL;
        h = calloc(1, sizeof *h); h->mem = t->mem; h->size = t->mem_size; t->handles++;
        return h;
    }
    if (!(d = opendir(t->dir))) return NULL;
    while ((de = readdir(d)))   /* the name as the caller gives it, matched without regard to case */
        if (!strcasecmp(de->d_name, archive)) { char p[4096]; snprintf(p, sizeof p, "%s/%s", t->dir, de->d_name); f = fopen(p, "rb"); break; }
    closedir(d);
    if (!f) return NULL;
    h = calloc(1, sizeof *h); h->f = f;
    fseeko(f, 0, SEEK_END); h->size = (unsigned long long)ftello(f);
    t->handles++;
    return h;
}
static unsigned t_read_at(void *ctx, void *fh, unsigned long long off, void *buf, unsigned len)
{
    t_io *t = ctx;
    t_handle *h = fh;
    unsigned got = 0;
    t->reads++;
    if (off < h->size) {
        unsigned n = h->size - off < len ? (unsigned)(h->size - off) : len;
        if (h->mem) { memcpy(buf, h->mem + off, n); got = n; }
        else if (!fseeko(h->f, (off_t)off, SEEK_SET)) got = (unsigned)fread(buf, 1, n, h->f);
    }
    if (t->fail_read_at && t->reads == t->fail_read_at && got) got--;   /* one byte short */
    return got;
}
static unsigned long long t_size(void *ctx, void *fh) { (void)ctx; return ((t_handle *)fh)->size; }
static void t_close(void *ctx, void *fh) { t_io *t = ctx; t_handle *h = fh; if (h->f) fclose(h->f); free(h); t->handles--; }
static mpq_io make_io(t_io *t) { mpq_io io = { t_alloc, t_release, t_open, t_read_at, t_size, t_close, t }; return io; }
static void io_on_dir(t_io *t, const char *dir) { memset(t, 0, sizeof *t); t->dir = dir; }
static void io_on_mem(t_io *t, const unsigned char *p, size_t n, const char *name) { memset(t, 0, sizeof *t); t->mem = p; t->mem_size = n; t->mem_name = name; }

/* ======== 1 and 2: the client's own archives ======== */
static const struct { const char *archive, *path; unsigned size; } REAL[] = {
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\RainLightLoop.wav",   1764044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\RainMediumLoop.wav",  2646044 },
    { "patch-S.mpq", "Sound\\Ambience\\Weather\\RainMediumLoop.wav",  5455916 },
    { "patch-S.mpq", "Sound\\Ambience\\Weather\\RainHeavyLoop.wav",   14419190 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\RainHeavyLoop.wav",   2205044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SnowLight.wav",       2641692 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SnowMedium.wav",      4410044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SnowHeavy.wav",       3528044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SandStormLight.wav",  5292044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SandStormMedium.wav", 5292044 },
    { "patch.MPQ",   "Sound\\Ambience\\Weather\\SandStormHeavy.wav",  5292044 },
    { "sound.MPQ",   "Sound\\Doodad\\BlastedLandsLightningbolt01Stand-Bolt.wav",  132344 },
    { "sound.MPQ",   "Sound\\Doodad\\BlastedLandsLightningbolt01Stand-Bolt1.wav", 186024 },
    { "sound.MPQ",   "Sound\\Doodad\\BlastedLandsLightningbolt01Stand-Bolt2.wav", 255062 },
    { "sound.MPQ",   "Sound\\Doodad\\BlastedLandsLightningbolt01Stand-Bolt3.wav", 175748 },
    { "patch-S.mpq", "Sound\\Spells\\CallLightning.wav", 154252 },
    { "sound.MPQ",   "Sound\\Spells\\CallLightning.wav", 132344 },
    { "patch-S.mpq", "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast01.ogg", 41334 },
    { "patch-S.mpq", "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast02.ogg", 39364 },
    { "patch-S.mpq", "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast03.ogg", 40448 },
};
static const char *g_ref;   /* the reference folder */

/* buf equals the reference file <ref>/<archive>__<file name> */
static int same_as_ref(const char *archive, const char *path, const unsigned char *buf, unsigned len)
{
    char rp[4096];
    size_t rn;
    unsigned char *ref;
    int ok;
    snprintf(rp, sizeof rp, "%s/%s__%s", g_ref, archive, base_name(path));
    ref = slurp(rp, &rn);
    ok = ref && rn == len && memcmp(ref, buf, len) == 0;
    free(ref);
    return ok;
}
static void expect_read(const char *dir, const char *archive, const char *path, int want)
{
    t_io t; mpq_io io; unsigned char *buf; unsigned len; int r;
    io_on_dir(&t, dir); io = make_io(&t);
    r = mpq_read(&io, archive, path, &buf, &len);
    CHECK(r == want, "%s %.60s: %s, want %s", archive, path, mpq_error_text(r), mpq_error_text(want));
    if (r == MPQ_OK) io.release(io.ctx, buf); else CHECK(buf == NULL && len == 0, "%s %.60s: output not cleared", archive, path);
    CHECK(t.live == 0 && t.handles == 0, "%s %.60s: %ld allocations and %ld archives left", archive, path, t.live, t.handles);
}
/* mpq_read_first over list; when it succeeds, the bytes must equal ref_archive's reference copy */
static void expect_first(const char *dir, const char *const *list, int n, const char *path, int want, int want_which, const char *ref_archive, unsigned want_len)
{
    t_io t; mpq_io io; unsigned char *buf; unsigned len; int which, r;
    io_on_dir(&t, dir); io = make_io(&t);
    r = mpq_read_first(&io, list, n, path, &buf, &len, &which);
    CHECK(r == want && which == want_which, "read_first %s: %s, which %d; want %s, which %d", path, mpq_error_text(r), which, mpq_error_text(want), want_which);
    if (r == MPQ_OK) {
        CHECK(len == want_len && ref_archive && same_as_ref(ref_archive, path, buf, len), "read_first %s: not %s's %u bytes", path, ref_archive, want_len);
        io.release(io.ctx, buf);
    } else CHECK(buf == NULL && len == 0, "read_first %s: output not cleared", path);
    CHECK(t.live == 0 && t.handles == 0, "read_first %s: %ld allocations and %ld archives left", path, t.live, t.handles);
}

static void test_real(const char *data)
{
    t_io t; mpq_io io; unsigned i; double total = 0; char longpath[1100];
    printf("== 1. the source files, against mpq-extract\n");
    for (i = 0; i < sizeof REAL / sizeof REAL[0]; i++) {
        unsigned char *buf; unsigned len; int r, ok; double t0, dt;
        io_on_dir(&t, data); io = make_io(&t);
        t0 = now_ms(); r = mpq_read(&io, REAL[i].archive, REAL[i].path, &buf, &len); dt = now_ms() - t0; total += dt;
        ok = r == MPQ_OK && len == REAL[i].size && same_as_ref(REAL[i].archive, REAL[i].path, buf, len);
        CHECK(ok, "%s %s: %s, %u bytes", REAL[i].archive, REAL[i].path, mpq_error_text(r), len);
        printf("EXACT %s %s %s %u bytes, %ld reads, %.1f ms\n", ok ? "ok" : "MISMATCH", REAL[i].archive, REAL[i].path, len, t.reads, dt);
        if (r == MPQ_OK) io.release(io.ctx, buf);
        CHECK(t.live == 0 && t.handles == 0, "%s: %ld allocations and %ld archives left", REAL[i].path, t.live, t.handles);
    }
    printf("   %u files in %.0f ms\n", i, total);

    /* the archive name through the caller's case-insensitive open, the path in another case and with '/' */
    {
        unsigned char *buf; unsigned len; int r;
        io_on_dir(&t, data); io = make_io(&t);
        r = mpq_read(&io, "PATCH-s.MPQ", "sound/spells/calllightning.WAV", &buf, &len);
        CHECK(r == MPQ_OK && same_as_ref("patch-S.mpq", "Sound\\Spells\\CallLightning.wav", buf, len), "case and slash folding: %s", mpq_error_text(r));
        if (r == MPQ_OK) io.release(io.ctx, buf);
    }
    expect_read(data, "patch.MPQ", "Sound\\Spells\\NoSuchSpell.wav", MPQ_E_NOT_FOUND);
    expect_read(data, "no-such.MPQ", "Sound\\Spells\\CallLightning.wav", MPQ_E_NO_ARCHIVE);
    /* real stubs: a size-zero file in patch-3.mpq, delete markers (0x82000000) in patch.MPQ */
    expect_read(data, "patch-3.mpq", "World\\wmo\\Lorderon\\Undercity\\Undercity_206.wmo", MPQ_E_EMPTY);
    expect_read(data, "patch.MPQ", "World\\wmo\\Lorderon\\Undercity\\Undercity_206.wmo", MPQ_E_EMPTY);
    expect_read(data, "patch.MPQ", "Creature\\OgreMage\\OgreMage.m2", MPQ_E_EMPTY);
    /* a path of 1023 characters is looked up (and not found); 1024 is refused, and so is an empty one */
    memset(longpath, 'a', sizeof longpath); longpath[1023] = 0;
    expect_read(data, "patch-S.mpq", longpath, MPQ_E_NOT_FOUND);
    longpath[1023] = 'a'; longpath[1024] = 0;
    expect_read(data, "patch-S.mpq", longpath, MPQ_E_ARG);
    expect_read(data, "patch-S.mpq", "", MPQ_E_ARG);

    printf("== 2. mpq_read_first\n");
    {
        static const char *const s_then_p[] = { "patch-S.mpq", "patch.MPQ" }, *const p_then_s[] = { "patch.MPQ", "patch-S.mpq" };
        static const char *const gap[] = { "no-such.MPQ", "patch.MPQ", "patch-S.mpq" }, *const none[] = { "no-such.MPQ", "also-missing.mpq" };
        static const char *const stubs[] = { "patch-3.mpq", "patch.MPQ", "wmo.MPQ" }, *const marker[] = { "patch.MPQ", "patch-3.mpq", "model.MPQ" };
        static const char *const only_stubs[] = { "patch-3.mpq", "patch.MPQ" }, *const with_null[] = { "patch.MPQ", NULL };
        static const char *const p_then_sound[] = { "patch.MPQ", "sound.MPQ" }, *const s_then_sound[] = { "patch-S.mpq", "sound.MPQ" };
        const char *heavy = "Sound\\Ambience\\Weather\\RainHeavyLoop.wav", *wmo = "World\\wmo\\Lorderon\\Undercity\\Undercity_206.wmo";
        expect_first(data, s_then_p, 2, heavy, MPQ_OK, 0, "patch-S.mpq", 14419190);
        expect_first(data, p_then_s, 2, heavy, MPQ_OK, 0, "patch.MPQ", 2205044);
        expect_first(data, gap, 3, "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast02.ogg", MPQ_OK, 2, "patch-S.mpq", 39364);
        expect_first(data, s_then_sound, 2, "Sound\\Spells\\CallLightning.wav", MPQ_OK, 0, "patch-S.mpq", 154252);
        expect_first(data, p_then_sound, 2, "Sound\\Spells\\CallLightning.wav", MPQ_OK, 1, "sound.MPQ", 132344);
        expect_first(data, p_then_sound, 2, "Sound\\Spells\\NoSuchSpell.wav", MPQ_E_NOT_FOUND, -1, NULL, 0);
        expect_first(data, none, 2, heavy, MPQ_E_NO_ARCHIVE, -1, NULL, 0);
        expect_first(data, none, 0, heavy, MPQ_E_NO_ARCHIVE, -1, NULL, 0);
        expect_first(data, NULL, 2, heavy, MPQ_E_ARG, -1, NULL, 0);
        expect_first(data, s_then_p, -1, heavy, MPQ_E_ARG, -1, NULL, 0);
        expect_first(data, with_null, 2, "Sound\\Spells\\NoSuchSpell.wav", MPQ_E_ARG, 1, NULL, 0);
        /* the stub in patch-3.mpq and the delete marker in patch.MPQ are skipped; wmo.MPQ's copy is read */
        expect_first(data, stubs, 3, wmo, MPQ_OK, 2, "wmo.MPQ", 14810);
        expect_first(data, marker, 3, "Creature\\OgreMage\\OgreMage.m2", MPQ_OK, 1, "patch-3.mpq", 388416);
        expect_first(data, only_stubs, 2, wmo, MPQ_E_EMPTY, -1, NULL, 0);
    }
    /* arguments */
    {
        unsigned char *buf = (unsigned char *)&t; unsigned len = 5; mpq_io bad;
        io_on_dir(&t, data); io = make_io(&t);
        CHECK(mpq_read(NULL, "patch.MPQ", "x", &buf, &len) == MPQ_E_ARG && buf == NULL && len == 0, "NULL io");
        CHECK(mpq_read(&io, NULL, "x", &buf, &len) == MPQ_E_ARG, "NULL archive");
        CHECK(mpq_read(&io, "patch.MPQ", NULL, &buf, &len) == MPQ_E_ARG, "NULL path");
        CHECK(mpq_read(&io, "patch.MPQ", "x", NULL, &len) == MPQ_E_ARG, "NULL out");
        CHECK(mpq_read(&io, "patch.MPQ", "x", &buf, NULL) == MPQ_E_ARG, "NULL out_len");
        bad = io; bad.read_at = NULL;
        CHECK(mpq_read(&bad, "patch.MPQ", "x", &buf, &len) == MPQ_E_ARG, "NULL callback");
        CHECK(t.live == 0 && t.handles == 0, "arguments: %ld allocations and %ld archives left", t.live, t.handles);
    }
    /* a read failing at each step of a real read comes back as MPQ_E_IO, with nothing left over */
    {
        const char *p = "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast03.ogg";
        unsigned char *buf; unsigned len; long n, k; int r, bad = 0;
        io_on_dir(&t, data); io = make_io(&t);
        r = mpq_read(&io, "patch-S.mpq", p, &buf, &len);
        n = t.reads;
        if (r == MPQ_OK) io.release(io.ctx, buf);
        for (k = 1; k <= n; k++) {
            io_on_dir(&t, data); t.fail_read_at = k; io = make_io(&t);
            r = mpq_read(&io, "patch-S.mpq", p, &buf, &len);
            if (r != MPQ_E_IO || t.live || t.handles) bad++;
            if (r == MPQ_OK) io.release(io.ctx, buf);
        }
        CHECK(n > 0 && bad == 0, "failing reads on a real archive: %d of %ld did not end cleanly in MPQ_E_IO", bad, n);
        printf("   a read failing at each of the %ld reads of a real file: MPQ_E_IO each time\n", n);
    }
}

/* ======== 3. the inflater ======== */
static infl_state *g_st;
/* inflate z into a buffer of exactly outn bytes, from an exact-size copy of the input, so that ASan sees a
   read or write one byte past either */
static int inf(const unsigned char *z, unsigned zn, unsigned outn, unsigned char **outp)
{
    unsigned char *zc = malloc(zn ? zn : 1), *out = malloc(outn ? outn : 1);
    int r;
    if (zn) memcpy(zc, z, zn);
    r = infl_zlib(g_st, zc, zn, out, outn);
    free(zc);
    if (outp) *outp = out; else free(out);
    return r;
}
/* zlib's deflate of in[0..n), flushed with flush after every chunk bytes (chunk 0: one piece) */
static unsigned char *zdef(const unsigned char *in, unsigned n, int level, int wbits, int strategy, int flush, unsigned chunk, unsigned *zn)
{
    z_stream z;
    unsigned cap, pos = 0;
    unsigned char *out;
    memset(&z, 0, sizeof z);
    if (deflateInit2(&z, level, Z_DEFLATED, wbits, 8, strategy) != Z_OK) return NULL;
    cap = (unsigned)deflateBound(&z, n) + 4096 + (chunk ? (n / chunk + 1) * 64 : 0);
    out = malloc(cap);
    z.next_out = out; z.avail_out = cap;
    while (pos < n) {
        unsigned k = chunk && n - pos > chunk ? chunk : n - pos;
        z.next_in = (unsigned char *)(in + pos); z.avail_in = k; pos += k;
        if (pos < n) deflate(&z, flush);
    }
    if (deflate(&z, Z_FINISH) != Z_STREAM_END) { deflateEnd(&z); free(out); return NULL; }
    *zn = cap - z.avail_out;
    deflateEnd(&z);
    return out;
}
/* test data: 0 zeros, 1 random, 2 words, 3 16-bit wave with a little noise (compresses about as well as the
   client's rain loops), 4 skewed bytes (long codes), 5 runs */
static void gen(unsigned char *p, unsigned n, int kind, unsigned seed)
{
    static const char *const words[] = { "rain ", "thunder ", "storm ", "Goldshire ", "inn ", "roof ", "wind ", "a ", "lightning ", "the " };
    unsigned x = seed * 2654435761u | 1u, i = 0;
    int v = 0, dv = 13;
    switch (kind) {
    case 0: memset(p, 0, n); break;
    case 1: for (i = 0; i < n; i++) p[i] = (unsigned char)xs(&x); break;
    case 2: while (i < n) { const char *w = words[xs(&x) % 10]; while (*w && i < n) p[i++] = (unsigned char)*w++; } break;
    case 3: for (i = 0; i + 1 < n; i += 2) { int s; v += dv; if (v > 20000 || v < -20000) dv = -dv; s = v + (int)(xs(&x) % 8) - 4; p[i] = (unsigned char)s; p[i + 1] = (unsigned char)(s >> 8); }
            if (n & 1) p[n - 1] = 0;
            break;
    case 4: for (i = 0; i < n; i++) { unsigned r = xs(&x), k = 0; while ((r & 1u) && k < 40) { k++; r >>= 1; } p[i] = (unsigned char)(k * 6 + (xs(&x) & 3u)); } break;
    default: while (i < n) { unsigned char b = (unsigned char)xs(&x); unsigned run = 1 + xs(&x) % 300; while (run-- && i < n) p[i++] = b; } break;
    }
}
static long g_streams, g_trunc, g_flips;
static void one_stream(const unsigned char *z, unsigned zn, const unsigned char *plain, unsigned n, unsigned *seed, const char *what)
{
    unsigned char *out, *c;
    unsigned k, j;
    int r;
    g_streams++;
    r = inf(z, zn, n, &out);
    CHECK(r == INFL_OK && memcmp(out, plain, n) == 0, "%s: %s", what, infl_error_text(r));
    free(out);
    if (n) { r = inf(z, zn, n - 1, NULL); CHECK(r == INFL_E_OVERFLOW, "%s, room for one byte less: %s", what, infl_error_text(r)); }
    r = inf(z, zn, n + 1, NULL); CHECK(r == INFL_E_SHORT, "%s, room for one byte more: %s", what, infl_error_text(r));
    /* every cut, or 40 random ones, must fail */
    if (zn <= 600) for (k = 0; k < zn; k++) { g_trunc++; r = inf(z, k, n, NULL); CHECK(r != INFL_OK, "%s cut to %u bytes decoded", what, k); }
    else for (j = 0; j < 40; j++) { g_trunc++; k = xs(seed) % zn; r = inf(z, k, n, NULL); CHECK(r != INFL_OK, "%s cut to %u bytes decoded", what, k); }
    /* one byte after the checksum */
    c = malloc(zn + 1); memcpy(c, z, zn); c[zn] = 0;
    r = inf(c, zn + 1, n, NULL); CHECK(r == INFL_E_TRAILING, "%s plus a byte: %s", what, infl_error_text(r));
    /* a flipped bit is an error, unless it is one of the padding bits nothing reads, and then the output is right */
    for (j = 0; j < (zn <= 5000 ? 24u : 8u); j++) {
        unsigned bit = xs(seed) % (zn * 8u);
        memcpy(c, z, zn); c[bit >> 3] ^= (unsigned char)(1u << (bit & 7u));
        g_flips++;
        r = inf(c, zn, n, &out);
        CHECK(r != INFL_OK || memcmp(out, plain, n) == 0, "%s with bit %u flipped decoded to different bytes", what, bit);
        free(out);
    }
    free(c);
}
static void test_zlib(void)
{
    static const unsigned sizes[] = { 0, 1, 2, 3, 100, 1000, 4096, 40000, 65537 };
    static const int levels[] = { 0, 1, 6, 9 };
    static const int strategies[] = { Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED };
    static const int wbits[] = { 9, 10, 12, 15 };
    static const struct { int flush; unsigned chunk; } fl[] = { { Z_NO_FLUSH, 0 }, { Z_SYNC_FLUSH, 1000 }, { Z_FULL_FLUSH, 3000 }, { Z_PARTIAL_FLUSH, 777 }, { Z_BLOCK, 500 } };
    unsigned char *data = malloc(1u << 20), *z;
    unsigned seed = 12345, s, l, st, zn, wi, fi;
    int k;
    char what[160];
    double t0 = now_ms();
    printf("== 3. the inflater\n");
    for (k = 0; k < 6; k++) for (s = 0; s < 9; s++) for (l = 0; l < 4; l++) for (st = 0; st < 5; st++) {
        gen(data, sizes[s], k, 7u * s + (unsigned)k + 1u);
        z = zdef(data, sizes[s], levels[l], 15, strategies[st], Z_NO_FLUSH, 0, &zn);
        CHECK(z != NULL, "zlib could not deflate");
        if (!z) continue;
        snprintf(what, sizeof what, "data kind %d, %u bytes, level %d, strategy %d", k, sizes[s], levels[l], strategies[st]);
        one_stream(z, zn, data, sizes[s], &seed, what);
        free(z);
    }
    /* small windows, and every kind of flush, which puts empty stored and fixed blocks and non-final blocks mid-stream */
    for (k = 2; k <= 4; k++) for (wi = 0; wi < 4; wi++) for (fi = 0; fi < 5; fi++) {
        gen(data, 100000, k, 99u + (unsigned)k);
        z = zdef(data, 100000, 6, wbits[wi], Z_DEFAULT_STRATEGY, fl[fi].flush, fl[fi].chunk, &zn);
        CHECK(z != NULL, "zlib could not deflate");
        if (!z) continue;
        snprintf(what, sizeof what, "data kind %d, window bits %d, flush %d every %u", k, wbits[wi], fl[fi].flush, fl[fi].chunk);
        one_stream(z, zn, data, 100000, &seed, what);
        free(z);
    }
    /* 1 MB: matches reaching the whole 32 KB back */
    for (k = 1; k <= 5; k++) for (l = 1; l <= 2; l++) {
        gen(data, 1u << 20, k, 555u + (unsigned)k);
        z = zdef(data, 1u << 20, l == 1 ? 1 : 9, 15, Z_DEFAULT_STRATEGY, Z_NO_FLUSH, 0, &zn);
        CHECK(z != NULL, "zlib could not deflate");
        if (!z) continue;
        snprintf(what, sizeof what, "1 MB of data kind %d, level %d", k, l == 1 ? 1 : 9);
        one_stream(z, zn, data, 1u << 20, &seed, what);
        free(z);
    }
    /* a preset dictionary is refused */
    {
        z_stream zs; unsigned char out[256]; int r;
        memset(&zs, 0, sizeof zs);
        deflateInit(&zs, 6); deflateSetDictionary(&zs, (const unsigned char *)"rain thunder", 12);
        zs.next_in = (unsigned char *)"rain rain thunder"; zs.avail_in = 17; zs.next_out = out; zs.avail_out = sizeof out;
        deflate(&zs, Z_FINISH);
        r = inf(out, (unsigned)(sizeof out - zs.avail_out), 17, NULL);
        CHECK(r == INFL_E_HEADER, "preset dictionary: %s", infl_error_text(r));
        deflateEnd(&zs);
    }
    printf("   %ld zlib streams, %ld truncations, %ld bit flips in %.0f ms\n", g_streams, g_trunc, g_flips, now_ms() - t0);
    free(data);
}

/* hand-built streams: a bit writer, canonical codes, a zlib wrapper */
typedef struct { unsigned char b[1 << 16]; unsigned n, acc, cnt; } bw_t;
static void bw_reset(bw_t *w) { w->n = w->acc = w->cnt = 0; }
static void bw_bits(bw_t *w, unsigned v, unsigned n) { while (n--) { w->acc |= (v & 1u) << w->cnt; v >>= 1; if (++w->cnt == 8) { w->b[w->n++] = (unsigned char)w->acc; w->acc = w->cnt = 0; } } }
static void bw_code(bw_t *w, unsigned code, unsigned len) { while (len--) bw_bits(w, (code >> len) & 1u, 1); }   /* Huffman codes go most significant bit first */
static void bw_align(bw_t *w) { if (w->cnt) { w->b[w->n++] = (unsigned char)w->acc; w->acc = w->cnt = 0; } }
static void bw_byte(bw_t *w, unsigned v) { w->b[w->n++] = (unsigned char)v; }
static void canon(const unsigned char *lens, unsigned n, unsigned *codes)
{
    unsigned count[16] = { 0 }, next[16] = { 0 }, code = 0, i;
    for (i = 0; i < n; i++) count[lens[i]]++;
    count[0] = 0;
    for (i = 1; i < 16; i++) { code = (code + count[i - 1]) << 1; next[i] = code; }
    for (i = 0; i < n; i++) codes[i] = lens[i] ? next[lens[i]]++ : 0;
}
/* the stream: header 78 01, the writer's bytes, then the Adler-32 of plain (zlib's own) */
static unsigned zwrap(unsigned char *out, bw_t *w, const unsigned char *plain, unsigned pn)
{
    unsigned a = (unsigned)adler32(adler32(0L, Z_NULL, 0), plain, pn);
    bw_align(w);
    out[0] = 0x78; out[1] = 0x01; memcpy(out + 2, w->b, w->n);
    out[w->n + 2] = (unsigned char)(a >> 24); out[w->n + 3] = (unsigned char)(a >> 16); out[w->n + 4] = (unsigned char)(a >> 8); out[w->n + 5] = (unsigned char)a;
    return w->n + 6;
}
static const unsigned char CL_ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
/* a dynamic block header from raw field values, the code length code's lengths cl[19], and the code length
   symbols to send (symbol | extra value << 8) */
static void bw_dyn(bw_t *w, int last, unsigned hlit, unsigned hdist, unsigned hclen, const unsigned char *cl, const unsigned *seq, unsigned nseq)
{
    unsigned clc[19], i;
    bw_bits(w, (unsigned)last, 1); bw_bits(w, 2, 2);
    bw_bits(w, hlit, 5); bw_bits(w, hdist, 5); bw_bits(w, hclen, 4);
    for (i = 0; i < hclen + 4; i++) bw_bits(w, cl[CL_ORDER[i]], 3);
    canon(cl, 19, clc);
    for (i = 0; i < nseq; i++) {
        unsigned sym = seq[i] & 0xFFu, ex = seq[i] >> 8;
        bw_code(w, clc[sym], cl[sym]);
        if (sym == 16) bw_bits(w, ex, 2); else if (sym == 17) bw_bits(w, ex, 3); else if (sym == 18) bw_bits(w, ex, 7);
    }
}
/* the usual header: code length code 0..15 at 4 bits each (complete), every length sent as it is */
static void bw_dyn_plain(bw_t *w, int last, const unsigned char *ll, unsigned nll, const unsigned char *dl, unsigned ndl)
{
    unsigned char cl[19] = { 0 };
    unsigned seq[320], i, n = 0;
    for (i = 0; i < 16; i++) cl[i] = 4;
    for (i = 0; i < nll; i++) seq[n++] = ll[i];
    for (i = 0; i < ndl; i++) seq[n++] = dl[i];
    bw_dyn(w, last, nll - 257, ndl - 1, 15, cl, seq, n);
}
static int hand(bw_t *w, const unsigned char *plain, unsigned pn, unsigned outn)   /* the result code only */
{
    static unsigned char z[(1 << 16) + 64];
    unsigned zn = zwrap(z, w, plain, pn);
    return inf(z, zn, outn, NULL);
}
static int hand_out(bw_t *w, const unsigned char *plain, unsigned pn)   /* INFL_OK only when it decodes to exactly plain */
{
    static unsigned char z[(1 << 16) + 64];
    unsigned char *out;
    unsigned zn = zwrap(z, w, plain, pn);
    int r = inf(z, zn, pn, &out);
    if (r == INFL_OK && pn && memcmp(out, plain, pn)) r = 99;
    free(out);
    return r;
}
#define SYM(s) bw_code(&w, lc[s], ll[s])
static void test_hand_built(void)
{
    static bw_t w;
    unsigned char ll[288], dl[32], cl5[19] = { 0 }, plain[600];
    unsigned lc[288], dc[32], i, seq[16], n;
    int r;
    for (i = 0; i < 16; i++) cl5[i] = 5;
    cl5[16] = 2; cl5[17] = 3; cl5[18] = 3;   /* a complete code length code with all three run symbols */

    /* one distance code, of one bit: allowed */
    memset(ll, 0, sizeof ll); memset(dl, 0, sizeof dl); ll['a'] = 2; ll[256] = 2; ll[257] = 1; dl[0] = 1;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 258, dl, 1); canon(ll, 258, lc); canon(dl, 1, dc);
    SYM('a'); SYM(257); bw_code(&w, dc[0], 1); SYM(256);
    r = hand_out(&w, (const unsigned char *)"aaaa", 4); CHECK(r == INFL_OK, "one distance code: %d", r);
    /* no distance codes at all, literals only: allowed */
    memset(ll, 0, sizeof ll); ll['a'] = 1; ll[256] = 1; dl[0] = 0;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); canon(ll, 257, lc);
    SYM('a'); SYM('a'); SYM(256);
    r = hand_out(&w, (const unsigned char *)"aa", 2); CHECK(r == INFL_OK, "no distance codes: %d", r);
    /* no distance codes, and a match: nothing to decode its distance with */
    memset(ll, 0, sizeof ll); ll['a'] = 1; ll[256] = 2; ll[257] = 2;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 258, dl, 1); canon(ll, 258, lc);
    SYM('a'); SYM(257); bw_bits(&w, 0, 24);
    r = hand(&w, (const unsigned char *)"aaaa", 4, 4); CHECK(r == INFL_E_SYMBOL, "a match with no distance codes: %s", infl_error_text(r));
    /* over-subscribed, incomplete and end-of-block-less literal/length codes, an incomplete distance code */
    memset(ll, 0, sizeof ll); ll['a'] = 1; ll['b'] = 1; ll[256] = 1; dl[0] = 1;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); bw_bits(&w, 0, 16);
    r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "over-subscribed: %s", infl_error_text(r));
    memset(ll, 0, sizeof ll); ll['a'] = 2; ll[256] = 2;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); bw_bits(&w, 0, 16);
    r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "incomplete: %s", infl_error_text(r));
    memset(ll, 0, sizeof ll); ll['a'] = 1; ll['b'] = 1;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); bw_bits(&w, 0, 16);
    r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "no end-of-block: %s", infl_error_text(r));
    memset(ll, 0, sizeof ll); ll['a'] = 1; ll[256] = 1; dl[0] = 2; dl[1] = 2;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 2); bw_bits(&w, 0, 16);
    r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "incomplete distance code: %s", infl_error_text(r));
    /* a literal/length code of one symbol (end-of-block, one bit): allowed, an empty block */
    memset(ll, 0, sizeof ll); ll[256] = 1; dl[0] = 0;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); bw_code(&w, 0, 1);
    r = hand_out(&w, plain, 0); CHECK(r == INFL_OK, "single end-of-block code: %d", r);
    /* counts deflate does not define: 287 and 288 literal/length codes, 31 and 32 distance codes */
    {
        static const unsigned hl[4] = { 30, 31, 0, 0 }, hd[4] = { 0, 0, 30, 31 };
        for (i = 0; i < 4; i++) {
            bw_reset(&w); bw_dyn(&w, 1, hl[i], hd[i], 15, cl5, NULL, 0); bw_bits(&w, 0, 32);
            r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "hlit %u hdist %u: %s", hl[i], hd[i], infl_error_text(r));
        }
    }
    /* code length codes: incomplete, over-subscribed; a run with nothing before it; a run past the end */
    {
        unsigned char cl[19] = { 0 };
        cl[0] = 1;
        bw_reset(&w); bw_dyn(&w, 1, 0, 0, 15, cl, NULL, 0); bw_bits(&w, 0, 32);
        r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "incomplete code length code: %s", infl_error_text(r));
        cl[1] = 1; cl[2] = 1;
        bw_reset(&w); bw_dyn(&w, 1, 0, 0, 15, cl, NULL, 0); bw_bits(&w, 0, 32);
        r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "over-subscribed code length code: %s", infl_error_text(r));
        seq[0] = 16;
        bw_reset(&w); bw_dyn(&w, 1, 0, 0, 15, cl5, seq, 1); bw_bits(&w, 0, 32);
        r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "a run first: %s", infl_error_text(r));
        seq[0] = 18 | 127u << 8; seq[1] = 18 | 127u << 8;
        bw_reset(&w); bw_dyn(&w, 1, 0, 0, 15, cl5, seq, 2); bw_bits(&w, 0, 32);
        r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "a run past the end: %s", infl_error_text(r));
        /* HCLEN 0 sends lengths for 16, 17, 18 and 0 only: all 258 lengths zero, so no end-of-block */
        memset(cl, 0, sizeof cl); cl[16] = 2; cl[17] = 2; cl[18] = 2; cl[0] = 2;
        seq[0] = 18 | 127u << 8; seq[1] = 18 | 109u << 8;
        bw_reset(&w); bw_dyn(&w, 1, 0, 0, 0, cl, seq, 2); bw_bits(&w, 0, 32);
        r = hand(&w, plain, 0, 0); CHECK(r == INFL_E_CODES, "hclen 0: %s", infl_error_text(r));
    }
    /* runs of all three kinds, used right: a b c d at 3 bits (a, then 16 repeats it three times), end-of-block
       and 257 at 2 bits, one distance code; "abcd" and a match of 3 at distance 1 */
    memset(ll, 0, sizeof ll); for (i = 'a'; i <= 'd'; i++) ll[i] = 3; ll[256] = 2; ll[257] = 2; dl[0] = 1;
    n = 0;
    seq[n++] = 18 | 86u << 8;                                                /* 0..96: 97 zeros */
    seq[n++] = 3; seq[n++] = 16 | 0u << 8;                                   /* 97, then 98..100 the same */
    seq[n++] = 18 | 127u << 8; seq[n++] = 17 | 7u << 8; seq[n++] = 17 | 4u << 8;   /* 101..255: 138 + 10 + 7 zeros */
    seq[n++] = 2; seq[n++] = 2; seq[n++] = 1;                                /* 256, 257, then the distance code */
    bw_reset(&w); bw_dyn(&w, 1, 1, 0, 15, cl5, seq, n); canon(ll, 258, lc); canon(dl, 1, dc);
    SYM('a'); SYM('b'); SYM('c'); SYM('d'); SYM(257); bw_code(&w, dc[0], 1); SYM(256);
    r = hand_out(&w, (const unsigned char *)"abcdddd", 7); CHECK(r == INFL_OK, "runs: %d", r);
    /* codes of 10 to 15 bits (past the lookup table): A..O at 1..15 bits, end-of-block at 15 */
    memset(ll, 0, sizeof ll); for (i = 0; i < 15; i++) ll['A' + i] = (unsigned char)(i + 1); ll[256] = 15; dl[0] = 0;
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 257, dl, 1); canon(ll, 257, lc);
    for (i = 0; i < 15; i++) SYM('A' + i);
    SYM('O'); SYM('N'); SYM(256);
    r = hand_out(&w, (const unsigned char *)"ABCDEFGHIJKLMNOON", 17); CHECK(r == INFL_OK, "long codes: %d", r);
    /* the largest code counts: 286 literal/length codes, 30 distance codes */
    for (i = 0; i < 286; i++) ll[i] = (unsigned char)(i < 226 ? 8 : 9);
    for (i = 0; i < 30; i++) dl[i] = (unsigned char)(i < 2 ? 4 : 5);
    bw_reset(&w); bw_dyn_plain(&w, 1, ll, 286, dl, 30); canon(ll, 286, lc); canon(dl, 30, dc);
    SYM('a'); SYM(285); bw_code(&w, dc[0], dl[0]); SYM(250); SYM(256);
    memset(plain, 'a', 259); plain[259] = 250;
    r = hand_out(&w, plain, 260); CHECK(r == INFL_OK, "286 and 30 codes: %d", r);

    /* fixed blocks */
    for (i = 0; i < 288; i++) ll[i] = (unsigned char)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
    for (i = 0; i < 32; i++) dl[i] = 5;
    canon(ll, 288, lc); canon(dl, 32, dc);
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM(286); bw_bits(&w, 0, 24);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_SYMBOL, "length symbol 286: %s", infl_error_text(r));
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM(287); bw_bits(&w, 0, 24);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_SYMBOL, "length symbol 287: %s", infl_error_text(r));
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM('x'); SYM(257); bw_code(&w, dc[30], 5); bw_bits(&w, 0, 24);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_SYMBOL, "distance symbol 30: %s", infl_error_text(r));
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM('x'); SYM(257); bw_code(&w, dc[31], 5); bw_bits(&w, 0, 24);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_SYMBOL, "distance symbol 31: %s", infl_error_text(r));
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM(257); bw_code(&w, dc[0], 5); SYM(256);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_DISTANCE, "a match before any output: %s", infl_error_text(r));
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM(257); bw_code(&w, dc[1], 5); SYM(256);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_DISTANCE, "distance 2 after one byte: %s", infl_error_text(r));
    /* length 258 both ways: 285, and 284 with all five extra bits set (227 + 31) */
    memset(plain, 'x', 259);
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM(285); bw_code(&w, dc[0], 5); SYM(256);
    r = hand_out(&w, plain, 259); CHECK(r == INFL_OK, "length 258 by 285: %d", r);
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('x'); SYM(284); bw_bits(&w, 31, 5); bw_code(&w, dc[0], 5); SYM(256);
    r = hand_out(&w, plain, 259); CHECK(r == INFL_OK, "length 258 by 284: %d", r);
    /* distance 32768 (symbol 29, all 13 extra bits) reaches the very first byte of 32 KB of output */
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 1, 2);
    SYM('y'); for (i = 1; i < 32768; i++) SYM('x');
    SYM(257); bw_code(&w, dc[29], 5); bw_bits(&w, 8191, 13); SYM(256);
    {
        unsigned char *big = malloc(32771);
        memset(big, 'x', 32771); big[0] = 'y'; big[32768] = 'y';
        r = hand_out(&w, big, 32771); CHECK(r == INFL_OK, "distance 32768: %d", r);
        r = hand(&w, big, 32771, 32771); CHECK(r == INFL_OK, "distance 32768 again: %d", r);
        free(big);
    }
    /* block type 3 */
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 3, 2); bw_bits(&w, 0, 16);
    r = hand(&w, plain, 0, 8); CHECK(r == INFL_E_BLOCK, "block type 3: %s", infl_error_text(r));
    /* stored blocks: length and complement disagree; longer than the input; longer than the room */
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 0, 2); bw_align(&w); bw_byte(&w, 5); bw_byte(&w, 0); bw_byte(&w, 0); bw_byte(&w, 0);
    for (i = 0; i < 5; i++) bw_byte(&w, 'z');
    r = hand(&w, (const unsigned char *)"zzzzz", 5, 5); CHECK(r == INFL_E_BLOCK, "stored, bad complement: %s", infl_error_text(r));
    {
        unsigned char raw[32] = { 0x78, 0x01, 0x01, 100, 0, 0x9B, 0xFF };
        r = inf(raw, 17, 100, NULL); CHECK(r == INFL_E_TRUNCATED, "stored, past the input: %s", infl_error_text(r));
    }
    bw_reset(&w); bw_bits(&w, 1, 1); bw_bits(&w, 0, 2); bw_align(&w); bw_byte(&w, 10); bw_byte(&w, 0); bw_byte(&w, 0xF5); bw_byte(&w, 0xFF);
    for (i = 0; i < 10; i++) bw_byte(&w, 'z');
    r = hand(&w, (const unsigned char *)"zzzzzzzzzz", 10, 5); CHECK(r == INFL_E_OVERFLOW, "stored, past the room: %s", infl_error_text(r));
    r = hand_out(&w, (const unsigned char *)"zzzzzzzzzz", 10); CHECK(r == INFL_OK, "stored: %d", r);
    /* a stored block that is not the last, then a fixed one that is */
    bw_reset(&w); bw_bits(&w, 0, 1); bw_bits(&w, 0, 2); bw_align(&w); bw_byte(&w, 3); bw_byte(&w, 0); bw_byte(&w, 0xFC); bw_byte(&w, 0xFF);
    bw_byte(&w, 'a'); bw_byte(&w, 'b'); bw_byte(&w, 'c');
    bw_bits(&w, 1, 1); bw_bits(&w, 1, 2); SYM('d'); SYM('e'); SYM('f'); SYM(256);
    r = hand_out(&w, (const unsigned char *)"abcdef", 6); CHECK(r == INFL_OK, "stored then fixed: %d", r);
    /* input that ends after a block that is not the last */
    {
        unsigned char raw[16] = { 0x78, 0x01 };
        bw_reset(&w); bw_bits(&w, 0, 1); bw_bits(&w, 1, 2); SYM('q'); SYM(256); bw_align(&w);
        memcpy(raw + 2, w.b, w.n);
        r = inf(raw, w.n + 2, 1, NULL); CHECK(r == INFL_E_TRUNCATED, "no last block: %s", infl_error_text(r));
    }
    /* headers: method 7, a 64 KB window, bad check bits, a preset dictionary; nothing, one byte; a bad checksum */
    {
        static const unsigned cmf[4] = { 0x77, 0x88, 0x78, 0x78 };
        unsigned char s[8];
        for (i = 0; i < 4; i++) {
            unsigned flg = 0;
            if (i < 2) while ((cmf[i] * 256 + flg) % 31) flg++;
            else flg = i == 2 ? 0x02 : 0xBB;   /* 78 BB: check bits right, FDICT set */
            s[0] = (unsigned char)cmf[i]; s[1] = (unsigned char)flg; s[2] = 0x03; s[3] = 0x00; s[4] = 0; s[5] = 0; s[6] = 0; s[7] = 1;
            r = inf(s, 8, 0, NULL); CHECK(r == INFL_E_HEADER, "header %02X %02X: %s", cmf[i], flg, infl_error_text(r));
        }
        s[0] = 0x78; s[1] = 0x01;
        r = inf(s, 8, 0, NULL); CHECK(r == INFL_OK, "an empty stream: %s", infl_error_text(r));
        s[7] = 2; r = inf(s, 8, 0, NULL); CHECK(r == INFL_E_CHECKSUM, "a wrong checksum: %s", infl_error_text(r));
        r = inf(s, 0, 0, NULL); CHECK(r == INFL_E_TRUNCATED, "no input: %s", infl_error_text(r));
        r = inf(s, 1, 0, NULL); CHECK(r == INFL_E_TRUNCATED, "one byte: %s", infl_error_text(r));
    }
    printf("   hand-built streams done\n");
}
#undef SYM

/* ======== 4. archives built here ======== */
static unsigned CT[0x500];
static void ct_init(void)
{
    unsigned seed = 0x00100001u, i, j;
    for (i = 0; i < 0x100; i++) for (j = 0; j < 5; j++) {
        unsigned hi;
        seed = (seed * 125u + 3u) % 0x2AAAABu; hi = (seed & 0xFFFFu) << 16;
        seed = (seed * 125u + 3u) % 0x2AAAABu;
        CT[i + j * 0x100] = hi | (seed & 0xFFFFu);
    }
}
static unsigned th(const char *s, unsigned type)
{
    unsigned s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32; else if (ch == '/') ch = '\\';
        s1 = CT[(type << 8) + ch] ^ (s1 + s2);
        s2 = ch + s1 + s2 + (s2 << 5) + 3u;
    }
    return s1;
}
/* the table cipher, both ways; the seed always runs on the plain value */
static void t_cipher(unsigned char *p, unsigned ndw, unsigned key, int encrypt)
{
    unsigned seed = 0xEEEEEEEEu, i;
    for (i = 0; i < ndw; i++) {
        unsigned v = get32(p + 4 * i), plain;
        seed += CT[0x400 + (key & 0xFFu)];
        if (encrypt) { plain = v; v ^= key + seed; } else { v ^= key + seed; plain = v; }
        key = ((~key << 21) + 0x11111111u) | (key >> 11);
        seed = plain + seed + (seed << 5) + 3u;
        put32(p + 4 * i, v);
    }
}

typedef struct { unsigned char *p; size_t n, cap; } gbuf;
static size_t gb_grow(gbuf *b, size_t n)   /* n more bytes, zeroed; their offset */
{
    size_t at = b->n;
    if (b->n + n > b->cap) { size_t c = b->cap ? b->cap : 4096; while (c < b->n + n) c *= 2; b->p = realloc(b->p, c); b->cap = c; }
    if (n) memset(b->p + at, 0, n);
    b->n += n;
    return at;
}
static void gb_put(gbuf *b, const void *d, size_t n) { size_t at = gb_grow(b, n); if (n) memcpy(b->p + at, d, n); }
static unsigned zc(const unsigned char *in, unsigned n, int strategy, unsigned char *out, unsigned cap)
{
    z_stream z;
    unsigned got;
    int r;
    memset(&z, 0, sizeof z);
    deflateInit2(&z, 9, Z_DEFLATED, 15, 8, strategy);
    z.next_in = (unsigned char *)in; z.avail_in = n; z.next_out = out; z.avail_out = cap;
    r = deflate(&z, Z_FINISH); got = cap - z.avail_out; deflateEnd(&z);
    return r == Z_STREAM_END ? got : 0;
}

/* how a test file is stored */
enum { K_SECT, K_SECT_CRC0, K_SECT_CRCRAW, K_FIXED, K_BAD_SHORT, K_BAD_LONG, K_BAD_TRAIL, K_PLAIN, K_SINGLE, K_SINGLE_RAW, K_STUB };
typedef struct { const char *name; const unsigned char *data; unsigned size; int kind; } tfile;
#define TMAX 24
typedef struct {
    unsigned char *img; size_t size;
    unsigned base, hash_off, block_off, hash_count, nfiles, shift;
    unsigned off[TMAX], csize[TMAX], fsize[TMAX], flags[TMAX], slot[TMAX], home[TMAX];
} tarc;
typedef struct { unsigned hash_count, base, shift; int version, user_data, tables_first, deleted_slot; } topts;

/* sectors with a table in front: zlib where it helps, stored where it does not; K_BAD_* spoil sector 0 */
static void enc_sectors(gbuf *b, const tfile *f, unsigned sector, unsigned *flags)
{
    unsigned n = f->size, nsec = (n + sector - 1) / sector, crc = f->kind == K_SECT_CRC0 || f->kind == K_SECT_CRCRAW, s, cap = sector * 2 + 1024;
    size_t start = b->n, tpos = gb_grow(b, (nsec + 1 + crc) * 4);
    unsigned char *z = malloc(cap), *tmp = malloc(sector + 1);
    for (s = 0; s < nsec; s++) {
        const unsigned char *src = f->data + s * sector, *zsrc = src;
        unsigned want = n - s * sector < sector ? n - s * sector : sector, zlen = want, zn, extra = 0;
        put32(b->p + tpos + 4 * s, (unsigned)(b->n - start));
        if (s == 0 && f->kind == K_BAD_SHORT) zlen = want - 1;
        if (s == 0 && f->kind == K_BAD_LONG) { memcpy(tmp, src, want); tmp[want] = 0x55; zsrc = tmp; zlen = want + 1; }
        if (s == 0 && f->kind == K_BAD_TRAIL) extra = 1;
        zn = zc(zsrc, zlen, f->kind == K_FIXED ? Z_FIXED : Z_DEFAULT_STRATEGY, z, cap);
        if (zn && zn + 1 + extra < want) { gb_put(b, "\x02", 1); gb_put(b, z, zn); if (extra) gb_put(b, "\x00", 1); }
        else gb_put(b, src, want);
    }
    put32(b->p + tpos + 4 * nsec, (unsigned)(b->n - start));
    if (crc) {
        if (f->kind == K_SECT_CRCRAW) for (s = 0; s < nsec; s++) { unsigned char c[4]; put32(c, 0x11111111u * (s + 1)); gb_put(b, c, 4); }
        put32(b->p + tpos + 4 * (nsec + 1), (unsigned)(b->n - start));
    }
    *flags = 0x80000200u | (crc ? 0x04000000u : 0u);
    free(z); free(tmp);
}
static void enc_file(gbuf *b, const tfile *f, unsigned sector, unsigned *flags)
{
    unsigned cap, zn;
    unsigned char *z;
    switch (f->kind) {
    case K_STUB: *flags = 0x80000200u; return;
    case K_PLAIN: gb_put(b, f->data, f->size); *flags = 0x80000000u; return;
    case K_SINGLE_RAW: gb_put(b, f->data, f->size); *flags = 0x81000200u; return;   /* compressed flag, stored whole */
    case K_SINGLE:
        cap = f->size * 2 + 1024; z = malloc(cap); zn = zc(f->data, f->size, Z_DEFAULT_STRATEGY, z, cap);
        if (zn && zn + 1 < f->size) { gb_put(b, "\x02", 1); gb_put(b, z, zn); } else gb_put(b, f->data, f->size);
        free(z); *flags = 0x81000200u; return;
    default: enc_sectors(b, f, sector, flags);
    }
}
/* an archive: junk or a user-data block before base, the header, the files, the two tables (encrypted) */
static void build(tarc *a, const tfile *files, unsigned nf, const topts *o)
{
    gbuf b = { 0, 0, 0 };
    unsigned hsize = o->version ? 44u : 32u, hc = o->hash_count, sector = 512u << o->shift, i;
    size_t ht = 0, bt = 0;
    unsigned char *h;
    memset(a, 0, sizeof *a);
    a->base = o->base; a->hash_count = hc; a->nfiles = nf; a->shift = o->shift;
    gb_grow(&b, o->base);
    for (i = 0; i < o->base; i++) b.p[i] = (unsigned char)(0xA5 ^ (i * 7));   /* at every 512-byte step this is A5 A2 AB AC: never "MPQ" */
    if (o->user_data) { memcpy(b.p, "MPQ\x1b", 4); put32(b.p + 4, 64); put32(b.p + 8, o->base); put32(b.p + 12, 16); }
    gb_grow(&b, hsize);
    if (o->tables_first) { ht = gb_grow(&b, hc * 16); bt = gb_grow(&b, nf * 16); }
    for (i = 0; i < nf; i++) {
        a->off[i] = (unsigned)(b.n - o->base);
        enc_file(&b, &files[i], sector, &a->flags[i]);
        a->csize[i] = (unsigned)(b.n - o->base) - a->off[i]; a->fsize[i] = files[i].size;
    }
    if (!o->tables_first) { ht = gb_grow(&b, hc * 16); bt = gb_grow(&b, nf * 16); }
    memset(b.p + ht, 0xFF, hc * 16);
    if (o->deleted_slot >= 0) { unsigned char *e = b.p + ht + 16 * (unsigned)o->deleted_slot; put32(e, 0x12345678u); put32(e + 4, 0x9ABCDEF0u); put32(e + 8, 0); put32(e + 12, 0xFFFFFFFEu); }
    for (i = 0; i < nf; i++) {
        unsigned s = th(files[i].name, 0) & (hc - 1), k = 0;
        a->home[i] = s;
        while (get32(b.p + ht + 16 * s + 12) != 0xFFFFFFFFu && k++ < hc) s = (s + 1) & (hc - 1);
        a->slot[i] = s;
        put32(b.p + ht + 16 * s, th(files[i].name, 1)); put32(b.p + ht + 16 * s + 4, th(files[i].name, 2));
        put32(b.p + ht + 16 * s + 8, 0); put32(b.p + ht + 16 * s + 12, i);
    }
    for (i = 0; i < nf; i++) { put32(b.p + bt + 16 * i, a->off[i]); put32(b.p + bt + 16 * i + 4, a->csize[i]); put32(b.p + bt + 16 * i + 8, a->fsize[i]); put32(b.p + bt + 16 * i + 12, a->flags[i]); }
    t_cipher(b.p + ht, hc * 4, th("(hash table)", 3), 1);
    t_cipher(b.p + bt, nf * 4, th("(block table)", 3), 1);
    h = b.p + o->base;
    memcpy(h, "MPQ\x1a", 4); put32(h + 4, hsize); put32(h + 8, (unsigned)(b.n - o->base));
    put16(h + 12, (unsigned)o->version); put16(h + 14, o->shift);
    put32(h + 16, (unsigned)(ht - o->base)); put32(h + 20, (unsigned)(bt - o->base)); put32(h + 24, hc); put32(h + 28, nf);
    a->hash_off = (unsigned)(ht - o->base); a->block_off = (unsigned)(bt - o->base);
    a->img = b.p; a->size = b.n;
}
static unsigned char *img_copy(const tarc *a) { unsigned char *p = malloc(a->size); memcpy(p, a->img, a->size); return p; }
/* set field f (0..3) of entry idx of the hash table (block 0) or the block table (block 1) */
static void table_edit(unsigned char *img, const tarc *a, int block, unsigned idx, unsigned f, unsigned v)
{
    unsigned char *t = img + a->base + (block ? a->block_off : a->hash_off);
    unsigned n = (block ? a->nfiles : a->hash_count) * 4u, key = th(block ? "(block table)" : "(hash table)", 3);
    t_cipher(t, n, key, 0); put32(t + 16u * idx + 4u * f, v); t_cipher(t, n, key, 1);
}
static unsigned char *sector_entry(unsigned char *img, const tarc *a, unsigned i, unsigned k) { return img + a->base + a->off[i] + 4u * k; }

static const char *g_syn;   /* where the built archives are written */
static long g_variants;
/* write img as <synthetic>/<name>.mpq, read path from it through the disk callbacks, and expect want (and
   f's bytes, when want is MPQ_OK and f is given) */
static void expect_img(const char *name, const unsigned char *img, size_t size, const char *path, int want, const tfile *f)
{
    t_io t; mpq_io io; unsigned char *buf; unsigned len; int r; char fn[256];
    snprintf(fn, sizeof fn, "%s.mpq", name);
    CHECK(spit(g_syn, fn, img, size), "could not write %s", fn);
    io_on_dir(&t, g_syn); io = make_io(&t);
    r = mpq_read(&io, fn, path, &buf, &len);
    CHECK(r == want, "%s, %s: %s (%d), want %s (%d)", fn, path, mpq_error_text(r), r, mpq_error_text(want), want);
    if (r == MPQ_OK) {
        if (want == MPQ_OK && f) CHECK(len == f->size && memcmp(buf, f->data, len) == 0, "%s, %s: wrong bytes", fn, path);
        io.release(io.ctx, buf);
    } else CHECK(buf == NULL && len == 0, "%s, %s: output not cleared", fn, path);
    CHECK(t.live == 0 && t.handles == 0, "%s, %s: %ld allocations and %ld archives left", fn, path, t.live, t.handles);
    g_variants++;
}
static void expect_all(const char *name, const tarc *a, const tfile *files, unsigned nf, const char *missing)
{
    unsigned i;
    for (i = 0; i < nf; i++) expect_img(name, a->img, a->size, files[i].name, files[i].kind == K_STUB ? MPQ_E_EMPTY : MPQ_OK, &files[i]);
    if (missing) expect_img(name, a->img, a->size, missing, MPQ_E_NOT_FOUND, NULL);
}

/* random damage to a good archive, read back through the memory callbacks: any result code, but never a
   crash, an out-of-bounds access (ASan), an allocation or archive left over, or more than 64 MB */
static void fuzz(const tarc *a, const tfile *files, unsigned nf, unsigned iters, unsigned seed, const char *label)
{
    static const char *const lists[2] = { "absent.mpq", "fuzz.mpq" };
    unsigned char *m = malloc(a->size);
    long ok = 0, codes[16] = { 0 };
    unsigned it, i;
    for (it = 0; it < iters; it++) {
        size_t size = a->size;
        unsigned kind = xs(&seed) % 6, k, n;
        t_io t; mpq_io io; unsigned char *buf; unsigned len; int r, which;
        memcpy(m, a->img, a->size);
        switch (kind) {
        case 0: n = 1 + xs(&seed) % 8; for (k = 0; k < n; k++) m[xs(&seed) % size] ^= (unsigned char)(1 + xs(&seed) % 255); break;
        case 1: n = 1 + xs(&seed) % 4; for (k = 0; k < n; k++) m[a->base + xs(&seed) % 44] = (unsigned char)xs(&seed); break;
        case 2: {
            int block = (int)(xs(&seed) & 1u);
            unsigned idx = xs(&seed) % (block ? a->nfiles : a->hash_count), f = xs(&seed) % 4, v = xs(&seed);
            if (xs(&seed) & 1u) v %= 40000;   /* small values: other blocks, sizes near the real ones */
            table_edit(m, a, block, idx, f, v);
            break;
        }
        case 3: size = xs(&seed) % a->size; break;
        case 4: i = xs(&seed) % nf; if (a->csize[i]) { size_t at = a->base + a->off[i] + xs(&seed) % a->csize[i]; n = 1 + xs(&seed) % 64; for (k = 0; k < n && at + k < size; k++) m[at + k] = (unsigned char)xs(&seed); } break;
        default:
            i = xs(&seed) % nf;
            if (files[i].kind <= K_BAD_TRAIL && a->fsize[i]) {
                unsigned sector = 512u << a->shift, nent = (a->fsize[i] + sector - 1) / sector + 1 + (a->flags[i] & 0x04000000u ? 1u : 0u);
                unsigned char *e = sector_entry(m, a, i, xs(&seed) % nent);
                unsigned v = get32(e);
                put32(e, (xs(&seed) & 1u) ? xs(&seed) : v + (xs(&seed) % 9) - 4);
            }
        }
        io_on_mem(&t, m, size, "fuzz.mpq"); io = make_io(&t);
        for (i = 0; i <= nf; i++) {
            r = mpq_read(&io, "fuzz.mpq", i < nf ? files[i].name : "Test\\Missing.bin", &buf, &len);
            CHECK(r <= 0 && r >= MPQ_E_NOMEM, "fuzz: result %d", r);
            if (r == MPQ_OK) { CHECK(len > 0 && len <= (64u << 20), "fuzz: %u bytes", len); ok++; io.release(io.ctx, buf); }
            else if (r < 0 && r >= -15) codes[-r]++;
        }
        r = mpq_read_first(&io, lists, 2, files[xs(&seed) % nf].name, &buf, &len, &which);
        CHECK(r <= 0 && r >= MPQ_E_NOMEM && (r != MPQ_OK || which == 1), "fuzz read_first: %d, which %d", r, which);
        if (r == MPQ_OK) io.release(io.ctx, buf);
        CHECK(t.live == 0 && t.handles == 0, "fuzz %s iteration %u: %ld allocations and %ld archives left", label, it, t.live, t.handles);
    }
    printf("   fuzz %s: %u damaged archives, %u reads each; %ld read ok; errors:", label, iters, nf + 2, ok);
    for (i = 1; i < 12; i++) if (codes[i]) printf(" %s %ld,", mpq_error_text(-(int)i), codes[i]);
    printf("\n");
    free(m);
}
/* a failing read, then a failing allocation, at every step of a good read of every file */
static void faults(const tarc *a, const tfile *files, unsigned nf)
{
    unsigned i;
    long steps = 0, bad = 0, k;
    for (i = 0; i < nf; i++) {
        t_io t; mpq_io io; unsigned char *buf; unsigned len; int r; long nr, na;
        if (files[i].kind == K_STUB) continue;
        io_on_mem(&t, a->img, a->size, "f.mpq"); io = make_io(&t);
        r = mpq_read(&io, "f.mpq", files[i].name, &buf, &len);
        CHECK(r == MPQ_OK, "faults: %s does not read: %s", files[i].name, mpq_error_text(r));
        if (r != MPQ_OK) continue;
        io.release(io.ctx, buf);
        nr = t.reads; na = t.allocs;
        for (k = 1; k <= nr; k++) {
            io_on_mem(&t, a->img, a->size, "f.mpq"); t.fail_read_at = k; io = make_io(&t);
            r = mpq_read(&io, "f.mpq", files[i].name, &buf, &len);
            steps++; if (r != MPQ_E_IO || t.live || t.handles) { bad++; if (r == MPQ_OK) io.release(io.ctx, buf); }
        }
        for (k = 1; k <= na; k++) {
            io_on_mem(&t, a->img, a->size, "f.mpq"); t.fail_alloc_at = k; io = make_io(&t);
            r = mpq_read(&io, "f.mpq", files[i].name, &buf, &len);
            steps++; if (r != MPQ_E_NOMEM || t.live || t.handles) { bad++; if (r == MPQ_OK) io.release(io.ctx, buf); }
        }
    }
    CHECK(bad == 0, "faults: %ld of %ld failing steps did not end cleanly in MPQ_E_IO or MPQ_E_NOMEM", bad, steps);
    printf("   %ld failing reads and allocations: each ended in its error, nothing left over\n", steps);
}

static void test_synthetic(void)
{
    static unsigned char wave[20000], noise[12000], plainb[5000], text7[7000], rnd3[3000], text10[10000], exact[8192], one[1], wa[5000], wb[4500], wc[6000];
    static char wrap[4][32];
    enum { WA, WB, WC, RAIN, NOISE, PLAIN, SINGLE, SINGLERAW, FIXED, STUB, EXACT, ONE, NF };
    tfile F[NF];
    tarc g, v1, other, tf;
    topts o = { 16, 0, 3, 0, 0, 0, 15 };
    unsigned i, found = 0, x = 7, sector = 4096, rain_s1, rain_s2, wrapped = 0;
    unsigned char *m;
    char name[64];
    printf("== 4. archives built here: %s\n", g_syn);
    gen(wave, sizeof wave, 3, 1); gen(noise, 9000, 1, 2); gen(plainb, sizeof plainb, 2, 3); gen(text7, sizeof text7, 2, 4);
    gen(rnd3, sizeof rnd3, 1, 5); gen(text10, sizeof text10, 2, 6); gen(exact, sizeof exact, 3, 7); one[0] = 0x42;
    gen(wa, sizeof wa, 2, 8); gen(wb, sizeof wb, 5, 9); gen(wc, sizeof wc, 3, 10);
    /* four names whose chains start in the last slot (15), where a deleted entry sits: three go in and
       wrap round to slots 0, 1 and 2; the fourth is looked for and must not be found */
    for (i = 0; found < 4 && i < 100000; i++) { snprintf(wrap[found], sizeof wrap[found], "Test\\Wrap%u.bin", i); if ((th(wrap[found], 0) & 15u) == 15u) found++; }
    CHECK(found == 4, "no names for the wrap test");
    F[WA] = (tfile){ wrap[0], wa, sizeof wa, K_SECT };         F[WB] = (tfile){ wrap[1], wb, sizeof wb, K_PLAIN };
    F[WC] = (tfile){ wrap[2], wc, sizeof wc, K_SINGLE };       F[RAIN] = (tfile){ "Test\\Rain.wav", wave, sizeof wave, K_SECT_CRC0 };
    F[NOISE] = (tfile){ "Test\\Noise.bin", noise, sizeof noise, K_SECT_CRCRAW };
    F[PLAIN] = (tfile){ "Test\\Plain.bin", plainb, sizeof plainb, K_PLAIN };
    F[SINGLE] = (tfile){ "Test\\Single.bin", text7, sizeof text7, K_SINGLE };
    F[SINGLERAW] = (tfile){ "Test\\SingleRaw.bin", rnd3, sizeof rnd3, K_SINGLE_RAW };
    F[FIXED] = (tfile){ "Test\\Fixed.bin", text10, sizeof text10, K_FIXED };
    F[STUB] = (tfile){ "Test\\Stub.wav", NULL, 0, K_STUB };
    F[EXACT] = (tfile){ "Test\\Exact.bin", exact, sizeof exact, K_SECT };
    F[ONE] = (tfile){ "Test\\One.bin", one, 1, K_SECT };

    build(&g, F, NF, &o);
    for (i = 0; i < NF; i++) if (g.slot[i] < g.home[i]) wrapped++;
    CHECK(wrapped >= 3, "only %u files wrapped round the hash table", wrapped);
    rain_s1 = get32(sector_entry(g.img, &g, RAIN, 2)) - get32(sector_entry(g.img, &g, RAIN, 1));
    rain_s2 = get32(sector_entry(g.img, &g, RAIN, 5)) - get32(sector_entry(g.img, &g, RAIN, 4));
    CHECK(rain_s1 < sector && rain_s2 < 20000 - 4 * sector, "Rain's sectors 1 and 4 should be compressed (%u, %u)", rain_s1, rain_s2);
    CHECK(get32(sector_entry(g.img, &g, NOISE, 1)) - get32(sector_entry(g.img, &g, NOISE, 0)) == sector, "Noise's sector 0 should be stored");
    expect_all("good", &g, F, NF, wrap[3]);
    o.version = 1; build(&v1, F, NF, &o); expect_all("good_v1", &v1, F, NF, wrap[3]);
    o.version = 0; o.user_data = 1; o.base = 512; build(&other, F, NF, &o); expect_all("user_data", &other, F, NF, NULL); free(other.img);
    o.user_data = 0; o.base = 0x100000; build(&other, F, NF, &o); expect_all("header_at_1mb", &other, F, NF, NULL); free(other.img);
    o.base = 0x100200; build(&other, F, NF, &o); expect_img("header_past_1mb", other.img, other.size, F[RAIN].name, MPQ_E_FORMAT, NULL); free(other.img);
    o.base = 0; o.tables_first = 1; build(&tf, F, NF, &o); expect_all("tables_first", &tf, F, NF, wrap[3]);
    o.tables_first = 0; o.shift = 0; build(&other, F, NF, &o); expect_all("sectors_512", &other, F, NF, NULL); free(other.img);
    o.shift = 4; build(&other, F, NF, &o); expect_all("sectors_8k", &other, F, NF, NULL); free(other.img);
    {   /* a hash table with no free slot: a name it lacks is looked for in all four slots, then not found */
        topts o4 = { 4, 0, 3, 0, 0, 0, -1 };
        tfile F4[4];
        F4[0] = F[PLAIN]; F4[1] = F[SINGLE]; F4[2] = F[EXACT]; F4[3] = F[ONE];
        build(&other, F4, 4, &o4); expect_all("full_table", &other, F4, 4, "Test\\Missing.bin"); free(other.img);
    }
    o.shift = 3;

    /* no header, nothing at all, and truncations (the tables are at the end, so every cut loses them) */
    {
        unsigned char junk[4096];
        for (i = 0; i < sizeof junk; i++) junk[i] = (unsigned char)xs(&x);
        for (i = 0; i < sizeof junk; i += 512) junk[i] = 'X';
        expect_img("no_header", junk, sizeof junk, F[RAIN].name, MPQ_E_FORMAT, NULL);
        expect_img("empty", g.img, 0, F[RAIN].name, MPQ_E_FORMAT, NULL);
    }
    {
        size_t cuts[8] = { 3, 31, 32, 44, 0, 0, 0, 0 };
        cuts[4] = g.size / 2; cuts[5] = g.hash_off + 8; cuts[6] = g.size - 16; cuts[7] = g.size - 1;
        for (i = 0; i < 8; i++) { snprintf(name, sizeof name, "cut_%zu", cuts[i]); expect_img(name, g.img, cuts[i], F[RAIN].name, MPQ_E_FORMAT, NULL); }
    }
    /* tables first, then a cut inside Rain's data: the tables hold, Rain's block runs past the end, and a
       file stored before the cut still reads */
    expect_img("tables_first_cut", tf.img, tf.base + tf.off[RAIN] + tf.csize[RAIN] / 2, F[RAIN].name, MPQ_E_CORRUPT, NULL);
    expect_img("tables_first_cut", tf.img, tf.base + tf.off[RAIN] + tf.csize[RAIN] / 2, F[WA].name, MPQ_OK, &F[WA]);

    /* header fields */
#define HDR(label, off, bits, v, want) do { m = img_copy(&g); if (bits == 16) put16(m + (off), (v)); else put32(m + (off), (v)); \
        expect_img(label, m, g.size, F[RAIN].name, want, NULL); free(m); } while (0)
    HDR("hash_table_past_end", 16, 32, (unsigned)g.size, MPQ_E_FORMAT);
    HDR("hash_table_far_past_end", 16, 32, 0xFFFFFFF0u, MPQ_E_FORMAT);
    HDR("block_table_past_end", 20, 32, (unsigned)g.size - 8, MPQ_E_FORMAT);
    HDR("hash_count_huge", 24, 32, 0x40000000u, MPQ_E_FORMAT);
    HDR("hash_count_2_20_plus", 24, 32, (1u << 20) * 2, MPQ_E_FORMAT);
    HDR("hash_count_not_pow2", 24, 32, 12, MPQ_E_FORMAT);
    HDR("hash_count_zero", 24, 32, 0, MPQ_E_FORMAT);
    HDR("block_count_huge", 28, 32, 0x40000000u, MPQ_E_FORMAT);
    HDR("block_count_2_20_plus", 28, 32, (1u << 20) + 1, MPQ_E_FORMAT);
    HDR("format_2", 12, 16, 2, MPQ_E_UNSUPPORTED);
    HDR("format_1_short_header", 12, 16, 1, MPQ_E_FORMAT);
    HDR("header_size_31", 4, 32, 31, MPQ_E_FORMAT);
    HDR("sector_shift_16", 14, 16, 16, MPQ_E_UNSUPPORTED);
#undef HDR
    {
        static const unsigned offs[4] = { 32, 36, 40, 42 };
        for (i = 0; i < 4; i++) {
            m = img_copy(&v1); m[offs[i]] = 1;
            snprintf(name, sizeof name, "format_1_high_%u", offs[i]); expect_img(name, m, v1.size, F[RAIN].name, MPQ_E_UNSUPPORTED, NULL); free(m);
        }
    }
    /* hash and block entries */
#define TBL(label, block, idx, f, v, file, want) do { m = img_copy(&g); table_edit(m, &g, block, idx, f, v); \
        expect_img(label, m, g.size, F[file].name, want, NULL); free(m); } while (0)
    TBL("block_index_999", 0, g.slot[RAIN], 3, 999, RAIN, MPQ_E_FORMAT);
    TBL("block_index_count", 0, g.slot[RAIN], 3, NF, RAIN, MPQ_E_FORMAT);
    TBL("block_offset_past_end", 1, RAIN, 0, (unsigned)g.size, RAIN, MPQ_E_CORRUPT);
    TBL("block_size_huge", 1, RAIN, 1, 0x7FFFFFFFu, RAIN, MPQ_E_CORRUPT);
    TBL("file_size_64mb_plus", 1, RAIN, 2, (64u << 20) + 1, RAIN, MPQ_E_TOO_BIG);
    TBL("file_size_64mb", 1, RAIN, 2, 64u << 20, RAIN, MPQ_E_CORRUPT);
    TBL("file_size_plus_1", 1, RAIN, 2, 20001, RAIN, MPQ_E_DATA);
    TBL("file_size_minus_1", 1, RAIN, 2, 19999, RAIN, MPQ_E_DATA);
    TBL("flag_encrypted", 1, RAIN, 3, g.flags[RAIN] | 0x00010000u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_fix_key", 1, RAIN, 3, g.flags[RAIN] | 0x00020000u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_implode", 1, RAIN, 3, g.flags[RAIN] | 0x00000100u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_patch", 1, RAIN, 3, g.flags[RAIN] | 0x00100000u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_delete_marker_sized", 1, RAIN, 3, g.flags[RAIN] | 0x02000000u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_unknown_400", 1, RAIN, 3, g.flags[RAIN] | 0x00000400u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_unknown_8000000", 1, RAIN, 3, g.flags[RAIN] | 0x08000000u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_unknown_1", 1, RAIN, 3, g.flags[RAIN] | 0x00000001u, RAIN, MPQ_E_UNSUPPORTED);
    TBL("flag_not_exists", 1, RAIN, 3, g.flags[RAIN] & 0x7FFFFFFFu, RAIN, MPQ_E_NOT_FOUND);
    TBL("stub_delete_marker", 1, STUB, 3, 0x82000000u, STUB, MPQ_E_EMPTY);
    TBL("stub_encrypted", 1, STUB, 3, 0x80010000u, STUB, MPQ_E_EMPTY);
    TBL("single_with_crc", 1, SINGLE, 3, 0x85000200u, SINGLE, MPQ_E_UNSUPPORTED);
    TBL("single_stored_bigger", 1, SINGLE, 1, g.fsize[SINGLE] + 1, SINGLE, MPQ_E_CORRUPT);
    TBL("single_not_compressed", 1, SINGLE, 3, 0x81000000u, SINGLE, MPQ_E_CORRUPT);
    TBL("plain_with_crc", 1, PLAIN, 3, 0x84000000u, PLAIN, MPQ_E_UNSUPPORTED);
    TBL("plain_short", 1, PLAIN, 1, g.csize[PLAIN] - 1, PLAIN, MPQ_E_CORRUPT);
#undef TBL
    m = img_copy(&g); table_edit(m, &g, 1, SINGLERAW, 3, 0x81000000u);   /* single unit, not compressed: stored whole either way */
    expect_img("single_raw_plain_flags", m, g.size, F[SINGLERAW].name, MPQ_OK, &F[SINGLERAW]); free(m);
    /* sector tables */
#define SEC(label, file, k, v, want) do { m = img_copy(&g); put32(sector_entry(m, &g, file, k), (v)); \
        expect_img(label, m, g.size, F[file].name, want, NULL); free(m); } while (0)
    SEC("sectors_backwards", RAIN, 2, get32(sector_entry(g.img, &g, RAIN, 1)) - 1, MPQ_E_CORRUPT);
    SEC("sector_first_entry", RAIN, 0, get32(sector_entry(g.img, &g, RAIN, 0)) + 4, MPQ_E_CORRUPT);
    SEC("sector_first_entry_small", RAIN, 0, 4, MPQ_E_CORRUPT);
    SEC("sector_huge", RAIN, 1, 0xFFFFFF00u, MPQ_E_CORRUPT);
    SEC("sector_empty", RAIN, 2, get32(sector_entry(g.img, &g, RAIN, 1)), MPQ_E_CORRUPT);
    SEC("sector_end_past_block", RAIN, 5, g.csize[RAIN] + 1, MPQ_E_CORRUPT);
    SEC("crc_end_past_block", RAIN, 6, g.csize[RAIN] + 1, MPQ_E_CORRUPT);
    SEC("crc_end_before_data_end", NOISE, 4, get32(sector_entry(g.img, &g, NOISE, 3)) - 1, MPQ_E_CORRUPT);
    SEC("sector_longer_than_its_share", NOISE, 1, get32(sector_entry(g.img, &g, NOISE, 1)) + 1, MPQ_E_CORRUPT);
#undef SEC
    /* sector data: masks this reader refuses, damaged deflate data, a damaged checksum */
    {
        static const unsigned char masks[8] = { 0x08, 0x10, 0x12, 0x22, 0x01, 0x00, 0x20, 0x82 };
        size_t s1 = g.base + g.off[RAIN] + get32(sector_entry(g.img, &g, RAIN, 1));
        for (i = 0; i < 8; i++) {
            m = img_copy(&g); m[s1] = masks[i];
            snprintf(name, sizeof name, "mask_%02x", masks[i]); expect_img(name, m, g.size, F[RAIN].name, MPQ_E_UNSUPPORTED, NULL); free(m);
        }
        m = img_copy(&g); m[s1 + rain_s1 / 2] ^= 0x55; expect_img("bad_deflate", m, g.size, F[RAIN].name, MPQ_E_DATA, NULL); free(m);
        m = img_copy(&g); m[s1 + 3] ^= 0xFF; expect_img("bad_deflate_start", m, g.size, F[RAIN].name, MPQ_E_DATA, NULL); free(m);
        m = img_copy(&g); m[s1 + rain_s1 - 1] ^= 0x01; expect_img("bad_checksum", m, g.size, F[RAIN].name, MPQ_E_DATA, NULL); free(m);
        m = img_copy(&g); m[g.base + g.off[SINGLE]] = 0x08; expect_img("single_mask_08", m, g.size, F[SINGLE].name, MPQ_E_UNSUPPORTED, NULL); free(m);
        m = img_copy(&g); m[g.base + g.off[SINGLE] + g.csize[SINGLE] / 2] ^= 0x55; expect_img("single_bad_deflate", m, g.size, F[SINGLE].name, MPQ_E_DATA, NULL); free(m);
    }
    /* sectors that inflate to one byte too few, one byte too many, or leave a byte over */
    {
        static const int kinds[3] = { K_BAD_SHORT, K_BAD_LONG, K_BAD_TRAIL };
        static const char *const names[3] = { "sector_inflates_short", "sector_inflates_long", "sector_trailing_byte" };
        topts ob = { 16, 0, 3, 0, 0, 0, -1 };
        for (i = 0; i < 3; i++) {
            tfile bf[2];
            bf[0] = (tfile){ "Test\\Bad.wav", wave, sizeof wave, kinds[i] }; bf[1] = F[PLAIN];
            build(&other, bf, 2, &ob);
            CHECK(get32(sector_entry(other.img, &other, 0, 1)) - get32(sector_entry(other.img, &other, 0, 0)) < sector, "%s: sector 0 came out stored", names[i]);
            expect_img(names[i], other.img, other.size, bf[0].name, MPQ_E_DATA, NULL);
            expect_img(names[i], other.img, other.size, bf[1].name, MPQ_OK, &bf[1]);
            free(other.img);
        }
    }
    printf("   %ld archives written and read, each with its expected result\n", g_variants);

    {
        double t0 = now_ms();
        fuzz(&g, F, NF, 12000, 0xC0FFEEu, "tables at the end");
        fuzz(&tf, F, NF, 6000, 0xBADF00Du, "tables first");
        fuzz(&v1, F, NF, 4000, 0x5EEDu, "format 1");
        printf("   fuzzing took %.0f ms\n", now_ms() - t0);
    }
    faults(&g, F, NF);
    free(g.img); free(v1.img); free(tf.img);
}

int main(int argc, char **argv)
{
    char syn[4096];
    if (argc != 4) { fprintf(stderr, "usage: %s <client Data folder> <reference folder> <scratch folder>\n", argv[0]); return 2; }
    if (in_git_work_tree(argv[3])) { fprintf(stderr, "refusing %s: it lands inside a git work tree (or where it lands cannot be told); name a scratch folder outside any repo\n", argv[3]); return 2; }
    g_ref = argv[2];
    snprintf(syn, sizeof syn, "%s/synthetic", argv[3]);
    if (mkdir(syn, 0755) && errno != EEXIST) { perror(syn); return 2; }
    g_syn = syn;
    g_st = malloc(sizeof *g_st);
    ct_init();
    test_real(argv[1]);
    test_zlib();
    test_hand_built();
    test_synthetic();
    printf("== %ld checks, %ld failed\n", g_checks, g_fails);
    free(g_st);
    return g_fails ? 1 : 0;
}
