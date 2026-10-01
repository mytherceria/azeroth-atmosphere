/* sounds.c: the recipes of tools/make_loops.sh and tools/make_storm_assets.sh, on the building blocks of
 * dsp.c. See sounds.h for how the DLL uses them. Each recipe quotes the ffmpeg chain it replaces. GPL-3.0.
 *
 * Where the result differs from the shipped files, on purpose:
 * - Rates. The scripts took most sounds to 44.1 kHz with ffmpeg's resampler, some before their recipes and some
 *   after; here each goes to the rate the client's mixer runs at (44,100 Hz on both clients: the rate the client's
 *   mixer plays at, below), through dsp_resample in ffmpeg's place and on the same side of its recipe (the medium and
 *   heavy indoor rain by way of 44.1 kHz for a slower mixer, where the scripts ran their recipe), snow and sand keep
 *   their sources' 22,050 Hz, as the shipped files did, and the thunder and the gusts go on from a slower mixer's rate
 *   to the least multiple of it that is 44.1 kHz or more. make_loops.sh's loudnorm left indoor_rain_light at 192 kHz;
 *   here it is at 44.1 kHz like the rest.
 * - Loops are processed as loops (LOOP_PRIME_MS): ffmpeg started each filter from rest and ran alimiter
 *   with its lookahead delay, which left a step, or 5 ms of silence, at the loop point of most shipped loops.
 * - loudnorm is a static gain to its target plus a peak limiter (dsp_normalize), not its dynamic ride, with
 *   the gain loudnorm starts from: the integrated loudness for the loops, the first 3 s for the thunder and
 *   gust one-shots (dsp.h). The loops land within 0.1 LU of ffmpeg's own output and the one-shots within
 *   0.3 LU of the shipped ones.
 */
#include "sounds.h"

#define WEATHER "Sound\\Ambience\\Weather\\"
#define BOLT    "Sound\\Doodad\\BlastedLandsLightningbolt01Stand-Bolt"
#define WIND    "Sound\\Spells\\SPELL_SH_Revamp_Wind_PreCast0"

/* Where each source comes from. Light and medium rain: only the original game's loops in patch.MPQ, flat and
   thunder-free (patch-S.mpq's medium loop has a thunder roll every 31 s baked in, and must never be used).
   Heavy: RavenCraft's own loop in patch-S.mpq when it is the file the splice times were measured on, else
   patch.MPQ's. The thunder: RavenCraft's newer CallLightning.wav, else the original. */
const snd_source_t SND_SOURCES[SRC_COUNT] = {
    { WEATHER "RainLightLoop.wav",   SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "RainMediumLoop.wav",  SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "RainHeavyLoop.wav",   SND_WAV, 0, { { "patch-S.mpq", 14419190u, SND_HEAVY_RAVENCRAFT }, { "patch.MPQ", 0u, SND_PLAIN } } },
    { WEATHER "SnowLight.wav",       SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "SnowMedium.wav",      SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "SnowHeavy.wav",       SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "SandStormLight.wav",  SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "SandStormMedium.wav", SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WEATHER "SandStormHeavy.wav",  SND_WAV, 0, { { "patch.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { "Sound\\Spells\\CallLightning.wav", SND_WAV, 0, { { "patch-S.mpq", 0u, SND_PLAIN }, { "sound.MPQ", 0u, SND_PLAIN } } },
    { BOLT ".wav",  SND_WAV, 0, { { "sound.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { BOLT "1.wav", SND_WAV, 0, { { "sound.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { BOLT "2.wav", SND_WAV, 0, { { "sound.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { BOLT "3.wav", SND_WAV, 0, { { "sound.MPQ", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WIND "1.ogg", SND_OGG, 1, { { "patch-S.mpq", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WIND "2.ogg", SND_OGG, 1, { { "patch-S.mpq", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
    { WIND "3.ogg", SND_OGG, 1, { { "patch-S.mpq", 0u, SND_PLAIN }, { 0, 0u, 0 } } },
};

/* The thunder by distance, as make_storm_assets.sh paired them: close is the crack, near to farther the four bolts. */
const snd_output_t SND_OUTPUTS[SND_COUNT] = {
    { "outdoor_rain_light", SRC_RAIN_LIGHT, 1 }, { "outdoor_rain_medium", SRC_RAIN_MEDIUM, 1 }, { "outdoor_rain_heavy", SRC_RAIN_HEAVY, 1 },
    { "indoor_rain_light", SRC_RAIN_LIGHT, 1 },  { "indoor_rain_medium", SRC_RAIN_MEDIUM, 1 },  { "indoor_rain_heavy", SRC_RAIN_HEAVY, 1 },
    { "indoor_snow_light", SRC_SNOW_LIGHT, 1 },  { "indoor_snow_medium", SRC_SNOW_MEDIUM, 1 },  { "indoor_snow_heavy", SRC_SNOW_HEAVY, 1 },
    { "indoor_sand_light", SRC_SAND_LIGHT, 1 },  { "indoor_sand_medium", SRC_SAND_MEDIUM, 1 },  { "indoor_sand_heavy", SRC_SAND_HEAVY, 1 },
    { "thunder_close", SRC_CALL_LIGHTNING, 0 }, { "thunder_near", SRC_BOLT, 0 }, { "thunder_mid", SRC_BOLT1, 0 },
    { "thunder_far", SRC_BOLT2, 0 }, { "thunder_far2", SRC_BOLT3, 0 },
    { "thunder_close_in", SRC_CALL_LIGHTNING, 0 }, { "thunder_near_in", SRC_BOLT, 0 }, { "thunder_mid_in", SRC_BOLT1, 0 },
    { "thunder_far_in", SRC_BOLT2, 0 }, { "thunder_far2_in", SRC_BOLT3, 0 },
    { "gust_1", SRC_GUST1, 0 }, { "gust_2", SRC_GUST2, 0 }, { "gust_3", SRC_GUST3, 0 },
};

/* How much of a loop's own end runs through each filter before its start (dsp.h, prime), so the loop point
   is filtered like any other moment: a second is many times the longest memory in these chains (the
   compressor's 400 ms release is a 100 ms time constant in ffmpeg's units). The limiters of the loops wrap
   around the loop point when this is on. 0 gives ffmpeg's behaviour, less alimiter's delay. */
#define LOOP_PRIME_MS 1000
static int prime_for(const dsp_buf_t *b, int loop) { int n = loop ? dsp_frames(b->rate, LOOP_PRIME_MS / 1000.0) : 0; return n > b->frames ? b->frames : n; }

/* ---- the heavy loop ----
 * make_storm_assets.sh: RavenCraft's 75 s loop has thunder rolls at 12-21 s and 42-56 s, so it keeps A
 * (0-11.5 s), B (22.5-41.5 s) and C (57-75 s), crossfades B into C over 1.5 s with qsin, puts A after C (C's
 * end is the original loop point into A: 75 s runs into 0), and loopifies the 47 s over 1.5 s: 45.5 s.
 *   [x]atrim=22.5:41.5[b]; [y]atrim=57:75[c]; [z]atrim=0:11.5[a]; [b][c]acrossfade=d=1.5:c1=qsin:c2=qsin[bc]; [bc][a]concat
 *   loopify heavy_clean.wav heavy_loop.wav 1.5
 * The times fit only that file. Any other heavy loop (patch.MPQ's 25 s original) is only loopified. The
 * splice is built straight from views of the source, so the only new buffer is the 47 s result. snd_prepare
 * makes it once for both heavy outputs, at the source's own rate, as the script did before its -ar 44100. */
static int heavy_loop(dsp_buf_t *out, const dsp_buf_t *src, int variant, const dsp_alloc_t *a)
{
    dsp_buf_t part[3]; int x[2], r, rate = src->rate;
    if (variant == SND_HEAVY_RAVENCRAFT && src->frames >= dsp_frames(rate, 75.0)) {
        if ((r = dsp_view(&part[0], src, dsp_frames(rate, 22.5), dsp_frames(rate, 41.5))) != DSP_OK) return r;
        if ((r = dsp_view(&part[1], src, dsp_frames(rate, 57.0), dsp_frames(rate, 75.0))) != DSP_OK) return r;
        if ((r = dsp_view(&part[2], src, 0, dsp_frames(rate, 11.5))) != DSP_OK) return r;
        x[0] = dsp_frames(rate, 1.5); x[1] = 0;
        r = dsp_join(out, part, x, 3, DSP_QSIN, a);
    } else r = dsp_copy(out, src, a);
    if (r != DSP_OK) return r;
    if ((r = dsp_loopify(out, dsp_frames(rate, 1.5), DSP_QSIN)) != DSP_OK) dsp_free(out, a);
    return r;
}

/* ---- the recipes; each works in place on its own copy of the source ---- */

/* outdoor rain, to_lufs in make_storm_assets.sh: volume=(target - measured)dB,alimiter=limit=0.89:level=false
   (alimiter's defaults: 5 ms attack, 50 ms release). Light -21, medium -17, heavy -14 LUFS, before the
   client's own 0.69 x AmbienceVolume. */
static int outdoor_rain(dsp_buf_t *b, double lufs, int prime, const dsp_alloc_t *a)
{
    double now; int r;
    if ((r = dsp_loudness(b, &now, a)) != DSP_OK) return r;
    dsp_gain(b, lufs - now);
    return dsp_limit(b, 0.89, 5.0, 50.0, prime > 0, a);
}

/* indoor rain, make_loops.sh (light) and make_storm_assets.sh (medium, heavy, from the thunder-free loops):
   highpass=f=160:p=2,lowpass=f=2500,acompressor=1:downward:-22dB:8:3:400:2,loudnorm=I=-16:TP=-1.5:LRA=6
   The rumble under 160 Hz goes (the thunder), the patter up to 2.5 kHz stays, the claps are compressed
   (acompressor's positional options: level_in 1, downward, threshold -22 dB, ratio 8, attack 3 ms, release
   400 ms, makeup 2, the last a factor of 2, +6 dB), and the level is brought to -16 LUFS. */
static int indoor_rain(dsp_buf_t *b, int prime, const dsp_alloc_t *a)
{
    dsp_comp_t c;
    c.level_in = 1.0; c.threshold = dsp_db2lin(-22.0); c.ratio = 8.0; c.attack_ms = 3.0; c.release_ms = 400.0;
    c.makeup = 2.0; c.knee = 2.82843; c.rms = 1; c.link_max = 0;   /* the rest are acompressor's defaults */
    dsp_highpass(b, 160.0, prime);
    dsp_lowpass(b, 2500.0, prime);
    dsp_compress(b, &c, prime);
    return dsp_normalize(b, -16.0, -1.5, prime > 0, a);
}

/* indoor snow and sand, make_loops.sh: lowpass=f=750:p=2, heard and approved that way on 26 Sep 2026 */
static int muffle(dsp_buf_t *b, int prime) { dsp_lowpass(b, 750.0, prime); return DSP_OK; }

/* thunder outdoors, make_storm_assets.sh:
   highpass=f=25,loudnorm=I=-18:TP=-1:LRA=11,volume=6dB,alimiter=limit=0.89:attack=3:release=120:level=false -ac 1
   pushed 6 dB into the limiter, so a thunder at full channel volume still stands clear of the louder heavy rain */
static int thunder_out(dsp_buf_t *b, const dsp_alloc_t *a)
{
    int r;
    dsp_highpass(b, 25.0, 0);
    if ((r = dsp_normalize(b, -18.0, -1.0, 0, a)) != DSP_OK) return r;
    dsp_gain(b, 6.0);
    if ((r = dsp_limit(b, 0.89, 3.0, 120.0, 0, a)) != DSP_OK) return r;
    dsp_mono(b);
    return DSP_OK;
}

/* thunder indoors, the muffled copy: highpass=f=25,lowpass=f=350:p=2,lowpass=f=350:p=2,loudnorm=I=-19:TP=-2:LRA=11 -ac 1 */
static int thunder_in(dsp_buf_t *b, const dsp_alloc_t *a)
{
    int r;
    dsp_highpass(b, 25.0, 0);
    dsp_lowpass(b, 350.0, 0);
    dsp_lowpass(b, 350.0, 0);
    if ((r = dsp_normalize(b, -19.0, -2.0, 0, a)) != DSP_OK) return r;
    dsp_mono(b);
    return DSP_OK;
}

/* gusts, the spell's sparkle above 3 kHz trimmed and a gentle fade at both ends:
   highpass=f=70,lowpass=f=3000,afade=t=in:d=0.4,areverse,afade=t=in:d=0.8,areverse,loudnorm=I=-20:TP=-2:LRA=9 -ac 1 */
static int gust(dsp_buf_t *b, const dsp_alloc_t *a)
{
    int r;
    dsp_highpass(b, 70.0, 0);
    dsp_lowpass(b, 3000.0, 0);
    dsp_fade(b, dsp_frames(b->rate, 0.4), dsp_frames(b->rate, 0.8), DSP_TRI);
    if ((r = dsp_normalize(b, -20.0, -2.0, 0, a)) != DSP_OK) return r;
    dsp_mono(b);
    return DSP_OK;
}

/* ---- what a source may be (sounds.h) ---- */
int snd_source_ok(unsigned int frames, int channels, int rate)
{
    return channels >= 1 && channels <= DSP_MAX_CHANNELS && rate >= SND_MIN_RATE && rate <= SND_MAX_RATE
        && frames <= (unsigned int)SND_MAX_SECONDS * (unsigned int)rate && frames * (unsigned int)channels <= SND_MAX_SAMPLES;
}
int snd_wav_decode(const void *data, unsigned int size, dsp_buf_t *pcm, const dsp_alloc_t *a)
{
    unsigned int frames; int ch, rate, r;
    pcm->s = 0; pcm->frames = 0; pcm->owned = 0;
    if ((r = dsp_wav_info(data, size, &frames, &ch, &rate)) != DSP_OK) return r;
    if (!snd_source_ok(frames, ch, rate)) return SND_UNFIT;   /* before a sample is decoded: its size is the point */
    return dsp_wav_decode(pcm, data, size, a);
}
static unsigned int le32(const unsigned char *p) { return p[0] | (unsigned int)p[1] << 8 | (unsigned int)p[2] << 16 | (unsigned int)p[3] << 24; }
const char *snd_ogg_probe(const void *data, unsigned int size, int *channels, int *rate, unsigned int *frames)
{
    const unsigned char *d = (const unsigned char *)data;
    unsigned int crc_table[256], at = 0, serial = 0, pages = 0, i, k, crc;
    *channels = 0; *rate = 0; *frames = 0;
    /* Ogg's page checksum: CRC-32 on the polynomial 0x04C11DB7, most significant bit first, from 0; built here, 1 KB
       of stack, rather than kept, so nothing is shared between threads */
    for (i = 0; i < 256u; i++) {
        for (crc = i << 24, k = 0; k < 8u; k++) crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        crc_table[i] = crc;
    }
    for (;;) {
        const unsigned char *p = d + at; unsigned int left = size - at, head, body = 0, flags;
        if (left < 27u || p[0] != 'O' || p[1] != 'g' || p[2] != 'g' || p[3] != 'S')
            return pages ? "bytes that are not a page where the next page should begin" : "not an Ogg file";
        if (p[4] != 0) return "a page of an Ogg version this does not read";
        head = 27u + p[26];
        if (head > left) return "a page that runs past the end of the file";
        for (k = 27u; k < head; k++) body += p[k];
        if (body > left - head) return "a page that runs past the end of the file";
        /* the checksum is taken over the whole page with its own four bytes (22 to 25) counted as zero */
        for (crc = 0, k = 0; k < head + body; k++) crc = (crc << 8) ^ crc_table[((crc >> 24) ^ (k - 22u < 4u ? 0u : p[k])) & 0xFFu];
        if (crc != le32(p + 22)) return "a page whose checksum is wrong";
        flags = p[5];
        if (!pages) {
            /* the first page begins the stream and holds the identification header alone, 30 bytes: type 1, "vorbis",
               version 0, the channels, the rate, three bitrates, the two block sizes and the framing bit (Vorbis I,
               4.2.2 and A.2) */
            const unsigned char *id = p + 28;
            if (!(flags & 2u) || (flags & 1u)) return "a file that does not begin a stream";
            if (p[26] != 1 || p[27] != 30 || id[0] != 1 || id[1] != 'v' || id[2] != 'o' || id[3] != 'r' || id[4] != 'b' || id[5] != 'i' || id[6] != 's')
                return "not an Ogg Vorbis file (no identification header alone on its first page)";
            if (le32(id + 7) != 0 || !(id[29] & 1u)) return "a Vorbis identification header this does not read";
            serial = le32(p + 14);
            *channels = id[11]; *rate = le32(id + 12) > 0x7FFFFFFFu ? 0 : (int)le32(id + 12);
        } else {
            if (flags & 2u) return "a second stream after the first (a chained file)";
            if (le32(p + 14) != serial) return "pages of another stream than the first";
        }
        pages++;
        at += head + body;
        if (flags & 4u) {   /* the end of the stream, which must be the end of the file */
            if (at != size) return "bytes after the end of its stream";
            if (le32(p + 10) != 0) return "a length past 2^32 frames";
            *frames = le32(p + 6);
            break;
        }
        if (at == size) return "no page that ends its stream, so no length of its own";
    }
    if (*channels < 1 || *channels > 2 || *rate < 1) return "a channel count or rate FMOD does not decode";
    return 0;
}

/* ---- the rate the client's mixer plays at ----
 * FMOD 3.75's software mixer, the one both clients' Sound.log name (MMXP6, at 44,100 Hz), steps through a voice at
 * the voice's rate over its own, and does not interpolate a 16-bit stereo voice: it holds each frame until the next
 * is due (a mono one it interpolates on a straight line, below). A loop at 11,025 Hz is held for four output frames
 * a frame, which sends images of its band up the spectrum: the muffled indoor rain's 160 to 2,500 Hz patter came back
 * as a hiss at 5.5 to 14 kHz, 16 to 27 dB over 0.13's there, and the outdoor rain at 22,050 Hz as a fizz at 11 to
 * 17 kHz; and a 48 kHz loop loses frames with nothing to stop its top folding down (0.14 review, from the client's own
 * fmod.dll). The DLL opens its own sounds as software voices, and the game's rain loop is one too wherever there are
 * no hardware voices (Wine or Proton without DSOAL, Windows since Vista). So each sound is built at the mixer's rate,
 * where the mixer steps one frame per frame, as it did through 0.13's files, which the scripts had made at 44.1 kHz:
 * dsp_resample, flat to 90% of the lower Nyquist frequency and 90 dB down from it up.
 * Snow and sand are one exception, while the mixer runs at twice their sources' rate: 0.13 shipped them at their
 * 22,050 Hz, and that is how they were heard and approved. The mixer plays each of their frames twice, which puts the
 * images of their band (it ends near 750 Hz) at 20 kHz and up, where the hold's own null takes them 25 to 40 dB
 * further down, as with 0.13: played through a model of that mixer, they are 28.8 to 34.5 dB under the whole loop,
 * within 0.1 dB of 0.13's own, and from 8 to 16 kHz ours are quieter than 0.13's in every third (the third 0.14
 * review). At the mixer's rate the six would take twice the memory, 50 MB, for nothing heard.
 * The thunder and the gusts are the other. The DLL plays them at a random pitch (88 to 104 % and 90 to 110 %,
 * shot_play), so the mixer does not step them a frame per frame: it draws a straight line between two frames of a
 * mono voice, which dulls the top of its band (by sinc^4 of each frequency's share of the voice's own rate) and makes
 * images of it that fold back into what is heard. Built at a slower mixer's own rate, the band is a large share of
 * the voice's rate: at 22,050 Hz thunder_close came out 3.6 dB duller at 8 kHz than the 44.1 kHz build, 0.13's rate,
 * played on the same mixer, and its images and aliases were 14 dB louder, -23 dB of the whole against -37 (the fourth
 * 0.14 review). So they are built at the least multiple of the mixer's rate that is 44.1 kHz or more
 * (snd_oneshot_rate): taken to the mixer's rate, which cuts the band to 90% of its Nyquist frequency, then up to that
 * multiple, which adds nothing to it. No frequency is then a larger share of the voice's rate than in the 44.1 kHz
 * build, so at every one of the DLL's pitches each is no duller than that build played on the same mixer and, where
 * they are within 60 dB of the whole, has no more images and aliases (tools/ownsounds_selftest.c rates); at 22,050 Hz
 * the thunder is the 44.1 kHz build itself.
 * Twice the mixer's rate alone, as first proposed, still left them a dB duller at an 8 kHz mixer. A whole multiple
 * keeps right the pitch the DLL sets from the voice's own rate, and a voice left at 100 % is stepped a whole number
 * of frames, with no line drawn. At 44.1 kHz and over they stay at the mixer's rate, as 0.13's files did.
 * Each recipe runs where the scripts ran it (recipe_rate). The medium and heavy indoor rain run at 44.1 kHz, the rate
 * the scripts took those loops to first (RavenCraft's heavy one after its splice, which snd_prepare makes at its own
 * 48 kHz), or at the mixer's rate where that is faster, and only then go to the mixer's rate: run at a 22,050 Hz
 * mixer's own rate, their 2.5 kHz low-pass left them 1.9 dB duller at 5 kHz and 7.7 dB at 8 kHz than the same loops
 * built at 44.1 kHz (the third 0.14 review); run at 48 kHz, it moves them 0.5 dB, at 10 kHz. The outdoor
 * rain, a gain and a limiter, which no rate changes, runs at the mixer's rate, after the resample. The light indoor
 * rain, the thunder, the gusts and snow and sand run at their sources' rates, before it (make_loops.sh ran the light
 * rain on its raw 22,050 Hz file, and each thunder chain ended in -ar 44100). The order is heard: a 2-pole low-pass
 * at 2.5 kHz run at 22,050 Hz falls to nothing at that rate's top, and run at 44.1 kHz it left the light indoor rain
 * 7 dB brighter at 8 kHz than 0.13's, as played. A recipe that ended in a limiter before the new rate has its ceiling
 * held again after it, since the band-limited samples between the old ones can peak a little higher. */
static int build_rate(int src, int rate, int mix) { return src >= SRC_SNOW_LIGHT && src <= SRC_SAND_HEAVY && 2 * rate == mix ? rate : mix; }
int snd_oneshot_rate(int mix_rate) { return mix_rate > 0 ? (SND_ONESHOT_RATE + mix_rate - 1) / mix_rate * mix_rate : 0; }
/* The rate a recipe runs at when its source goes to a new rate before it (to: the rate it is built at), else 0: the
   recipe runs at its source's own rate, and the resample comes after it */
static int recipe_rate(int out, int to)
{
    if (out <= SND_OUTDOOR_RAIN_HEAVY) return to;
    if (out == SND_INDOOR_RAIN_MEDIUM || out == SND_INDOOR_RAIN_HEAVY) return to > 44100 ? to : 44100;
    return 0;
}
static int to_rate(dsp_buf_t *b, int rate, int loop, const dsp_alloc_t *a) { return dsp_resample(b, rate, 0.45 * (b->rate < rate ? b->rate : rate), loop, a); }
int snd_prepare(int src, snd_input_t *in, int mix_rate, const dsp_alloc_t *a)
{
    dsp_buf_t *b; int r;
    if (src < 0 || src >= SRC_COUNT || !in || !a || mix_rate < SND_MIX_MIN || mix_rate > SND_MIX_MAX) return DSP_BADARG;
    b = &in->pcm;
    if (!b->s || b->frames <= 0) return SND_NO_SOURCE;
    if (!b->owned || in->mix_rate) return DSP_BADARG;   /* a view cannot be replaced, and a source is made ready once */
    if (!snd_source_ok((unsigned int)b->frames, b->channels, b->rate)) return SND_UNFIT;   /* the gusts, which FMOD decodes, come here unchecked */
    if (src == SRC_RAIN_HEAVY) {   /* once for both heavy outputs, at the source's own rate */
        dsp_buf_t spliced;
        if ((r = heavy_loop(&spliced, b, in->variant, a)) != DSP_OK) return r;
        dsp_free(b, a);
        *b = spliced;
    }
    in->mix_rate = mix_rate;
    return DSP_OK;
}

int snd_build(int out, const snd_input_t *src, void **wav, unsigned int *wav_size, const dsp_alloc_t *a)
{
    static const double OUTDOOR_LUFS[3] = { -21.0, -17.0, -14.0 };
    dsp_buf_t b; int r, loop, prime, to, fin, run; double ceiling_db = 0.0;
    *wav = 0; *wav_size = 0;
    if (out < 0 || out >= SND_COUNT || !a) return DSP_BADARG;
    if (!src || !src->pcm.s || src->pcm.frames <= 0) return SND_NO_SOURCE;
    if (!src->mix_rate) return DSP_BADARG;   /* not through snd_prepare: its mixer rate unknown, and the heavy loop not spliced */
    loop = SND_OUTPUTS[out].loop;
    to = build_rate(SND_OUTPUTS[out].source, src->pcm.rate, src->mix_rate);
    fin = loop ? to : snd_oneshot_rate(src->mix_rate);   /* the thunder and the gusts: a multiple of a slower mixer's rate (above) */
    /* a working copy: the source is only read, so one decode can serve both outputs that share it */
    if ((r = dsp_copy(&b, &src->pcm, a)) != DSP_OK) return r;
    if ((run = recipe_rate(out, to)) && (r = to_rate(&b, run, loop, a)) != DSP_OK) { dsp_free(&b, a); return r; }
    prime = prime_for(&b, loop);
    switch (out) {
    case SND_OUTDOOR_RAIN_LIGHT: case SND_OUTDOOR_RAIN_MEDIUM: case SND_OUTDOOR_RAIN_HEAVY:
        r = outdoor_rain(&b, OUTDOOR_LUFS[out - SND_OUTDOOR_RAIN_LIGHT], prime, a); break;
    case SND_INDOOR_RAIN_LIGHT: case SND_INDOOR_RAIN_MEDIUM: case SND_INDOOR_RAIN_HEAVY:
        r = indoor_rain(&b, prime, a); ceiling_db = -1.5; break;   /* loudnorm's TP */
    case SND_INDOOR_SNOW_LIGHT: case SND_INDOOR_SNOW_MEDIUM: case SND_INDOOR_SNOW_HEAVY:
    case SND_INDOOR_SAND_LIGHT: case SND_INDOOR_SAND_MEDIUM: case SND_INDOOR_SAND_HEAVY:
        r = muffle(&b, prime); break;   /* no limiter */
    case SND_THUNDER_CLOSE: case SND_THUNDER_NEAR: case SND_THUNDER_MID: case SND_THUNDER_FAR: case SND_THUNDER_FAR2:
        r = thunder_out(&b, a); ceiling_db = 20.0 * dsp_log10(0.89); break;
    case SND_THUNDER_CLOSE_IN: case SND_THUNDER_NEAR_IN: case SND_THUNDER_MID_IN: case SND_THUNDER_FAR_IN: case SND_THUNDER_FAR2_IN:
        r = thunder_in(&b, a); ceiling_db = -2.0; break;
    default:
        r = gust(&b, a); ceiling_db = -2.0; break;
    }
    /* to the mixer's rate, which cuts the band to 90% of its Nyquist frequency, then a one-shot on a slower mixer up to
       its multiple: one whose source is at that multiple already goes down and back up, since its band reaches past
       the mixer's. A limiter's ceiling is held again after. */
    if (r == DSP_OK && (b.rate != to || fin != to)) {
        if (b.rate != to) r = to_rate(&b, to, loop, a);
        if (r == DSP_OK && fin != to) r = to_rate(&b, fin, loop, a);
        if (r == DSP_OK && ceiling_db < 0.0) r = dsp_limit(&b, dsp_db2lin(ceiling_db), 10.0, 100.0, loop, a);
    }
    if (r == DSP_OK) r = dsp_wav_encode16(&b, wav, wav_size, a);
    dsp_free(&b, a);
    return r;
}

const char *snd_why(int r)
{
    switch (r) {
    case DSP_OK: return "ok";
    case DSP_NOMEM: return "out of memory";
    case DSP_BADWAV: return "not a WAV file this can read";
    case DSP_BADARG: return "a length or parameter out of range (a source much shorter than expected?)";
    case DSP_SILENT: return "the source is silent, or shorter than 0.4 s";
    case SND_NO_SOURCE: return "no samples to build from (an empty file?)";
    case SND_UNFIT: return "not a sound the recipes were made for (over 90 s, over 32 MB as samples, or a rate outside 8 to 96 kHz)";
    default: return "unknown error";
    }
}
