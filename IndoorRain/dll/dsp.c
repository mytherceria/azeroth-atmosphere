/* dsp.c: the audio building blocks declared in dsp.h (read that first). No C runtime: the math, the WAV
 * reading and writing and every loop are written here, and memory comes from the caller's allocator. GPL-3.0.
 * One block is not ours alone: the compressor is a port of FFmpeg's, and its notice stands above it.
 */
#include "dsp.h"

/* ---- math without libm ----
 * Textbook range reduction and series, in double precision. The recipes only need tame arguments (angles
 * up to pi, exponents of a few tens, levels in dB), so nothing here handles infinity or NaN: input out of
 * range is clamped instead. gcc builds i686 without SSE on the x87 unit, which evaluates in 80 bits; that
 * only helps. tools/sounds_render.c (mathtest) holds every function against libm. */
typedef union { double d; unsigned int w[2]; } dbits_t;   /* x86 is little-endian: w[1] holds sign, exponent and the top 20 mantissa bits */

#define PI       3.14159265358979323846
#define LN2_HI   6.93147180369123816490e-01   /* ln 2 and pi/2 each split in two (fdlibm's constants), so that k ln 2 */
#define LN2_LO   1.90821492927058770002e-10   /* and k pi/2 come off an argument without taking its low bits with them */
#define PIO2_HI  1.57079632673412561417e+00
#define PIO2_LO  6.07710050650619224932e-11
#define INV_LN2  1.44269504088896338700
#define LN10     2.30258509299404568402
#define SQRT2    1.41421356237309504880

/* Taylor coefficients, highest power last; each series stops where the next term is under 1e-17 on its range */
static const double EXP_C[14] = { 1.0, 1.0, 1.0 / 2, 1.0 / 6, 1.0 / 24, 1.0 / 120, 1.0 / 720, 1.0 / 5040, 1.0 / 40320, 1.0 / 362880,
    1.0 / 3628800, 1.0 / 39916800, 1.0 / 479001600, 1.0 / 6227020800.0 };
static const double LOG_C[11] = { 1.0, 1.0 / 3, 1.0 / 5, 1.0 / 7, 1.0 / 9, 1.0 / 11, 1.0 / 13, 1.0 / 15, 1.0 / 17, 1.0 / 19, 1.0 / 21 };
static const double SIN_C[9] = { 1.0, -1.0 / 6, 1.0 / 120, -1.0 / 5040, 1.0 / 362880, -1.0 / 39916800, 1.0 / 6227020800.0,
    -1.0 / 1307674368000.0, 1.0 / 355687428096000.0 };
static const double COS_C[9] = { 1.0, -1.0 / 2, 1.0 / 24, -1.0 / 720, 1.0 / 40320, -1.0 / 3628800, 1.0 / 479001600,
    -1.0 / 87178291200.0, 1.0 / 20922789888000.0 };

static double horner(const double *c, int n, double x) { double p = c[n - 1]; int i; for (i = n - 2; i >= 0; i--) p = p * x + c[i]; return p; }

double dsp_sqrt(double x)
{
    dbits_t u; double y; int i;
    if (!(x > 1e-300)) return 0.0;
    /* halving the exponent bits gives a first guess within 6%; each Newton step doubles the correct bits */
    u.d = x; u.w[1] = (u.w[1] >> 1) + 0x1FF80000u; u.w[0] = 0;
    y = u.d;
    for (i = 0; i < 5; i++) y = 0.5 * (y + x / y);
    return y;
}

double dsp_exp(double x)
{
    dbits_t s; double r; int k;
    if (x > 709.0) x = 709.0;
    if (x < -708.0) return 0.0;
    k = (int)(x * INV_LN2 + (x < 0.0 ? -0.5 : 0.5));   /* x = k ln 2 + r, |r| <= ln 2 / 2 */
    r = (x - k * LN2_HI) - k * LN2_LO;
    s.w[0] = 0; s.w[1] = (unsigned int)(k + 1023) << 20;   /* 2^k, built from its exponent bits */
    return s.d * horner(EXP_C, 14, r);
}

double dsp_log(double x)
{
    dbits_t u; double m, s; int e;
    if (!(x > 0.0)) return -745.0;   /* below the smallest double's log: a floor, never -inf */
    u.d = x; e = (int)(u.w[1] >> 20) & 0x7FF;
    if (e == 0x7FF) return 709.8;
    if (e == 0) { u.d = x * 18014398509481984.0; e = ((int)(u.w[1] >> 20) & 0x7FF) - 54; }   /* subnormal: scaled by 2^54 first */
    e -= 1023;
    u.w[1] = (u.w[1] & 0x000FFFFFu) | 0x3FF00000u;   /* the mantissa alone, a number in [1, 2) */
    m = u.d;
    if (m > SQRT2) { m *= 0.5; e++; }                  /* now in [sqrt(1/2), sqrt(2)] */
    s = (m - 1.0) / (m + 1.0);                         /* log m = 2 atanh s = 2 (s + s^3/3 + s^5/5 + ...), |s| < 0.172 */
    return e * LN2_HI + (2.0 * s * horner(LOG_C, 11, s * s) + e * LN2_LO);
}

double dsp_log10(double x) { return dsp_log(x) * (1.0 / LN10); }
double dsp_pow(double x, double y) { return x > 0.0 ? dsp_exp(y * dsp_log(x)) : 0.0; }
double dsp_db2lin(double db) { return dsp_exp(db * (LN10 / 20.0)); }

/* sin and cos: x = k pi/2 + r with |r| <= pi/4, then the series for r and the quadrant k picks */
static double reduce(double x, int *q)
{
    int k;
    if (x > 1e9 || x < -1e9) x = 0.0;   /* never asked for; keeps k inside an int */
    k = (int)(x * (2.0 / PI) + (x < 0.0 ? -0.5 : 0.5));
    *q = k & 3;
    return (x - k * PIO2_HI) - k * PIO2_LO;
}
static double sin_r(double r) { return r * horner(SIN_C, 9, r * r); }
static double cos_r(double r) { return horner(COS_C, 9, r * r); }
double dsp_sin(double x) { int q; double r = reduce(x, &q); return q == 0 ? sin_r(r) : q == 1 ? cos_r(r) : q == 2 ? -sin_r(r) : -cos_r(r); }
double dsp_cos(double x) { int q; double r = reduce(x, &q); return q == 0 ? cos_r(r) : q == 1 ? -sin_r(r) : q == 2 ? -cos_r(r) : sin_r(r); }
double dsp_tan(double x) { return dsp_sin(x) / dsp_cos(x); }

/* ---- buffers ---- */
int dsp_frames(int rate, double seconds) { return seconds <= 0.0 ? 0 : (int)(seconds * rate + 0.5); }

int dsp_new(dsp_buf_t *b, int frames, int channels, int rate, const dsp_alloc_t *a)
{
    unsigned int bytes;
    b->s = 0; b->frames = 0; b->channels = channels; b->rate = rate; b->owned = 0;
    /* under 1 GB of floats, so the byte count below cannot wrap */
    if (frames < 0 || channels < 1 || channels > DSP_MAX_CHANNELS || rate < 1 || frames > 0x0FFFFFFF / channels) return DSP_BADARG;
    bytes = (unsigned int)frames * (unsigned int)channels * 4u;
    b->s = (float *)a->alloc(a->ctx, bytes ? bytes : 4u);
    if (!b->s) return DSP_NOMEM;
    b->frames = frames; b->owned = 1;
    return DSP_OK;
}

int dsp_copy(dsp_buf_t *dst, const dsp_buf_t *src, const dsp_alloc_t *a)
{
    int r = dsp_new(dst, src->frames, src->channels, src->rate, a), i, n = src->frames * src->channels;
    if (r != DSP_OK) return r;
    for (i = 0; i < n; i++) dst->s[i] = src->s[i];
    return DSP_OK;
}

void dsp_free(dsp_buf_t *b, const dsp_alloc_t *a)
{
    if (b->owned && b->s) a->release(a->ctx, b->s);
    b->s = 0; b->frames = 0; b->owned = 0;
}

int dsp_view(dsp_buf_t *v, const dsp_buf_t *b, int start, int end)
{
    if (start < 0 || end < start || end > b->frames) return DSP_BADARG;
    v->s = b->s + start * b->channels; v->frames = end - start; v->channels = b->channels; v->rate = b->rate; v->owned = 0;
    return DSP_OK;
}

/* ---- WAV ---- */
static unsigned int rd16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int rd32(const unsigned char *p) { return rd16(p) | (rd16(p + 2) << 16); }
static int is_id(const unsigned char *p, const char *id) { return p[0] == id[0] && p[1] == id[1] && p[2] == id[2] && p[3] == id[3]; }

/* The header of a WAV this reads: its format, and where its samples are (the data chunk, clipped to the file) */
typedef struct { const unsigned char *pcm; unsigned int pcm_len, tag, ch, rate, bits; } wav_hdr_t;
static int wav_header(const unsigned char *d, unsigned int size, wav_hdr_t *w)
{
    const unsigned char *fmt = 0; unsigned int pos = 12, fmt_len = 0;
    w->pcm = 0; w->pcm_len = 0;
    if (!d || size < 12 || !is_id(d, "RIFF") || !is_id(d + 8, "WAVE")) return DSP_BADWAV;
    /* walk every chunk: the data chunk need not come last, and a chunk of odd length is padded to even */
    while (pos <= size - 8) {   /* size >= 12, so this cannot wrap */
        unsigned int len = rd32(d + pos + 4), body = pos + 8;
        if (len > size - body) len = size - body;   /* a truncated last chunk: take what is there */
        if (is_id(d + pos, "fmt ") && !fmt) { fmt = d + body; fmt_len = len; }
        else if (is_id(d + pos, "data") && !w->pcm) { w->pcm = d + body; w->pcm_len = len; }
        if (len + (len & 1u) > size - body) break;
        pos = body + len + (len & 1u);
    }
    if (!fmt || fmt_len < 16 || !w->pcm) return DSP_BADWAV;
    w->tag = rd16(fmt); w->ch = rd16(fmt + 2); w->rate = rd32(fmt + 4); w->bits = rd16(fmt + 14);
    if (w->tag == 0xFFFEu && fmt_len >= 26) w->tag = rd16(fmt + 24);   /* WAVE_FORMAT_EXTENSIBLE: its sub-format GUID opens with the plain tag */
    if (w->ch < 1 || w->ch > DSP_MAX_CHANNELS || w->rate < 1000u || w->rate > 768000u) return DSP_BADWAV;
    if (!((w->tag == 1u && (w->bits == 8u || w->bits == 16u || w->bits == 24u || w->bits == 32u)) || (w->tag == 3u && w->bits == 32u))) return DSP_BADWAV;
    return DSP_OK;
}

int dsp_wav_info(const void *data, unsigned int size, unsigned int *frames, int *channels, int *rate)
{
    wav_hdr_t w; int r = wav_header((const unsigned char *)data, size, &w);
    *frames = 0; *channels = 0; *rate = 0;
    if (r != DSP_OK) return r;
    *frames = w.pcm_len / (w.ch * (w.bits / 8u)); *channels = (int)w.ch; *rate = (int)w.rate;
    return DSP_OK;
}

int dsp_wav_decode(dsp_buf_t *out, const void *data, unsigned int size, const dsp_alloc_t *a)
{
    wav_hdr_t w; unsigned int tag, ch, bits, bpf, i, n; const unsigned char *pcm;
    int r = wav_header((const unsigned char *)data, size, &w);
    out->s = 0; out->frames = 0; out->owned = 0;
    if (r != DSP_OK) return r;
    tag = w.tag; ch = w.ch; bits = w.bits; pcm = w.pcm;
    bpf = ch * (bits / 8u);   /* from the format itself: some writers get the block-align field wrong */
    if ((r = dsp_new(out, (int)(w.pcm_len / bpf), (int)ch, (int)w.rate, a)) != DSP_OK) return r;
    n = (unsigned int)out->frames * ch;
    for (i = 0; i < n; i++) {
        const unsigned char *p = pcm + i * (bits / 8u);
        float v;
        if (bits == 8u) v = (float)((int)p[0] - 128) * (1.0f / 128.0f);   /* 8-bit WAV is unsigned */
        else if (bits == 16u) v = (float)(short)rd16(p) * (1.0f / 32768.0f);
        else if (bits == 24u) { int s = (int)(rd16(p) | ((unsigned int)p[2] << 16)); if (s & 0x800000) s -= 0x1000000; v = (float)s * (1.0f / 8388608.0f); }
        else if (tag == 1u) v = (float)((double)(int)rd32(p) * (1.0 / 2147483648.0));
        else { union { unsigned int u; float f; } w; w.u = rd32(p); v = w.f; if (!(v == v)) v = 0.0f; }   /* a NaN is silence, not a click */
        out->s[i] = v;
    }
    return DSP_OK;
}

static void wr16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *p, unsigned int v) { wr16(p, v & 0xFFFFu); wr16(p + 2, v >> 16); }
static void wr_id(unsigned char *p, const char *id) { p[0] = (unsigned char)id[0]; p[1] = (unsigned char)id[1]; p[2] = (unsigned char)id[2]; p[3] = (unsigned char)id[3]; }

int dsp_wav_encode16(const dsp_buf_t *b, void **wav, unsigned int *size, const dsp_alloc_t *a)
{
    unsigned int n, data, i, ch = (unsigned int)b->channels;
    unsigned char *p;
    *wav = 0; *size = 0;
    if (b->frames < 0 || b->channels < 1 || b->channels > DSP_MAX_CHANNELS || b->rate < 1 || b->frames > 0x0FFFFFFF / b->channels) return DSP_BADARG;
    n = (unsigned int)b->frames * ch; data = n * 2u;
    if (!(p = (unsigned char *)a->alloc(a->ctx, 44u + data))) return DSP_NOMEM;
    wr_id(p, "RIFF"); wr32(p + 4, 36u + data); wr_id(p + 8, "WAVE");
    wr_id(p + 12, "fmt "); wr32(p + 16, 16u); wr16(p + 20, 1u); wr16(p + 22, ch); wr32(p + 24, (unsigned int)b->rate);
    wr32(p + 28, (unsigned int)b->rate * ch * 2u); wr16(p + 32, ch * 2u); wr16(p + 34, 16u);
    wr_id(p + 36, "data"); wr32(p + 40, data);
    for (i = 0; i < n; i++) {
        /* ffmpeg's float to s16: times 32768, rounded to nearest, clipped */
        double v = (double)b->s[i] * 32768.0; int s;
        if (!(v == v)) v = 0.0;
        if (v >= 32767.0) s = 32767; else if (v <= -32768.0) s = -32768; else s = v >= 0.0 ? (int)(v + 0.5) : -(int)(-v + 0.5);
        wr16(p + 44 + 2u * i, (unsigned int)s & 0xFFFFu);
    }
    *wav = p; *size = 44u + data;
    return DSP_OK;
}

/* ---- filters ---- */
/* ffmpeg's af_biquads.c, lowpass and highpass with poles=2 and width_type=q: the RBJ cookbook forms,
   normalised by a0, run in direct form I (its default transform) */
void dsp_biquad_lowpass(dsp_biquad_t *q, double f, double Q, int rate)
{
    double w0 = 2.0 * PI * f / rate, c = dsp_cos(w0), alpha = dsp_sin(w0) / (2.0 * Q), a0 = 1.0 + alpha;
    q->b0 = (1.0 - c) / 2.0 / a0; q->b1 = (1.0 - c) / a0; q->b2 = q->b0; q->a1 = -2.0 * c / a0; q->a2 = (1.0 - alpha) / a0;
}
void dsp_biquad_highpass(dsp_biquad_t *q, double f, double Q, int rate)
{
    double w0 = 2.0 * PI * f / rate, c = dsp_cos(w0), alpha = dsp_sin(w0) / (2.0 * Q), a0 = 1.0 + alpha;
    q->b0 = (1.0 + c) / 2.0 / a0; q->b1 = -(1.0 + c) / a0; q->b2 = q->b0; q->a1 = -2.0 * c / a0; q->a2 = (1.0 - alpha) / a0;
}

void dsp_biquad(dsp_buf_t *b, const dsp_biquad_t *q, int prime)
{
    int c, n, ch = b->channels, N = b->frames;
    if (prime > N) prime = N;
    for (c = 0; c < ch; c++) {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, x, y;
        float *s = b->s + c;
        /* a loop's own last frames first: its first frame is then filtered in the state its last frame leaves */
        for (n = N - prime; n < N; n++) { x = s[n * ch]; y = q->b0 * x + q->b1 * x1 + q->b2 * x2 - q->a1 * y1 - q->a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; }
        for (n = 0; n < N; n++) {
            x = s[n * ch]; y = q->b0 * x + q->b1 * x1 + q->b2 * x2 - q->a1 * y1 - q->a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            s[n * ch] = (float)y;
        }
    }
}
void dsp_highpass(dsp_buf_t *b, double f, int prime) { dsp_biquad_t q; dsp_biquad_highpass(&q, f, 0.707, b->rate); dsp_biquad(b, &q, prime); }
void dsp_lowpass(dsp_buf_t *b, double f, int prime) { dsp_biquad_t q; dsp_biquad_lowpass(&q, f, 0.707, b->rate); dsp_biquad(b, &q, prime); }

void dsp_gain(dsp_buf_t *b, double db)
{
    float g = (float)dsp_db2lin(db); int i, n = b->frames * b->channels;
    for (i = 0; i < n; i++) b->s[i] *= g;
}

void dsp_mono(dsp_buf_t *b)
{
    int n;
    if (b->channels != 2) return;
    for (n = 0; n < b->frames; n++) b->s[n] = 0.5f * (b->s[2 * n] + b->s[2 * n + 1]);
    b->channels = 1;
}

/* ---- the compressor: ffmpeg's acompressor (af_sidechaincompress.c and hermite.h), line for line ----
 * This block, hermite, comp_level and dsp_compress, is a port of FFmpeg code, made for IndoorRain in September 2026
 * (C with no C runtime; a native transcription of FFmpeg's code gives the same floats, bit for bit). The originals
 * are FFmpeg's libavfilter/af_sidechaincompress.c, whose compressor came to FFmpeg from Calf Studio Gear, and
 * libavfilter/hermite.h: free software under the GNU Lesser General Public License, version 2.1 or (at your option)
 * any later version, copyright their authors, whom the header of af_sidechaincompress.c names. As section 3 of the
 * LGPL-2.1 allows, this port is used under the GNU General Public License, version 3, like the rest of this file,
 * and comes WITHOUT ANY WARRANTY; see LICENSE. */
static double hermite(double x, double x0, double x1, double p0, double p1, double m0, double m1)
{
    double width = x1 - x0, t = (x - x0) / width, t2, t3;
    m0 *= width; m1 *= width;
    t2 = t * t; t3 = t2 * t;
    return (2 * p0 + m0 - 2 * p1 + m1) * t3 + (-3 * p0 - 2 * m0 + 3 * p1 - m1) * t2 + m0 * t + p0;
}
/* the detector's input for one frame: the channels' mean (or loudest) level, squared for rms detection */
static double comp_level(const float *x, int ch, const dsp_comp_t *p)
{
    double v = 0.0; int c;
    for (c = 0; c < ch; c++) {
        double s = (double)x[c] * p->level_in; if (s < 0.0) s = -s;
        if (p->link_max) { if (s > v) v = s; } else v += s;
    }
    if (!p->link_max) v /= ch;
    return p->rms ? v * v : v;
}
void dsp_compress(dsp_buf_t *b, const dsp_comp_t *p, int prime)
{
    const int ch = b->channels, N = b->frames;
    const double thres = dsp_log(p->threshold), sk = dsp_sqrt(p->knee);
    const double lin_knee_start = p->threshold / sk, lin_knee_stop = p->threshold * sk;
    const double knee_start = dsp_log(lin_knee_start), knee_stop = dsp_log(lin_knee_stop);
    const double compressed_knee_stop = (knee_stop - thres) / p->ratio + thres;
    const double detected_above = p->rms ? lin_knee_start * lin_knee_start : lin_knee_start;
    /* ffmpeg's coefficients: 1 / (ms * rate / 4000), a time constant of a quarter of the stated time */
    double attack = 1.0 / (p->attack_ms * b->rate / 4000.0), release = 1.0 / (p->release_ms * b->rate / 4000.0), lin_slope = 0.0, lvl;
    int n, c;
    if (attack > 1.0) attack = 1.0;
    if (release > 1.0) release = 1.0;
    if (prime > N) prime = N;
    /* priming for a loop: the detector alone runs over the loop's end, so the start is compressed as the end leaves it */
    for (n = N - prime; n < N; n++) { lvl = comp_level(b->s + n * ch, ch, p); lin_slope += (lvl - lin_slope) * (lvl > lin_slope ? attack : release); }
    for (n = 0; n < N; n++) {
        float *x = b->s + n * ch; double gain = 1.0;
        lvl = comp_level(x, ch, p);
        lin_slope += (lvl - lin_slope) * (lvl > lin_slope ? attack : release);
        if (lin_slope > 0.0 && lin_slope > detected_above) {   /* downward mode: only above the knee's start */
            double slope = dsp_log(lin_slope), g;
            if (p->rms) slope *= 0.5;
            g = (slope - thres) / p->ratio + thres;
            if (p->knee > 1.0 && slope < knee_stop) g = hermite(slope, knee_start, knee_stop, knee_start, compressed_knee_stop, 1.0, 1.0 / p->ratio);
            gain = dsp_exp(g - slope);
        }
        for (c = 0; c < ch; c++) x[c] = (float)((double)x[c] * p->level_in * gain * p->makeup);   /* mix=1 */
    }
}

/* ---- the limiter ----
 * The gain each frame needs is ceiling / its peak (1 when under). Three steps turn that into a smooth gain:
 * a minimum held over the next L frames (L = the attack), a release that climbs back to unity at a slope
 * set by the depth of the last reduction ((1 - depth) / release, as alimiter's does), and an average over
 * the last L of those. Every frame inside the L-frame average saw the peak ahead, so the gain at a peak is
 * at most what the peak needs: nothing gets through above the ceiling, and each reduction is a ramp of L
 * frames that ends on its peak. It streams in place with the output L - 1 frames behind the input, keeping
 * only the last L values, so a 45 s loop costs a few kB of state. */
static double need_of(const float *x, int ch, double ceiling)
{
    double p = 0.0; int c;
    for (c = 0; c < ch; c++) { double v = x[c] < 0.0f ? -(double)x[c] : (double)x[c]; if (v > p) p = v; }
    return p > ceiling ? ceiling / p : 1.0;
}
int dsp_limit(dsp_buf_t *b, double ceiling, double attack_ms, double release_ms, int loop, const dsp_alloc_t *a)
{
    const int N = b->frames, ch = b->channels;
    int L = dsp_frames(b->rate, attack_ms / 1000.0), P, v, m, c, qh = 0, qt = 0, qn = 0, ri = 0, cap;
    double R = release_ms / 1000.0 * b->rate, hold = 1.0, slope = 0.0, sum, need, g;
    double *qv, *head, *ring; int *qi;
    if (N <= 0) return DSP_OK;
    if (!(ceiling > 0.0) || ceiling > 1.0) return DSP_BADARG;
    if (L < 1) L = 1;
    if (L > N) L = N;
    if (R < 1.0) R = 1.0;
    cap = L + 1;
    qv = (double *)a->alloc(a->ctx, (unsigned int)cap * 8u); qi = (int *)a->alloc(a->ctx, (unsigned int)cap * 4u);
    ring = (double *)a->alloc(a->ctx, (unsigned int)L * 8u); head = (double *)a->alloc(a->ctx, (unsigned int)L * 8u);
    if (!qv || !qi || !ring || !head) {
        if (qv) a->release(a->ctx, qv);
        if (qi) a->release(a->ctx, qi);
        if (ring) a->release(a->ctx, ring);
        if (head) a->release(a->ctx, head);
        return DSP_NOMEM;
    }
    /* A loop is limited as the circle it is played as: its start is primed with the gain its end leaves (P
       frames: the release and two lookaheads, so the state has settled), and its end looks ahead into its
       own start, whose needs are saved here before the frames are overwritten. */
    P = loop ? (int)R + 2 * L : 0;
    if (P > N) P = N;
    for (v = 0; v < L - 1; v++) head[v] = need_of(b->s + v * ch, ch, ceiling);
    for (v = 0; v < L; v++) ring[v] = 1.0;
    sum = L;
    for (v = -P; v <= N + L - 2; v++) {
        if (v < 0) need = need_of(b->s + (N + v) * ch, ch, ceiling);   /* priming: the loop's own end, not yet written */
        else if (v < N) need = need_of(b->s + v * ch, ch, ceiling);     /* frame v is read before anything at or after it is written */
        else need = loop ? head[v - N] : 1.0;                            /* past the end: a loop runs into its start, a one-shot into silence */
        /* the smallest need over the last L frames, by a monotonic queue (ring of cap entries) */
        while (qn && qv[(qt + cap - 1) % cap] >= need) { qt = (qt + cap - 1) % cap; qn--; }
        while (qn && qi[qh] <= v - L) { qh = (qh + 1) % cap; qn--; }
        qv[qt] = need; qi[qt] = v; qt = (qt + 1) % cap; qn++;
        /* hold it, then release back to unity */
        g = hold + slope; if (g > 1.0) g = 1.0;
        if (qv[qh] < g) { g = qv[qh]; slope = (1.0 - g) / R; }
        hold = g;
        /* the average of the last L held gains is the gain for frame v - (L - 1) */
        sum += hold - ring[ri]; ring[ri] = hold; if (++ri == L) ri = 0;
        m = v - (L - 1);
        if (m >= 0 && m < N) {
            float *x = b->s + m * ch; double k = sum / L;
            if (k > 1.0) k = 1.0;
            for (c = 0; c < ch; c++) {
                double y = (double)x[c] * k;
                if (y > ceiling) y = ceiling; else if (y < -ceiling) y = -ceiling;   /* rounding only: the gain already keeps it under */
                x[c] = (float)y;
            }
        }
    }
    a->release(a->ctx, qv); a->release(a->ctx, qi); a->release(a->ctx, ring); a->release(a->ctx, head);
    return DSP_OK;
}

/* ---- loudness: ITU-R BS.1770-4 / EBU R128 integrated ---- */
typedef struct { double b0, b1, b2, a1, a2, x1, x2, y1, y2; } kstage_t;
static double kstage(kstage_t *k, double x)
{
    double y = k->b0 * x + k->b1 * k->x1 + k->b2 * k->x2 - k->a1 * k->y1 - k->a2 * k->y2;
    k->x2 = k->x1; k->x1 = x; k->y2 = k->y1; k->y1 = y;
    return y;
}
/* The K-weighting: a high shelf (+4 dB above about 1.7 kHz, the head) and a high-pass near 38 Hz, from the
   analog prototypes libebur128 uses, so any rate gets them; at 48 kHz they are the standard's coefficients. */
static void kweight(kstage_t *shelf, kstage_t *hp, int rate)
{
    double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    double K = dsp_tan(PI * f0 / rate), Vh = dsp_pow(10.0, G / 20.0), Vb = dsp_pow(Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
    shelf->b0 = (Vh + Vb * K / Q + K * K) / a0; shelf->b1 = 2.0 * (K * K - Vh) / a0; shelf->b2 = (Vh - Vb * K / Q + K * K) / a0;
    shelf->a1 = 2.0 * (K * K - 1.0) / a0; shelf->a2 = (1.0 - K / Q + K * K) / a0;
    f0 = 38.13547087602444; Q = 0.5003270373238773; K = dsp_tan(PI * f0 / rate); a0 = 1.0 + K / Q + K * K;
    hp->b0 = 1.0; hp->b1 = -2.0; hp->b2 = 1.0; hp->a1 = 2.0 * (K * K - 1.0) / a0; hp->a2 = (1.0 - K / Q + K * K) / a0;
    shelf->x1 = shelf->x2 = shelf->y1 = shelf->y2 = 0.0; hp->x1 = hp->x2 = hp->y1 = hp->y2 = 0.0;
}
int dsp_loudness(const dsp_buf_t *b, double *lufs, const dsp_alloc_t *a)
{
    const int hop = dsp_frames(b->rate, 0.1), ch = b->channels;
    int nsub, nblk, j, f, c, kept;
    double *e, z, sum, zabs, zrel;
    kstage_t shelf[DSP_MAX_CHANNELS], hp[DSP_MAX_CHANNELS];
    *lufs = -70.0;
    if (hop < 1 || ch < 1 || ch > DSP_MAX_CHANNELS) return DSP_BADARG;
    nsub = b->frames / hop; nblk = nsub - 3;   /* frames after the last whole 100 ms belong to no block, as in ffmpeg */
    if (nblk < 1) return DSP_SILENT;
    if (!(e = (double *)a->alloc(a->ctx, (unsigned int)nsub * 8u))) return DSP_NOMEM;
    for (c = 0; c < ch; c++) kweight(&shelf[c], &hp[c], b->rate);
    /* the K-weighted energy of every 100 ms, channels summed with weight 1; a 400 ms block is four of them */
    for (j = 0; j < nsub; j++) {
        sum = 0.0;
        for (f = j * hop; f < (j + 1) * hop; f++)
            for (c = 0; c < ch; c++) { double y = kstage(&hp[c], kstage(&shelf[c], (double)b->s[f * ch + c])); sum += y * y; }
        e[j] = sum;
    }
    zabs = dsp_pow(10.0, (-70.0 + 0.691) / 10.0);   /* the absolute gate, -70 LUFS, as a mean square */
    for (sum = 0.0, kept = 0, j = 0; j < nblk; j++) { z = (e[j] + e[j + 1] + e[j + 2] + e[j + 3]) / (4.0 * hop); if (z > zabs) { sum += z; kept++; } }
    if (!kept) { a->release(a->ctx, e); return DSP_SILENT; }
    zrel = sum / kept * 0.1;                        /* the relative gate: 10 LU under the loudness of what passed */
    for (sum = 0.0, kept = 0, j = 0; j < nblk; j++) { z = (e[j] + e[j + 1] + e[j + 2] + e[j + 3]) / (4.0 * hop); if (z > zabs && z > zrel) { sum += z; kept++; } }
    a->release(a->ctx, e);
    if (!kept) return DSP_SILENT;
    *lufs = -0.691 + 10.0 * dsp_log10(sum / kept);
    return DSP_OK;
}

/* The ungated K-weighted loudness of the first 3 s: ebur128's short-term loudness once 3 s have gone in, which is
   what loudnorm's first frame measures and every later gain starts from. 0 when it is at or under -70 LUFS. */
static int first_3s(const dsp_buf_t *b, double *lufs)
{
    kstage_t shelf[DSP_MAX_CHANNELS], hp[DSP_MAX_CHANNELS];
    const int ch = b->channels, n = 3 * b->rate;
    double sum = 0.0; int f, c;
    for (c = 0; c < ch; c++) kweight(&shelf[c], &hp[c], b->rate);
    for (f = 0; f < n; f++)
        for (c = 0; c < ch; c++) { double y = kstage(&hp[c], kstage(&shelf[c], (double)b->s[f * ch + c])); sum += y * y; }
    *lufs = -0.691 + 10.0 * dsp_log10(sum / n);
    return *lufs > -70.0;
}

int dsp_normalize(dsp_buf_t *b, double target_lufs, double peak_db, int loop, const dsp_alloc_t *a)
{
    double now, first; int r = dsp_loudness(b, &now, a);
    if (r != DSP_OK) return r;
    /* a one-shot of 3 s or more: loudnorm's gain comes from its first 3 s and, on a clip this short, stays close
       to it to the end; the integrated loudness left the muffled close and near thunders 1.2 and 1.3 LU quieter
       than the 0.13 files */
    if (!loop && b->frames >= 3 * b->rate && first_3s(b, &first)) now = first;
    dsp_gain(b, target_lufs - now);
    return dsp_limit(b, dsp_db2lin(peak_db), 10.0, 100.0, loop, a);
}

/* ---- another rate: the windowed-sinc resampler (dsp.h) ---- */
/* I0, the modified Bessel function of the first kind, order 0, by its series: the Kaiser window's shape */
static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, h = 0.5 * x; int k;
    for (k = 1; k < 64; k++) {
        term *= (h / k) * (h / k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}
static int gcd(int x, int y) { while (y) { int t = x % y; x = y; y = t; } return x; }
/* One output sample of one channel: the W input frames from i0 on (s is the channel's first sample), weighted by w */
static double resample_dot(const float *s, int ch, int N, int loop, int i0, const double *w, int W)
{
    double acc = 0.0; int j;
    if (i0 >= 0 && i0 + W <= N) {
        /* the whole window inside the buffer. Eight taps to a statement: C99 rounds x87's excess precision at each
           assignment, through memory, and an assignment per tap pair made the decimator this replaced over three
           times slower (eight measured fastest, 1.3 times four's speed) */
        const float *x = s + i0 * ch;
        const int c1 = ch, c2 = 2 * ch, c3 = 3 * ch, c4 = 4 * ch, c5 = 5 * ch, c6 = 6 * ch, c7 = 7 * ch;
        for (j = 0; j + 8 <= W; j += 8) {
            const float *y = x + j * ch; const double *g = w + j;
            acc += g[0] * y[0] + g[1] * y[c1] + g[2] * y[c2] + g[3] * y[c3] + g[4] * y[c4] + g[5] * y[c5] + g[6] * y[c6] + g[7] * y[c7];
        }
        for (; j < W; j++) acc += w[j] * x[j * ch];
        return acc;
    }
    for (j = 0; j < W; j++) {   /* near an end: a loop reads round to its other end, a one-shot reads silence */
        int i = i0 + j;
        if (i < 0 || i >= N) {
            if (!loop) continue;
            i %= N; if (i < 0) i += N;
        }
        acc += w[j] * (double)s[i * ch];
    }
    return acc;
}
int dsp_resample(dsp_buf_t *b, int new_rate, double pass_hz, int loop, const dsp_alloc_t *a)
{
    const int N = b->frames, ch = b->channels, rate = b->rate;
    int g, up, down, exact, R, A, B, F, W, n_out, r, j, k, c, q, rem;
    unsigned int whole, part, total;
    double stop_hz, cut, beta, dw, T, i0b, *h, *between = 0;
    dsp_buf_t o;
    if (new_rate == rate) return DSP_OK;
    if (N < 1 || ch < 1 || ch > DSP_MAX_CHANNELS || rate < 1000 || rate > 768000 || new_rate < 1000 || new_rate > 768000) return DSP_BADARG;
    g = gcd(rate, new_rate); up = new_rate / g; down = rate / g;
    /* a ratio of more steps than this is never asked for, and under it every product below fits 32 bits: there is no
       64-bit division without a C runtime to supply it */
    if (up > 65535 || down > 65535) return DSP_BADARG;
    stop_hz = (rate < new_rate ? rate : new_rate) / 2.0;
    if (!(pass_hz > 0.0) || pass_hz >= stop_hz) return DSP_BADARG;
    /* N up / down, the new length, is whole up + part / down; up and down share no factor, so the ratio divides a
       loop's length exactly when down divides N */
    whole = (unsigned int)(N / down); part = (unsigned int)(N % down) * (unsigned int)up;
    if (whole > 0x0FFFFFFFu / (unsigned int)ch / (unsigned int)up) return DSP_BADARG;
    exact = up <= DSP_RESAMPLE_PHASES && (!loop || part == 0);
    total = whole * (unsigned int)up + (loop ? (part + (unsigned int)down / 2u) / (unsigned int)down : (part + (unsigned int)down - 1u) / (unsigned int)down);
    if (total < 1u || total > 0x0FFFFFFFu / (unsigned int)ch) return DSP_BADARG;
    n_out = (int)total;
    /* Output frame k falls k A / B input frames in: A / B is down / up, the ratio, except for a loop the ratio does
       not divide, where N / n_out lands the frame after the last on the loop's start again. Its whole part and its
       remainder are kept as they grow, in integers, so the moment never drifts. */
    if (loop && !exact) { A = N; B = n_out; } else { A = down; B = up; }
    R = exact ? up : DSP_RESAMPLE_PHASES;
    /* Kaiser's formulas for the window's shape and length, designed 5 dB past the attenuation promised: on the
       shortest of the decimator's filters (29 taps) the formulas alone fell 1.4 dB short of it. The taps are one
       input frame apart whichever way the rate goes, so the transition band is measured at the input's rate; the sinc
       is cut half way across it, which puts the stopband's edge on the lower Nyquist frequency. */
    beta = 0.1102 * (DSP_RESAMPLE_STOP_DB + 5.0 - 8.7);
    dw = 2.0 * PI * (stop_hz - pass_hz) / rate;
    T = (DSP_RESAMPLE_STOP_DB + 5.0 - 7.95) / (2.285 * dw) / 2.0 + 1.0;   /* the window's half width, in input frames */
    if (!(T <= 4096.0)) return DSP_BADARG;   /* a band this narrow is never asked for (and T must fit an int) */
    F = (int)T; W = 2 * F + 2;
    if ((R + 1) * W > 1 << 20) return DSP_BADARG;   /* nor a table of 8 MB */
    cut = (pass_hz + stop_hz) / rate;   /* the sinc's corner as a fraction of the input rate, times 2 */
    if (!(h = (double *)a->alloc(a->ctx, (unsigned int)((R + 1) * W) * 8u))) return DSP_NOMEM;
    if (!exact && !(between = (double *)a->alloc(a->ctx, (unsigned int)W * 8u))) { a->release(a->ctx, h); return DSP_NOMEM; }
    /* Row r holds the taps for an output frame r / R of the way from input frame q to q + 1, over input frames q - F
       to q + F + 1. Row R, one whole frame on, is only there to draw lines to. */
    i0b = bessel_i0(beta);
    for (r = 0; r <= R; r++) {
        double *w = h + (unsigned int)(r * W), sum = 0.0;
        for (j = 0; j < W; j++) {
            double t = (double)r / R - (j - F), u = t / T, x = PI * cut * t;
            w[j] = u >= 1.0 || u <= -1.0 ? 0.0 : cut * (x != 0.0 ? dsp_sin(x) / x : 1.0) * bessel_i0(beta * dsp_sqrt(1.0 - u * u)) / i0b;
            sum += w[j];
        }
        for (j = 0; j < W; j++) w[j] /= sum;   /* each row sums to exactly 1, so a constant passes unchanged */
    }
    if ((r = dsp_new(&o, n_out, ch, new_rate, a)) != DSP_OK) { a->release(a->ctx, h); if (between) a->release(a->ctx, between); return r; }
    for (k = 0, q = 0, rem = 0; k < n_out; k++) {
        const double *w;
        if (exact) w = h + (unsigned int)(rem * W);
        else {   /* between two rows of the finer table, on the straight line from one to the other */
            double x = (double)rem * R / B, m; int i = (int)x; const double *w0 = h + (unsigned int)(i * W), *w1 = w0 + W;
            m = x - i;
            for (j = 0; j < W; j++) between[j] = w0[j] + m * (w1[j] - w0[j]);
            w = between;
        }
        for (c = 0; c < ch; c++) o.s[k * ch + c] = (float)resample_dot(b->s + c, ch, N, loop, q - F, w, W);
        q += A / B; rem += A % B;
        if (rem >= B) { rem -= B; q++; }
    }
    a->release(a->ctx, h);
    if (between) a->release(a->ctx, between);
    dsp_free(b, a);
    *b = o;
    return DSP_OK;
}

/* ---- edits ---- */
/* ffmpeg's fade_gain (af_afade.c) for the two curves the recipes use: index / range clipped to [0, 1], then shaped */
static double fade_gain(int curve, int index, int range)
{
    double t = range > 0 ? (double)index / range : 1.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return curve == DSP_QSIN ? dsp_sin(t * PI / 2.0) : t;
}

int dsp_join(dsp_buf_t *out, const dsp_buf_t *parts, const int *xfade, int n, int curve, const dsp_alloc_t *a)
{
    int i, k, c, ch, total = 0, pos = 0, r;
    out->s = 0; out->frames = 0; out->owned = 0;
    if (n < 1) return DSP_BADARG;
    ch = parts[0].channels;
    for (i = 0; i < n; i++) {
        int x_in = i > 0 ? xfade[i - 1] : 0, x_out = i < n - 1 ? xfade[i] : 0;
        if (parts[i].channels != ch || parts[i].rate != parts[0].rate || x_in < 0 || x_out < 0 || x_in + x_out > parts[i].frames) return DSP_BADARG;
        total += parts[i].frames - x_out;
    }
    if ((r = dsp_new(out, total, ch, parts[0].rate, a)) != DSP_OK) return r;
    for (i = 0; i < n; i++) {
        const dsp_buf_t *p = &parts[i];
        int x_in = i > 0 ? xfade[i - 1] : 0, x_out = i < n - 1 ? xfade[i] : 0;
        /* the part's own stretch, between the crossfades at its two ends */
        for (k = x_in * ch; k < (p->frames - x_out) * ch; k++) out->s[pos * ch + k - x_in * ch] = p->s[k];
        pos += p->frames - x_in - x_out;
        if (x_out > 0) {   /* acrossfade: gain0 for the part fading out, gain1 for the one fading in */
            const dsp_buf_t *q = &parts[i + 1];
            for (k = 0; k < x_out; k++) {
                double g0 = fade_gain(curve, x_out - 1 - k, x_out), g1 = fade_gain(curve, k, x_out);
                for (c = 0; c < ch; c++) out->s[(pos + k) * ch + c] = (float)(p->s[(p->frames - x_out + k) * ch + c] * g0 + q->s[k * ch + c] * g1);
            }
            pos += x_out;
        }
    }
    return DSP_OK;
}

int dsp_loopify(dsp_buf_t *b, int n, int curve)
{
    int k, c, N = b->frames, ch = b->channels;
    if (n < 0 || n > N - n) return DSP_BADARG;   /* the tail and the head must not overlap */
    /* [tail][head]acrossfade: the tail fades out over the head fading in; the body after the head is untouched */
    for (k = 0; k < n; k++) {
        double g0 = fade_gain(curve, n - 1 - k, n), g1 = fade_gain(curve, k, n);
        for (c = 0; c < ch; c++) b->s[k * ch + c] = (float)(b->s[(N - n + k) * ch + c] * g0 + b->s[k * ch + c] * g1);
    }
    b->frames = N - n;
    return DSP_OK;
}

void dsp_fade(dsp_buf_t *b, int in_frames, int out_frames, int curve)
{
    int k, c, N = b->frames, ch = b->channels;
    for (k = 0; k < in_frames && k < N; k++) { float g = (float)fade_gain(curve, k, in_frames); for (c = 0; c < ch; c++) b->s[k * ch + c] *= g; }
    /* the fade out as the script made it: reversed, faded in, reversed back, so the last frame gets gain 0 */
    for (k = 0; k < out_frames && k < N; k++) { float g = (float)fade_gain(curve, k, out_frames); for (c = 0; c < ch; c++) b->s[(N - 1 - k) * ch + c] *= g; }
}
