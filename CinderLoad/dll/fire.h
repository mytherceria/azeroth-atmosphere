/* The loading bar's fire sound, the part of CinderLoad.dll kept free of Windows and FMOD calls so it can be tested on
 * its own (tests/test_switch.c). Included once, by cinderload.c.
 *
 * The sound is the mock Matt approved by ear on 2 Oct 2026 (make_fire_audio.py, loading-bar-fire-80-20.mp4), rebuilt
 * from the same six loops of the game's own (read out of the player's client at run time; none is shipped):
 *   the bed, always there:  CampFireLargeLoop 1.0, UndeadCampfireLArge 0.55, AlterOfKingsFireLoop 0.45, LavaPoolLoop 0.18,
 *                           at 0.85 to 1.15 as the fill goes from 0 to a third;
 *   the crackle, the surge: UndeadFireLarge 0.8, TorchFireLoop 0.5, as loud as the fill is moving: 0 while it stalls,
 *                           1 at a quarter of the bar a second, at most 1.2;
 * each loop resampled to the mixer's rate, started at the mock's offset into it and looped with a 0.12 s crossfade.
 * The mono mix is panned EQUAL-POWER after the fill's front: 0.295 at an empty bar, 0.705 at a full one (80% left and
 * 20% right by loudness at the start, the other way round at the end), gliding with a 0.3 s time constant so a jump
 * in the bar does not snap the sound across. It fades in over 0.4 s as a loading screen comes up and out over 0.4 s
 * when it goes.
 *
 * One change from the mock, made for the game: the mock took the crackle from how fast its smooth fill curve moved.
 * The game's bar moves in steps, and is read every 20 ms, so here it is taken from how fast the GLIDING front moves,
 * which a step sets moving and which, on a smooth fill, moves as the fill does.
 */
#ifndef CINDERLOAD_FIRE_H
#define CINDERLOAD_FIRE_H

#define FIRE_PAN_FROM   0.295f   /* equal-power pan at an empty bar: 80% of the loudness left */
#define FIRE_PAN_TO     0.705f   /* at a full bar: 80% right */
#define FIRE_GLIDE_S    0.30f    /* the pan follows the fill with this time constant (the mock's) */
#define FIRE_MOVE_S     0.25f    /* the crackle follows the front's speed with this one (the mock's) */
#define FIRE_MOVE_CAP   0.60f    /* speeds over this many bars a second count as this */
#define FIRE_MOVE_FULL  0.25f    /* at this speed the crackle is at 1 */
#define FIRE_MOVE_MAX   1.20f    /* and never more than this */
#define FIRE_FADE_IN_S  0.40f
#define FIRE_FADE_OUT_S 0.40f
#define FIRE_XFADE_S    0.12f    /* each loop's seam */
#define FIRE_LEVEL      2.12f    /* the mock's own scale: its mix brought to a peak of 0.8 (2.1198 for these six files) */
#define FIRE_MOCK_RATE  44100    /* the rate the mock's offsets are counted at */
#define FIRE_MAX_FRAMES (1 << 21) /* a source over this many frames is not one of these loops */

enum { FIRE_BED, FIRE_CRACKLE };
#define FIRE_LOOPS 6
static const struct { const char *path; float gain; int layer; int offset; } kFireLoops[FIRE_LOOPS] = {
    { "Sound\\Doodad\\CampFireLargeLoop.wav",     1.00f, FIRE_BED,     0     },
    { "Sound\\Doodad\\UndeadCampfireLArge.wav",   0.55f, FIRE_BED,     30000 },
    { "Sound\\Doodad\\AlterOfKingsFireLoop.wav",  0.45f, FIRE_BED,     51000 },
    { "Sound\\Ambience\\Water\\LavaPoolLoop.wav", 0.18f, FIRE_BED,     90000 },
    { "Sound\\Doodad\\UndeadFireLarge.wav",       0.80f, FIRE_CRACKLE, 20000 },
    { "Sound\\Doodad\\TorchFireLoop.wav",         0.50f, FIRE_CRACKLE, 70000 },
};

/* ---- a little arithmetic, with no C runtime ---- */
#define FIRE_HALF_PI 1.57079632679489662f
static float FireClamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static double FireExp(double x)                    /* e^x: halved until small, a series, squared back */
{
    int k = 0;
    while ((x > 0.5 || x < -0.5) && k < 60) { x *= 0.5; k++; }
    double term = 1, sum = 1;
    for (int i = 1; i < 14; i++) { term *= x / i; sum += term; }
    while (k-- > 0) sum *= sum;
    return sum;
}
static float FireSin(float x)                       /* for 0 <= x <= pi/2, to about 1e-7 */
{
    float x2 = x * x;
    return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156))))));
}
static float FireCos(float x)
{
    float x2 = x * x;
    return 1 - x2 / 2 * (1 - x2 / 12 * (1 - x2 / 30 * (1 - x2 / 56 * (1 - x2 / 90 * (1 - x2 / 132 * (1 - x2 / 182))))));
}

/* The equal-power gains for a fill of 0..1: pan = 0.295 + 0.41 * fill, left = cos(pan * pi/2), right = sin(pan * pi/2),
 * so left^2 + right^2 is always 1 and only the balance moves. */
static float FirePan(float fill) { return FIRE_PAN_FROM + (FIRE_PAN_TO - FIRE_PAN_FROM) * FireClamp(fill, 0, 1); }
static void FireGains(float fill, float *left, float *right)
{
    float a = FirePan(fill) * FIRE_HALF_PI;
    *left = FireCos(a);
    *right = FireSin(a);
}

/* The bed's level for a fill: 0.85 empty, rising to 1.15 by a third of the bar. */
static float FireBedLevel(float fill) { return 0.85f + 0.3f * FireClamp(fill * 3, 0, 1); }
/* The crackle's level for a smoothed speed of the front, in bars a second. */
static float FireCrackleLevel(float speed) { return FireClamp(speed / FIRE_MOVE_FULL, 0, FIRE_MOVE_MAX); }

/* ---- the source files ---- */

/* A RIFF WAVE of 16-bit PCM, mono or stereo (stereo averaged to mono, as the mock did), into floats of -1..1 on buf,
 * which holds room for max frames. The frame count, or 0 for anything else (and *rate set). Bounds are checked
 * against len at every step: a damaged file is refused, never read past. */
static unsigned FireRd16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static unsigned FireRd32(const unsigned char *p) { return FireRd16(p) | FireRd16(p + 2) << 16; }
static int FireWavInfo(const unsigned char *wav, unsigned len, int *rate, int *channels, const unsigned char **data,
                       unsigned *frames)
{
    if (len < 12 || FireRd32(wav) != 0x46464952u || FireRd32(wav + 8) != 0x45564157u) return 0;   /* "RIFF" "WAVE" */
    int haveFmt = 0;
    for (unsigned at = 12; at + 8 <= len;) {
        unsigned id = FireRd32(wav + at), size = FireRd32(wav + at + 4);
        if (size > len - at - 8) return 0;
        const unsigned char *body = wav + at + 8;
        if (id == 0x20746D66u) {                                                                    /* "fmt " */
            if (size < 16 || FireRd16(body) != 1 || FireRd16(body + 14) != 16) return 0;            /* PCM, 16 bit */
            *channels = (int)FireRd16(body + 2);
            *rate = (int)FireRd32(body + 4);
            if ((*channels != 1 && *channels != 2) || *rate < 8000 || *rate > 192000) return 0;
            haveFmt = 1;
        } else if (id == 0x61746164u) {                                                             /* "data" */
            if (!haveFmt) return 0;
            *data = body;
            *frames = size / (2u * (unsigned)*channels);
            return *frames > 1 && *frames <= FIRE_MAX_FRAMES;
        }
        at += 8 + size + (size & 1);
    }
    return 0;
}
static void FireWavRead(const unsigned char *data, int channels, unsigned frames, float *out)
{
    for (unsigned i = 0; i < frames; i++) {
        const unsigned char *s = data + (size_t)i * 2 * (unsigned)channels;
        float v = (float)(short)FireRd16(s);
        if (channels == 2) v = (v + (float)(short)FireRd16(s + 2)) * 0.5f;
        out[i] = v / 32768.0f;
    }
}

/* How many frames a source of n frames at rate `from` becomes at rate `to`: floor(n * to / from), as the mock's
 * resampler counts them. */
static unsigned FireResampledLength(unsigned n, int from, int to) { return (unsigned)((double)n * to / from); }

/* One loop, made as the mock made it: the source resampled to `to` by straight lines between its frames (np.interp, the
 * last frame held past the end), turned to start `start` frames in, faded in and out over xf frames at that seam, and
 * laid end to end every n - xf frames, each copy's fade-out over the next one's fade-in. One period of that, n - xf
 * frames, is written to out (room for FireResampledLength frames). Returns the period, or 0 when the loop is too
 * short to cross-fade. */
static unsigned FireMakeLoop(const float *src, unsigned frames, int from, int to, int start, float *out)
{
    if (from <= 0 || to <= 0 || frames < 2) return 0;
    unsigned n = FireResampledLength(frames, from, to), xf = (unsigned)to * 12u / 100u;     /* FIRE_XFADE_S, exactly */
    if (n < 2 * xf + 2) return 0;
    /* resampled, into out: frame j sits at j * from / to source frames, stepped with whole numbers so it never drifts */
    unsigned idx = 0, rem = 0;
    for (unsigned j = 0; j < n; j++) {
        float a = src[idx];
        out[j] = idx + 1 < frames ? a + (src[idx + 1] - a) * ((float)rem / (float)to) : src[frames - 1];
        rem += (unsigned)from;
        while (rem >= (unsigned)to) { rem -= (unsigned)to; idx++; }
        if (idx >= frames) idx = frames - 1;
    }
    /* turned by start: a seam there instead of at the file's own ends (np.roll) */
    unsigned s = (unsigned)start % n;
    if (s) {                                                    /* rotate in place, three reversals */
        for (unsigned a = 0, b = s - 1; a < b; a++, b--) { float t = out[a]; out[a] = out[b]; out[b] = t; }
        for (unsigned a = s, b = n - 1; a < b; a++, b--) { float t = out[a]; out[a] = out[b]; out[b] = t; }
        for (unsigned a = 0, b = n - 1; a < b; a++, b--) { float t = out[a]; out[a] = out[b]; out[b] = t; }
    }
    /* the seam: frame i of the head (rising ramp) plus frame n - xf + i of the tail (falling ramp), as the mock's
     * np.linspace(0, 1, xf) ramps */
    for (unsigned i = 0; i < xf; i++) {
        float up = xf > 1 ? (float)i / (float)(xf - 1) : 1;
        out[i] = out[i] * up + out[n - xf + i] * (1 - up);
    }
    return n - xf;
}

/* ---- the mixer ---- */
typedef struct {
    int rate;                                  /* frames a second, the stream's */
    float *loop[FIRE_LOOPS];                   /* one period each, or NULL when that file could not be had */
    unsigned len[FIRE_LOOPS], pos[FIRE_LOOPS];
    float kGlide, kMove, stepIn, stepOut;      /* per frame */
    float front, last, speed, env;             /* the gliding fill, its last value, its smoothed speed, the fade */
} FireMix;

/* Coefficients for a rate, and the mixer set back to silence at the start of every loop. */
static void FireSetRate(FireMix *m, int rate)
{
    m->rate = rate;
    m->kGlide = (float)(1 - FireExp(-1.0 / (rate * (double)FIRE_GLIDE_S)));
    m->kMove = (float)(1 - FireExp(-1.0 / (rate * (double)FIRE_MOVE_S)));
    m->stepIn = 1.0f / (FIRE_FADE_IN_S * rate);
    m->stepOut = 1.0f / (FIRE_FADE_OUT_S * rate);
}
static void FireReset(FireMix *m)
{
    m->front = m->last = m->speed = m->env = 0;
    for (int i = 0; i < FIRE_LOOPS; i++)
        m->pos[i] = 0;
}

/* frames of 16-bit stereo into out. gate: a loading screen is showing; fill: how far its bar is, 0..1. Returns 1 while
 * there is still something to hear (the fade not yet at zero). */
static int FireRender(FireMix *m, int gate, float fill, short *out, int frames)
{
    fill = FireClamp(fill, 0, 1);
    for (int f = 0; f < frames; f++) {
        if (gate && m->env <= 0) { m->front = m->last = fill; m->speed = 0; }   /* a new screen: start at its fill */
        m->env = FireClamp(m->env + (gate ? m->stepIn : -m->stepOut), 0, 1);
        m->front += (fill - m->front) * m->kGlide;
        float speed = (m->front - m->last) * (float)m->rate;
        m->last = m->front;
        m->speed += ((speed < FIRE_MOVE_CAP ? speed : FIRE_MOVE_CAP) - m->speed) * m->kMove;
        float bed = 0, crackle = 0;
        for (int i = 0; i < FIRE_LOOPS; i++) {
            if (!m->loop[i]) continue;
            float v = m->loop[i][m->pos[i]] * kFireLoops[i].gain;
            if (++m->pos[i] >= m->len[i]) m->pos[i] = 0;
            if (kFireLoops[i].layer == FIRE_BED) bed += v; else crackle += v;
        }
        float mono = (bed * FireBedLevel(fill) + crackle * FireCrackleLevel(m->speed)) * FIRE_LEVEL * m->env, l, r;
        FireGains(m->front, &l, &r);
        float sl = FireClamp(mono * l * 32767.0f, -32767.0f, 32767.0f), sr = FireClamp(mono * r * 32767.0f, -32767.0f, 32767.0f);
        out[2 * f] = (short)sl;
        out[2 * f + 1] = (short)sr;
    }
    return m->env > 0 || gate;
}

/* ---- what the worker makes of the client's numbers ---- */

/* The bar's fill as the worker passes it on: the client clears the number to 0 and then works it out again every
 * frame, so a read can catch the 0 (or a part sum) in between. Within one loading screen the fill only grows, so the
 * largest seen is the fill; a fall that lasts FIRE_FALL_POLLS reads running (20 ms apart, never a passing 0) is the
 * client starting a new screen in the same window, and is taken. */
#define FIRE_FALL_POLLS 3
typedef struct { float fill; int falls; } FireFill;
static void FireFillStart(FireFill *s, float read) { s->fill = (read >= 0 && read <= 1) ? read : (read > 1 ? 1 : 0); s->falls = 0; }
static float FireFillRead(FireFill *s, float read)
{
    if (!(read >= 0)) read = 0;                     /* NaN as 0 */
    if (read > 1) read = 1;
    if (read >= s->fill) { s->fill = read; s->falls = 0; }
    else if (read < s->fill - 0.25f && ++s->falls >= FIRE_FALL_POLLS) { s->fill = read; s->falls = 0; }
    else if (read >= s->fill - 0.25f) s->falls = 0;
    return s->fill;
}

#endif
