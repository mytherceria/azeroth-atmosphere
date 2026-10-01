/* dsp.h: the audio building blocks IndoorRain uses to remake its weather and storm sounds in memory, at run
 * time, from the player's own client archives (sounds.c holds the 25 recipes that use them).
 *
 * The addon used to ship 25 .ogg files made offline by tools/make_loops.sh and tools/make_storm_assets.sh,
 * ffmpeg chains run on the client's own sound files. Those are copies of Blizzard audio and cannot go on a
 * public site, so the DLL is to run the same chains itself. Each block here is the ffmpeg filter those
 * scripts used, ported with the same parameters and checked against ffmpeg by tools/sounds_render.c; where
 * a block is a stand-in rather than a port, its comment says so and how it differs (dsp_limit, dsp_normalize).
 * dsp_resample stands in for the scripts' -ar 44100 (ffmpeg's resampler): it takes each sound to the rate the
 * client's mixer runs at, before or after its recipe as the scripts had it (sounds.c), and tools/sounds_render.c
 * resampletest checks it on test tones.
 *
 * Everything works on interleaved float buffers, once, offline, on the DLL's worker thread, so the code
 * is plain rather than fast. Like the rest of the DLL it needs no C runtime: no libc, no libm (the math
 * below is our own), and memory only through the allocator the caller passes in. GPL-3.0.
 */
#ifndef INDOORRAIN_DSP_H
#define INDOORRAIN_DSP_H

#define DSP_OK        0
#define DSP_NOMEM   (-1)   /* the allocator refused */
#define DSP_BADWAV  (-2)   /* not a RIFF WAV this reads: PCM 8, 16, 24 or 32-bit or 32-bit float, 1 or 2 channels */
#define DSP_BADARG  (-3)   /* a length, range or parameter out of bounds */
#define DSP_SILENT  (-4)   /* nothing above the loudness gate, so no gain to a loudness can be worked out */

#define DSP_MAX_CHANNELS 2

/* Memory comes from the caller: VirtualAlloc in the DLL, malloc in the native test. The buffers are large
   (a 75 s stereo 48 kHz source is 28.8 MB as floats), so a refusal is an ordinary result, never a crash. */
typedef struct {
    void *(*alloc)(void *ctx, unsigned int bytes);   /* NULL when refused; the contents need not be zeroed */
    void (*release)(void *ctx, void *p);             /* never called with NULL */
    void *ctx;
} dsp_alloc_t;

/* Interleaved samples, full scale 1.0. owned: 1 when s came from the allocator (dsp_free gives it back),
   0 for a view into another buffer (dsp_view), which must not outlive it. */
typedef struct {
    float *s;
    int frames, channels, rate;
    int owned;
} dsp_buf_t;

/* ---- math without libm (double precision; tools/sounds_render.c mathtest checks each against libm) ---- */
double dsp_sqrt(double x);    /* 0 for x <= 0 */
double dsp_exp(double x);     /* clamped to the double range */
double dsp_log(double x);     /* natural log; a large negative number for x <= 0, never -inf */
double dsp_log10(double x);
double dsp_pow(double x, double y);   /* x > 0 only; 0 otherwise */
double dsp_sin(double x);
double dsp_cos(double x);
double dsp_tan(double x);
double dsp_db2lin(double db); /* 10^(db/20) */

/* ---- buffers ---- */
int  dsp_new(dsp_buf_t *b, int frames, int channels, int rate, const dsp_alloc_t *a);
int  dsp_copy(dsp_buf_t *dst, const dsp_buf_t *src, const dsp_alloc_t *a);
void dsp_free(dsp_buf_t *b, const dsp_alloc_t *a);
int  dsp_view(dsp_buf_t *v, const dsp_buf_t *b, int start, int end);   /* atrim: frames [start, end), no copy */
int  dsp_frames(int rate, double seconds);                              /* seconds to frames, rounded */

/* ---- WAV in and out ---- */
/* Any RIFF WAV with PCM 8/16/24/32-bit or 32-bit float, 1 or 2 channels, any rate, chunks in any order
   (RavenCraft's heavy rain loop carries LIST, bext, iXML and _PMX chunks after its data). */
int dsp_wav_decode(dsp_buf_t *out, const void *data, unsigned int size, const dsp_alloc_t *a);
/* What dsp_wav_decode would make of the same bytes, without decoding a sample: the frames its data chunk holds
   (what is there, if the file is cut short), its channels and its rate. DSP_OK or DSP_BADWAV. A caller can refuse
   a file by its length before any memory goes to its samples, which take four times its 8-bit size (sounds.c). */
int dsp_wav_info(const void *data, unsigned int size, unsigned int *frames, int *channels, int *rate);
/* 16-bit PCM WAV, 44-byte header, rounded and clipped the way ffmpeg writes s16 (no dither). The caller
   gives *wav back with a->release. */
int dsp_wav_encode16(const dsp_buf_t *b, void **wav, unsigned int *size, const dsp_alloc_t *a);

/* ---- filters ----
 * prime: for a loop, how many of the buffer's own last frames to run through first, so the first frame
 * is filtered in the state the last one leaves (ffmpeg starts every filter from rest, which puts a step
 * at the loop point); 0 starts from rest, as ffmpeg does, and is right for a one-shot. */
typedef struct { double b0, b1, b2, a1, a2; } dsp_biquad_t;   /* normalised: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2 */
void dsp_biquad_lowpass(dsp_biquad_t *q, double f, double Q, int rate);    /* RBJ cookbook, as ffmpeg's p=2 */
void dsp_biquad_highpass(dsp_biquad_t *q, double f, double Q, int rate);
void dsp_biquad(dsp_buf_t *b, const dsp_biquad_t *q, int prime);
/* ffmpeg's highpass=f=F and lowpass=f=F: two poles (p=2 is the default), Q 0.707. None of the recipes
   uses p=1, so there is no one-pole version. */
void dsp_highpass(dsp_buf_t *b, double f, int prime);
void dsp_lowpass(dsp_buf_t *b, double f, int prime);
void dsp_gain(dsp_buf_t *b, double db);                                    /* volume=XdB */
void dsp_mono(dsp_buf_t *b);                                               /* -ac 1: the channels averaged */

/* ffmpeg's acompressor, ported line for line (af_sidechaincompress.c): a feed-forward downward compressor
   whose detector follows the mean of the channels (link=average), squared (detection=rms), and whose gain
   curve has a soft knee spliced in with a Hermite curve. Every field is in ffmpeg's own units. FFmpeg's code
   is LGPL-2.1 or later; the port's license notice is at dsp_compress in dsp.c. */
typedef struct {
    double level_in;       /* linear input gain */
    double threshold;      /* linear (ffmpeg parses "-22dB" to 0.0794) */
    double ratio;
    double attack_ms, release_ms;
    double makeup;         /* linear, 1 to 64: acompressor's makeup is a factor, not dB (2 is +6 dB) */
    double knee;           /* linear, ffmpeg's default 2.82843 */
    int rms;               /* 1 = detection=rms (ffmpeg's default), 0 = peak */
    int link_max;          /* 0 = link=average (ffmpeg's default), 1 = maximum */
} dsp_comp_t;
void dsp_compress(dsp_buf_t *b, const dsp_comp_t *c, int prime);

/* A lookahead peak limiter: no sample leaves above ceiling (linear; alimiter's limit=0.89 is -1.0 dB).
   The gain ramps down over attack_ms ahead of each peak and climbs back to unity over release_ms, as
   alimiter's does, but it is not a port of alimiter: it works offline on the whole buffer, so its output
   is aligned with its input. alimiter (latency=false, ffmpeg's default) delays its output by the attack,
   which is why the shipped outdoor rain loops opened with 5 ms of silence and lost their last 5 ms.
   loop: the buffer is a loop, so its end looks ahead into its own start and its start carries the gain
   its end leaves. */
int dsp_limit(dsp_buf_t *b, double ceiling, double attack_ms, double release_ms, int loop, const dsp_alloc_t *a);

/* ---- loudness ---- */
/* EBU R128 / ITU-R BS.1770-4 integrated loudness in LUFS: K-weighting (the two filters derived for any
   rate, as libebur128 does), 400 ms blocks every 100 ms, the -70 LUFS absolute gate and the -10 LU relative
   gate; channel weights 1.0 (a mono file is one channel, as ffmpeg's ebur128 measures it). DSP_SILENT when
   no block passes the gates, or the buffer is shorter than one block. */
int dsp_loudness(const dsp_buf_t *b, double *lufs, const dsp_alloc_t *a);
/* The stand-in for loudnorm=I=target:TP=peak_db. ffmpeg's loudnorm is dynamic: it rides a gain every 100 ms
   from a 3 s window, smoothed, and limits true peaks at 192 kHz. This is static: one gain, then dsp_limit at
   peak_db (10 ms attack, 100 ms release, loudnorm's own lengths) on the samples at their own rate, not on true
   peaks. The gain follows what loudnorm's own gain comes from. A loop, or a clip under 3 s (which loudnorm
   treats linearly), is brought to target by its integrated loudness; on the rain loops that lands within
   0.1 LU of loudnorm. A one-shot of 3 s or more is brought to target by the ungated loudness of its first 3 s,
   the window loudnorm fills before its first gain and holds that gain from: on the 3 to 6 s thunder and gust
   clips the integrated rule left the muffled close and near thunders 1.2 and 1.3 LU quieter than the 0.13
   files (tools/sounds_render.c against ffmpeg on the RavenCraft client's files, 30 Sep 2026). A first 3 s at
   or under -70 LUFS, which loudnorm meets with no gain at all, falls back to the integrated rule. */
int dsp_normalize(dsp_buf_t *b, double target_lufs, double peak_db, int loop, const dsp_alloc_t *a);

/* ---- another rate ----
 * The buffer at new_rate, through a linear-phase low-pass: the ideal one (a sinc) cut to length under a Kaiser
 * window, flat to pass_hz and at least DSP_RESAMPLE_STOP_DB down from the lower of the two Nyquist frequencies up,
 * so that going up no image of the band comes through, and going down nothing folds back into it. It is centred, so
 * the output is not delayed. Each output frame is a sum over the input frames round its own moment, with taps from a
 * table of the moments that fall between two input frames: exact when the two rates' ratio in lowest terms has
 * DSP_RESAMPLE_PHASES steps or fewer (22,050 to 44,100 Hz has two, 48,000 to 44,100 has 147), else that many, and a
 * moment between two of them takes taps on the straight line between theirs. A loop is resampled as the circle it
 * is played as, the taps that run off one end reading the other, so its loop point is like any other moment; it
 * comes out the same length in time, to the frame where the ratio divides its length, else stretched or shrunk to the
 * nearest whole number of frames, by half a frame at most: a change of pitch of at most 0.5 over that number, under
 * 3.2 millionths (0.006 cents) for a loop of 20 s or more at 8 kHz or more. The most for a loop sounds.c makes is 2.7
 * millionths, the plain client's 23.5 s heavy loop for a mixer at 8,001 Hz (the fourth 0.14 review: this said under a
 * millionth; tools/sounds_render.c resampletest holds it). A one-shot reads silence past its ends and gets every frame
 * that reaches into it. b is replaced by the result (a new buffer from a; the old one is given back); on an error it
 * is left as it was. The same rate does nothing. */
#define DSP_RESAMPLE_STOP_DB 90.0
#define DSP_RESAMPLE_PHASES  1024
int dsp_resample(dsp_buf_t *b, int new_rate, double pass_hz, int loop, const dsp_alloc_t *a);

/* ---- edits ---- */
#define DSP_TRI  0   /* linear: ffmpeg's default fade curve (afade, acrossfade) */
#define DSP_QSIN 1   /* quarter sine, sin(t * pi / 2): c1=qsin:c2=qsin, which keeps the power of two uncorrelated sounds */
/* concat with acrossfade between neighbours: xfade[i] frames of parts[i]'s end fade out (ffmpeg's gain0)
   while parts[i + 1]'s start fades in (gain1); 0 is a plain concat. n parts, n - 1 crossfades, all parts
   the same rate and channels. */
int  dsp_join(dsp_buf_t *out, const dsp_buf_t *parts, const int *xfade, int n, int curve, const dsp_alloc_t *a);
/* make_storm_assets.sh's loopify, in place: the last n frames crossfaded over the first n (tail fading out,
   head fading in), the tail dropped; frames becomes frames - n and the end runs into the start. */
int  dsp_loopify(dsp_buf_t *b, int n, int curve);
/* afade=t=in over in_frames at the start, and the areverse,afade=t=in,areverse fade over out_frames at the end. */
void dsp_fade(dsp_buf_t *b, int in_frames, int out_frames, int curve);

#endif
