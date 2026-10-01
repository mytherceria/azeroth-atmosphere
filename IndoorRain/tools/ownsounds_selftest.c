/* ownsounds_selftest.c: the native end-to-end check of what IndoorRain.dll does since 0.14 when it builds its
 * sounds: read the client files out of a client's Data folder, pick each one's archive as the DLL does, decode,
 * run the 25 recipes, and look hard at the WAV images the DLL would hand FMOD. Not part of the DLL, and it uses
 * the C library. What it tests is the DLL's own code: dll/mpq.c, dll/inflate.c, dll/dsp.c and dll/sounds.c are
 * compiled as they are, with the DLL's own source table (SND_SOURCES). What lives in indoorrain.c (the archive
 * callbacks and what they make of a failure, io_open and the rest; the archive walk, source_read; the builds, their
 * x87 control word and what each failure costs, build_failed, build_one and build_source; the grouping of outputs by
 * source, sounds_build; what the settings want built, wanted_sounds and sounds_upkeep; the checks on what FMOD
 * decoded, the length first) is Win32 code, so it is restated here, step for step, with each restatement named after
 * the function it follows and the Windows calls replaced by POSIX ones (errno for GetLastError: ENOENT and ENOTDIR are
 * the DLL's "file" and "path not found"). If indoorrain.c changes, those must follow it. What an Ogg file says of
 * itself before FMOD decodes it is sounds.c's snd_ogg_probe, run as it is.
 *
 * Build, in tools/ (the binary goes outside the repo):
 *   gcc -O2 -Wall -Wextra -std=c99 -I../dll -o /tmp/ownsounds_selftest ownsounds_selftest.c \
 *       ../dll/mpq.c ../dll/inflate.c ../dll/dsp.c ../dll/sounds.c
 * The DLL's own arithmetic is 32-bit x87 with the control word it sets (0x27F); add -m32 -mfpmath=387 to get
 * that (a 32-bit C library is needed), and this sets the same control word around each sound. Only that build
 * can run --fpu-clobber. Add -fsanitize=address,undefined to hunt leaks and overruns.
 * Use:
 *   ownsounds_selftest build <Data folder> <out folder> [--sources] [--expect ravencraft|no-patch-s] [--mix-rate R]
 *                            [--fail-nth K] [--fail-from K] [--open-error ARCHIVE[:N]] [--read-error ARCHIVE[:N]]
 *                            [--unfit FILE] [--fpu-clobber] [--hashes FILE] [--no-write]
 *       The DLL's builds with the ini's defaults, in the DLL's order: the six rain loops at start; the ten
 *       thunders and the three gusts at the poll the addon connects with the storm on; then the six snow and sand
 *       loops, one build each, as each is wanted the first time; each for a mixer at R Hz (FSOUND_GetOutputRate in
 *       the DLL; 44,100, both clients' rate, when none is given). Writes <out>/<name>.wav, the WAV image the DLL
 *       would hand FMOD, and prints the DLL's log lines, the time and the memory: the allocator's count (the DLL's
 *       "held" and "at the build's peak") and the process's peak resident set. The gusts are Ogg Vorbis, which the
 *       DLL has the client's fmod.dll decode; here ffmpeg decodes them (to 16-bit, as FMOD does) from a copy written
 *       to <out>/src/. --sources also writes every client file as it was read, to <out>/src/<archive>/<file>, to
 *       hold against another reader. --expect adds what a known client must give (the archive and size of each
 *       file, what gets built at each step). Every image must be at the mixer's rate R, where it steps one frame per
 *       frame, or for snow and sand R / 2 when their files have that rate, or for the thunder and the gusts, which
 *       it steps at a random pitch, the least multiple of R that is 44,100 Hz or more (sounds.c). Exit code: the
 *       number of failed checks.
 *       Faults, each followed by the retry the DLL makes a minute later, which must then build everything, the
 *       same bytes as a clean run (--hashes FILE holds every image against the HASH lines a clean run printed):
 *         --fail-nth K         refuses the K-th memory request (a clean run prints how many there are, ALLOCS),
 *                              as VirtualAlloc would in a crowded 32-bit address space; swept over every K, it
 *                              walks each allocation site of the reader and the recipes through its failure path
 *         --open-error A[:N]   the next N opens of archive A fail as when another program holds it (the DLL would
 *                              log Windows error 32, a sharing violation); N 0 or left out: every open until the retry
 *         --read-error A[:N]   the next N reads inside archive A come back short (Windows error 33)
 *       A fault must cost only the sounds that met it, as "tried again in a minute", never "failed for the
 *       session", never built from another archive's copy in the meantime (with --expect), and the archive's
 *       trouble is logged once.
 *         --fail-from K        refuses every memory request from the K-th on, as a client at the end of its address
 *                              space would: each sound that meets it must be tried BUILD_NOMEM_TRIES times in all, a
 *                              minute apart, then given up for the session, logged once more, and not read again
 *         --unfit FILE         hands the build, in place of the client file FILE (a base name: RainLightLoop.wav), a
 *                              WAV the recipes were not made for, 64 MB of 8-bit mono at 8 kHz, which would decode to
 *                              256 MB of samples: its sounds must fail for the session at once, logged once, with no
 *                              memory given to its samples, and the file must not be read again
 *       --fpu-clobber has the Ogg decode (FMOD's place) leave the x87 control word at single precision, as an FMOD
 *       call on the DLL's thread may, and starts the build from extended precision: every sound's arithmetic must
 *       still run at 0x27F and come out the same bytes. --no-write writes no images.
 *   ownsounds_selftest settings <Data folder> <out folder>
 *       What the settings let the DLL build: wanted_sounds over every combination of its switches, then builds on
 *       the client's archives as the worker makes them when the mod, the swap or the storm comes on, or a test
 *       strike waits for its thunder; nothing that cannot play is built. Writes nothing but the Ogg copies.
 *   ownsounds_selftest rates <Data folder> <out folder> --mix-rate R
 *       Each of the 25 built for a mixer at R against the same sound built for 44,100 Hz and taken to its rate: each
 *       third of an octave within 1 dB (cmd_rates says how), since every recipe runs where the scripts ran it,
 *       whatever the mixer's rate. The thunder and the gusts also as the mixer plays them, at every pitch the DLL
 *       gives them (as_played): the mixer's straight line may dull them no more than 1 dB further, third by third,
 *       than it does the 44.1 kHz build played on the same mixer, nor make images or aliases more than 1 dB louder
 *       than that build's (or else under -60 dB of the whole). Writes nothing but the Ogg copies.
 *   ownsounds_selftest limits
 *       snd_wav_decode and snd_prepare at the edges of what a source may be (sounds.h: 90 s, 32 MB as samples, 8 to
 *       96 kHz), a frame each side of each edge; a refusal must allocate nothing. snd_build must refuse a source
 *       snd_prepare has not made ready. snd_ogg_probe on Ogg pages made here: it must take a stream that tells its
 *       length and refuse each way a file could hand FMOD more than it says (a second stream, bytes after the end,
 *       a page whose checksum is wrong, and the rest). Then the output guard (files out) on a work tree made in
 *       $TMPDIR (or /tmp), removed after. Needs no client files.
 *   ownsounds_selftest inventory <Data folder>
 *       Every .mpq in the folder asked for each of the 17 files: which archives hold one, at what size and with
 *       what checksum, next to the one the DLL takes. The DLL ignores the client's patch order on purpose; this
 *       shows what that order would have given.
 * The archives are only ever opened read-only. Everything written goes under <out>, and those files are copies
 * of the client's own audio: point <out> at a scratch folder, never at a game folder. An <out> that lands inside a
 * git work tree is refused, as the scripts in tools/ refuse theirs, since a commit there could pick the copies up,
 * and so is any file or folder that would land in one on the way (files out, below).
 */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mpq.h"
#include "dsp.h"
#include "sounds.h"

static int g_failed = 0;
static void check(int ok, const char *what, const char *detail)
{
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail && detail[0] ? ": " : "", detail ? detail : "");
    if (!ok) g_failed++;
}
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }

/* ---- the x87 control word (--fpu-clobber) ---- */
#if defined(__i386__)
static unsigned short cw_get(void) { unsigned short w; __asm__ __volatile__ ("fnstcw %0" : "=m" (w)); return w; }
static void cw_set(unsigned short w) { __asm__ __volatile__ ("fldcw %0" : : "m" (w)); }
#else
static unsigned short cw_get(void) { return 0x027F; }
static void cw_set(unsigned short w) { (void)w; }
#endif
static int g_in_dsp = 0;                     /* set by build_one and build_source around the DLL's arithmetic */
static unsigned long g_dsp_allocs = 0, g_dsp_cw_bad = 0;

/* ---- memory: the DLL's ir_alloc, counted the same way ----
 * ir_alloc keeps each block's size in a 16-byte header so the log can say what the sounds hold and what a build
 * needed at its peak; this does the same over malloc, so the two figures mean the same thing here as there. Every
 * request made inside a sound's arithmetic also notes the control word it ran under. */
static size_t g_now = 0, g_peak = 0;
static unsigned long g_blocks = 0;
static unsigned long g_alloc_calls = 0, g_fail_nth = 0, g_fail_from = 0;
static unsigned int g_largest = 0;   /* the largest request made, granted or not */
static void *t_alloc(void *ctx, unsigned int n)
{
    size_t *p;
    (void)ctx;
    if (g_in_dsp) { g_dsp_allocs++; if (cw_get() != 0x027F) g_dsp_cw_bad++; }
    if (n > g_largest) g_largest = n;
    if (++g_alloc_calls == g_fail_nth || (g_fail_from && g_alloc_calls >= g_fail_from)) return NULL;
    if (n > 0x7FFF0000u) return NULL;   /* ir_alloc's own limit */
    if (!(p = malloc((size_t)n + 16u))) return NULL;
    p[0] = n; g_now += n; g_blocks++;
    if (g_now > g_peak) g_peak = g_now;
    return (char *)p + 16;
}
static void t_release(void *ctx, void *q)
{
    size_t *p;
    (void)ctx;
    if (!q) return;
    p = (size_t *)((char *)q - 16);
    g_now -= p[0]; g_blocks--;
    free(p);
}
static const dsp_alloc_t A = { t_alloc, t_release, NULL };
static double mb(size_t n) { return n / 1048576.0; }

/* ---- faults to inject (--open-error, --read-error) ---- */
typedef struct { char archive[64]; long left; int code; unsigned long hits; } fault_t;
static fault_t g_open_fault, g_read_fault;
#define T_SHARING_VIOLATION 32   /* the Windows errors the DLL would log for them */
#define T_LOCK_VIOLATION    33
#define T_HANDLE_EOF        38
static int fault_hit(fault_t *f, const char *archive)
{
    if (!f->archive[0] || !f->left || strcasecmp(f->archive, archive)) return 0;
    if (f->left > 0) f->left--;
    f->hits++;
    return 1;
}

/* ---- the archives: the DLL's io_open, io_read_at, io_size and io_close over POSIX calls ----
 * Read-only, and closed after each file, as there. An archive not found under the name sounds.c spells is looked
 * for in the folder without regard to case, as io_open does with FindFirstFileW (ASCII case only, like wname_is).
 * g_io_trouble is the DLL's: what went wrong other than "no such file", for source_read. */
static struct { int err; const char *op; } g_io_trouble;
static void io_trouble(const char *op, int err) { g_io_trouble.err = err ? err : -1; g_io_trouble.op = op; }
static int not_there(int err) { return err == ENOENT || err == ENOTDIR; }
typedef struct { int fd; const char *archive; } fh_t;
static char g_data[4096];
static unsigned long g_opens = 0, g_open_fallbacks = 0;
static void *io_open(void *ctx, const char *archive)
{
    char path[8192]; int fd, err = 0; DIR *d; struct dirent *e; fh_t *h;
    (void)ctx;
    if (fault_hit(&g_open_fault, archive)) { io_trouble("open", g_open_fault.code); return NULL; }
    snprintf(path, sizeof path, "%s/%s", g_data, archive);
    fd = open(path, O_RDONLY);
    if (fd < 0 && not_there(err = errno)) {
        err = ENOENT;
        if ((d = opendir(g_data))) {
            while ((e = readdir(d))) {
                struct stat st;
                snprintf(path, sizeof path, "%s/%s", g_data, e->d_name);
                if (strcasecmp(e->d_name, archive) == 0 && stat(path, &st) == 0 && !S_ISDIR(st.st_mode)) {
                    if ((fd = open(path, O_RDONLY)) < 0) err = errno;
                    g_open_fallbacks++;
                    break;
                }
            }
            closedir(d);
        }
    }
    if (fd < 0) {
        if (!not_there(err)) io_trouble("open", err);
        return NULL;
    }
    if (!(h = malloc(sizeof *h))) { close(fd); return NULL; }
    h->fd = fd; h->archive = archive; g_opens++;
    return h;
}
static unsigned io_read_at(void *ctx, void *fh, unsigned long long off, void *buf, unsigned len)
{
    unsigned done = 0;
    (void)ctx;
    while (done < len) {
        ssize_t got;
        if (fault_hit(&g_read_fault, ((fh_t *)fh)->archive)) { io_trouble("read", g_read_fault.code); break; }
        got = pread(((fh_t *)fh)->fd, (char *)buf + done, len - done, (off_t)(off + done));
        if (got < 0) { io_trouble("read", errno); break; }
        if (got == 0) { io_trouble("read", T_HANDLE_EOF); break; }
        done += (unsigned)got;
    }
    return done;
}
static unsigned long long io_size(void *ctx, void *fh)
{
    struct stat st;
    (void)ctx;
    if (fstat(((fh_t *)fh)->fd, &st) != 0) { io_trouble("size", errno); return 0ull; }
    return (unsigned long long)st.st_size;
}
static void io_close(void *ctx, void *fh) { (void)ctx; close(((fh_t *)fh)->fd); free(fh); }
static const mpq_io g_io = { t_alloc, t_release, io_open, io_read_at, io_size, io_close, NULL };

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
/* the folders of path, made; refused before any is made when path lands inside a git work tree */
static int mkdirs(const char *path)
{
    char p[8192]; size_t i, n = strlen(path);
    if (n >= sizeof p) return 0;
    if (in_git_work_tree(path)) { fprintf(stderr, "refusing to make %s: it lands inside a git work tree\n", path); return 0; }
    memcpy(p, path, n + 1);
    for (i = 1; i <= n; i++)
        if (p[i] == '/' || p[i] == 0) { char c = p[i]; p[i] = 0; if (mkdir(p, 0755) != 0) { struct stat st; if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) return 0; } p[i] = c; }
    return 1;
}
static int write_file(const char *path, const void *d, size_t n)
{
    FILE *f = create_out(path);
    if (!f) return 0;
    if (fwrite(d, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}
static const char *base_name(const char *path) { const char *b = path; for (; *path; path++) if (*path == '\\' || *path == '/') b = path + 1; return b; }
static unsigned long long fnv1a(const unsigned char *p, unsigned n) { unsigned long long h = 1469598103934665603ull; while (n--) { h ^= *p++; h *= 1099511628211ull; } return h; }

/* ---- the gusts ----
 * What the Ogg file says of itself is snd_ogg_probe's word (sounds.c, the DLL's own, compiled as it is): its channels,
 * its rate and its length in frames, held to snd_source_ok before anything decodes it. The DLL checks what FMOD
 * decoded against it; this checks what ffmpeg decoded against it the same way. */
static unsigned rd_le32(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
/* ffmpeg in FMOD's place: the Ogg bytes as read go to a file, ffmpeg decodes it to 16-bit PCM (FMOD hands the
   DLL 16-bit samples), and the samples come back as the DLL copies them out of FMOD's sample, v / 32768. Then the
   DLL's checks: the rate and channels the file names, a length near its granule (trimmed to it), not silent.
   With --fpu-clobber it leaves the x87 control word at single precision (0x007F), as an FMOD call may.
   The decode comes back through a pipe, as a WAV whose sizes ffmpeg leaves unknown (dsp_wav_decode takes the data
   there is), never through a file: ffmpeg -y writes through a link at a file's name, a hard link as much as a symlink
   (the fourth 0.14 review), and nothing of the decode needs keeping. */
static char g_out[4096];
static int g_fpu_clobber = 0;
static unsigned char *run_ffmpeg(const char *in, unsigned *size)
{
    int fds[2], st = 0, ok = 1; pid_t pid; unsigned char *d = NULL, *more; size_t n = 0, cap = 0, want; ssize_t got;
    if (pipe(fds) != 0) return NULL;
    if ((pid = fork()) < 0) { close(fds[0]); close(fds[1]); return NULL; }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], 1) < 0) _exit(127);
        close(fds[1]);
        execlp("ffmpeg", "ffmpeg", "-nostdin", "-hide_banner", "-v", "error", "-i", in, "-map", "0:a:0", "-map_metadata", "-1",
               "-c:a", "pcm_s16le", "-f", "wav", "pipe:1", (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {   /* a lack of memory or a failed read stops it, and ffmpeg then fails on its broken pipe */
        if (n == cap) {
            want = cap ? 2 * cap : (size_t)1 << 20;
            if (want > 0xFFFFFFFFu || !(more = realloc(d, want))) { ok = 0; break; }
            d = more; cap = want;
        }
        got = read(fds[0], d + n, cap - n);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { ok = 0; break; }
        if (got == 0) break;
        n += (size_t)got;
    }
    close(fds[0]);
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st) || WEXITSTATUS(st) != 0 || !ok) { free(d); return NULL; }
    *size = (unsigned)n;
    return d;
}
static int ogg_decode(const char *archive, const char *path, const unsigned char *ogg, unsigned n, dsp_buf_t *out, int *peak_db, char *why, size_t cap)
{
    char dir[8192], copy[8300]; int ch = 0, rate = 0, r, peak = 0; unsigned granule = 0, size = 0, i; unsigned char *d;
    const char *not_ogg;
    out->s = NULL; out->frames = 0; out->owned = 0; *peak_db = -99;
    if ((not_ogg = snd_ogg_probe(ogg, n, &ch, &rate, &granule))) { snprintf(why, cap, "not an Ogg Vorbis file this can read: %s", not_ogg); return -20; }
    if (!granule) { snprintf(why, cap, "an Ogg Vorbis file that does not say its own length"); return -20; }
    if (!snd_source_ok(granule, ch, rate)) { snprintf(why, cap, "%s", snd_why(SND_UNFIT)); return SND_UNFIT; }
    /* the paths are used only when they fit whole: a cut one could name some other file */
    if (snprintf(dir, sizeof dir, "%s/src/%s", g_out, archive) >= (int)sizeof dir || snprintf(copy, sizeof copy, "%s/%s", dir, base_name(path)) >= (int)sizeof copy) {
        snprintf(why, cap, "the output folder's name is too long");
        return -20;
    }
    if (!mkdirs(dir) || !write_file(copy, ogg, n)) { snprintf(why, cap, "cannot write the copy of %s", base_name(path)); return -20; }
    if (!(d = run_ffmpeg(copy, &size))) { snprintf(why, cap, "ffmpeg could not decode it"); return -20; }
    r = dsp_wav_decode(out, d, size, &A);   /* 16-bit: (float)(short)v * (1 / 32768), the DLL's own conversion */
    free(d);
    if (g_fpu_clobber) cw_set(0x007F);
    if (r != DSP_OK) { snprintf(why, cap, "the decoded copy: %s", snd_why(r)); return r; }
    for (i = 0; i < (unsigned)(out->frames * out->channels); i++) { int v = (int)(out->s[i] * 32768.0f), m = v < 0 ? -v : v; if (m > peak) peak = m; }
    if (out->rate != rate || out->channels != ch) { snprintf(why, cap, "decoded at %d Hz, %d ch; the file says %d Hz, %d ch", out->rate, out->channels, rate, ch); dsp_free(out, &A); return -20; }
    if (granule && ((unsigned)out->frames + 8192u < granule || (unsigned)out->frames > granule + 65536u)) { snprintf(why, cap, "decoded %d frames, the file says %u", out->frames, granule); dsp_free(out, &A); return -20; }
    if (peak <= 33) { snprintf(why, cap, "decoded to silence"); dsp_free(out, &A); return -20; }
    if (granule && (unsigned)out->frames > granule) out->frames = (int)granule;
    *peak_db = (int)(20.0 * dsp_log10((double)peak / 32768.0) - 0.5);
    return DSP_OK;
}

/* ---- the builds: indoorrain.c's source_read, build_one, build_source, sounds_build, wanted_sounds, sounds_upkeep ---- */
#define BUILD_NONE   0
#define BUILD_READY  1
#define BUILD_FAILED (-1)
#define BUILD_LATER  (-2)
#define BUILD_NOMEM_TRIES 3
typedef struct { void *data; unsigned size; int state; int later_logged; int nomem_tries; int gave_up; } built_t;
static built_t g_snd[SND_COUNT];
static int g_reads[SRC_COUNT], g_decodes[SRC_COUNT], g_which[SRC_COUNT], g_which_mask[SRC_COUNT];
static unsigned g_src_len[SRC_COUNT];
static int g_src_frames[SRC_COUNT], g_src_rate[SRC_COUNT], g_src_ch[SRC_COUNT], g_src_variant[SRC_COUNT];
static int g_dump_sources = 0, g_write_images = 1;
static const char *g_hashes = NULL;   /* --hashes FILE: "HASH <name> <fnv>" lines of a clean run, to hold each image against */

/* trouble_first: one line per archive in a session for trouble other than "no such file" */
static const char *g_trouble_said[8];
static int g_trouble_n = 0, g_trouble_lines = 0;
static int trouble_first(const char *archive)
{
    int i;
    for (i = 0; i < g_trouble_n; i++) if (!strcasecmp(g_trouble_said[i], archive)) return 0;
    if (g_trouble_n < 8) g_trouble_said[g_trouble_n++] = archive;
    return 1;
}
#define SRC_FOUND   1
#define SRC_NOWHERE 0
#define SRC_LATER   (-1)
#define SRC_BROKEN  (-2)
#define SRC_NOMEM   (-3)
/* --unfit: the client file handed over, in place of what the archive holds, as a WAV the recipes were not made for:
   64 MB of 8-bit mono at 8 kHz, 8,388 s, which would decode to 256 MB of samples (0.14 review) */
static const char *g_unfit = NULL;
static unsigned long g_unfit_reads = 0, g_unfit_lines = 0;
static int unfit_wav(unsigned char **bytes, unsigned *len)
{
    const unsigned n = 64u * 1024u * 1024u - 44u; unsigned i; unsigned char *p = (unsigned char *)t_alloc(NULL, n + 44u);
    if (!p) return MPQ_E_NOMEM;
    memcpy(p, "RIFF", 4); p[4] = (unsigned char)(36u + n); p[5] = (unsigned char)((36u + n) >> 8); p[6] = (unsigned char)((36u + n) >> 16); p[7] = (unsigned char)((36u + n) >> 24);
    memcpy(p + 8, "WAVEfmt ", 8); p[16] = 16; p[17] = p[18] = p[19] = 0; p[20] = 1; p[21] = 0; p[22] = 1; p[23] = 0;
    p[24] = 0x40; p[25] = 0x1F; p[26] = p[27] = 0; p[28] = 0x40; p[29] = 0x1F; p[30] = p[31] = 0; p[32] = 1; p[33] = 0; p[34] = 8; p[35] = 0;   /* 8,000 Hz, 8 bits, mono */
    memcpy(p + 36, "data", 4); p[40] = (unsigned char)n; p[41] = (unsigned char)(n >> 8); p[42] = (unsigned char)(n >> 16); p[43] = (unsigned char)(n >> 24);
    for (i = 0; i < n; i++) p[44 + i] = (unsigned char)(120u + ((i * 2654435761u) >> 28));   /* quiet noise */
    *bytes = p; *len = n + 44u;
    return MPQ_OK;
}
static int source_read(int src, unsigned char **bytes, unsigned *len, int *which, char *tried, size_t cap)
{
    const snd_source_t *s = &SND_SOURCES[src]; int w, r; char part[240];
    for (w = 0; w < SND_WHERE_MAX && s->where[w].archive; w++) {
        const char *arch = s->where[w].archive;
        g_reads[src]++;
        g_io_trouble.err = 0;
        if (g_unfit && !strcasecmp(base_name(s->path), g_unfit)) { g_unfit_reads++; r = unfit_wav(bytes, len); }
        else r = mpq_read(&g_io, arch, s->path, bytes, len);
        if (r == MPQ_OK && (!s->where[w].size || *len == s->where[w].size)) { *which = w; g_which_mask[src] |= 1 << w; return SRC_FOUND; }
        if (r == MPQ_OK) {   /* a changed file: a recipe cut at fixed times must never run on it */
            snprintf(part, sizeof part, "%s%s holds it at %u bytes, not the %u the recipe was made for", tried[0] ? "; " : "", arch, *len, s->where[w].size);
            t_release(NULL, *bytes); *bytes = NULL; *len = 0;
            strncat(tried, part, cap - strlen(tried) - 1);
            continue;
        }
        if (g_io_trouble.err) {   /* there, but not openable or readable now: whatever mpq.c made of it */
            snprintf(part, sizeof part, "%s%s: could not %s it (error %d)", tried[0] ? "; " : "", arch, g_io_trouble.op, g_io_trouble.err);
            strncat(tried, part, cap - strlen(tried) - 1);
            if (trouble_first(arch)) {
                g_trouble_lines++;
                printf("  cannot %s %s in the Data folder just now (error %d); the sounds read from it are tried again every minute\n", g_io_trouble.op, arch, g_io_trouble.err);
            }
            return SRC_LATER;
        }
        snprintf(part, sizeof part, "%s%s: %s", tried[0] ? "; " : "", arch, mpq_error_text(r));
        strncat(tried, part, cap - strlen(tried) - 1);
        if (r == MPQ_E_NO_ARCHIVE || r == MPQ_E_NOT_FOUND || r == MPQ_E_EMPTY) continue;
        return r == MPQ_E_NOMEM ? SRC_NOMEM : r == MPQ_E_IO ? SRC_LATER : SRC_BROKEN;
    }
    return SRC_NOWHERE;
}
#define WHY_FINAL   0
#define WHY_ARCHIVE 1
#define WHY_MEMORY  2
static const char *build_failed(int out, int why)
{
    built_t *b = &g_snd[out];
    if (why == WHY_MEMORY && ++b->nomem_tries >= BUILD_NOMEM_TRIES) {
        b->state = BUILD_FAILED; b->gave_up++;
        return "; out of memory 3 times a minute apart, so not tried again this session";
    }
    if (why == WHY_FINAL) { b->state = BUILD_FAILED; return ""; }
    b->state = BUILD_LATER;
    if (b->later_logged) return NULL;
    b->later_logged = 1;
    return "; trying again in a minute";
}
/* fpu_enter and fpu_leave: the DLL's, around each sound's arithmetic (on the 64-bit build they do nothing) */
static unsigned short fpu_enter(void)
{
    unsigned short was = cw_get();
    cw_set(0x027F);
    return was;
}
static void fpu_leave(unsigned short was)
{
#if defined(__i386__)
    __asm__ __volatile__ ("fnclex");
#endif
    cw_set(was);
}
static int build_one(int out, const snd_input_t *in, const char *archive)
{
    void *wav; unsigned size; int r, ch; const unsigned char *h; double t0 = now_ms(); unsigned short cw; const char *tail;
    cw = fpu_enter();
    g_in_dsp = 1;
    r = snd_build(out, in, &wav, &size, &A);
    g_in_dsp = 0;
    fpu_leave(cw);
    if (r != DSP_OK) {
        if ((tail = build_failed(out, r == DSP_NOMEM ? WHY_MEMORY : WHY_FINAL))) printf("  cannot build %s: %s%s\n", SND_OUTPUTS[out].name, snd_why(r), tail);
        return 0;
    }
    h = (const unsigned char *)wav;
    ch = h[22] | h[23] << 8;
    if (ch < 1) ch = 1;
    g_snd[out].data = wav; g_snd[out].size = size; g_snd[out].state = BUILD_READY;
    printf("  built %s from %s: %u frames at %u Hz, %d ch, %u bytes, in %.1f ms\n", SND_OUTPUTS[out].name, archive,
           (size - 44u) / (2u * (unsigned)ch), rd_le32(h + 24), ch, size, now_ms() - t0);
    return 1;
}
static int g_mix = 44100;   /* --mix-rate: the DLL's g_mix_rate, FSOUND_GetOutputRate held to SND_MIX_MIN..MAX */
static int build_source(int src, const int *outs, int n)
{
    const snd_source_t *s = &SND_SOURCES[src];
    snd_input_t in; unsigned char *bytes = NULL; unsigned len = 0;
    int w = -1, r = DSP_OK, i, built = 0, peak_db = 0, found, why_kind, rate0 = 0, frames0 = 0;
    char tried[480], why[320], names[200]; double t0 = now_ms(); const char *tail = NULL, *t;
    tried[0] = 0; why[0] = 0; names[0] = 0;
    for (i = 0; i < n; i++) { if (i) strcat(names, ", "); strcat(names, SND_OUTPUTS[outs[i]].name); }
    in.pcm.s = NULL; in.pcm.frames = 0; in.pcm.channels = 0; in.pcm.rate = 0; in.pcm.owned = 0; in.variant = SND_PLAIN; in.mix_rate = 0;
    found = source_read(src, &bytes, &len, &w, tried, sizeof tried);
    if (found == SRC_FOUND) {
        in.variant = s->where[w].variant;
        g_which[src] = w; g_src_len[src] = len; g_src_variant[src] = in.variant;
        if (g_dump_sources && s->format == SND_WAV) {
            char dir[8192], file[8400];
            snprintf(dir, sizeof dir, "%s/src/%s", g_out, s->where[w].archive);
            snprintf(file, sizeof file, "%s/%s", dir, base_name(s->path));
            if (!mkdirs(dir) || !write_file(file, bytes, len)) printf("  (could not write %s)\n", file);
        }
        g_decodes[src]++;
        if (s->format == SND_OGG) r = ogg_decode(s->where[w].archive, s->path, bytes, len, &in.pcm, &peak_db, why, sizeof why);
        else {
            unsigned short cw = fpu_enter();
            g_in_dsp = 1;
            r = snd_wav_decode(bytes, len, &in.pcm, &A);
            g_in_dsp = 0;
            fpu_leave(cw);
            if (r != DSP_OK) snprintf(why, sizeof why, "%s", snd_why(r));
        }
        t_release(NULL, bytes);
        rate0 = in.pcm.rate; frames0 = in.pcm.frames;   /* the decoded source, before the heavy splice */
        if (r == DSP_OK) {
            unsigned short cw = fpu_enter();
            g_in_dsp = 1;
            r = snd_prepare(src, &in, g_mix, &A);
            g_in_dsp = 0;
            fpu_leave(cw);
            if (r != DSP_OK) { snprintf(why, sizeof why, "%s", snd_why(r)); dsp_free(&in.pcm, &A); }
        }
    } else snprintf(why, sizeof why, "%s", found == SRC_LATER ? "its archive could not be read just now" : found == SRC_NOMEM ? "out of memory reading it just now"
                                            : found == SRC_BROKEN ? "the first archive that holds it could not give it" : "not in the client's archives as the recipe needs it");
    if (found != SRC_FOUND || r != DSP_OK) {
        why_kind = found == SRC_NOMEM || r == DSP_NOMEM ? WHY_MEMORY : found == SRC_LATER ? WHY_ARCHIVE : WHY_FINAL;
        for (i = 0; i < n; i++) if ((t = build_failed(outs[i], why_kind)) && !tail) tail = t;
        if (tail) printf("  cannot build %s from %s: %s%s%s%s%s%s\n", names, s->path, why, tried[0] ? " (" : "", tried, tried[0] ? ")" : "", tail,
                         why_kind == WHY_FINAL && s->optional ? "; the storm plays only the gusts that were built" : "");
        if (tail && r == SND_UNFIT) g_unfit_lines++;
        return 0;
    }
    g_src_frames[src] = frames0; g_src_rate[src] = rate0; g_src_ch[src] = in.pcm.channels;
    if (s->format == SND_OGG)
        printf("  read %s from %s: %u bytes, decoded by ffmpeg (FMOD in the DLL): %d frames at %d Hz, %d ch, peak %d dBFS, in %.1f ms\n",
               s->path, s->where[w].archive, len, frames0, rate0, in.pcm.channels, peak_db, now_ms() - t0);
    else printf("  read %s from %s: %u bytes, %d frames at %d Hz, %d ch, in %.1f ms%s%s\n", s->path, s->where[w].archive, len,
                frames0, rate0, in.pcm.channels, now_ms() - t0, tried[0] ? "; passed over: " : "", tried);
    for (i = 0; i < n; i++) built += build_one(outs[i], &in, s->where[w].archive);
    dsp_free(&in.pcm, &A);
    return built;
}
/* the DLL tries a build that had to wait again a minute later; g_retry_due stands for that minute */
static int g_retry_due = 0;
static int wants_build(int out) { return g_snd[out].state == BUILD_NONE || (g_snd[out].state == BUILD_LATER && g_retry_due); }
static int sounds_build(const int *list, int n)
{
    int i, j, k, m, built = 0, outs[SND_COUNT]; unsigned done = 0;
    for (i = 0; i < n; i++) {
        int src = SND_OUTPUTS[list[i]].source;
        if ((done & (1u << src)) || !wants_build(list[i])) continue;
        done |= 1u << src;
        for (m = 0, j = i; j < n; j++) {
            if (SND_OUTPUTS[list[j]].source != src || !wants_build(list[j])) continue;
            for (k = 0; k < m && outs[k] != list[j]; k++) ;
            if (k == m) outs[m++] = list[j];
        }
        built += build_source(src, outs, m);
    }
    return built;
}
/* wanted_sounds, with every rain track at its default (builtin: indoor rain light, medium, heavy) */
static int wanted_sounds(int *list, int enabled, int outdoor, int thunder, int gusts)
{
    int n = 0, l, id, i;
    if (!enabled) return 0;
    for (l = 3; l >= 1; l--) {
        if (outdoor) list[n++] = SND_OUTDOOR_RAIN_LIGHT + l - 1;
        id = SND_INDOOR_RAIN_LIGHT + l - 1;
        for (i = 0; i < n && list[i] != id; i++) ;
        if (i == n) list[n++] = id;
    }
    if (thunder) for (id = SND_THUNDER_CLOSE; id <= SND_THUNDER_FAR2_IN; id++) list[n++] = id;
    if (gusts) for (id = SND_GUST_1; id <= SND_GUST_3; id++) list[n++] = id;
    return n;
}
/* sounds_upkeep: indoor is the loop the weather wants indoors now (-1 for none); pending, a strike waiting for its thunder */
static int sounds_upkeep(int enabled, int outdoor, int storms, int pending, int indoor)
{
    int list[SND_COUNT + 1], n = wanted_sounds(list, enabled, outdoor, storms || pending, storms), i, any = 0;
    if (indoor >= 0 && enabled) list[n++] = indoor;
    for (i = 0; i < n && !any; i++) any = wants_build(list[i]);
    return any ? sounds_build(list, n) : 0;
}

/* ---- what the built images must be ---- */
typedef struct { unsigned frames; int ch, rate, peak, full; } img_t;
static int parse_image(int out, img_t *im, char *why, size_t cap)
{
    const unsigned char *h = (const unsigned char *)g_snd[out].data; unsigned size = g_snd[out].size, data, i, n;
    memset(im, 0, sizeof *im);
    if (size < 46) { snprintf(why, cap, "only %u bytes", size); return 0; }
    data = rd_le32(h + 40);
    im->ch = h[22] | h[23] << 8; im->rate = (int)rd_le32(h + 24);
    if (memcmp(h, "RIFF", 4) || rd_le32(h + 4) != size - 8 || memcmp(h + 8, "WAVEfmt ", 8) || rd_le32(h + 16) != 16 || (h[20] | h[21] << 8) != 1
        || (h[34] | h[35] << 8) != 16 || im->ch < 1 || im->ch > 2 || rd_le32(h + 28) != (unsigned)im->rate * (unsigned)im->ch * 2u
        || (h[32] | h[33] << 8) != im->ch * 2 || memcmp(h + 36, "data", 4) || data != size - 44u || data % (unsigned)(im->ch * 2)) {
        snprintf(why, cap, "the 44-byte header does not describe a 16-bit PCM WAV of exactly its data");
        return 0;
    }
    im->frames = data / (unsigned)(im->ch * 2);
    for (n = data / 2u, i = 0; i < n; i++) {
        int v = (short)(h[44 + 2 * i] | h[45 + 2 * i] << 8), m = v < 0 ? -v : v;
        if (m > im->peak) im->peak = m;
        if (v >= 32767 || v <= -32768) im->full++;
    }
    return 1;
}
/* the ceiling the recipe's last limiter holds, in 16-bit steps (dsp_limit clamps at it; s16 rounding can add one) */
static int ceiling_of(int out)
{
    double c;
    if (out <= SND_OUTDOOR_RAIN_HEAVY) c = 0.89;
    else if (out <= SND_INDOOR_RAIN_HEAVY) c = dsp_db2lin(-1.5);
    else if (out <= SND_INDOOR_SAND_HEAVY) return 32767;   /* a plain low-pass: no limiter */
    else if (out <= SND_THUNDER_FAR2) c = 0.89;
    else if (out <= SND_THUNDER_FAR2_IN) c = dsp_db2lin(-2.0);
    else c = dsp_db2lin(-2.0);
    return (int)(c * 32768.0 + 0.5) + 1;
}
/* The rate each image must have, from the rule rather than from sounds.c: the one the mixer steps one frame per frame,
   its own, since it holds each frame of a slower stereo voice instead of interpolating (the 0.14 review heard the
   muffled rain at 11,025 Hz come back with a hiss); snow and sand at half of it when their files have that rate, as
   0.13 shipped them and as they were heard and approved; and the thunder and the gusts, which the DLL plays at a
   random pitch, so that the mixer draws a line between their frames, at the least multiple of the mixer's rate that
   is 44,100 Hz or more: at a slower mixer's own rate the line dulled and imaged them (the fourth 0.14 review; rates
   checks them as played). */
static int rate_for(int out, int src_rate, int mix)
{
    if (out >= SND_INDOOR_SNOW_LIGHT && out <= SND_INDOOR_SAND_HEAVY && 2 * src_rate == mix) return src_rate;
    if (out >= SND_THUNDER_CLOSE) { int m = 1; while (m * mix < 44100) m++; return m * mix; }
    return mix;
}

static void reset_builds(void)
{
    int i;
    for (i = 0; i < SND_COUNT; i++) { if (g_snd[i].state == BUILD_READY) t_release(NULL, g_snd[i].data); memset(&g_snd[i], 0, sizeof g_snd[i]); }
    for (i = 0; i < SRC_COUNT; i++) { g_reads[i] = 0; g_decodes[i] = 0; g_which[i] = -1; g_which_mask[i] = 0; }
}

/* ---- build ---- */
static int cmd_build(const char *expect)
{
    static const char *const ARCH[] = { "patch.MPQ", "patch-S.mpq", "sound.MPQ" };
    int list[SND_COUNT], n, built_start, built_storm, i, gusts = 0, rc, faulted; char line[1024], detail[640];
    double t0, t_start, t_storm, t_lazy = 0.0;
    size_t held_start, peak_start, held_rain, peak_rain, peak_all = 0; struct rusage ru;
    int raven = expect && !strcmp(expect, "ravencraft"), nos = expect && !strcmp(expect, "no-patch-s");
    unsigned short cw_caller = 0x037F;
    faulted = g_fail_nth || g_open_fault.archive[0] || g_read_fault.archive[0];
    /* data_scan: which Data folder, and whether each archive the recipes read is there */
    line[0] = 0;
    for (i = 0; i < 3; i++) {
        void *h; char part[160];
        g_io_trouble.err = 0;
        if ((h = io_open(NULL, ARCH[i]))) { snprintf(part, sizeof part, "%s%s (%llu MB)", line[0] ? ", " : "", ARCH[i], io_size(NULL, h) >> 20); io_close(NULL, h); }
        else if (g_io_trouble.err) { trouble_first(ARCH[i]); g_trouble_lines++; snprintf(part, sizeof part, "%s%s cannot be opened just now (error %d)", line[0] ? ", " : "", ARCH[i], g_io_trouble.err); }
        else snprintf(part, sizeof part, "%s%s missing", line[0] ? ", " : "", ARCH[i]);
        strncat(line, part, sizeof line - strlen(line) - 1);
    }
    g_io_trouble.err = 0;
    printf("sound sources: the client's archives in %s: %s\n", g_data, line);
    if (g_fpu_clobber) cw_set(cw_caller);   /* the worker's word before the build, here extended precision, so a missed set shows */

    /* sounds_at_start: the ini's defaults (the mod on, outdoor_rain=1, every rain track builtin); no storm before the addon connects */
    n = wanted_sounds(list, 1, 1, 0, 0);
    g_peak = g_now;
    t0 = now_ms();
    built_start = sounds_build(list, n);
    t_start = now_ms() - t0;
    held_start = g_now; peak_start = g_peak;
    printf("sounds built at start: %d of %d in %.0f ms; %.1f MB held, %.1f MB at the build's peak\n", built_start, n, t_start, mb(g_now), mb(g_peak));
    /* sounds_upkeep: the addon connects with its storm switch on; the thunder and the gusts are built at that poll */
    g_peak = g_now;
    t0 = now_ms();
    built_storm = sounds_upkeep(1, 1, 1, 0, -1);
    t_storm = now_ms() - t0;
    for (i = SND_GUST_1; i <= SND_GUST_3; i++) gusts += g_snd[i].state == BUILD_READY;
    held_rain = g_now; peak_rain = peak_start > g_peak ? peak_start : g_peak;
    printf("sounds held now: %.1f MB (%.1f MB at that build's peak): the storm's %d sounds in %.0f ms, gusts %d of 3\n", mb(g_now), mb(g_peak), built_storm, t_storm, gusts);
    /* sounds_upkeep: the six snow and sand loops, each built the first time the weather wants it indoors */
    for (i = SND_INDOOR_SNOW_LIGHT; i <= SND_INDOOR_SAND_HEAVY; i++) {
        double t1 = now_ms();
        g_peak = g_now;
        if (sounds_upkeep(1, 1, 1, 0, i)) printf("sounds held now: %.1f MB (%.1f MB at that build's peak), %s in %.0f ms\n", mb(g_now), mb(g_peak), SND_OUTPUTS[i].name, now_ms() - t1);
        if (g_peak > peak_all) peak_all = g_peak;
        t_lazy += now_ms() - t1;
    }
    if (peak_rain > peak_all) peak_all = peak_rain;
    getrusage(RUSAGE_SELF, &ru);
    printf("TIME start %.0f ms, the storm's %.0f ms, rain set %.0f ms, six snow and sand loops %.0f ms, all 25 %.0f ms (%s build)\n",
           t_start, t_storm, t_start + t_storm, t_lazy, t_start + t_storm + t_lazy,
#if defined(__i386__)
           "32-bit x87"
#else
           "64-bit SSE"
#endif
    );
    printf("MEMORY start %.1f MB held, %.1f MB at its peak; rain set %.1f MB held, %.1f MB at its peak; all 25 %.1f MB held; highest build peak %.1f MB; process peak resident %.1f MB\n",
           mb(held_start), mb(peak_start), mb(held_rain), mb(peak_rain), mb(g_now), mb(peak_all), ru.ru_maxrss / 1024.0);
    printf("ALLOCS %lu requests\n", g_alloc_calls);
    if (g_fpu_clobber) {
        snprintf(detail, sizeof detail, "%lu requests made inside a sound's arithmetic, %lu of them under another control word than 0x27F", g_dsp_allocs, g_dsp_cw_bad);
        check(g_dsp_allocs > 0 && g_dsp_cw_bad == 0, "every sound's arithmetic ran at 0x27F, whatever the Ogg decode (FMOD) left", detail);
        /* the caller's word goes back after each sound: after the last gust, and the snow and sand after it, that is the one FMOD left */
        snprintf(detail, sizeof detail, "0x%04x after the builds (the build began at 0x%04x; the Ogg decode leaves 0x007f)", cw_get(), cw_caller);
        check(cw_get() == 0x007F, "the caller's control word is put back after each sound", detail);
    }
    /* A fault (a refused allocation, an archive another program holds, a short read) must cost only the sounds that
       met it, as "tried again in a minute", never as a failure for the session, and nothing may be built meanwhile
       from another archive's copy; the retry a minute later must then build everything, the same bytes as a run
       with no fault (--hashes). */
    if (faulted) {
        int all[SND_COUNT], later = 0, other = 0, s;
        for (i = 0; i < SND_COUNT; i++) { all[i] = i; if (g_snd[i].state == BUILD_LATER) later++; else if (g_snd[i].state != BUILD_READY) other++; }
        snprintf(detail, sizeof detail, "request %lu refused, %s opens failed %lu times, %s reads %lu times: %d sounds waiting to retry, %d failed for the session",
                 g_fail_nth, g_open_fault.archive[0] ? g_open_fault.archive : "no", g_open_fault.hits, g_read_fault.archive[0] ? g_read_fault.archive : "no", g_read_fault.hits, later, other);
        check(other == 0 && (later > 0 || (g_fail_nth > g_alloc_calls && !g_open_fault.hits && !g_read_fault.hits)), "a fault costs only a retry", detail);
        if (g_open_fault.hits || g_read_fault.hits) {
            snprintf(detail, sizeof detail, "%d lines for %s", g_trouble_lines, g_open_fault.hits ? g_open_fault.archive : g_read_fault.archive);
            check(g_trouble_lines == 1, "the archive's trouble is logged once", detail);
        }
        if (raven) for (s = 0; s < SRC_COUNT; s++) {
            snprintf(detail, sizeof detail, "%s: taken from where[] entries 0x%x", SND_SOURCES[s].path, (unsigned)g_which_mask[s]);
            check(g_which_mask[s] == 0 || g_which_mask[s] == 1, "nothing built from a lesser copy while the first archive could not give it", detail);
        }
        g_open_fault.left = 0; g_read_fault.left = 0; g_fail_nth = 0;   /* the minute passes, and the trouble with it */
        g_retry_due = 1;
        sounds_build(all, SND_COUNT);
        g_retry_due = 0;
        for (later = 0, i = 0; i < SND_COUNT; i++) later += g_snd[i].state != BUILD_READY && !(nos && i >= SND_GUST_1);
        snprintf(detail, sizeof detail, "%d sounds still not built", later);
        check(later == 0, "the retry builds what the fault cost", detail);
        if (raven) for (s = 0; s < SRC_COUNT; s++) {
            snprintf(detail, sizeof detail, "%s: taken from where[] entries 0x%x", SND_SOURCES[s].path, (unsigned)g_which_mask[s]);
            check(g_which_mask[s] == 1, "and from the first archive", detail);
        }
    }
    /* --fail-from: memory that stays short. Each sound that met it waits a minute and is tried again, BUILD_NOMEM_TRIES
       times in all, then is given up for the session, logged once more, and never read again (0.14 review: a lack of
       memory was tried again every minute for the whole session, each time reading and decoding its source). */
    if (g_fail_from) {
        int all[SND_COUNT], cycle, s, later, gone, twice, reads;
        for (i = 0; i < SND_COUNT; i++) all[i] = i;
        for (cycle = 1; cycle <= BUILD_NOMEM_TRIES; cycle++) {
            for (reads = 0, s = 0; s < SRC_COUNT; s++) reads -= g_reads[s];
            g_retry_due = 1; sounds_build(all, SND_COUNT); g_retry_due = 0;   /* a minute on, the memory still short */
            for (s = 0; s < SRC_COUNT; s++) reads += g_reads[s];
            for (later = 0, gone = 0, twice = 0, i = 0; i < SND_COUNT; i++) { later += g_snd[i].state == BUILD_LATER; gone += g_snd[i].gave_up == 1; twice += g_snd[i].gave_up > 1; }
            snprintf(detail, sizeof detail, "retry %d, a minute after the last: %d archive reads, %d sounds still waiting, %d given up (%d of them more than once)", cycle, reads, later, gone, twice);
            if (cycle < BUILD_NOMEM_TRIES - 1) check(reads > 0 && later > 0 && gone == 0, "memory that stays short: tried again a minute later", detail);
            else if (cycle == BUILD_NOMEM_TRIES - 1) check(later == 0 && gone > 0 && twice == 0, "and given up for the session after 3 tries in all, each sound once", detail);
            else check(reads == 0 && later == 0 && twice == 0, "and after that never read or tried again", detail);
        }
    }
    /* --unfit: a source the recipes were not made for fails for the session at once, logged once, and no memory goes
       to its samples: the largest request of the run is the file itself, never the 256 MB its samples would take */
    if (g_unfit) {
        int all[SND_COUNT], s, outs = 0, failed = 0; unsigned long reads;
        for (s = 0; s < SRC_COUNT && strcasecmp(base_name(SND_SOURCES[s].path), g_unfit); s++) ;
        for (i = 0; i < SND_COUNT; i++) if (s < SRC_COUNT && SND_OUTPUTS[i].source == s) { outs++; failed += g_snd[i].state == BUILD_FAILED; }
        snprintf(detail, sizeof detail, "%s: %d of %d sounds failed for the session, %lu line%s, the largest memory request %u bytes (the file: %u)", g_unfit,
                 failed, outs, g_unfit_lines, g_unfit_lines == 1 ? "" : "s", g_largest, 64u * 1024u * 1024u);
        check(outs > 0 && failed == outs && g_unfit_lines == 1 && g_largest <= 64u * 1024u * 1024u, "a source the recipes were not made for fails at once, and nothing is decoded", detail);
        for (i = 0; i < SND_COUNT; i++) all[i] = i;
        reads = g_unfit_reads;
        g_retry_due = 1; sounds_build(all, SND_COUNT); g_retry_due = 0;
        snprintf(detail, sizeof detail, "%lu reads of it before a retry, %lu after, %lu line%s", reads, g_unfit_reads, g_unfit_lines, g_unfit_lines == 1 ? "" : "s");
        check(g_unfit_reads == reads && g_unfit_lines == 1, "and it is not read again, or logged again", detail);
    }
    if (g_fpu_clobber) cw_set(0x027F);

    /* the images, written as the DLL holds them, then checked */
    for (i = 0; i < SND_COUNT; i++) {
        char path[8400];
        if (g_snd[i].state != BUILD_READY) continue;
        printf("HASH %s %016llx\n", SND_OUTPUTS[i].name, fnv1a((const unsigned char *)g_snd[i].data, g_snd[i].size));
        if (!g_write_images) continue;
        snprintf(path, sizeof path, "%s/%s.wav", g_out, SND_OUTPUTS[i].name);
        if (!write_file(path, g_snd[i].data, g_snd[i].size)) check(0, "write", path);
    }
    if (g_hashes) {
        FILE *f = fopen(g_hashes, "r"); char name[64]; unsigned long long want; int seen = 0, same = 0;
        while (f && fscanf(f, "HASH %63s %llx\n", name, &want) == 2)
            for (i = 0; i < SND_COUNT; i++)
                if (!strcmp(name, SND_OUTPUTS[i].name)) { seen++; same += g_snd[i].state == BUILD_READY && fnv1a((const unsigned char *)g_snd[i].data, g_snd[i].size) == want; }
        if (f) fclose(f);
        snprintf(detail, sizeof detail, "%d of %d images the same bytes as the clean run's", same, seen);
        check(seen > 0 && same == seen && (seen == SND_COUNT || nos), "images", detail);
    }
    printf("\n");
    /* every source read exactly once (one archive tried where the first choice holds it) and decoded once; a
       fault reads its source again on the retry, as the DLL does */
    for (i = 0; i < SRC_COUNT && !faulted; i++) {
        int want_reads = 1;
        if (nos && (i == SRC_RAIN_HEAVY || i == SRC_CALL_LIGHTNING)) want_reads = 2;   /* patch-S.mpq tried first, missing */
        if (!raven && !nos) want_reads = g_reads[i];
        snprintf(detail, sizeof detail, "%s: %d archive reads, %d decodes", SND_SOURCES[i].path, g_reads[i], g_decodes[i]);
        check(g_reads[i] == want_reads && g_decodes[i] <= 1, "each source read and decoded once", detail);
    }
    /* the archive each source came from, and its size, where the client is known */
    if (raven || nos) {
        static const struct { int src; const char *arch; unsigned size; int variant; } R[] = {
            { SRC_RAIN_LIGHT, "patch.MPQ", 1764044u, SND_PLAIN }, { SRC_RAIN_MEDIUM, "patch.MPQ", 2646044u, SND_PLAIN },
            { SRC_RAIN_HEAVY, "patch-S.mpq", 14419190u, SND_HEAVY_RAVENCRAFT }, { SRC_CALL_LIGHTNING, "patch-S.mpq", 154252u, SND_PLAIN },
            { SRC_SNOW_LIGHT, "patch.MPQ", 0, SND_PLAIN }, { SRC_SNOW_MEDIUM, "patch.MPQ", 0, SND_PLAIN }, { SRC_SNOW_HEAVY, "patch.MPQ", 0, SND_PLAIN },
            { SRC_SAND_LIGHT, "patch.MPQ", 0, SND_PLAIN }, { SRC_SAND_MEDIUM, "patch.MPQ", 0, SND_PLAIN }, { SRC_SAND_HEAVY, "patch.MPQ", 0, SND_PLAIN },
            { SRC_BOLT, "sound.MPQ", 0, SND_PLAIN }, { SRC_BOLT1, "sound.MPQ", 0, SND_PLAIN }, { SRC_BOLT2, "sound.MPQ", 0, SND_PLAIN },
            { SRC_BOLT3, "sound.MPQ", 0, SND_PLAIN }, { SRC_GUST1, "patch-S.mpq", 0, SND_PLAIN }, { SRC_GUST2, "patch-S.mpq", 0, SND_PLAIN },
            { SRC_GUST3, "patch-S.mpq", 0, SND_PLAIN } };
        for (i = 0; i < (int)(sizeof R / sizeof R[0]); i++) {
            int s = R[i].src; const char *arch = R[i].arch; unsigned size = R[i].size; int variant = R[i].variant, want_found = 1;
            if (nos && s == SRC_RAIN_HEAVY) { arch = "patch.MPQ"; size = 2205044u; variant = SND_PLAIN; }
            if (nos && s == SRC_CALL_LIGHTNING) { arch = "sound.MPQ"; size = 132344u; }
            if (nos && s >= SRC_GUST1) want_found = 0;
            if (!want_found) { snprintf(detail, sizeof detail, "%s", SND_SOURCES[s].path); check(g_decodes[s] == 0 || g_src_frames[s] == 0, "not found, as this client has none", detail); continue; }
            snprintf(detail, sizeof detail, "%s from %s at %u bytes, variant %d (want %s%s%u, variant %d)", SND_SOURCES[s].path,
                     g_which[s] >= 0 && g_decodes[s] ? SND_SOURCES[s].where[g_which[s]].archive : "(none)", g_src_len[s], g_src_variant[s],
                     arch, size ? " at " : "", size, variant);
            check(g_decodes[s] >= 1 && (faulted || g_decodes[s] == 1) && g_which[s] >= 0 && !strcmp(SND_SOURCES[s].where[g_which[s]].archive, arch)
                  && (!size || g_src_len[s] == size) && g_src_variant[s] == variant, "source archive", detail);
        }
    }
    /* each image: a 16-bit PCM WAV of exactly its data, at the rate the mixer steps one frame per frame (or for snow
       and sand half of it, as 0.13), mono for the one-shots, the length the recipe gives at that rate, under its
       limiter's ceiling, and nothing at full scale */
    for (i = 0; i < SND_COUNT; i++) {
        img_t im; char why[200]; int src = SND_OUTPUTS[i].source, ok, want_rate, want_ch; unsigned want_frames; unsigned long long base;
        if (g_snd[i].state != BUILD_READY) {
            int may_fail = nos && i >= SND_GUST_1;
            snprintf(detail, sizeof detail, "%s, state %d", SND_OUTPUTS[i].name, g_snd[i].state);
            check(may_fail ? g_snd[i].state == BUILD_FAILED : (!raven && !nos), may_fail ? "not built, and not tried again: this client has no such file" : "built", detail);
            continue;
        }
        if (!parse_image(i, &im, why, sizeof why)) { snprintf(detail, sizeof detail, "%s: %s", SND_OUTPUTS[i].name, why); check(0, "WAV image", detail); continue; }
        want_ch = SND_OUTPUTS[i].loop ? g_src_ch[src] : 1;
        base = (unsigned long long)g_src_frames[src];
        if (src == SRC_RAIN_HEAVY)   /* RavenCraft's: 19 + 18 - 1.5 + 11.5 s joined, then 1.5 s loopified; any other: 1.5 s loopified; at the source's rate */
            base = g_src_variant[src] == SND_HEAVY_RAVENCRAFT ? (unsigned long long)dsp_frames(g_src_rate[src], 45.5) : base - (unsigned long long)dsp_frames(g_src_rate[src], 1.5);
        want_rate = rate_for(i, g_src_rate[src], g_mix);
        /* the same length in time at the new rate: a loop to the nearest frame, a one-shot every frame that reaches
           into it; one at a multiple of the mixer's rate goes to the mixer's first, then up by that multiple */
        if (SND_OUTPUTS[i].loop) want_frames = (unsigned)((base * (unsigned)want_rate + (unsigned)g_src_rate[src] / 2u) / (unsigned)g_src_rate[src]);
        else want_frames = (unsigned)(want_rate / g_mix) * (unsigned)((base * (unsigned)g_mix + (unsigned)g_src_rate[src] - 1u) / (unsigned)g_src_rate[src]);
        ok = im.rate == want_rate && im.ch == want_ch && im.frames == want_frames;
        snprintf(detail, sizeof detail, "%s: %u frames at %d Hz, %d ch = %.3f s (want %u frames at %d Hz, %d ch: %s)", SND_OUTPUTS[i].name, im.frames, im.rate, im.ch,
                 (double)im.frames / im.rate, want_frames, want_rate, want_ch, want_rate == g_mix ? "the mixer's rate"
                 : want_rate > g_mix ? "a multiple of the mixer's, 44.1 kHz or more, for a pitched one-shot" : "half the mixer's, as 0.13");
        check(ok, "length, rate and channels", detail);
        snprintf(detail, sizeof detail, "%s: peak %d (%.2f dBFS), ceiling %d, %d samples at full scale", SND_OUTPUTS[i].name, im.peak,
                 20.0 * dsp_log10(im.peak / 32768.0), ceiling_of(i), im.full);
        check(im.peak <= ceiling_of(i) && im.full == 0 && im.peak > 33, "level", detail);
    }
    if (raven) {
        static const int SECONDS10[3] = { 200, 300, 455 };   /* the outdoor loops the hook hands the game: 20, 30, 45.5 s */
        for (i = 0; i < 3; i++) {
            img_t im; char why[200];
            if (g_snd[SND_OUTDOOR_RAIN_LIGHT + i].state != BUILD_READY || !parse_image(SND_OUTDOOR_RAIN_LIGHT + i, &im, why, sizeof why)) { check(0, "outdoor loop length", SND_OUTPUTS[SND_OUTDOOR_RAIN_LIGHT + i].name); continue; }
            /* the nearest whole frame: at a mixer rate where 45.5 s is not one (11,025 Hz), the loop is stretched to the next */
            snprintf(detail, sizeof detail, "%s: %u ms, %u frames at %d Hz", SND_OUTPUTS[SND_OUTDOOR_RAIN_LIGHT + i].name, (unsigned)((double)im.frames * 1000.0 / im.rate + 0.5), im.frames, im.rate);
            check(im.frames == ((unsigned)SECONDS10[i] * (unsigned)im.rate + 5u) / 10u, "outdoor loop length (harness check 19 expects it)", detail);
        }
        if (!faulted) {
            snprintf(detail, sizeof detail, "%d of 6 at start, then %d of 13 when the storm came on, %d of 3 gusts", built_start, built_storm, gusts);
            check(built_start == 6 && built_storm == 13 && gusts == 3, "the rain loops at start, the storm's sounds with the storm", detail);
        }
    }
    if (nos) {
        snprintf(detail, sizeof detail, "%d of 6 at start, then %d of 13 when the storm came on, %d of 3 gusts", built_start, built_storm, gusts);
        check(faulted || (built_start == 6 && built_storm == 10 && gusts == 0), "without patch-S.mpq: all but the gusts", detail);
    }
    /* nothing kept but the images: every source, working copy and limiter buffer was given back */
    {
        size_t images = 0;
        for (i = 0; i < SND_COUNT; i++) if (g_snd[i].state == BUILD_READY) images += g_snd[i].size;
        snprintf(detail, sizeof detail, "%zu bytes held for %zu bytes of images", g_now, images);
        check(g_now == images, "nothing held but the images", detail);
        reset_builds();
        snprintf(detail, sizeof detail, "%lu blocks, %zu bytes", g_blocks, g_now);
        check(g_now == 0 && g_blocks == 0, "everything given back", detail);
    }
    snprintf(detail, sizeof detail, "%lu opens, %lu through the case-insensitive fallback", g_opens, g_open_fallbacks);
    check(1, "archives", detail);
    rc = g_failed;
    printf("\n%d check%s failed\n", rc, rc == 1 ? "" : "s");
    return rc;
}

/* ---- settings: what the switches let the DLL build ---- */
static int ready_list(char *out, size_t cap)
{
    int i, n = 0; out[0] = 0;
    for (i = 0; i < SND_COUNT; i++) if (g_snd[i].state == BUILD_READY) { if (n++) strncat(out, " ", cap - strlen(out) - 1); strncat(out, SND_OUTPUTS[i].name, cap - strlen(out) - 1); }
    return n;
}
/* 1 when exactly the sounds in want[0..n) are built, no more and no fewer */
static int built_exactly(const int *want, int n)
{
    int i, j, ok = 1;
    for (i = 0; i < SND_COUNT; i++) {
        int in = 0;
        for (j = 0; j < n; j++) in |= want[j] == i;
        ok &= (g_snd[i].state == BUILD_READY) == in;
    }
    return ok;
}
static int cmd_settings(void)
{
    char detail[1600], names[1400]; int list[SND_COUNT + 1], n, e, o, t, g, i, ok = 1, before;
    /* wanted_sounds over every combination: nothing while the mod is off; the outdoor loops only with the swap, the
       thunder only with the storm or a waiting strike, the gusts only with the storm; the indoor rain loops always */
    for (e = 0; e <= 1; e++) for (o = 0; o <= 1; o++) for (t = 0; t <= 1; t++) for (g = 0; g <= 1; g++) {
        int outdoor = 0, indoor = 0, thunder = 0, gust = 0;
        n = wanted_sounds(list, e, o, t, g);
        for (i = 0; i < n; i++) {
            int id = list[i];
            if (id <= SND_OUTDOOR_RAIN_HEAVY) outdoor++;
            else if (id <= SND_INDOOR_RAIN_HEAVY) indoor++;
            else if (id >= SND_THUNDER_CLOSE && id <= SND_THUNDER_FAR2_IN) thunder++;
            else if (id >= SND_GUST_1) gust++;
            else ok = 0;   /* a snow or sand loop is never on the list: it comes from the weather indoors */
        }
        ok &= outdoor == (e && o ? 3 : 0) && indoor == (e ? 3 : 0) && thunder == (e && t ? 10 : 0) && gust == (e && g ? 3 : 0);
    }
    check(ok, "wanted_sounds: each switch adds exactly its own sounds, and nothing at all while the mod is off", "16 combinations");

    /* 1. the ini says off at start: nothing; then the addon turns the mod on: the rain loops, no storm yet */
    reset_builds();
    n = wanted_sounds(list, 0, 0, 0, 0);
    sounds_build(list, n);
    ready_list(names, sizeof names);
    snprintf(detail, sizeof detail, "%d on the list, built: %s", n, names[0] ? names : "nothing");
    check(n == 0 && g_now == 0, "the mod off at start builds nothing", detail);
    sounds_upkeep(1, 1, 0, 0, -1);
    { int want[6] = { 0, 1, 2, 3, 4, 5 }; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 6), "the mod comes on: the six rain loops at that poll, and no storm sound", detail); }
    /* 2. the storm comes on: the ten thunders and the three gusts */
    sounds_upkeep(1, 1, 1, 0, -1);
    { int want[19]; for (i = 0; i < 19; i++) want[i] = i < 6 ? i : SND_THUNDER_CLOSE + i - 6; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 19), "the storm comes on: its thunder and gusts at that poll", detail); }
    /* 3. snow indoors: its loop, the first time it is wanted, and only while the mod is on */
    sounds_upkeep(0, 0, 0, 0, SND_INDOOR_SNOW_MEDIUM);
    check(g_snd[SND_INDOOR_SNOW_MEDIUM].state == BUILD_NONE, "snow indoors with the mod off: its loop is not built", SND_OUTPUTS[SND_INDOOR_SNOW_MEDIUM].name);
    sounds_upkeep(1, 1, 1, 0, SND_INDOOR_SNOW_MEDIUM);
    check(g_snd[SND_INDOOR_SNOW_MEDIUM].state == BUILD_READY, "snow indoors with the mod on: built at that poll", SND_OUTPUTS[SND_INDOOR_SNOW_MEDIUM].name);
    before = ready_list(names, sizeof names);
    sounds_upkeep(1, 1, 1, 0, SND_INDOOR_SNOW_MEDIUM);
    snprintf(detail, sizeof detail, "%d built before, %d after", before, ready_list(names, sizeof names));
    check(ready_list(names, sizeof names) == before, "a poll with nothing new to want builds nothing", detail);

    /* 4. outdoor_rain=0 at start: the indoor loops only; a test strike with the storm off: the thunders, no gusts;
       outdoor_rain back on: the outdoor loops; the mod off: nothing more, whatever else is on */
    reset_builds();
    n = wanted_sounds(list, 1, 0, 0, 0);
    sounds_build(list, n);
    { int want[3] = { 3, 4, 5 }; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 3), "outdoor_rain=0 at start: no outdoor loop", detail); }
    sounds_upkeep(1, 0, 0, 1, -1);
    { int want[13]; for (i = 0; i < 13; i++) want[i] = i < 3 ? 3 + i : SND_THUNDER_CLOSE + i - 3; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 13), "a test strike with the storm off: the thunders only, no gust", detail); }
    sounds_upkeep(0, 1, 1, 1, SND_INDOOR_SAND_HEAVY);
    { int want[13]; for (i = 0; i < 13; i++) want[i] = i < 3 ? 3 + i : SND_THUNDER_CLOSE + i - 3; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 13), "the mod off: nothing more, with the storm, the swap and sand indoors all asking", detail); }
    sounds_upkeep(1, 1, 0, 0, -1);
    { int want[16]; for (i = 0; i < 16; i++) want[i] = i < 6 ? i : SND_THUNDER_CLOSE + i - 6; ready_list(names, sizeof names); snprintf(detail, sizeof detail, "built: %s", names);
      check(built_exactly(want, 16), "outdoor_rain back on with the mod: the outdoor loops at that poll", detail); }
    reset_builds();
    snprintf(detail, sizeof detail, "%lu blocks, %zu bytes", g_blocks, g_now);
    check(g_now == 0 && g_blocks == 0, "everything given back", detail);
    printf("\n%d check%s failed\n", g_failed, g_failed == 1 ? "" : "s");
    return g_failed;
}

/* ---- rates: each sound for a mixer at R, against the same sound for 44,100 Hz taken to R ----
 * The scripts ran every recipe at 44.1 kHz or at its source's own rate, and sounds.c runs each where they did, whatever
 * the mixer's rate (the third 0.14 review: for a 22,050 Hz mixer the medium and heavy indoor rain ran their 2.5 kHz
 * low-pass at 22,050 Hz, and came out 1.9 dB duller at 5 kHz and 7.7 dB at 8 kHz than when built for 44,100 Hz). So a
 * sound built for a mixer at R must hold the spectrum of the one built for 44,100 Hz, taken to its rate by
 * dsp_resample: third of an octave by third (Welch's method on 8,192-point Hann windows, each third in dB of the whole
 * sound, the channels summed), from 50 Hz up to the top of the band both carry, 90% of the lowest of the two rates and
 * the mixer's, wherever the 44.1 kHz one is above -60 dB of the whole. No third may be more than 1 dB off; at 48 kHz,
 * where the rain's low-pass runs at 48 kHz, the most is 0.5 dB, at 10 kHz, and every other sound is within 0.1 dB at
 * every rate. The thunder and the gusts, which the DLL plays at a random pitch, are held to the 44.1 kHz build as
 * played too (as_played, below): at a slower mixer's own rate the mixer's straight line dulled and imaged them (the
 * fourth 0.14 review), which a spectrum taken before the mixer cannot show. */
#define T_PI 3.14159265358979323846
#define T_NFFT 8192
#define T_THIRDS 27   /* centres 1000 * 2^(k / 3) Hz, k from -13 (50 Hz) to 13 (20 kHz) */
static void fft(double *re, double *im, int n)
{
    int i, j, k, len, bit;
    for (i = 1, j = 0; i < n; i++) {
        for (bit = n >> 1; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (len = 2; len <= n; len <<= 1) {
        double cw = dsp_cos(-2.0 * T_PI / len), sw = dsp_sin(-2.0 * T_PI / len);
        for (i = 0; i < n; i += len) {
            double wr = 1.0, wi = 0.0;
            for (k = 0; k < len / 2; k++) {
                double *ar = re + i + k, *ai = im + i + k, *br = ar + len / 2, *bi = ai + len / 2, vr = *br * wr - *bi * wi, vi = *br * wi + *bi * wr, t;
                *br = *ar - vr; *bi = *ai - vi; *ar += vr; *ai += vi;
                t = wr * cw - wi * sw; wi = wr * sw + wi * cw; wr = t;
            }
        }
    }
}
/* Welch's power spectrum of b, the channels summed: psd[0..T_NFFT / 2], bin i at i * rate / T_NFFT Hz */
static void welch(const dsp_buf_t *b, double *psd)
{
    static double re[T_NFFT], im[T_NFFT], win[T_NFFT];
    int i, c, at;
    for (i = 0; i < T_NFFT; i++) win[i] = 0.5 - 0.5 * dsp_cos(2.0 * T_PI * i / T_NFFT);
    for (i = 0; i <= T_NFFT / 2; i++) psd[i] = 0.0;
    for (c = 0; c < b->channels; c++)
        for (at = 0; at + T_NFFT <= b->frames; at += T_NFFT / 2) {
            for (i = 0; i < T_NFFT; i++) { re[i] = b->s[(at + i) * b->channels + c] * win[i]; im[i] = 0.0; }
            fft(re, im, T_NFFT);
            for (i = 0; i <= T_NFFT / 2; i++) psd[i] += re[i] * re[i] + im[i] * im[i];
        }
}
static double third_lo(int k) { return 1000.0 * dsp_pow(2.0, (k - 13) / 3.0) / dsp_pow(2.0, 1.0 / 6.0); }
static double third_hi(int k) { return 1000.0 * dsp_pow(2.0, (k - 13) / 3.0) * dsp_pow(2.0, 1.0 / 6.0); }
static void thirds(const dsp_buf_t *b, double *db)
{
    static double psd[T_NFFT / 2 + 1];
    double total = 0.0; int i, k;
    welch(b, psd);
    for (i = 1; i <= T_NFFT / 2; i++) total += psd[i];
    for (k = 0; k < T_THIRDS; k++) {
        double lo = third_lo(k), hi = third_hi(k), e = 0.0;
        for (i = 1; i <= T_NFFT / 2; i++) { double f = (double)i * b->rate / T_NFFT; if (f >= lo && f < hi) e += psd[i]; }
        db[k] = 10.0 * dsp_log10(e / (total > 0.0 ? total : 1.0) + 1e-30);
    }
}
/* ---- as played: the thunder and the gusts through the mixer's straight line, at the DLL's pitches ----
 * shot_play sets a thunder's voice to 88 to 104 % of its rate and a gust's to 90 to 110 % (FSOUND_SetFrequency with
 * the rate / 100 * pct, in integers; at exactly 100 it sets nothing), and the mixer steps through a mono voice at that
 * rate over its own, drawing a straight line between two frames (sounds.c). The line is the frames run through a
 * triangle one frame wide: the band's power at nu cycles a frame times sinc(nu)^4, which dulls its top, and copies of
 * it at nu + k for every whole k, images, each at sinc(nu + k)^4; the mixer's sampling then folds whatever lands past
 * its Nyquist frequency back down. Here each band of the voice's Welch spectrum sends its copies (|k| <= 8: sinc^4
 * leaves under a millionth past that) to |nu + k| * f / mix cycles a mixer frame, folded: the band itself (k = 0),
 * where it lands under the Nyquist frequency, is what is heard as meant, and every other copy an image or an alias.
 * Copies that land together add as powers, as they do in sounds like these; not at exactly 100 %, a whole step, where
 * every copy lands on its own band and they sum back to it, which is why the DLL's pitch of 100 is left out.
 * For each third of an octave, where the band lands in it: its level as heard, in dB of the whole, and the line's
 * droop there, what is heard of the band against what a perfect change of pitch would give, taken over the same bins
 * of the same spectrum, so two builds at different rates are held against each other by what the line does to each,
 * not by how their bins fall on the thirds' edges. */
typedef struct { double ia_db, level_db[T_THIRDS], droop_db[T_THIRDS]; } played_t;
static void as_played(const double *psd, int rate, int mix, int pct, played_t *p)
{
    const int fv = rate / 100 * pct;
    double total = 0.0, ia = 0.0, meant[T_THIRDS], plain[T_THIRDS], lo[T_THIRDS], hi[T_THIRDS]; int i, k, t;
    for (t = 0; t < T_THIRDS; t++) { meant[t] = plain[t] = 0.0; lo[t] = third_lo(t); hi[t] = third_hi(t); }
    for (i = 1; i <= T_NFFT / 2; i++) {
        const double nu = (double)i / T_NFFT, s = dsp_sin(T_PI * nu), s4 = s * s * s * s;
        for (k = -8; k <= 8; k++) {
            double g = nu + k, pg = T_PI * g, e = psd[i] * s4 / (pg * pg * pg * pg), y = (g < 0.0 ? -g : g) * fv / mix;
            total += e;
            if (k != 0 || y > 0.5) { ia += e; continue; }   /* an image, or the band folded past the Nyquist frequency */
            for (t = 0; t < T_THIRDS; t++) if (y * mix >= lo[t] && y * mix < hi[t]) { meant[t] += e; plain[t] += psd[i]; }
        }
    }
    p->ia_db = 10.0 * dsp_log10(ia / (total > 0.0 ? total : 1.0) + 1e-30);
    for (t = 0; t < T_THIRDS; t++) {
        p->level_db[t] = 10.0 * dsp_log10(meant[t] / (total > 0.0 ? total : 1.0) + 1e-30);
        p->droop_db[t] = plain[t] > 0.0 ? 10.0 * dsp_log10(meant[t] / plain[t] + 1e-30) : 0.0;
    }
}
static int cmd_rates(void)
{
    int s, o, r, compared_all = 0; char detail[400], why[320], tried[480];
    printf("each sound for a mixer at %d Hz against the one for 44,100 Hz, taken to %d Hz\n", g_mix, g_mix);
    for (s = 0; s < SRC_COUNT; s++) {
        const snd_source_t *src = &SND_SOURCES[s]; snd_input_t in_r, in_44; unsigned char *bytes = NULL; unsigned len = 0; int w = -1, peak_db;
        memset(&in_r, 0, sizeof in_r); memset(&in_44, 0, sizeof in_44); tried[0] = 0; why[0] = 0;
        if (source_read(s, &bytes, &len, &w, tried, sizeof tried) != SRC_FOUND) { printf("  %s: not in this client's archives (%s)\n", src->path, tried); continue; }
        in_r.variant = in_44.variant = src->where[w].variant;
        r = src->format == SND_OGG ? ogg_decode(src->where[w].archive, src->path, bytes, len, &in_r.pcm, &peak_db, why, sizeof why) : snd_wav_decode(bytes, len, &in_r.pcm, &A);
        t_release(NULL, bytes);
        if (r == DSP_OK && (r = dsp_copy(&in_44.pcm, &in_r.pcm, &A)) == DSP_OK && (r = snd_prepare(s, &in_r, g_mix, &A)) == DSP_OK) r = snd_prepare(s, &in_44, 44100, &A);
        if (r != DSP_OK) {
            snprintf(detail, sizeof detail, "%s: %s %s", src->path, snd_why(r), why);
            check(0, "rates: the source decoded and made ready for both rates", detail);
            dsp_free(&in_r.pcm, &A); dsp_free(&in_44.pcm, &A);
            continue;
        }
        for (o = 0; o < SND_COUNT; o++) {
            static double psd_r[T_NFFT / 2 + 1], psd_44[T_NFFT / 2 + 1];
            void *wr = NULL, *w44 = NULL; unsigned nr = 0, n44 = 0; dsp_buf_t br, b44; double dr[T_THIRDS], d44[T_THIRDS], worst = 0.0, top;
            int k, at = -1, n = 0, shot = !SND_OUTPUTS[o].loop;
            if (SND_OUTPUTS[o].source != s) continue;
            memset(&br, 0, sizeof br); memset(&b44, 0, sizeof b44);
            if ((r = snd_build(o, &in_r, &wr, &nr, &A)) == DSP_OK && (r = snd_build(o, &in_44, &w44, &n44, &A)) == DSP_OK
                && (r = dsp_wav_decode(&br, wr, nr, &A)) == DSP_OK && (r = dsp_wav_decode(&b44, w44, n44, &A)) == DSP_OK) {
                if (shot) { welch(&br, psd_r); welch(&b44, psd_44); }   /* each as built, for as_played below */
                r = dsp_resample(&b44, br.rate, 0.45 * (b44.rate < br.rate ? b44.rate : br.rate), SND_OUTPUTS[o].loop, &A);
            }
            if (r != DSP_OK) {
                snprintf(detail, sizeof detail, "%s: %s", SND_OUTPUTS[o].name, snd_why(r));
                check(0, "rates: built for both rates", detail);
            } else {
                thirds(&br, dr); thirds(&b44, d44);
                /* up to the top of the band both carry: 90% of the lower Nyquist frequency, the mixer's among them (a
                   one-shot built at a multiple of a slower mixer's rate carries only the mixer's band) */
                top = 0.45 * (br.rate < 44100 ? br.rate : 44100);
                if (0.45 * g_mix < top) top = 0.45 * g_mix;
                for (k = 0; k < T_THIRDS; k++) {
                    double d = dr[k] - d44[k];
                    if (third_hi(k) > top || d44[k] < -60.0) continue;
                    n++;
                    if (d * d > worst * worst) { worst = d; at = k; }
                }
                compared_all += n;
                if (at >= 0) snprintf(detail, sizeof detail, "%s at %d Hz: %d thirds held, the most off %+.2f dB at %.0f Hz", SND_OUTPUTS[o].name, br.rate, n, worst,
                                      1000.0 * dsp_pow(2.0, (at - 13) / 3.0));
                else snprintf(detail, sizeof detail, "%s at %d Hz: %d thirds held, every one the same", SND_OUTPUTS[o].name, br.rate, n);
                check(n > 0 && worst <= 1.0 && worst >= -1.0, "rates: its spectrum is the 44.1 kHz one's, to 1 dB a third", detail);
            }
            /* The thunder and the gusts as the mixer plays them at every pitch shot_play gives them (indoorrain.c: 88
               to 104 % for a thunder, 90 to 110 % for a gust), but 100 (as_played), against the same sound built for
               44,100 Hz, 0.13's rate, played on the same mixer at the same pitch: the straight line's droop, third by
               third where the band lands up to the top of what both carry at that pitch (and the 44.1 kHz one is above
               -60 dB of the whole), no more than 1 dB deeper than its droop there (less is nearer a perfect change of
               pitch), and images and aliases no more than 1 dB over its, or under -60 dB of the whole (the fourth 0.14
               review: built at a 22,050 Hz mixer's own rate, thunder_close came out 3.6 dB duller at 8 kHz than the
               44.1 kHz build played there, its images and aliases 14 dB louder). */
            if (r == DSP_OK && shot) {
                const int lo_pct = o >= SND_GUST_1 ? 90 : 88, hi_pct = o >= SND_GUST_1 ? 110 : 104;
                double worst_d = 0.0, worst_x = -400.0, x_ours = 0.0, x_ref = 0.0; int pct, d_pct = 0, d_k = -1, x_pct = 0, thirds_n = 0; char dull[160];
                for (pct = lo_pct; pct <= hi_pct; pct++) {
                    played_t pr, p44; double top_out = 0.45 * (g_mix < 44100 ? g_mix : 44100) * pct / 100.0, x;
                    if (pct == 100) continue;
                    as_played(psd_r, br.rate, g_mix, pct, &pr);
                    as_played(psd_44, 44100, g_mix, pct, &p44);
                    for (k = 0; k < T_THIRDS; k++) {
                        double d = pr.droop_db[k] - p44.droop_db[k];
                        if (third_hi(k) > top_out || p44.level_db[k] < -60.0) continue;
                        thirds_n++;
                        if (d < worst_d) { worst_d = d; d_pct = pct; d_k = k; }
                    }
                    x = pr.ia_db - (p44.ia_db > -60.0 ? p44.ia_db : -60.0);
                    if (x > worst_x) { worst_x = x; x_pct = pct; x_ours = pr.ia_db; x_ref = p44.ia_db; }
                }
                if (d_k >= 0) snprintf(dull, sizeof dull, "the line's droop at most %.2f dB deeper than the 44.1 kHz build's (%.0f Hz, %d %%)", -worst_d,
                                       1000.0 * dsp_pow(2.0, (d_k - 13) / 3.0), d_pct);
                else snprintf(dull, sizeof dull, "the line's droop nowhere deeper than the 44.1 kHz build's");
                snprintf(detail, sizeof detail, "%s at %d Hz on a %d Hz mixer, %d to %d %%, %d thirds: %s; images and aliases %.1f dB of the whole against "
                         "its %.1f (%d %%)", SND_OUTPUTS[o].name, br.rate, g_mix, lo_pct, hi_pct, thirds_n, dull, x_ours, x_ref, x_pct);
                check(thirds_n > 0 && worst_d >= -1.0 && worst_x <= 1.0, "rates: as played at the DLL's pitches, no duller and no more images than the 44.1 kHz build", detail);
            }
            dsp_free(&br, &A); dsp_free(&b44, &A);
            if (wr) t_release(NULL, wr);
            if (w44) t_release(NULL, w44);
        }
        dsp_free(&in_r.pcm, &A); dsp_free(&in_44.pcm, &A);
    }
    snprintf(detail, sizeof detail, "%d thirds in all; %lu blocks, %zu bytes still held", compared_all, g_blocks, g_now);
    check(g_now == 0 && g_blocks == 0, "rates: everything given back", detail);
    printf("\n%d check%s failed\n", g_failed, g_failed == 1 ? "" : "s");
    return g_failed;
}

/* ---- limits: what a source may be (sounds.h), a frame each side of each edge ---- */
static unsigned char *wav_of(unsigned frames, int ch, int rate, int bits, unsigned *size)
{
    unsigned bpf = (unsigned)ch * (unsigned)bits / 8u, data = frames * bpf, i; unsigned char *p = malloc(44u + data);
    if (!p) return NULL;
    memcpy(p, "RIFF", 4); memcpy(p + 8, "WAVEfmt ", 8); memcpy(p + 36, "data", 4);
    for (i = 0; i < 4; i++) { p[4 + i] = (unsigned char)((36u + data) >> (8 * i)); p[16 + i] = (unsigned char)(16u >> (8 * i)); p[24 + i] = (unsigned char)((unsigned)rate >> (8 * i));
                              p[28 + i] = (unsigned char)((unsigned)rate * bpf >> (8 * i)); p[40 + i] = (unsigned char)(data >> (8 * i)); }
    p[20] = 1; p[21] = 0; p[22] = (unsigned char)ch; p[23] = 0; p[32] = (unsigned char)bpf; p[33] = 0; p[34] = (unsigned char)bits; p[35] = 0;
    for (i = 0; i < data; i++) p[44 + i] = (unsigned char)(bits == 8 ? 120u + ((i * 2654435761u) >> 28) : (i & 1u) ? 0u : (i * 2654435761u) >> 26);   /* quiet noise */
    *size = 44u + data;
    return p;
}
/* Ogg pages made here, for snd_ogg_probe: a stream in the shape of a real one, the identification header alone on its
   first page, then pages of packet bytes (not Vorbis audio: the probe reads the pages and that header, never the audio)
   up to the one that ends the stream at a granule position. The checksum is taken bit by bit here, not by the table
   sounds.c builds, so a stream this makes passes only if the two agree. */
static void put32le(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static unsigned ogg_crc_bits(const unsigned char *p, unsigned n)
{
    unsigned crc = 0, i; int b;
    for (i = 0; i < n; i++) { crc ^= (unsigned)p[i] << 24; for (b = 0; b < 8; b++) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1; }
    return crc;
}
static void ogg_crc_set(unsigned char *page)   /* after a change to a page made by ogg_page */
{
    unsigned n = 27u + page[26], i;
    for (i = 0; i < page[26]; i++) n += page[27 + i];
    put32le(page + 22, 0); put32le(page + 22, ogg_crc_bits(page, n));
}
/* one page at o, len bytes of body laced as one packet (255s, then the rest); returns its size */
static unsigned ogg_page(unsigned char *o, unsigned flags, unsigned gran_lo, unsigned gran_hi, unsigned serial, unsigned seq, const unsigned char *body, unsigned len)
{
    unsigned nseg = len / 255u + 1u, i;
    memcpy(o, "OggS", 4); o[4] = 0; o[5] = (unsigned char)flags;
    put32le(o + 6, gran_lo); put32le(o + 10, gran_hi); put32le(o + 14, serial); put32le(o + 18, seq);
    o[26] = (unsigned char)nseg;
    for (i = 0; i < nseg; i++) o[27 + i] = (unsigned char)(i + 1 < nseg ? 255u : len % 255u);
    memcpy(o + 27 + nseg, body, len);
    ogg_crc_set(o);
    return 27u + nseg + len;
}
/* a stream: the identification page (ch, rate), a page in the place of the comment and setup headers, then `pages`
   pages of packet bytes, the last one ending the stream at granule position `frames` (and the high word `hi`);
   eos 0 leaves the end-of-stream flag off. Returns its size; where[] gets each page's offset, if given. */
static unsigned ogg_stream(unsigned char *o, unsigned serial, int ch, unsigned rate, int pages, unsigned frames, unsigned hi, int eos, unsigned *where)
{
    unsigned char id[30], body[4000]; unsigned n = 0, i; int k;
    memset(id, 0, sizeof id); id[0] = 1; memcpy(id + 1, "vorbis", 6); id[11] = (unsigned char)ch; put32le(id + 12, rate);
    id[28] = 0xB8; id[29] = 1;   /* block sizes 256 and 2,048, and the framing bit */
    for (i = 0; i < sizeof body; i++) body[i] = (unsigned char)((i * 2654435761u) >> 24);
    if (where) where[0] = n;
    n += ogg_page(o + n, 2, 0, 0, serial, 0, id, 30);
    if (where) where[1] = n;
    n += ogg_page(o + n, 0, 0, 0, serial, 1, body, 3000);
    for (k = 0; k < pages; k++) {
        int last = k + 1 == pages;
        if (where) where[2 + k] = n;
        n += ogg_page(o + n, last && eos ? 4 : 0, last ? frames : frames / (unsigned)pages * (unsigned)(k + 1), last ? hi : 0, serial, 2u + (unsigned)k, body, 4000);
    }
    return n;
}
static int cmd_limits(void)
{
    static const struct { unsigned frames; int ch, rate, bits, ok; const char *what; } C[] = {
        { 90u * 22050u, 2, 22050, 16, 1, "90 s at 22,050 Hz, stereo" },
        { 90u * 22050u + 1u, 2, 22050, 16, 0, "a frame over 90 s" },
        { 4194304u, 2, 96000, 16, 1, "32 MB as samples: 4,194,304 stereo frames at 96 kHz, 43.7 s" },
        { 4194305u, 2, 96000, 16, 0, "a frame over 32 MB as samples" },
        { 80000u, 1, 8000, 8, 1, "8,000 Hz" },
        { 79990u, 1, 7999, 8, 0, "7,999 Hz" },
        { 96000u, 1, 96000, 16, 1, "96,000 Hz" },
        { 96001u, 1, 96001, 16, 0, "96,001 Hz" },
        { 64u * 1024u * 1024u - 44u, 1, 8000, 8, 0, "64 MB of 8-bit mono at 8 kHz, 256 MB as samples (0.14 review)" } };
    char detail[400]; int t, r;
    for (t = 0; t < (int)(sizeof C / sizeof C[0]); t++) {
        unsigned size; unsigned char *d = wav_of(C[t].frames, C[t].ch, C[t].rate, C[t].bits, &size); dsp_buf_t pcm; unsigned long calls = g_alloc_calls;
        if (!d) { check(0, "limits", "the test could not make its WAV"); continue; }
        r = snd_wav_decode(d, size, &pcm, &A);
        snprintf(detail, sizeof detail, "%s: %s, %lu memory requests", C[t].what, snd_why(r), g_alloc_calls - calls);
        if (C[t].ok) check(r == DSP_OK && pcm.frames == (int)C[t].frames && snd_source_ok((unsigned)pcm.frames, pcm.channels, pcm.rate), "snd_wav_decode takes a source at the edge", detail);
        else check(r == SND_UNFIT && g_alloc_calls == calls && !pcm.s, "snd_wav_decode refuses a source past the edge, before any memory", detail);
        if (r == DSP_OK) dsp_free(&pcm, &A);
        free(d);
    }
    /* snd_prepare holds what FMOD decoded (the gusts) to the same edges, and each input is made ready once, for a mixer
       rate the sounds are built for; snd_build takes only a ready one */
    {
        snd_input_t in; dsp_buf_t *b = &in.pcm; unsigned long calls; void *wav; unsigned wsize; int i, r2, r3, r4, r5;
        memset(&in, 0, sizeof in);
        dsp_new(b, 90 * 44100 + 1, 1, 44100, &A);
        for (i = 0; i < b->frames; i++) b->s[i] = 0.0f;
        calls = g_alloc_calls;
        r = snd_prepare(SRC_GUST1, &in, 44100, &A);
        snprintf(detail, sizeof detail, "a gust of 90 s and a frame: %s, %lu memory requests", snd_why(r), g_alloc_calls - calls);
        check(r == SND_UNFIT && g_alloc_calls == calls && in.mix_rate == 0, "snd_prepare refuses what FMOD decoded past the edge", detail);
        dsp_free(b, &A);
        dsp_new(b, 4 * 44100, 1, 44100, &A);
        for (i = 0; i < b->frames; i++) b->s[i] = (float)(0.1 * ((double)((i * 2654435761u) >> 16) / 65536.0 - 0.5));
        r2 = snd_build(SND_GUST_1, &in, &wav, &wsize, &A);
        r3 = snd_prepare(SRC_GUST1, &in, 7999, &A);
        r4 = snd_prepare(SRC_GUST1, &in, 48001, &A);
        r = snd_prepare(SRC_GUST1, &in, 44100, &A);
        r5 = snd_prepare(SRC_GUST1, &in, 44100, &A);
        snprintf(detail, sizeof detail, "build before prepare %d, prepare for a 7,999 Hz mixer %d, for 48,001 Hz %d, for 44,100 Hz %d, again %d", r2, r3, r4, r, r5);
        check(r2 == DSP_BADARG && r3 == DSP_BADARG && r4 == DSP_BADARG && r == DSP_OK && r5 == DSP_BADARG && in.mix_rate == 44100, "snd_prepare and snd_build take a source once, ready, for a mixer rate they build for", detail);
        r = snd_build(SND_GUST_1, &in, &wav, &wsize, &A);
        snprintf(detail, sizeof detail, "%s, %u bytes at %u Hz", snd_why(r), wsize, r == DSP_OK ? rd_le32((const unsigned char *)wav + 24) : 0u);
        check(r == DSP_OK && rd_le32((const unsigned char *)wav + 24) == 44100u, "and then build it, at the mixer's rate", detail);
        if (r == DSP_OK) t_release(NULL, wav);
        dsp_free(b, &A);
    }
    /* snd_ogg_probe: a gust's length, the one the client's vorbisfile will find, known before FMOD decodes a sample of
       it (the third 0.14 review: a second stream after the first, or page-like bytes after the end whose checksum is
       wrong, passed a file whose decode was far longer than the length taken). It must take a stream that tells its
       length and refuse every file below it, allocating nothing. */
    {
        static unsigned char a[64 * 1024], l[64 * 1024], o[192 * 1024];
        unsigned na, nl, n, pg[8], frames = 0, calls0 = (unsigned)g_alloc_calls; int ch = 0, rate = 0; const char *why;
#define PROBE_TAKES(what_, want_frames, want_ch, want_rate) do { \
            why = snd_ogg_probe(o, n, &ch, &rate, &frames); \
            snprintf(detail, sizeof detail, "%s: %s, %u frames at %d Hz, %d ch", what_, why ? why : "taken", frames, rate, ch); \
            check(!why && frames == (want_frames) && ch == (want_ch) && rate == (want_rate), "snd_ogg_probe takes a stream that tells its length", detail); } while (0)
#define PROBE_REFUSES(what_) do { \
            why = snd_ogg_probe(o, n, &ch, &rate, &frames); \
            snprintf(detail, sizeof detail, "%s: %s", what_, why ? why : "TAKEN"); \
            check(why != NULL, "snd_ogg_probe refuses a file whose decode could differ from what it says", detail); } while (0)
        na = ogg_stream(a, 0x1234u, 1, 44100u, 4, 44100u, 0, 1, pg);
        nl = ogg_stream(l, 0x5678u, 1, 44100u, 3, 600u * 44100u, 0, 1, NULL);
        memcpy(o, a, n = na);                                   PROBE_TAKES("1 s, mono, 44,100 Hz", 44100u, 1, 44100);
        n = ogg_stream(o, 7u, 2, 22050u, 2, 3u * 22050u, 0, 1, NULL); PROBE_TAKES("3 s, stereo, 22,050 Hz", 3u * 22050u, 2, 22050);
        n = ogg_stream(o, 7u, 1, 44100u, 2, 90u * 44100u, 0, 1, NULL); PROBE_TAKES("90 s", 90u * 44100u, 1, 44100);
        check(snd_source_ok(frames, ch, rate), "and snd_source_ok takes 90 s", "");
        n = ogg_stream(o, 7u, 1, 44100u, 2, 90u * 44100u + 1u, 0, 1, NULL); PROBE_TAKES("90 s and a frame", 90u * 44100u + 1u, 1, 44100);
        check(!snd_source_ok(frames, ch, rate), "and snd_source_ok refuses 90 s and a frame, before FMOD sees it", "");
        memcpy(o, l, n = nl);                                   PROBE_TAKES("600 s, honest", 600u * 44100u, 1, 44100);
        check(!snd_source_ok(frames, ch, rate), "and snd_source_ok refuses what it says, 600 s", "");
        /* what the review found: 600 s that the old probe took for 1 s */
        memcpy(o, l, nl); memcpy(o + nl, a, na); n = nl + na;   PROBE_REFUSES("chained: 600 s, then a 1 s stream");
        memcpy(o, a, na); memcpy(o + na, l, nl); n = na + nl;   PROBE_REFUSES("chained: 1 s, then a 600 s stream");
        memcpy(o, l, nl); n = nl; memset(o + n, 0, 27); memcpy(o + n, "OggS", 4); o[n + 5] = 4; put32le(o + n + 6, 44100u); memcpy(o + n + 14, l + 14, 4);
        put32le(o + n + 22, ogg_crc_bits(o + n, 27) ^ 1u); n += 27; PROBE_REFUSES("600 s, then 27 bytes shaped like an end-of-stream page at 1 s, checksum wrong");
        put32le(o + n - 27 + 22, 0); put32le(o + n - 27 + 22, ogg_crc_bits(o + n - 27, 27)); PROBE_REFUSES("600 s, then the same page with its checksum right");
        nl = ogg_stream(l, 0x5678u, 1, 44100u, 3, 600u * 44100u, 0, 0, NULL);
        memcpy(o, l, nl); n = nl; memset(o + n, 0, 27); memcpy(o + n, "OggS", 4); o[n + 5] = 4; put32le(o + n + 6, 44100u); memcpy(o + n + 14, l + 14, 4);
        put32le(o + n + 22, ogg_crc_bits(o + n, 27) ^ 1u); n += 27; PROBE_REFUSES("600 s that never ends its stream, then that page with its checksum wrong");
        /* each rule alone, on the 1 s stream */
        memcpy(o, a, n = na); o[pg[3] + 100] ^= 1u;            PROBE_REFUSES("a byte of a page changed: its checksum wrong");
        memcpy(o, a, n = na); put32le(o + pg[3] + 14, 0x1235u); ogg_crc_set(o + pg[3]); PROBE_REFUSES("a page of another stream in it");
        memcpy(o, a, n = na); o[pg[3] + 5] |= 2u; ogg_crc_set(o + pg[3]); PROBE_REFUSES("a second page that begins a stream");
        memcpy(o, a, n = na); o[pg[0] + 5] &= (unsigned char)~2u; ogg_crc_set(o + pg[0]); PROBE_REFUSES("a first page that does not begin one");
        memcpy(o, a, n = na); o[pg[5] + 5] &= (unsigned char)~4u; ogg_crc_set(o + pg[5]); PROBE_REFUSES("no page that ends the stream");
        memcpy(o, a, n = na); put32le(o + pg[5] + 10, 1u); ogg_crc_set(o + pg[5]); PROBE_REFUSES("a length of 2^32 frames and more");
        memcpy(o, a, n = na); o[n++] = 0;                      PROBE_REFUSES("a byte after the end of the stream");
        memcpy(o, a, n = na); n -= 10;                         PROBE_REFUSES("the last page cut short");
        memcpy(o, a, na); n = pg[5] + 20;                      PROBE_REFUSES("cut inside the last page's header");
        memcpy(o, a, pg[3]); memset(o + pg[3], 0, 5); memcpy(o + pg[3] + 5, a + pg[3], na - pg[3]); n = na + 5; PROBE_REFUSES("five bytes between two pages");
        memcpy(o, a, n = na); o[pg[2] + 4] = 1; ogg_crc_set(o + pg[2]); PROBE_REFUSES("a page of another Ogg version");
        memcpy(o, a, n = na); o[pg[0] + 28 + 6] = 'z'; ogg_crc_set(o + pg[0]); PROBE_REFUSES("an identification header that does not say vorbis");
        memcpy(o, a, n = na); o[pg[0] + 28 + 7] = 1; ogg_crc_set(o + pg[0]); PROBE_REFUSES("Vorbis version 1");
        memcpy(o, a, n = na); o[pg[0] + 28 + 11] = 0; ogg_crc_set(o + pg[0]); PROBE_REFUSES("no channels");
        memcpy(o, a, n = na); o[pg[0] + 28 + 11] = 3; ogg_crc_set(o + pg[0]); PROBE_REFUSES("three channels");
        memcpy(o, a, n = na); put32le(o + pg[0] + 28 + 12, 0); ogg_crc_set(o + pg[0]); PROBE_REFUSES("a rate of 0");
        memcpy(o, a, n = na); o[pg[0] + 28 + 29] = 0; ogg_crc_set(o + pg[0]); PROBE_REFUSES("no framing bit");
        n = ogg_page(o, 2, 0, 0, 0x1234u, 0, a + pg[0] + 28, 29); n += ogg_page(o + n, 4, 44100u, 0, 0x1234u, 1, a + pg[1] + 27 + a[pg[1] + 26], 3000);
        PROBE_REFUSES("an identification header of 29 bytes");
        n = 0;                                                  PROBE_REFUSES("no bytes at all");
        memcpy(o, "OggS\0\2", 6); memset(o + 6, 0, 20); n = 26; PROBE_REFUSES("26 bytes");
        memcpy(o, "RIFF", 4); memcpy(o + 4, a + 4, na - 4); n = na; PROBE_REFUSES("not an Ogg file");
        snprintf(detail, sizeof detail, "%lu memory requests", g_alloc_calls - calls0);
        check(g_alloc_calls == calls0, "and snd_ogg_probe asks for no memory", detail);
#undef PROBE_TAKES
#undef PROBE_REFUSES
    }
    /* The output guard (files out, above), on a work tree made here under $TMPDIR (or /tmp): its .git a file, as a
       worktree's is, naming a main checkout that is not there, so git itself would fail in it (the third 0.14 review:
       the scripts then let the write through). Nothing is written but in that temporary folder, which goes after. */
    {
        char base[4096], repo[4200], p[4400], q[4400]; const char *tmp = getenv("TMPDIR"); FILE *f; struct stat st; int ok;
        static const char *const made[] = { "out/image.wav", "out/planted.wav", "out/sub", "out/linked.wav", "out/fifo.wav", "repo/planted.wav", "repo/image.wav",
                                            "repo/tracked.wav", "repo/.git" };
        snprintf(base, sizeof base, "%s/ownsounds_limits.XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
        if (!mkdtemp(base)) { check(0, "the output guard", "could not make a temporary folder"); goto guard_done; }
        snprintf(repo, sizeof repo, "%s/repo", base); mkdir(repo, 0755);
        snprintf(p, sizeof p, "%s/.git", repo);
        if ((f = fopen(p, "w"))) { fputs("gitdir: /nonexistent/.git/worktrees/repo\n", f); fclose(f); }
        snprintf(p, sizeof p, "%s/out", base); mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/x", base); mkdir(p, 0755);
        snprintf(p, sizeof p, "%s/out", base);
        check(!in_git_work_tree(p), "the guard: a folder outside any work tree passes", p);
        snprintf(p, sizeof p, "%s/new/deeper", repo);
        check(in_git_work_tree(p), "the guard: a folder not made yet, in a work tree git itself cannot read, is refused", p);
        snprintf(p, sizeof p, "%s/x/new/../../repo/out", base);
        check(in_git_work_tree(p), "the guard: through a folder not made yet and back out, into a work tree, is refused", p);
        snprintf(p, sizeof p, "%s/x/new/../../out", base);
        check(!in_git_work_tree(p), "the guard: the same way to a folder outside passes", p);
        snprintf(p, sizeof p, "%s/out/planted.wav", base); snprintf(q, sizeof q, "%s/planted.wav", repo);
        ok = symlink(q, p) == 0 && !write_file(p, "x", 1) && lstat(q, &st) != 0;
        check(ok, "the guard: a symlink at an output file's name, dangling into a work tree, is not written through", p);
        snprintf(p, sizeof p, "%s/out/sub", base); ok = symlink(repo, p) == 0;
        snprintf(p, sizeof p, "%s/out/sub/deeper", base); snprintf(q, sizeof q, "%s/deeper", repo);
        ok = ok && !mkdirs(p) && lstat(q, &st) != 0;
        snprintf(p, sizeof p, "%s/out/sub/image.wav", base); snprintf(q, sizeof q, "%s/image.wav", repo);
        ok = ok && !write_file(p, "x", 1) && lstat(q, &st) != 0;
        check(ok, "the guard: a symlink in the way of a subfolder carries no folder or file into a work tree", p);
        /* a hard link at an output file's name, to a file a work tree tracks (the fourth 0.14 review: O_TRUNC and the
           write went through it, and the tracked file showed as changed); both names must keep their bytes */
        snprintf(q, sizeof q, "%s/tracked.wav", repo);
        if ((f = fopen(q, "wb"))) { fputs("tracked", f); fclose(f); }
        snprintf(p, sizeof p, "%s/out/linked.wav", base);
        ok = link(q, p) == 0 && !write_file(p, "x", 1) && stat(q, &st) == 0 && st.st_size == 7 && st.st_nlink == 2;
        if (ok && (f = fopen(q, "rb"))) { char got[8] = { 0 }; ok = fread(got, 1, 7, f) == 7 && !memcmp(got, "tracked", 7); fclose(f); }
        check(ok, "the guard: a hard link at an output file's name, to a file in a work tree, is not written through", p);
        /* anything but a plain file at the name is refused, and a FIFO with no reader at once, not after a wait for one
           (a child with a five-second alarm makes the wait a failure rather than a hang) */
        snprintf(p, sizeof p, "%s/out/fifo.wav", base);
        ok = 0;
        if (mkfifo(p, 0644) == 0) {
            pid_t pid = fork(); int ws = 0;
            if (pid == 0) { alarm(5); _exit(write_file(p, "x", 1) ? 1 : 0); }
            ok = pid > 0 && waitpid(pid, &ws, 0) == pid && WIFEXITED(ws) && WEXITSTATUS(ws) == 0;
        }
        check(ok, "the guard: a FIFO at an output file's name is refused, at once", p);
        snprintf(p, sizeof p, "%s/out/image.wav", base);
        check(write_file(p, "x", 1), "the guard: a plain file outside is written", p);
        for (t = 0; t < (int)(sizeof made / sizeof made[0]); t++) { snprintf(p, sizeof p, "%s/%s", base, made[t]); unlink(p); }
        snprintf(p, sizeof p, "%s/deeper", repo); rmdir(p);
        rmdir(repo); snprintf(p, sizeof p, "%s/out", base); rmdir(p); snprintf(p, sizeof p, "%s/x", base); rmdir(p);
        if (rmdir(base) != 0) printf("  (could not remove %s)\n", base);
    guard_done: ;
    }
    snprintf(detail, sizeof detail, "%lu blocks, %zu bytes", g_blocks, g_now);
    check(g_now == 0 && g_blocks == 0, "everything given back", detail);
    printf("\n%d check%s failed\n", g_failed, g_failed == 1 ? "" : "s");
    return g_failed;
}

/* ---- inventory ---- */
static int arch_rank(const char *n)
{
    /* pull_weather.py's order, later wins: base, the plain archives, patch.MPQ, patch-<digit>, patch-<letter> */
    static const char *const plain[] = { "dbc.mpq", "fonts.mpq", "interface.mpq", "misc.mpq", "model.mpq", "sound.mpq", "speech.mpq", "terrain.mpq", "texture.mpq", "wmo.mpq", "backup.mpq" };
    int i;
    if (!strcasecmp(n, "base.mpq")) return 0;
    for (i = 0; i < 11; i++) if (!strcasecmp(n, plain[i])) return 100 + i;
    if (!strcasecmp(n, "patch.mpq")) return 200;
    if (!strncasecmp(n, "patch-", 6) && n[6] && !strcasecmp(n + 7, ".mpq")) {
        char c = n[6];
        if (c >= '0' && c <= '9') return 300 + (c - '0');
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c >= 'a' && c <= 'z') return 400 + (c - 'a');
    }
    return 500;
}
static int by_rank(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b; int d = arch_rank(x) - arch_rank(y);
    return d ? d : strcasecmp(x, y);
}
static int cmd_inventory(void)
{
    char *names[256]; int na = 0, s, a; DIR *d = opendir(g_data); struct dirent *e;
    if (!d) { printf("cannot open %s\n", g_data); return 1; }
    while ((e = readdir(d)) && na < 256) { size_t l = strlen(e->d_name); if (l > 4 && !strcasecmp(e->d_name + l - 4, ".mpq")) names[na++] = strdup(e->d_name); }
    closedir(d);
    qsort(names, (size_t)na, sizeof names[0], by_rank);
    printf("%d archives, in the patch order pull_weather.py used (later wins)\n", na);
    for (s = 0; s < SRC_COUNT; s++) {
        const snd_source_t *src = &SND_SOURCES[s]; char dll[256], last[256];
        int w; unsigned char *b = NULL; unsigned len = 0; char tried[480];
        tried[0] = 0; dll[0] = 0; last[0] = 0;
        printf("%s\n", src->path);
        for (a = 0; a < na; a++) {
            int r = mpq_read(&g_io, names[a], src->path, &b, &len);
            if (r == MPQ_OK) { printf("  %-14s %9u bytes  fnv %016llx\n", names[a], len, fnv1a(b, len)); snprintf(last, sizeof last, "%s (%u)", names[a], len); t_release(NULL, b); }
            else if (r != MPQ_E_NOT_FOUND && r != MPQ_E_NO_ARCHIVE) printf("  %-14s %s\n", names[a], mpq_error_text(r));
        }
        if (source_read(s, &b, &len, &w, tried, sizeof tried) == SRC_FOUND) { snprintf(dll, sizeof dll, "%s (%u)", src->where[w].archive, len); t_release(NULL, b); }
        printf("  the DLL takes: %s; the patch order would play: %s\n", dll[0] ? dll : "nothing", last[0] ? last : "nothing");
    }
    for (a = 0; a < na; a++) free(names[a]);
    return 0;
}

static int parse_fault(fault_t *f, const char *arg, int code)
{
    const char *colon = strchr(arg, ':'); size_t n = colon ? (size_t)(colon - arg) : strlen(arg);
    if (!n || n >= sizeof f->archive) return 0;
    memcpy(f->archive, arg, n); f->archive[n] = 0;
    f->left = colon ? strtol(colon + 1, NULL, 10) : 0;
    if (f->left <= 0) f->left = -1;   /* every time, until the retry */
    f->code = code;
    return 1;
}
int main(int argc, char **argv)
{
    const char *expect = NULL; int i;
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (i = 0; i < SRC_COUNT; i++) g_which[i] = -1;
    if (argc >= 2 && !strcmp(argv[1], "limits")) return cmd_limits();
    if (argc >= 3 && !strcmp(argv[1], "inventory")) {
        snprintf(g_data, sizeof g_data, "%s", argv[2]);
        return cmd_inventory();
    }
    if (argc >= 4 && (!strcmp(argv[1], "build") || !strcmp(argv[1], "settings") || !strcmp(argv[1], "rates"))) {
        snprintf(g_data, sizeof g_data, "%s", argv[2]);
        snprintf(g_out, sizeof g_out, "%s", argv[3]);
        for (i = 4; i < argc; i++) {
            if (!strcmp(argv[i], "--sources")) g_dump_sources = 1;
            else if (!strcmp(argv[i], "--expect") && i + 1 < argc) expect = argv[++i];
            else if (!strcmp(argv[i], "--fail-nth") && i + 1 < argc) g_fail_nth = strtoul(argv[++i], NULL, 10);
            else if (!strcmp(argv[i], "--fail-from") && i + 1 < argc) g_fail_from = strtoul(argv[++i], NULL, 10);
            else if (!strcmp(argv[i], "--unfit") && i + 1 < argc) g_unfit = argv[++i];
            else if (!strcmp(argv[i], "--mix-rate") && i + 1 < argc) {
                g_mix = atoi(argv[++i]);
                if (g_mix < SND_MIX_MIN || g_mix > SND_MIX_MAX) { fprintf(stderr, "--mix-rate: %d to %d Hz, the rates the DLL builds for\n", SND_MIX_MIN, SND_MIX_MAX); return 2; }
            }
            else if (!strcmp(argv[i], "--open-error") && i + 1 < argc) { if (!parse_fault(&g_open_fault, argv[++i], T_SHARING_VIOLATION)) return 2; }
            else if (!strcmp(argv[i], "--read-error") && i + 1 < argc) { if (!parse_fault(&g_read_fault, argv[++i], T_LOCK_VIOLATION)) return 2; }
            else if (!strcmp(argv[i], "--fpu-clobber")) {
#if defined(__i386__)
                g_fpu_clobber = 1;
#else
                fprintf(stderr, "--fpu-clobber needs the -m32 -mfpmath=387 build\n"); return 2;
#endif
            }
            else if (!strcmp(argv[i], "--hashes") && i + 1 < argc) g_hashes = argv[++i];
            else if (!strcmp(argv[i], "--no-write")) g_write_images = 0;
            else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
        }
        if (in_git_work_tree(g_out)) {
            fprintf(stderr, "refusing %s: it lands inside a git work tree (or where it lands cannot be told), and what this writes is made from the client's own sounds; name a scratch folder outside any repo\n", g_out);
            return 2;
        }
        if (!mkdirs(g_out)) { fprintf(stderr, "cannot make %s\n", g_out); return 2; }
        return !strcmp(argv[1], "build") ? cmd_build(expect) : !strcmp(argv[1], "rates") ? cmd_rates() : cmd_settings();
    }
    fprintf(stderr, "use: %s build <Data folder> <out folder> [--sources] [--expect ravencraft|no-patch-s] [--mix-rate R] [--fail-nth K]\n"
                    "        [--fail-from K] [--open-error ARCHIVE[:N]] [--read-error ARCHIVE[:N]] [--unfit FILE] [--fpu-clobber] [--hashes FILE] [--no-write]\n"
                    "     %s settings <Data folder> <out folder>\n"
                    "     %s rates <Data folder> <out folder> --mix-rate R\n"
                    "     %s limits\n"
                    "     %s inventory <Data folder>\n", argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 2;
}
