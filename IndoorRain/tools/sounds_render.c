/* sounds_render.c: the native Linux test of dll/dsp.c and dll/sounds.c. Not part of the DLL, and the only one
 * of these files that uses the C library (the DLL's own copies of the math are checked against libm here).
 *
 * Build, in tools/ (the binary goes outside the repo; -m32 -mfpmath=387 builds the DLL's own x87 arithmetic):
 *   gcc -O2 -Wall -Wextra -std=c99 -I../dll -o /tmp/sounds_render sounds_render.c ../dll/dsp.c ../dll/sounds.c -lm
 * Use:
 *   sounds_render mathtest
 *       dsp_sin, cos, tan, exp, log, log10, pow and sqrt against libm, over the ranges the recipes use and beyond
 *   sounds_render render <raw> <out> [mixer rate]
 *       all 25 sounds from <raw>/<archive>/<file name>: the client files extracted one folder per archive
 *       (raw/patch-S.mpq/RainHeavyLoop.wav), each source found by walking its where[] list as the DLL walks
 *       the archives, made ready for a mixer at the rate given (44,100 Hz, the clients', when none is) as the DLL
 *       makes it (snd_prepare). An Ogg source is read from the WAV decoded next to it (ffmpeg here, FMOD in the DLL).
 *       Writes <out>/<name>.wav and prints what it built, with the allocator's peak.
 *   sounds_render loudness <file.wav>...
 *       dsp_loudness of each file, to hold against ffmpeg -af ebur128
 *   sounds_render block <op> <in.wav> <out.wav> [args]
 *       one building block alone, written as 32-bit float WAV, to hold against the ffmpeg filter it ports:
 *       highpass F | lowpass F | compress | heavy | loopify SECONDS | fade IN_S OUT_S | limit CEILING ATTACK_MS RELEASE_MS
 *       | resample RATE PASS_HZ LOOP (compress is the rain's acompressor; heavy is RavenCraft's splice and loopify)
 *   sounds_render resampletest
 *       dsp_resample on test tones, for each rate change sounds.c makes and for a loop whose length the ratio does not
 *       divide: flat in the passband, a loop's loop point no different from any other moment, no image of the band
 *       coming through going up and nothing folded back going down (at least 90 dB down), a constant through
 *       unchanged, the lengths (a loop the ratio does not divide to the nearest frame: dsp.h's bound on the change
 *       of pitch, on the plain client's heavy loop for an 8,001 Hz mixer), and the refusals
 * It writes only where it is told to, and never into a git work tree (what it writes is made from the client's own
 * sounds); point <out> at a scratch folder, never at a game folder.
 */
#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "dsp.h"
#include "sounds.h"

/* ---- an allocator that keeps count, so the peak a build needs is measured, not guessed ---- */
static size_t g_now = 0, g_peak = 0;
static void *t_alloc(void *ctx, unsigned int n)
{
    size_t *p = malloc((size_t)n + 16); (void)ctx;
    if (!p) return NULL;
    p[0] = n; g_now += n; if (g_now > g_peak) g_peak = g_now;
    return (char *)p + 16;
}
static void t_release(void *ctx, void *q) { size_t *p = (size_t *)((char *)q - 16); (void)ctx; g_now -= p[0]; free(p); }
static const dsp_alloc_t A = { t_alloc, t_release, NULL };

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
static unsigned char *read_file(const char *path, unsigned int *size)
{
    FILE *f = fopen(path, "rb"); long n; unsigned char *d;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(n > 0 ? (size_t)n : 1);
    if (!d || fread(d, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    fclose(f); *size = (unsigned int)n;
    return d;
}
static int write_file(const char *path, const void *d, size_t n)
{
    FILE *f = create_out(path);
    if (!f) return 0;
    if (fwrite(d, 1, n, f) != n) { fclose(f); return 0; }
    return fclose(f) == 0;
}
static int load_wav(const char *path, dsp_buf_t *b)
{
    unsigned int n; unsigned char *d = read_file(path, &n); int r;
    if (!d) { fprintf(stderr, "cannot read %s\n", path); return 0; }
    r = dsp_wav_decode(b, d, n, &A); free(d);
    if (r != DSP_OK) { fprintf(stderr, "%s: %s\n", path, snd_why(r)); return 0; }
    return 1;
}
/* 32-bit float WAV, for block tests: the comparison with ffmpeg must not be blurred by 16-bit rounding */
static int write_float_wav(const char *path, const dsp_buf_t *b)
{
    unsigned int data = (unsigned int)(b->frames * b->channels) * 4u, h[11];
    FILE *f = create_out(path); int ok;
    if (!f) return 0;
    memcpy(&h[0], "RIFF", 4); h[1] = 36u + data; memcpy(&h[2], "WAVE", 4); memcpy(&h[3], "fmt ", 4); h[4] = 16;
    h[5] = 3u | ((unsigned int)b->channels << 16); h[6] = (unsigned int)b->rate; h[7] = (unsigned int)b->rate * (unsigned int)b->channels * 4u;
    h[8] = ((unsigned int)b->channels * 4u) | (32u << 16); memcpy(&h[9], "data", 4); h[10] = data;
    ok = fwrite(h, 4, 11, f) == 11 && fwrite(b->s, 4, (size_t)(b->frames * b->channels), f) == (size_t)(b->frames * b->channels);
    return fclose(f) == 0 && ok;
}
static double ms_since(const struct timespec *t0) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (t.tv_sec - t0->tv_sec) * 1e3 + (t.tv_nsec - t0->tv_nsec) / 1e6; }

/* ---- mathtest ---- */
static int check(const char *name, double worst, double tol, const char *kind)
{
    int ok = worst <= tol;
    printf("  %-6s worst %s error %.3g (tolerance %.0e) %s\n", name, kind, worst, tol, ok ? "ok" : "FAIL");
    return ok;
}
static int mathtest(void)
{
    double e_sin = 0, e_cos = 0, e_tan = 0, e_exp = 0, e_log = 0, e_l10 = 0, e_pow = 0, e_sqrt = 0, x, y, d;
    int i, j, ok = 1;
    for (i = 0; i <= 2000000; i++) {
        x = -20.0 + 40.0 * i / 2000000.0;   /* sin and cos: absolute error, over ranges far wider than pi */
        d = fabs(dsp_sin(x) - sin(x)); if (d > e_sin) e_sin = d;
        d = fabs(dsp_cos(x) - cos(x)); if (d > e_cos) e_cos = d;
    }
    for (i = 0; i <= 1000000; i++) {   /* tan: relative, on (-1.5, 1.5), which covers every pi f / rate below Nyquist */
        x = -1.5 + 3.0 * i / 1000000.0;
        d = fabs(dsp_tan(x) - tan(x)) / fmax(fabs(tan(x)), 1e-300); if (d > e_tan && fabs(x) > 1e-9) e_tan = d;
    }
    for (i = 0; i <= 1000000; i++) {   /* exp: relative, -700 to 700 */
        x = -700.0 + 1400.0 * i / 1000000.0;
        d = fabs(dsp_exp(x) - exp(x)) / exp(x); if (d > e_exp) e_exp = d;
    }
    for (i = 0; i <= 1000000; i++) {   /* log, log10: 1e-300 to 1e300 log-spaced, plus the stretch around 1 where log is near 0; */
        x = pow(10.0, -300.0 + 600.0 * i / 1000000.0);   /* error relative to max(|log x|, 1): ulps far out, absolute near 1 */
        d = fabs(dsp_log(x) - log(x)) / fmax(fabs(log(x)), 1.0); if (d > e_log) e_log = d;
        d = fabs(dsp_log10(x) - log10(x)) / fmax(fabs(log10(x)), 1.0); if (d > e_l10) e_l10 = d;
        x = 0.5 + 1.5 * i / 1000000.0;
        d = fabs(dsp_log(x) - log(x)); if (d > e_log) e_log = d;
    }
    for (i = 0; i <= 2000; i++) for (j = 0; j <= 200; j++) {   /* pow: relative, x 1e-4 to 1e4, y -4 to 4 */
        x = pow(10.0, -4.0 + 8.0 * i / 2000.0); y = -4.0 + 8.0 * j / 200.0;
        d = fabs(dsp_pow(x, y) - pow(x, y)) / pow(x, y); if (d > e_pow) e_pow = d;
    }
    for (i = 0; i <= 1000000; i++) {   /* sqrt: relative, 1e-290 to 1e290 */
        x = pow(10.0, -290.0 + 580.0 * i / 1000000.0);
        d = fabs(dsp_sqrt(x) - sqrt(x)) / sqrt(x); if (d > e_sqrt) e_sqrt = d;
    }
    printf("mathtest: our functions against libm\n");
    ok &= check("sin", e_sin, 1e-14, "absolute");
    ok &= check("cos", e_cos, 1e-14, "absolute");
    ok &= check("tan", e_tan, 1e-13, "relative");
    ok &= check("exp", e_exp, 1e-13, "relative");
    ok &= check("log", e_log, 1e-15, "scaled");
    ok &= check("log10", e_l10, 1e-15, "scaled");
    ok &= check("pow", e_pow, 1e-12, "relative");
    ok &= check("sqrt", e_sqrt, 1e-15, "relative");
    /* the coefficients the recipes compute, which is what the accuracy is for */
    {
        dsp_biquad_t q; double w0 = 2.0 * M_PI * 160.0 / 22050.0, c = cos(w0), al = sin(w0) / (2.0 * 0.707), a0 = 1.0 + al;
        dsp_biquad_highpass(&q, 160.0, 0.707, 22050);
        d = fmax(fabs(q.b0 - (1 + c) / 2 / a0), fmax(fabs(q.a1 + 2 * c / a0), fabs(q.a2 - (1 - al) / a0)));
        ok &= check("hp160", d, 1e-14, "coefficient");
        d = fabs(dsp_db2lin(-22.0) - pow(10.0, -22.0 / 20.0)) / pow(10.0, -22.0 / 20.0);
        ok &= check("-22dB", d, 1e-14, "relative");
    }
    printf("%s\n", ok ? "mathtest: all ok" : "mathtest: FAILED");
    return ok ? 0 : 1;
}

/* ---- render ---- */
static const char *base_name(const char *path) { const char *b = path, *p; for (p = path; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }
static long file_size(const char *path) { struct stat st; return stat(path, &st) == 0 ? (long)st.st_size : -1; }

/* Finds a source the way the DLL will: the first where[] archive that holds the file at the required size. */
static int find_source(const char *raw, int src, snd_input_t *in, char *used, size_t cap)
{
    const snd_source_t *s = &SND_SOURCES[src]; int w;
    for (w = 0; w < SND_WHERE_MAX && s->where[w].archive; w++) {
        char path[1024]; long sz;
        snprintf(path, sizeof path, "%s/%s/%s", raw, s->where[w].archive, base_name(s->path));
        sz = file_size(path);
        if (sz < 0) continue;
        if (s->where[w].size && (unsigned long)sz != s->where[w].size) { printf("  (%s in %s is %ld bytes, not %u: next archive)\n", base_name(s->path), s->where[w].archive, sz, s->where[w].size); continue; }
        if (s->format == SND_OGG) { char *dot = strrchr(path, '.'); if (dot) strcpy(dot, ".wav"); }   /* what FMOD would hand over */
        if (!load_wav(path, &in->pcm)) return 0;
        in->variant = s->where[w].variant; in->mix_rate = 0;
        snprintf(used, cap, "%s/%s", s->where[w].archive, base_name(s->path));
        return 1;
    }
    return 0;
}
static int render(const char *raw, const char *outdir, int mix)
{
    snd_input_t in[SRC_COUNT]; char used[SRC_COUNT][300]; int have[SRC_COUNT], o, s, failed = 0, built = 0;
    size_t total = 0;
    memset(in, 0, sizeof in); memset(have, 0, sizeof have);
    for (o = 0; o < SND_COUNT; o++) {
        const snd_output_t *so = &SND_OUTPUTS[o]; void *wav; unsigned int n; int r; struct timespec t0; size_t base; double ms, lufs = 0.0;
        s = so->source;
        if (!have[s]) {
            have[s] = find_source(raw, s, &in[s], used[s], sizeof used[s]) ? 1 : -1;
            if (have[s] < 0) printf("%-20s source %s found in no archive%s\n", so->name, base_name(SND_SOURCES[s].path), SND_SOURCES[s].optional ? " (optional: no such sound)" : "");
            else {   /* as the DLL makes each source ready once, for both of its outputs */
                clock_gettime(CLOCK_MONOTONIC, &t0);
                if ((r = snd_prepare(s, &in[s], mix, &A)) != DSP_OK) { printf("%-20s cannot prepare %s: %s\n", so->name, base_name(SND_SOURCES[s].path), snd_why(r)); dsp_free(&in[s].pcm, &A); have[s] = -1; failed++; continue; }
                printf("%-20s %s ready for a %d Hz mixer in %.0f ms\n", "", base_name(SND_SOURCES[s].path), mix, ms_since(&t0));
            }
        }
        if (have[s] < 0) { failed += !SND_SOURCES[s].optional; continue; }
        base = g_now; g_peak = g_now;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        r = snd_build(o, &in[s], &wav, &n, &A);
        ms = ms_since(&t0);
        if (r != DSP_OK) { printf("%-20s FAILED: %s\n", so->name, snd_why(r)); failed++; continue; }
        {
            char path[1024]; dsp_buf_t back;
            snprintf(path, sizeof path, "%s/%s.wav", outdir, so->name);
            if (!write_file(path, wav, n)) { printf("%-20s cannot write %s\n", so->name, path); failed++; }
            if (dsp_wav_decode(&back, wav, n, &A) == DSP_OK) {
                dsp_loudness(&back, &lufs, &A);
                printf("%-20s %-44s v%d %6d Hz %d ch %7.3f s %8u B  %6.2f LUFS  %5.0f ms  peak %4.1f MB over the source\n", so->name, used[s], in[s].variant,
                       back.rate, back.channels, (double)back.frames / back.rate, n, lufs, ms, (double)(g_peak - base) / 1048576.0);
                dsp_free(&back, &A);
            }
        }
        total += n; built++;
        t_release(NULL, wav);
    }
    for (s = 0; s < SRC_COUNT; s++) if (have[s] > 0) dsp_free(&in[s].pcm, &A);
    printf("built %d of %d, %.1f MB of WAV in all; %s\n", built, SND_COUNT, (double)total / 1048576.0, failed ? "SOME FAILED" : "none failed");
    return failed ? 1 : 0;
}

static int loudness(int n, char **files)
{
    int i, bad = 0;
    for (i = 0; i < n; i++) {
        dsp_buf_t b; double l; int r;
        if (!load_wav(files[i], &b)) { bad = 1; continue; }
        r = dsp_loudness(&b, &l, &A);
        if (r == DSP_OK) printf("%-40s %8.3f LUFS\n", base_name(files[i]), l); else { printf("%-40s %s\n", base_name(files[i]), snd_why(r)); bad = 1; }
        dsp_free(&b, &A);
    }
    return bad;
}

static int block(int argc, char **argv)
{
    const char *op = argv[0], *in = argv[1], *out = argv[2];
    dsp_buf_t b, o; int r = DSP_OK;
    if (!load_wav(in, &b)) return 1;
    if (!strcmp(op, "highpass") && argc > 3) dsp_highpass(&b, atof(argv[3]), 0);
    else if (!strcmp(op, "lowpass") && argc > 3) dsp_lowpass(&b, atof(argv[3]), 0);
    else if (!strcmp(op, "compress")) {
        dsp_comp_t c = { 1.0, 0, 8.0, 3.0, 400.0, 2.0, 2.82843, 1, 0 };
        c.threshold = dsp_db2lin(-22.0);
        dsp_compress(&b, &c, 0);
    }
    else if (!strcmp(op, "heavy")) {   /* the splice and loopify of sounds.c's heavy_loop, alone */
        dsp_buf_t p[3]; int x[2] = { dsp_frames(b.rate, 1.5), 0 };
        dsp_view(&p[0], &b, dsp_frames(b.rate, 22.5), dsp_frames(b.rate, 41.5));
        dsp_view(&p[1], &b, dsp_frames(b.rate, 57.0), dsp_frames(b.rate, 75.0));
        dsp_view(&p[2], &b, 0, dsp_frames(b.rate, 11.5));
        if ((r = dsp_join(&o, p, x, 3, DSP_QSIN, &A)) == DSP_OK) { r = dsp_loopify(&o, dsp_frames(b.rate, 1.5), DSP_QSIN); dsp_free(&b, &A); b = o; }
    }
    else if (!strcmp(op, "loopify") && argc > 3) r = dsp_loopify(&b, dsp_frames(b.rate, atof(argv[3])), DSP_QSIN);
    else if (!strcmp(op, "fade") && argc > 4) dsp_fade(&b, dsp_frames(b.rate, atof(argv[3])), dsp_frames(b.rate, atof(argv[4])), DSP_TRI);
    else if (!strcmp(op, "limit") && argc > 5) r = dsp_limit(&b, atof(argv[3]), atof(argv[4]), atof(argv[5]), 0, &A);
    else if (!strcmp(op, "resample") && argc > 5) r = dsp_resample(&b, atoi(argv[3]), atof(argv[4]), atoi(argv[5]), &A);
    else { fprintf(stderr, "unknown block %s\n", op); return 2; }
    if (r != DSP_OK) { fprintf(stderr, "%s: %s\n", op, snd_why(r)); return 1; }
    r = !write_float_wav(out, &b);
    dsp_free(&b, &A);
    return r;
}

/* ---- resampletest ----
 * dsp_resample on test tones, for each rate change sounds.c makes on the clients (their sources' 22,050 and 48,000 Hz
 * to the mixer's 44,100), a gust to a 22,050 Hz mixer, and a loop whose length the ratio does not divide (22,050 to a
 * 48,000 Hz mixer, an odd length), which takes the table's in-between rows. Tones are whole numbers of cycles in the
 * buffer, so a loop of one is a seamless circle, and the resampled loop must be the same tone at the new rate, every
 * sample of it, round the loop point too. */
static int dt_fails = 0;
static void dt_check(int ok, const char *what) { printf("  %-112s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) dt_fails++; }
/* a tone of k cycles in n frames (1 or 2 channels, the second a quarter cycle on), as a loop or a one-shot */
static void tone(dsp_buf_t *b, int n, int ch, int rate, int k, double amp)
{
    int i, c;
    dsp_new(b, n, ch, rate, &A);
    for (i = 0; i < n; i++) for (c = 0; c < ch; c++) b->s[i * ch + c] = (float)(amp * sin(2.0 * M_PI * k * (double)i / n + c * M_PI / 2.0));
}
static double rms(const dsp_buf_t *b, int from, int to)
{
    double s = 0.0; int i, n = 0;
    for (i = from * b->channels; i < to * b->channels; i++, n++) s += (double)b->s[i] * b->s[i];
    return n ? sqrt(s / n) : 0.0;
}
/* the amplitude of channel 0 at a whole number of cycles over the buffer: one bin of its DFT */
static double bin_amp(const dsp_buf_t *b, double cycles)
{
    double re = 0.0, im = 0.0; int i;
    for (i = 0; i < b->frames; i++) { double ph = 2.0 * M_PI * cycles * (double)i / b->frames; re += b->s[i * b->channels] * cos(ph); im -= b->s[i * b->channels] * sin(ph); }
    return 2.0 * sqrt(re * re + im * im) / b->frames;
}
static int resampletest(void)
{
    static const struct { int from, to, n, loop; const char *what; } C[] = {
        { 22050, 44100, 44100, 1, "the sources' 22,050 Hz to the mixer's 44,100, a loop (the rain, snow and sand)" },
        { 22050, 44100, 44101, 0, "the same, a one-shot of an odd length (the thunder)" },
        { 48000, 44100, 96000, 1, "RavenCraft's heavy loop, 48,000 to 44,100 Hz" },
        { 22050, 48000, 44101, 1, "22,050 Hz to a 48,000 Hz mixer, a loop the ratio does not divide (the table's in-between rows)" },
        { 44100, 22050, 88201, 0, "a gust to a 22,050 Hz mixer, a one-shot, down by half" } };
    char what[300]; int t;
    for (t = 0; t < (int)(sizeof C / sizeof C[0]); t++) {
        const int from = C[t].from, to = C[t].to, n = C[t].n, loop = C[t].loop, lower = from < to ? from : to;
        const double pass = 0.45 * lower, secs = (double)n / from;
        double worst_pass = 0.0, worst_img = -400.0, worst_fold = -400.0, f; int k, i, ch, m = 0, edge = 0;
        printf("%s\n", C[t].what);
        /* the passband: tones from 20 Hz to its edge come through unchanged, sample for sample at the new rate: a loop
           everywhere, a one-shot away from its ends (which start and stop the tone from silence) */
        for (f = 20.0; f <= pass; f *= 1.25) {
            dsp_buf_t b; double e = 0.0;
            k = (int)(f * secs + 0.5);
            tone(&b, n, 2, from, k, 0.5);
            if (dsp_resample(&b, to, pass, loop, &A) != DSP_OK) { dt_check(0, "resample"); dsp_free(&b, &A); continue; }
            m = b.frames; edge = loop ? 0 : 400;
            for (ch = 0; ch < 2; ch++) for (i = edge; i < m - edge; i++) {
                double want = 0.5 * sin(2.0 * M_PI * k * (double)i / (loop ? m : (double)to * secs) + ch * M_PI / 2.0), d = fabs(b.s[i * 2 + ch] - want);
                if (d > e) e = d;
            }
            if (e > worst_pass) worst_pass = e;
            dsp_free(&b, &A);
        }
        snprintf(what, sizeof what, "passband: every tone to %.0f Hz the same tone at %d Hz, %s (worst sample error %.2e of 0.5)", pass, to,
                 loop ? "round the loop point too" : "away from the ends", worst_pass);
        dt_check(worst_pass <= 0.5 * 0.0012, what);   /* 0.01 dB of ripple is 0.12% of the amplitude */
        if (to > from) {
            /* going up: the image of every tone below the old Nyquist frequency, at the old rate less the tone (and plus
               it, where that is under the new Nyquist frequency), at least 90 dB down; a loop, so the DFT bins are exact */
            if (loop) {
                for (f = 20.0; f < from / 2.0 - 1.0; f += (f < 1000.0 ? f * 0.5 : (from / 2.0 - 1000.0) / 60.0)) {
                    dsp_buf_t b; double g;
                    k = (int)(f * secs + 0.5);
                    if (k >= n / 2) break;
                    tone(&b, n, 1, from, k, 0.5);
                    if (dsp_resample(&b, to, pass, loop, &A) != DSP_OK) { dt_check(0, "resample"); dsp_free(&b, &A); continue; }
                    g = 20.0 * log10(bin_amp(&b, n - k) / 0.5 + 1e-30);
                    if (g > worst_img) worst_img = g;
                    if (n + k < b.frames / 2) { g = 20.0 * log10(bin_amp(&b, n + k) / 0.5 + 1e-30); if (g > worst_img) worst_img = g; }
                    dsp_free(&b, &A);
                }
                snprintf(what, sizeof what, "images: of tones from 20 Hz to %.0f Hz, every one at least %.0f dB down (worst %.1f dB)", from / 2.0, DSP_RESAMPLE_STOP_DB, worst_img);
                dt_check(worst_img <= -DSP_RESAMPLE_STOP_DB, what);
            }
        } else {
            /* going down: tones above the new Nyquist frequency fold back into nothing, at least 90 dB down (a one-shot
               away from its ends) */
            for (f = to / 2.0 + 1.0; f < from / 2.0 - 1.0; f += (from / 2.0 - to / 2.0) / 199.0) {
                dsp_buf_t b; double g;
                k = (int)(f * secs + 0.5);
                if (k <= (int)(to / 2.0 * secs)) k = (int)(to / 2.0 * secs) + 1;
                if (k >= n / 2) break;
                tone(&b, n, 1, from, k, 0.5);
                if (dsp_resample(&b, to, pass, loop, &A) != DSP_OK) { dt_check(0, "resample"); dsp_free(&b, &A); continue; }
                edge = loop ? 0 : 400;
                g = 20.0 * log10(rms(&b, edge, b.frames - edge) / (0.5 / sqrt(2.0)) + 1e-30);
                if (g > worst_fold) worst_fold = g;
                dsp_free(&b, &A);
            }
            snprintf(what, sizeof what, "stopband: 200 tones from %.0f to %.0f Hz, every one at least %.0f dB down (worst %.1f dB)", to / 2.0, from / 2.0, DSP_RESAMPLE_STOP_DB, worst_fold);
            dt_check(worst_fold <= -DSP_RESAMPLE_STOP_DB, what);
        }
        /* a constant, and the lengths */
        {
            dsp_buf_t b; double e = 0.0; long long want = loop ? ((long long)n * to + from / 2) / from : ((long long)n * to + from - 1) / from;
            dsp_new(&b, n, 2, from, &A);
            for (i = 0; i < 2 * n; i++) b.s[i] = 0.25f;
            dsp_resample(&b, to, pass, loop, &A);
            for (i = (loop ? 0 : 400) * 2; i < 2 * (b.frames - (loop ? 0 : 400)); i++) if (fabs(b.s[i] - 0.25) > e) e = fabs(b.s[i] - 0.25);
            snprintf(what, sizeof what, "a constant comes through unchanged (worst %.2e); %d frames at %d Hz (want %lld: %s)", e, b.frames, b.rate, want,
                     loop ? "the loop's length at the new rate, to the nearest frame" : "every frame that reaches into the one-shot");
            dt_check(e < 1e-6 && b.frames == want && b.rate == to && b.channels == 2, what);
            dsp_free(&b, &A);
        }
    }
    /* A loop the ratio does not divide, at the lowest mixer rates: the plain client's heavy rain loop, 23.5 s at
       22,050 Hz (518,175 frames, an odd number), for a mixer at 8,001 Hz. It must come out at the nearest whole number
       of frames, 188,024 for 188,023.5, half a frame long, a change of pitch of 2.7 millionths, which dsp.h puts under
       3.2 for a loop of 20 s or more at 8 kHz or more (the fourth 0.14 review: dsp.h said under a millionth); and the
       100 Hz tone of whole cycles it holds must come through whole round the loop point, as the same number of
       cycles. */
    {
        const int from = 22050, to = 8001, n = 518175, k = 2350;
        const double ideal = (double)n * to / from;
        dsp_buf_t b; double e = 0.0, stretch = 0.0; int i, r;
        tone(&b, n, 1, from, k, 0.5);
        if ((r = dsp_resample(&b, to, 0.45 * to, 1, &A)) == DSP_OK) {
            stretch = (b.frames - ideal) / ideal;
            for (i = 0; i < b.frames; i++) { double d = fabs(b.s[i] - 0.5 * sin(2.0 * M_PI * k * (double)i / b.frames)); if (d > e) e = d; }
        }
        snprintf(what, sizeof what, "the plain client's 23.5 s heavy loop for an 8,001 Hz mixer: %d frames for %.1f, a change of pitch of %.2f millionths "
                 "(dsp.h: under 3.2), the tone whole round the loop point (worst %.2e of 0.5)", r == DSP_OK ? b.frames : -1, ideal, stretch * 1e6, e);
        dt_check(r == DSP_OK && b.frames == (int)(ideal + 0.5) && stretch > 0.0 && stretch * ideal <= 0.5 && stretch < 3.2e-6 && e <= 0.5 * 0.0012, what);
        dsp_free(&b, &A);
    }
    /* what it refuses, leaving the buffer as it was */
    {
        dsp_buf_t b; int r1, r2, r3, r4, r5, r6, same;
        tone(&b, 44101, 1, 22050, 10, 0.5);
        r1 = dsp_resample(&b, 44100, 11025.0, 1, &A);   /* a passband up to the lower Nyquist frequency */
        r2 = dsp_resample(&b, 800, 300.0, 0, &A);       /* a rate under 1,000 Hz */
        r3 = dsp_resample(&b, 44100, -5.0, 1, &A);      /* a passband of nothing */
        r4 = dsp_resample(&b, 44100, 11020.0, 0, &A);   /* a transition band of 5 Hz: a window over 4,096 frames wide */
        r5 = dsp_resample(&b, 22051, 10900.0, 0, &A);   /* 22,051 steps and a 125 Hz band: a table past 8 MB */
        r6 = dsp_resample(&b, 96001, 9000.0, 0, &A);    /* 96,001 steps: past the 65,535 that keep its sums in 32 bits */
        same = b.frames == 44101 && b.rate == 22050 && b.owned;
        snprintf(what, sizeof what, "refused: a passband to Nyquist (%d), a rate under 1 kHz (%d), no passband (%d), a 5 Hz band (%d), a table past 8 MB (%d), "
                 "96,001 steps (%d); buffer untouched", r1, r2, r3, r4, r5, r6);
        dt_check(r1 == DSP_BADARG && r2 == DSP_BADARG && r3 == DSP_BADARG && r4 == DSP_BADARG && r5 == DSP_BADARG && r6 == DSP_BADARG && same, what);
        dsp_free(&b, &A);
    }
    printf("%s\n", dt_fails ? "resampletest: FAILED" : "resampletest: all ok");
    return dt_fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "mathtest")) return mathtest();
    if (argc >= 2 && !strcmp(argv[1], "resampletest")) return resampletest();
    if ((argc >= 4 && !strcmp(argv[1], "render") && in_git_work_tree(argv[3])) || (argc >= 5 && !strcmp(argv[1], "block") && in_git_work_tree(argv[4]))) {
        fprintf(stderr, "refusing %s: it lands inside a git work tree (or where it lands cannot be told), and what this writes is made from the client's own sounds; name a scratch folder outside any repo\n",
                !strcmp(argv[1], "render") ? argv[3] : argv[4]);
        return 2;
    }
    if (argc >= 4 && !strcmp(argv[1], "render")) return render(argv[2], argv[3], argc >= 5 ? atoi(argv[4]) : 44100);
    if (argc >= 3 && !strcmp(argv[1], "loudness")) return loudness(argc - 2, argv + 2);
    if (argc >= 5 && !strcmp(argv[1], "block")) return block(argc - 2, argv + 2);
    fprintf(stderr, "usage: %s mathtest | resampletest | render <raw> <out> [mixer rate] | loudness <wav>... | block <op> <in.wav> <out.wav> [args]\n", argv[0]);
    return 2;
}
