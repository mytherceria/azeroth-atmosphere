/* sounds.h: the 25 sounds IndoorRain plays, rebuilt at run time from the player's own client archives.
 *
 * Up to 0.13 the addon shipped them as .ogg files that tools/make_loops.sh and tools/make_storm_assets.sh
 * made offline from the client's own sound files. Those are copies of Blizzard audio and cannot go on a
 * public site. With these files the DLL can read the same client files out of the MPQs in Data\ and run the
 * same recipes itself (sounds.c, on the building blocks of dsp.c), in memory, and play each result from
 * memory with FSOUND_LOADMEMORY as it plays the files it reads today, so the addon need ship no sound at all.
 *
 * What the DLL does for one source: walk its where[] list in order and take the first archive that holds the
 * file (at the size given, where one is given); decode it (a WAV with snd_wav_decode, which refuses a file the
 * recipes were not made for before decoding a sample; the Ogg Vorbis gusts through FMOD); snd_prepare it for the
 * rate the client's mixer runs at, with the variant of the where[] entry that matched; then snd_build each of its
 * outputs. A source found nowhere means its outputs cannot be built; for an optional source (the gusts) that only
 * means no gusts, not a failure.
 *
 * Memory, as a guide for building in a 32-bit game process: a decoded source is 4 bytes per sample (RavenCraft's
 * heavy rain loop, 75 s of 48 kHz stereo, is 27.5 MB, and no source may pass SND_MAX_SAMPLES, 32 MB); snd_prepare
 * splices the heavy loop out of it (16.7 MB at 48 kHz) before the source is let go; snd_build takes a working copy of
 * the source and one at the mixer's rate while it moves there (15.3 MB for the heavy loop at 44.1 kHz), and the WAV
 * it returns is 2 bytes per sample (7.7 MB for each heavy rain loop at 44.1 kHz; 61.9 MB for all 25, of which the six
 * sandstorm and snow loops, at 22,050 Hz, are 25.2 MB). Every sound is built at the mixer's rate, or snow and sand at
 * half of it, because the mixer does not interpolate a stereo voice, and the thunder and the gusts, which the DLL plays
 * at a random pitch, at the least multiple of it that is 44.1 kHz or more (sounds.c), so these figures follow the
 * game's SoundMixRate setting: with the mixer at 48 kHz, snow and sand are built at 48 kHz too, and all 25 take
 * 94.8 MB, the six 54.9 MB of it, and a snow or sand loop's build up to 43 MB on top of what is held; at 22,050 Hz all
 * 25 take 45.8 MB, the thunder and the gusts 4.5 MB of it at 44.1 kHz. MB here are the DLL log's, 1,048,576 bytes.
 * GPL-3.0.
 */
#ifndef INDOORRAIN_SOUNDS_H
#define INDOORRAIN_SOUNDS_H

#include "dsp.h"

/* The 25 outputs. SND_OUTPUTS[i].name is the DLL's name for each: the shipped file was
   Interface\AddOns\IndoorRain\<name>.ogg. */
enum {
    SND_OUTDOOR_RAIN_LIGHT, SND_OUTDOOR_RAIN_MEDIUM, SND_OUTDOOR_RAIN_HEAVY,
    SND_INDOOR_RAIN_LIGHT, SND_INDOOR_RAIN_MEDIUM, SND_INDOOR_RAIN_HEAVY,
    SND_INDOOR_SNOW_LIGHT, SND_INDOOR_SNOW_MEDIUM, SND_INDOOR_SNOW_HEAVY,
    SND_INDOOR_SAND_LIGHT, SND_INDOOR_SAND_MEDIUM, SND_INDOOR_SAND_HEAVY,
    SND_THUNDER_CLOSE, SND_THUNDER_NEAR, SND_THUNDER_MID, SND_THUNDER_FAR, SND_THUNDER_FAR2,
    SND_THUNDER_CLOSE_IN, SND_THUNDER_NEAR_IN, SND_THUNDER_MID_IN, SND_THUNDER_FAR_IN, SND_THUNDER_FAR2_IN,
    SND_GUST_1, SND_GUST_2, SND_GUST_3,
    SND_COUNT
};

/* The 17 client files they are made from. */
enum {
    SRC_RAIN_LIGHT, SRC_RAIN_MEDIUM, SRC_RAIN_HEAVY,
    SRC_SNOW_LIGHT, SRC_SNOW_MEDIUM, SRC_SNOW_HEAVY,
    SRC_SAND_LIGHT, SRC_SAND_MEDIUM, SRC_SAND_HEAVY,
    SRC_CALL_LIGHTNING, SRC_BOLT, SRC_BOLT1, SRC_BOLT2, SRC_BOLT3,
    SRC_GUST1, SRC_GUST2, SRC_GUST3,
    SRC_COUNT
};

/* A where[] entry's variant, handed back to snd_build: which recipe the file it found takes. */
#define SND_PLAIN            0
#define SND_HEAVY_RAVENCRAFT 1   /* RavenCraft's heavy rain loop (patch-S.mpq, 14419190 bytes): 75 s at 48 kHz with two thunder
                                    rolls in it, cut out at times that fit only that file; every other heavy loop is only loopified */

#define SND_WAV 0   /* RIFF PCM: dsp_wav_decode reads it */
#define SND_OGG 1   /* Ogg Vorbis: the caller decodes it (the DLL through the client's FMOD) and hands over the samples */

typedef struct {
    const char *archive;   /* the MPQ's file name in Data\, as the RavenCraft client spells it; compare without regard to case */
    unsigned int size;     /* the file's unpacked size in that archive must be exactly this; 0 = any */
    int variant;           /* SND_PLAIN or SND_HEAVY_RAVENCRAFT */
} snd_where_t;

#define SND_WHERE_MAX 2
typedef struct {
    const char *path;                  /* the file inside the archive */
    int format;                        /* SND_WAV or SND_OGG */
    int optional;                      /* 1 = found nowhere means no such sound, not a failure (the gusts) */
    snd_where_t where[SND_WHERE_MAX];  /* in order of preference; the first archive holding the file at the required size
                                          wins, whatever the client's own patch order says; archive 0 ends the list */
} snd_source_t;

typedef struct {
    const char *name;   /* the DLL's name for it, e.g. "thunder_near_in" */
    int source;         /* SRC_* */
    int loop;           /* 1 = played looped, and built as a loop: its end runs into its start */
} snd_output_t;

extern const snd_source_t SND_SOURCES[SRC_COUNT];
extern const snd_output_t SND_OUTPUTS[SND_COUNT];

/* A decoded source: its samples (only read by snd_build, so one decode can feed both outputs that share it, such
   as the indoor and outdoor rain), the variant of the where[] entry it was found through, and the mixer rate
   snd_prepare made it ready for (0 until then: set it so when the samples are decoded). */
typedef struct {
    dsp_buf_t pcm;
    int variant;
    int mix_rate;
} snd_input_t;

#define SND_NO_SOURCE (-10)   /* no samples for the output's source */
#define SND_UNFIT     (-11)   /* a source the recipes were not made for (snd_source_ok): final, trying again changes nothing */

/* What a source may be. The recipes were made for sources of 3 to 75 s at 22,050 to 48,000 Hz (RavenCraft's heavy
   rain loop, 75 s at 48 kHz, is the largest: 27.5 MB of samples). A changed archive can hold a WAV of up to the
   reader's 64 MB, which at 8 bits and 8 kHz would decode to 256 MB of samples, run each recipe for minutes and be
   held as an image for the session (0.14 review). So a source over 90 s, over 32 MB of samples or outside 8 to
   96 kHz is refused before any sample is decoded, and for good: it is not a lack of memory. */
#define SND_MAX_SECONDS 90
#define SND_MAX_SAMPLES (8u * 1024u * 1024u)   /* frames times channels: 32 MB as samples */
#define SND_MIN_RATE    8000
#define SND_MAX_RATE    96000
/* 1 when a source of that shape is one the recipes can take */
int snd_source_ok(unsigned int frames, int channels, int rate);
/* dsp_wav_decode behind snd_source_ok: SND_UNFIT, before a sample is decoded, for a WAV the recipes were not made
   for; otherwise what dsp_wav_decode returns. */
int snd_wav_decode(const void *data, unsigned int size, dsp_buf_t *pcm, const dsp_alloc_t *a);
/* What an Ogg Vorbis file (the gusts) says of itself, for snd_source_ok to hold before FMOD decodes a sample of it:
   NULL, with its channels, rate and length in frames, or why it is not a file whose length can be known. FMOD sizes
   the sample for a gust by what the client's vorbisfile makes of the file's first stream, and decodes that much into
   the game's own heap before the DLL sees any of it, so the length must be the one vorbisfile will find. Every page
   is read in order from the first byte to the last: each one whole, its checksum right (vorbisfile skips a page whose
   checksum is wrong), with the first page's serial number (another is another stream); the first page begins the
   stream and holds the Vorbis identification header alone; no later page begins one (a chained file, whose last page
   gives its last stream's length while FMOD sizes the sample by its first); and the page that ends the stream is the
   file's last byte. The length is that page's granule position, where vorbisfile takes it from too, less an offset it
   may find at the start, so FMOD decodes no more than that. (The third 0.14 review: the first page-like bytes found
   from the end were taken as the length, unchecked, and a second stream, or a page past the end whose checksum is
   wrong, passed a file whose decode was far longer.) Only reads the file. */
const char *snd_ogg_probe(const void *data, unsigned int size, int *channels, int *rate, unsigned int *frames);

/* The rates of the client's mixer the sounds are built for (its SoundMixRate setting picks the mixer's rate; both
   clients run 44,100 Hz). The DLL clamps what FMOD reports into this range. FSOUND_Init takes 4,000 to 65,535 Hz, so a
   mixer set past 48 kHz by hand plays each frame of a 48 kHz image once or twice, unevenly, which folds images of its
   band back down to 12.5 kHz and up, as it would 0.13's 44.1 kHz files (the third 0.14 review; the log names both
   rates): the memory the sounds take grows with the rate they are built at (all 25 take 94.8 MB at 48 kHz), so none
   is built faster. */
#define SND_MIX_MIN 8000
#define SND_MIX_MAX 48000
/* The thunder and the gusts, which the DLL plays at a random pitch, are built at the least multiple of the mixer's
   rate that is SND_ONESHOT_RATE or more (snd_oneshot_rate): the mixer's own at 44.1 kHz and over, twice it at
   22,050 Hz, four times at 11,025 Hz. The mixer draws a straight line between the frames of a pitched mono voice, and
   at a slower mixer's own rate the line dulled and imaged them (sounds.c). 0 for a mix_rate of 0 or less. */
#define SND_ONESHOT_RATE 44100
int snd_oneshot_rate(int mix_rate);
/* Makes a decoded source (in->pcm from the allocator, in->mix_rate 0) ready to build its outputs for a mixer
   running at mix_rate: held to snd_source_ok (the gusts, which FMOD decodes, come here unchecked), and RavenCraft's
   heavy loop spliced, once for both heavy outputs. DSP_OK, or a DSP_* / SND_* error; either way in->pcm is the
   caller's to free, and after an error it may already be spliced, so it is only good for freeing. */
int snd_prepare(int src, snd_input_t *in, int mix_rate, const dsp_alloc_t *a);

/* Builds one output from its prepared source: DSP_OK and a 16-bit PCM WAV in *wav at the mixer's own rate, where it
   steps one frame per frame, or for snow and sand half of it, or for the thunder and the gusts snd_oneshot_rate
   (sounds.c), from a->alloc (the caller gives it back with a->release); or a DSP_* / SND_* error and *wav = 0. A
   source snd_prepare has not made ready is refused (DSP_BADARG). The slowest, the heavy indoor loop (its compressor
   takes a log and an exp per frame at 44.1 kHz), took 0.73 s in a 32-bit x87 build on a fast desktop CPU, and all 25,
   read and built, 2.6 s with the mixer at 44.1 kHz and 4.2 s at 48 kHz; call it from the worker thread, never the
   game's. */
int snd_build(int out, const snd_input_t *src, void **wav, unsigned int *wav_size, const dsp_alloc_t *a);
/* The reason behind an error code, for the log. */
const char *snd_why(int r);

#endif
