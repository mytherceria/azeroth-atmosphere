/* Runs CinderLoad.dll's own code (dll/cinderload.c) against fake game folders, on POSIX through tests/shim:
 *   gcc -std=c99 -Wall -Wno-unused-function -Itests/shim -I../IndoorRain/dll -o /tmp/test_switch tests/test_switch.c \
 *       ../IndoorRain/dll/mpq.c ../IndoorRain/dll/inflate.c && /tmp/test_switch <scratch dir> [<client folder>]
 * Each case is a fresh folder under <scratch dir>; nothing outside it is touched. With a client folder (one holding
 * Data/patch.MPQ and Data/sound.MPQ, opened for reading only), the fire sound is also built from that client's own
 * files and played through the mock's timeline into <scratch dir>/fire-timeline.wav, for listening to, and the places
 * the fire sound and the fire bar rely on are checked against that client's WoW.exe. */
/* The client's 32-bit addresses are not real ones here: they are read from the fake image, shim_module, as if it sat
 * at the client's base, 0x400000. */
#define ClientAt(va) ((unsigned)(va) - 0x400000u <= SHIM_MODULE_SIZE - 4u ? shim_module + ((unsigned)(va) - 0x400000u) \
                                                                       : (const unsigned char *)0)
#include "../dll/cinderload.c"
#include <stdio.h>
#include <sys/stat.h>

char shim_gamedir[512];
unsigned char shim_module[SHIM_MODULE_SIZE];
int shim_threads;
DWORD shim_ticks;
int shim_screen_w = 3440, shim_screen_h = 1440;

static int fails, checks;
static void Check(int cond, const char *what) { checks++; if (!cond) { printf("FAIL: %s\n", what); fails++; } }

static const char *g_root;
static void Fresh(const char *name)
{
    snprintf(shim_gamedir, sizeof shim_gamedir, "%s/%s/", g_root, name);
    const char *dirs[] = { "", "Data", "Data/CinderLoad", "WTF", "Logs" };
    for (int i = 0; i < 5; i++) {
        char p[700];
        snprintf(p, sizeof p, "%s%s", shim_gamedir, dirs[i]);
        mkdir(p, 0755);
    }
    memset(shim_module, 0, sizeof shim_module);
    memcpy(shim_module + PATCH_RVA, kStock, sizeof kStock);
    memcpy(shim_module + BAR_FILL_RVA, kFillStock, 16);
    memcpy(shim_module + BAR_BORDER_RVA, kBorderStock, 16);
    for (int i = 0; i < FIRE_CODE_COUNT; i++)
        memcpy(shim_module + kFireCode[i].rva, kFireCode[i].bytes, (size_t)kFireCode[i].n);
    for (int i = 0; i < BAR_CODE_COUNT; i++)
        memcpy(shim_module + kBarCode[i].rva, kBarCode[i].bytes, (size_t)kBarCode[i].n);
    for (int i = 0; i < REVEAL_CODE_COUNT; i++)
        memcpy(shim_module + kRevealCode[i].rva, kRevealCode[i].bytes, (size_t)kRevealCode[i].n);
    memcpy(shim_module + WALL_LOAD_RVA, kWallLoad, sizeof kWallLoad);
    g_walls.count = 0;
    g_wallPick = 0;
    g_barsOn = 0;
    g_barShown = 0;
    shim_screen_w = 3440; shim_screen_h = 1440;
    shim_threads = 0;
}
static void Put(const char *rel, const char *text)
{
    char p[700];
    snprintf(p, sizeof p, "%s%s", shim_gamedir, rel);
    FILE *f = fopen(p, "wb");
    fputs(text, f);
    fclose(f);
}
static const char *Get(const char *rel)
{
    static char buf[16384];
    char p[700];
    snprintf(p, sizeof p, "%s%s", shim_gamedir, rel);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}
static int Is(const char *rel, const char *text) { const char *g = Get(rel); return g && !strcmp(g, text); }
static int Gone(const char *rel) { return Get(rel) == NULL; }
static int On(void) { return Same(shim_module + PATCH_RVA, kWide, sizeof kWide); }
static int BarWide(void) { return Same(shim_module + BAR_FILL_RVA, kFillWide, 16) && Same(shim_module + BAR_BORDER_RVA, kBorderWide, 16); }
static int BarStock(void) { return Same(shim_module + BAR_FILL_RVA, kFillStock, 16) && Same(shim_module + BAR_BORDER_RVA, kBorderStock, 16); }
static void Start(void) { DllMain(NULL, DLL_PROCESS_ATTACH, NULL); }
static int LogSays(const char *words) { const char *g = Get("Logs/CinderLoad.log"); return g && strstr(g, words); }

#define UW "Data/CinderLoad/LoadingScreens-21x9.MPQ"
#define HD "Data/CinderLoad/LoadingScreens-16x9.MPQ"
#define ACTIVE "Data/patch-~.MPQ"
#define MARK "Data/patch-~.cinderload"
#define OLDMARK "Data/CinderLoad/installed.txt"
#define RES(w) "SET gxWindow \"1\"\r\nSET gxResolution \"" w "\"\r\n"

/* ---- the fire sound ---- */
#include <math.h>

static int Near(double a, double b, double tol) { return fabs(a - b) <= tol; }

/* A fake fmod.dll: every call is written down, in order. */
static char g_calls[4096];
static void Called(const char *what) { strcat(g_calls, what); strcat(g_calls, " "); }
static fn_StreamCallback g_cb;
static unsigned g_mode;
static int g_rate = 8000, g_pan = 999, g_createOk = 1, g_streamTag;
static void *WINAPI FakeCreate(fn_StreamCallback cb, int len, unsigned int mode, int rate, void *user)
{
    (void)len; (void)user; Called("create"); g_cb = cb; g_mode = mode; g_rate = rate; return g_createOk ? &g_streamTag : NULL;
}
static void *WINAPI FakeGetSample(void *st) { (void)st; return NULL; }
static int WINAPI FakePlayEx(int ch, void *st, void *dsp, signed char paused) { (void)ch; (void)st; (void)dsp; Called(paused ? "start-paused" : "start"); return 3; }
static signed char WINAPI FakePriority(int ch, int pri) { (void)ch; (void)pri; Called("priority"); return 1; }
static signed char WINAPI FakeReserved(int ch, signed char r) { (void)ch; Called(r ? "reserve" : "unreserve"); return 1; }
static signed char WINAPI FakePan(int ch, int pan) { (void)ch; g_pan = pan; Called("pan"); return 1; }
static signed char WINAPI FakeVolume(int ch, int v) { (void)ch; (void)v; Called("volume"); return 1; }
static signed char WINAPI FakePaused(int ch, signed char p) { (void)ch; Called(p ? "pause" : "unpause"); return 1; }
static signed char WINAPI FakeStop(int ch) { (void)ch; Called("stop"); return 1; }
static signed char WINAPI FakeClose(void *st) { (void)st; Called("close"); return 1; }
static int WINAPI FakeRate(void) { return 8000; }
static float WINAPI FakeVersion(void) { return 3.75f; }
static int WINAPI FakeError(void) { return 0; }
static void FakeFmod(void)
{
    F.Stream_Create = FakeCreate; F.Stream_GetSample = FakeGetSample; F.Sample_SetDefaults = NULL; F.Stream_PlayEx = FakePlayEx;
    F.SetPriority = FakePriority; F.SetReserved = FakeReserved; F.SetPan = FakePan; F.SetVolume = FakeVolume;
    F.SetPaused = FakePaused; F.StopSound = FakeStop; F.Stream_Close = FakeClose; F.GetOutputRate = FakeRate;
    F.GetVersion = FakeVersion; F.GetError = FakeError;
    g_calls[0] = 0;
}
static void Client(unsigned layer, float progress, int soundUp)
{
    memcpy(shim_module + FIRE_LAYER_RVA, &layer, 4);
    memcpy(shim_module + FIRE_PROGRESS_RVA, &progress, 4);
    shim_module[FIRE_SOUNDUP_RVA] = (unsigned char)soundUp;
}
static float Published(void) { union { LONG bits; float f; } v; v.bits = g_fire.fillBits; return v.f; }

/* A loop that is a constant: what a mixer test hears is then only the levels, the pan and the fade. */
static float g_dc[64];
static void DcMix(FireMix *m, int rate, int which, float value)
{
    memset(m, 0, sizeof *m);
    FireSetRate(m, rate);
    for (int i = 0; i < 64; i++) g_dc[i] = value;
    m->loop[which] = g_dc;
    m->len[which] = 64;
}
static short g_pcm[2 * 48000];
/* frames rendered at one gate and fill; the last frame's left and right */
static void Run(FireMix *m, int gate, float fill, int frames, int *l, int *r)
{
    while (frames > 0) {
        int n = frames < 48000 ? frames : 48000;
        FireRender(m, gate, fill, g_pcm, n);
        frames -= n;
        if (l) *l = g_pcm[2 * n - 2];
        if (r) *r = g_pcm[2 * n - 1];
    }
}
static size_t Wav(unsigned char *out, int format, int channels, int bits, int rate, const short *s, int frames)
{
    unsigned data = (unsigned)(frames * channels * bits / 8);
    unsigned char *p = out;
    memcpy(p, "RIFF", 4); p += 4; unsigned v = 36 + 12 + data; memcpy(p, &v, 4); p += 4; memcpy(p, "WAVE", 4); p += 4;
    memcpy(p, "LIST", 4); p += 4; v = 4; memcpy(p, &v, 4); p += 4; memcpy(p, "INFO", 4); p += 4;   /* a chunk to skip */
    memcpy(p, "fmt ", 4); p += 4; v = 16; memcpy(p, &v, 4); p += 4;
    unsigned short h[8] = { (unsigned short)format, (unsigned short)channels, (unsigned short)(rate & 0xFFFF), (unsigned short)(rate >> 16),
                            0, 0, (unsigned short)(channels * bits / 8), (unsigned short)bits };
    unsigned bps = (unsigned)(rate * channels * bits / 8);
    h[4] = (unsigned short)(bps & 0xFFFF); h[5] = (unsigned short)(bps >> 16);
    memcpy(p, h, 16); p += 16;
    memcpy(p, "data", 4); p += 4; memcpy(p, &data, 4); p += 4;
    memcpy(p, s, data); p += data;
    return (size_t)(p - out);
}

static void FireTests(void)
{
    float l, r;
    int il, ir;

    /* the pan curve: equal power, 80/20 by loudness at both ends */
    Check(Near(FirePan(0), 0.295, 1e-6) && Near(FirePan(1), 0.705, 1e-6) && Near(FirePan(0.5f), 0.5, 1e-6) &&
          Near(FirePan(-1), 0.295, 1e-6) && Near(FirePan(2), 0.705, 1e-6), "pan: 0.295 at an empty bar, 0.705 at a full one");
    int power = 1;
    for (int i = 0; i <= 20; i++) {
        FireGains(i / 20.0f, &l, &r);
        power &= Near(l * l + r * r, 1, 1e-5);
    }
    Check(power, "pan: equal power, left^2 + right^2 = 1 all the way along");
    FireGains(0, &l, &r);
    int ends = Near(l * l, 0.80, 0.002);
    FireGains(1, &l, &r);
    ends &= Near(r * r, 0.80, 0.002);
    FireGains(0.5f, &l, &r);
    Check(ends && Near(l, r, 1e-6), "pan: 80% of the loudness left at the start, 80% right at the end, even halfway");
    double worst = 0;
    for (int i = 0; i <= 1000; i++) {
        double x = i * (M_PI / 2) / 1000;
        worst = fmax(worst, fmax(fabs(FireSin((float)x) - sin(x)), fabs(FireCos((float)x) - cos(x))));
    }
    Check(worst < 1e-6 && Near(FireExp(-0.001), exp(-0.001), 1e-15) && Near(FireExp(-3), exp(-3), 1e-12),
          "the arithmetic without a C runtime: sin, cos and exp as the library's");

    /* the glide: a jump in the bar moves the sound over about a third of a second, never at once */
    FireMix m;
    DcMix(&m, 1000, 0, 0.1f);
    Run(&m, 1, 0, 10, NULL, NULL);
    Run(&m, 1, 1, 1, NULL, NULL);
    float first = m.front;
    Run(&m, 1, 1, 299, NULL, NULL);
    float at300 = m.front;
    Run(&m, 1, 1, 50, NULL, NULL);
    Check(first < 0.005f && Near(at300, 1 - exp(-1), 0.01) && Near(m.front, 1 - exp(-0.35 / 0.3), 0.01),
          "glide: one frame after a jump the front has hardly moved; 63% at 0.3 s, 69% at 0.35 s");
    DcMix(&m, 1000, 0, 0.1f);
    Run(&m, 1, 0, 1000, &il, &ir);
    double startRight = (double)ir * ir / ((double)il * il + (double)ir * ir);
    Run(&m, 1, 1, 3000, &il, &ir);
    double endRight = (double)ir * ir / ((double)il * il + (double)ir * ir);
    Check(Near(startRight, 0.2, 0.002) && Near(endRight, 0.8, 0.002), "glide: the sound itself goes from 20% right to 80% right");
    DcMix(&m, 1000, 0, 0.1f);
    Run(&m, 1, 0.7f, 1, NULL, NULL);
    Check(Near(m.front, 0.7, 1e-6), "glide: a screen starts where its bar is, with no sweep across");

    /* the crackle: loud while the fill moves, soft while it stalls */
    Check(Near(FireBedLevel(0), 0.85, 1e-6) && Near(FireBedLevel(1 / 3.0f), 1.15, 1e-5) && Near(FireBedLevel(1), 1.15, 1e-6),
          "bed: 0.85 at an empty bar, 1.15 from a third on");
    DcMix(&m, 1000, 4, 0.1f);                               /* UndeadFireLarge alone: the crackle */
    Run(&m, 1, 0, 500, NULL, NULL);
    for (int step = 0; step < 100; step++) Run(&m, 1, step * 0.005f, 20, NULL, NULL);   /* 0.25 a second, in 20 ms steps */
    float moving = FireCrackleLevel(m.speed);
    Run(&m, 1, 0.5f, 1, &il, &ir);
    double loudMoving = sqrt((double)il * il + (double)ir * ir);
    Run(&m, 1, 0.5f, 2000, &il, &ir);
    float stalled = FireCrackleLevel(m.speed);
    double loudStalled = sqrt((double)il * il + (double)ir * ir);
    Check(Near(moving, 1, 0.1) && stalled < 0.02f && loudMoving > 20 * loudStalled,
          "crackle: at a quarter of the bar a second it is at 1; two seconds of stall and it has all but gone");
    DcMix(&m, 1000, 4, 0.1f);
    Run(&m, 1, 0, 100, NULL, NULL);
    float peak = 0;
    for (int i = 0; i < 2000; i++) {
        Run(&m, 1, 1, 1, NULL, NULL);
        if (m.speed > peak) peak = m.speed;
    }
    Check(peak <= FIRE_MOVE_CAP + 1e-6f && FireCrackleLevel(10) == FIRE_MOVE_MAX, "crackle: a leap of the whole bar is held to its cap");

    /* the fades */
    DcMix(&m, 1000, 0, 0.1f);
    Run(&m, 1, 0.5f, 200, NULL, NULL);
    float half = m.env;
    Run(&m, 1, 0.5f, 200, NULL, NULL);
    float full = m.env;
    Run(&m, 0, 0.5f, 200, NULL, NULL);
    float out = m.env;
    int going = FireRender(&m, 0, 0.5f, g_pcm, 199);
    int quiet = !FireRender(&m, 0, 0.5f, g_pcm, 10) && g_pcm[18] == 0 && g_pcm[19] == 0;
    Check(Near(half, 0.5, 0.01) && Near(full, 1, 1e-6) && Near(out, 0.5, 0.01) && going && quiet,
          "fades: in over 0.4 s as a screen comes up, out over 0.4 s when it goes, then silence");

    /* the fill the worker passes on */
    FireFill ff;
    FireFillStart(&ff, 0);
    float a = FireFillRead(&ff, 0.5f), b = FireFillRead(&ff, 0), c = FireFillRead(&ff, 0.2f), d = FireFillRead(&ff, 0.55f);
    Check(a == 0.5f && b == 0.5f && c == 0.5f && d == 0.55f, "fill: the client's passing 0 (and part sums) never reach the sound");
    FireFillRead(&ff, 0.05f); FireFillRead(&ff, 0.05f);
    float e = FireFillRead(&ff, 0.05f);
    Check(e == 0.05f, "fill: a fall that lasts three reads is a new screen in the same window, and is taken");
    Check(FireFillRead(&ff, NAN) == 0.05f && FireFillRead(&ff, 1.7f) == 1, "fill: NaN counts as 0, over 1 as 1");

    /* the loops, made as the mock made them */
    static float src[4000], loop[9000];
    for (int i = 0; i < 1000; i++) src[i] = 0.5f;
    unsigned period = FireMakeLoop(src, 1000, 1000, 1000, 0, loop);
    int flat = period == 1000 - 120;
    for (unsigned i = 0; i < period; i++) flat &= Near(loop[i], 0.5, 1e-6);
    Check(flat, "loop: n - 0.12 s long, and a steady sound stays steady through the seam");
    for (int i = 0; i < 1000; i++) src[i] = (float)i;
    period = FireMakeLoop(src, 1000, 1000, 1000, 300, loop);
    Check(period == 880 && loop[120] == 420 && loop[500] == 800 && loop[700] == 0 && loop[0] == 180,
          "loop: started 300 frames in, as the mock's roll, with the seam laid over that start");
    period = FireMakeLoop(src, 1000, 1000, 2000, 0, loop);
    int line = period == 2000 - 240;
    for (unsigned j = 240; j < period; j++) line &= Near(loop[j], j / 2.0, 1e-3);
    Check(line, "loop: 1 kHz to 2 kHz by straight lines between frames");
    Check(FireResampledLength(66152, 22050, 44100) == 132304 && FireResampledLength(66152, 22050, 48000) == 144004,
          "loop: as many frames at the mixer's rate as the mock's resampler makes");
    Check(FireMakeLoop(src, 200, 1000, 1000, 0, loop) == 0, "loop: one too short to cross-fade is refused");

    /* the source files */
    static unsigned char wav[20000];
    static short pcm[4000];
    for (int i = 0; i < 4000; i++) pcm[i] = (short)(i * 8);
    int rate = 0, ch = 0;
    unsigned frames = 0;
    const unsigned char *data = NULL;
    size_t n = Wav(wav, 1, 1, 16, 22050, pcm, 2000);
    int ok = FireWavInfo(wav, (unsigned)n, &rate, &ch, &data, &frames) && rate == 22050 && ch == 1 && frames == 2000;
    FireWavRead(data, ch, frames, src);
    Check(ok && Near(src[1000], 8000 / 32768.0, 1e-7), "wav: 16-bit mono PCM, as the client's fire loops are");
    n = Wav(wav, 1, 2, 16, 44100, pcm, 1000);
    ok = FireWavInfo(wav, (unsigned)n, &rate, &ch, &data, &frames) && ch == 2 && frames == 1000;
    FireWavRead(data, ch, frames, src);
    Check(ok && Near(src[10], (160 + 168) / 2 / 32768.0, 1e-7), "wav: stereo is taken as the average of its sides");
    Check(!FireWavInfo(wav, (unsigned)Wav(wav, 1, 1, 8, 22050, pcm, 2000), &rate, &ch, &data, &frames) &&
          !FireWavInfo(wav, (unsigned)Wav(wav, 0x11, 1, 16, 22050, pcm, 2000), &rate, &ch, &data, &frames) &&
          !FireWavInfo(wav, (unsigned)Wav(wav, 1, 1, 16, 22050, pcm, 2000) - 2, &rate, &ch, &data, &frames) &&
          !FireWavInfo((const unsigned char *)"RIFF\4\0\0\0WAVE", 12, &rate, &ch, &data, &frames),
          "wav: 8-bit, ADPCM, a cut-off file and one with no sound in it are refused");

    /* the places the sound relies on: all must hold their bytes, or no sound at all */
    Fresh("fire-on");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(shim_threads == 1 && LogSays("fire sound on") && On() && BarWide(), "the client's bytes all there: the fire's thread starts");
    int silent = 1;
    for (int i = 0; i < FIRE_CODE_COUNT; i++) {
        Fresh("fire-other");
        Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
        shim_module[kFireCode[i].rva + (unsigned)kFireCode[i].n - 1] ^= 0x40;
        Start();
        char words[80];
        snprintf(words, sizeof words, "other bytes at RVA 0x%lX (%s)", (unsigned long)kFireCode[i].rva, kFireCode[i].what);
        silent &= shim_threads == 0 && LogSays(words) && On() && BarWide();
    }
    Check(silent, "any one of the eight places changed: no thread, no sound, the log names it; screens and bar unchanged");

    /* the stream's life, against a fake fmod.dll */
    Fresh("fire-stream");
    GameDir();                                              /* the log goes to this folder */
    FakeFmod();
    memset(&g_fire, 0, sizeof g_fire);
    g_fire.base = shim_module;
    g_fire.channel = -1;
    DcMix(&g_fire.mix, 8000, 0, 0.1f);
    g_fire.built = 1;
    shim_ticks = 1000;
    Client(0x1234, 0.1f, 0);
    FireStep(shim_ticks);
    Check(!g_calls[0] && !g_fire.stream, "sound engine down (sound off in the game): a loading screen gets no FMOD call");
    Client(0, 0, 1);
    FireStep(shim_ticks);
    Check(!g_calls[0], "no loading screen: no FMOD call");
    Client(0x1234, 0.1f, 1);
    FireStep(shim_ticks);
    Check(!strcmp(g_calls, "create start-paused priority reserve pan volume unpause ") && g_pan == FSOUND_STEREOPAN &&
          g_mode == (FSOUND_16BITS | FSOUND_SIGNED | FSOUND_STEREO | FSOUND_2D) && g_rate == 8000 && g_fire.gate == 1 &&
          Published() == 0.1f, "a loading screen comes up: one stereo stream, our own pan, started silent and let go");
    g_calls[0] = 0;
    Client(0x1234, 0, 1); FireStep(shim_ticks += 20);
    float held = Published();
    Client(0x1234, 0.6f, 1); FireStep(shim_ticks += 20);
    Check(held == 0.1f && Published() == 0.6f && !g_calls[0], "while it shows: the fill passed on, a passing 0 ignored, no FMOD calls");
    static short buf[2 * FIRE_BLOCK_FRAMES];
    signed char more = g_cb(&g_streamTag, buf, (int)sizeof buf, NULL);
    Check(more == 1 && buf[2 * FIRE_BLOCK_FRAMES - 2] != 0 && !g_fire.quiet, "FMOD asks for sound: it gets the fire, fading in");
    Client(0, 0.6f, 1); FireStep(shim_ticks += 20);
    Check(g_fire.gate == 0 && g_fire.stream && !g_calls[0] && LogSays("the fire burned for") && LogSays("the bar reached 60%"),
          "the screen goes: the fire fades out, and the log says how long it burned");
    for (int i = 0; i < 5; i++) g_cb(&g_streamTag, buf, (int)sizeof buf, NULL);
    FireStep(shim_ticks += 20);
    Check(!strcmp(g_calls, "unreserve stop close ") && !g_fire.stream && g_fire.quiet, "faded out: the stream closed, the channel given back");
    g_calls[0] = 0;
    Client(0x1234, 0, 1); FireStep(shim_ticks += 20);
    Client(0, 1, 1); FireStep(shim_ticks += 20);
    FireStep(shim_ticks += 1000);
    Check(!strstr(g_calls, "close"), "a fade FMOD has not played yet: the stream is left to finish");
    FireStep(shim_ticks += 600);
    Check(strstr(g_calls, "close") && !g_fire.stream, "but closed after 1.5 s at most");
    g_calls[0] = 0;
    Client(0x1234, 0.2f, 1); FireStep(shim_ticks += 20);
    Client(0x1234, 0.2f, 0); FireStep(shim_ticks += 20);
    Client(0, 0.2f, 0); FireStep(shim_ticks += 2000);
    Check(!strcmp(g_calls, "create start-paused priority reserve pan volume unpause ") && !g_fire.stream &&
          LogSays("closed its sound engine"), "the game closes its sound engine mid-screen: the stream is forgotten, never called");
    g_calls[0] = 0;
    g_createOk = 0;
    Client(0x1234, 0.2f, 1); FireStep(shim_ticks += 20);
    Client(0x1234, 0.4f, 1); FireStep(shim_ticks += 20);
    Check(!strcmp(g_calls, "create ") && !g_fire.stream && LogSays("would not make the stream"),
          "the engine will not make the stream: logged, silent for that screen");
    g_createOk = 1;
    g_calls[0] = 0;
    Client(0, 0.4f, 1); FireStep(shim_ticks += 20);
    Client(0x1234, 0, 1); FireStep(shim_ticks += 20);
    Check(strstr(g_calls, "create start-paused") && g_fire.stream, "and tried again at the next screen");

    Fresh("fire-no-files");
    GameDir();                                              /* the log goes to this folder */
    FakeFmod();
    memset(&g_fire, 0, sizeof g_fire);
    g_fire.base = shim_module;
    g_fire.channel = -1;
    Client(0x1234, 0.1f, 1);
    FireStep(shim_ticks += 20);
    Client(0x1234, 0.2f, 1);
    FireStep(shim_ticks += 20);
    Check(!g_calls[0] && g_fire.built == -1 && LogSays("none of its 6 loops"),
          "the client's fire sounds are not to be had: logged once, no FMOD call, the screens as ever");
}

/* ---- the fire bar ---- */

/* A small MPQ (format 0, files stored as they are), written the way Storm lays one out, for the DLL's own reader. */
static unsigned g_crypt[0x500];
static void CryptInit(void)
{
    unsigned seed = 0x00100001u;
    for (unsigned i = 0; i < 0x100; i++)
        for (unsigned j = 0; j < 5; j++) {
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            unsigned hi = (seed & 0xFFFFu) << 16;
            seed = (seed * 125u + 3u) % 0x2AAAABu;
            g_crypt[i + j * 0x100] = hi | (seed & 0xFFFFu);
        }
}
static unsigned HashName(const char *s, unsigned type)
{
    unsigned s1 = 0x7FED7FEDu, s2 = 0xEEEEEEEEu;
    for (; *s; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        s1 = g_crypt[(type << 8) + ch] ^ (s1 + s2);
        s2 = ch + s1 + s2 + (s2 << 5) + 3u;
    }
    return s1;
}
static void Encrypt(unsigned *d, unsigned n, unsigned key)
{
    unsigned seed = 0xEEEEEEEEu;
    for (unsigned i = 0; i < n; i++) {
        seed += g_crypt[0x400 + (key & 0xFFu)];
        unsigned plain = d[i];
        d[i] = plain ^ (key + seed);
        key = ((~key << 21) + 0x11111111u) | (key >> 11);
        seed = plain + seed + (seed << 5) + 3u;
    }
}
#define MPQ_FILES 16
static void MakeMpq(const char *rel, int n, const char *const *names, const char *const *data, const unsigned *lens)
{
    static unsigned char out[1 << 20];
    unsigned hash[64 * 4], block[MPQ_FILES * 4], at = 32;
    memset(hash, 0xFF, sizeof hash);
    for (int i = 0; i < n; i++) {
        unsigned len = lens ? lens[i] : (unsigned)strlen(data[i]);
        memcpy(out + at, data[i], len);
        block[4 * i] = at; block[4 * i + 1] = len; block[4 * i + 2] = len; block[4 * i + 3] = 0x80000000u;
        at += len;
        unsigned slot = HashName(names[i], 0) & 63u;
        while (hash[4 * slot + 3] != 0xFFFFFFFFu) slot = (slot + 1) & 63u;
        hash[4 * slot] = HashName(names[i], 1); hash[4 * slot + 1] = HashName(names[i], 2);
        hash[4 * slot + 2] = 0; hash[4 * slot + 3] = (unsigned)i;
    }
    Encrypt(hash, 64 * 4, HashName("(hash table)", 3));
    Encrypt(block, (unsigned)n * 4, HashName("(block table)", 3));
    unsigned hashOff = at, blockOff = at + sizeof hash, size = blockOff + (unsigned)n * 16;
    memcpy(out + hashOff, hash, sizeof hash);
    memcpy(out + blockOff, block, (size_t)n * 16);
    unsigned head[8] = { 0x1A51504Du, 32, size, 3u << 16, hashOff, blockOff, 64, (unsigned)n };   /* "MPQ\x1A", v0, 4 KB */
    memcpy(out, head, 32);
    char p[700];
    snprintf(p, sizeof p, "%s%s", shim_gamedir, rel);
    FILE *f = fopen(p, "wb");
    fwrite(out, 1, size, f);
    fclose(f);
}

#define TEX "Interface\\Glues\\LoadingBar\\"
static const char kBars[] =
    "# CinderLoad: each loading screen's fire\r\n"
    "\r\n"
    "loadscreendeadmines fel\r\n"
    "LoadScreenMoltenCore.blp   Aqua   # its lava is aqua in this test\r\n"
    "loadscreenstrat orange\r\n"
    "loading fel\r\n"
    "default arcane\r\n";
/* Our pack, as make_pack.py builds it with fire bars: fel, aqua and arcane carried, orange only named. */
static void PackWithBars(const char *bars)
{
    const char *names[] = { "Interface\\Glues\\LoadingScreens\\LoadScreenDeadmines.blp", "CinderLoad\\bars.txt",
                            TEX "Loading-BarFill-fel.blp", TEX "Loading-BarBorder-fel.blp",
                            TEX "Loading-BarFill-aqua.blp", TEX "Loading-BarBorder-aqua.blp",
                            TEX "Loading-BarFill-arcane.blp", TEX "Loading-BarBorder-arcane.blp" };
    const char *data[] = { "BLP2screen", bars, "BLP2f", "BLP2b", "BLP2f", "BLP2b", "BLP2f", "BLP2b" };
    MakeMpq(UW, bars ? 8 : 1, names, data, NULL);
    Put("WTF/Config.wtf", RES("5120x2160"));
}

/* The client's tables, as the game has them while a screen begins: Map.dbc's and LoadingScreens.dbc's records by id. */
#define VA(rva) (0x400000u + (unsigned)(rva))
static void Poke(unsigned va, unsigned v) { memcpy(shim_module + va - 0x400000u, &v, 4); }
static void PokeText(unsigned va, const char *s) { memcpy(shim_module + va - 0x400000u, s, strlen(s) + 1); }
/* Written from the facts read out of WoW.exe, not from the DLL's names for them, so a slip in one shows. */
static void FakeTables(void)
{
    Poke(0xC0DAA8, VA(0x860000)); Poke(0xC0DAAC, 600);   /* Map.dbc's records by id; the largest id */
    Poke(0xC0DB0C, VA(0x861000)); Poke(0xC0DB10, 50);    /* LoadingScreens.dbc's */
    static const struct { int map, screen; const char *file; } k[] = {
        { 36, 5, "Interface\\Glues\\LoadingScreens\\LoadScreenDeadmines.blp" },
        { 409, 6, "Interface\\Glues\\LoadingScreens\\LoadScreenMoltenCore.blp" },
        { 0, 7, "Interface\\Glues\\LoadingScreens\\LoadScreenEasternKingdom.blp" },
        { 329, 8, "Interface\\Glues\\LoadingScreens\\LoadScreenStrat.blp" },
        { 3, 99, NULL },                                   /* a LoadingScreenID past the table's end */
        { 4, 9, NULL },                                    /* a LoadingScreenID with no record */
        { 5, -1, NULL },                                   /* a negative one */
        { 6, 10, "Interface\\Glues\\LoadingScreens\\LoadScreenWithANameMuchLongerThanAnyBarsTxtCouldList.blp" },
        { 600, 50, "Interface\\Glues\\LoadingScreens\\LoadScreenMoltenCore.blp" },   /* the largest ids: in range */
        { 601, 6, NULL },                                  /* one past Map.dbc's largest id, a record there all the same */
        { 7, 51, "Interface\\Glues\\LoadingScreens\\LoadScreenMoltenCore.blp" },     /* one past LoadingScreens' */
    };
    for (int i = 0; i < (int)(sizeof k / sizeof k[0]); i++) {
        unsigned map = VA(0x862000 + 0x100 * i), rec = VA(0x864000 + 0x20 * i), file = VA(0x866000 + 0x100 * i);
        Poke(VA(0x860000) + 4u * (unsigned)k[i].map, map);
        Poke(map + 0x98, (unsigned)k[i].screen);           /* the Map record's LoadingScreenID */
        if (!k[i].file) continue;
        Poke(VA(0x861000) + 4u * (unsigned)k[i].screen, rec);
        Poke(rec + 8, file);                               /* the LoadingScreens record's file name */
        PokeText(file, k[i].file);
    }
}
static const char *GameFill(void) { return (const char *)shim_module + BAR_FILL_PATH_RVA; }
static const char *GameBorder(void) { return (const char *)shim_module + BAR_BORDER_PATH_RVA; }
static const char *ForMap(int map) { Poke(0x82F00C, (unsigned)map); return BarPath(GameFill()); }
static int Ours(const char *p) { return p >= (const char *)&g_bars && p < (const char *)(&g_bars + 1); }
static int Call(void) { return Rd32(shim_module + BAR_CALL_RVA + 1); }
static int CallStock(void) { return Same(shim_module + BAR_LOAD_RVA, kBarLoad, (int)sizeof kBarLoad); }

static void FireBarTests(void)
{
    CryptInit();

    /* on: the call sent to the stub, every other byte as it was */
    Fresh("firebar-on");
    PackWithBars(kBars);
    Start();
    unsigned rel = (unsigned)((unsigned long)BarStub - (unsigned long)(shim_module + 0x6891 + 5));
    Check(LogSays("fire bar on") && (unsigned)Call() == rel && shim_module[0x6891] == 0xE8 &&
          Same(shim_module + BAR_LOAD_RVA, kBarLoad, BAR_REL_AT) && shim_module[0x6896] == 0x89,
          "fire bar on: the rel32 at RVA 0x6891 is stub - (RVA 0x6891 + 5 + base), the E8 and every other byte kept");
    Check(g_barLoader == shim_module + 0x49D90, "the stub goes on to the game's loader, VA 0x449D90");
    Check(On() && BarWide() && shim_threads == 1 && LogSays("fire sound on"), "the screens, the bar's size and the fire sound as ever");
    Check(LogSays("bars.txt names 4 screens, and a default for the rest, 0 lines skipped") &&
          LogSays("families in the pack: fel, aqua, arcane; named but not in it (their screens keep the pack's orange): orange"),
          "the log says what bars.txt named and which families the pack carries");

    /* which bar each screen gets, from the client's own tables */
    FakeTables();
    const char *p = ForMap(36);
    Check(Ours(p) && !strcmp(p, TEX "Loading-BarFill-fel"), "Deadmines (map 36): fel");
    Check(!strcmp(BarPath(GameBorder()), TEX "Loading-BarBorder-fel"), "and its border: fel too");
    Check(!strcmp(ForMap(409), TEX "Loading-BarFill-aqua"), "Molten Core, named LoadScreenMoltenCore.blp in bars.txt: aqua");
    char shown[BAR_SHOWN_MAX];
    BarShownSince(shown, (int)sizeof shown);
    Check(!strcmp(shown, ", its fire aqua") && g_barShown == 0, "the worker's line says which fire the screen had, once");
    Check(!strcmp(ForMap(0), TEX "Loading-BarFill-arcane"), "a continent bars.txt does not list: the default family");
    Check(ForMap(329) == GameFill(), "Stratholme, orange, whose files the pack does not carry: the game's own path back");
    BarShownSince(shown, (int)sizeof shown);
    Check(!strcmp(shown, ", its fire the pack's orange"), "and the worker's line says so");
    /* the longest line there can be (the pack's orange and wallpaper 64), and a buffer too small: nothing written past
     * the end (3 Oct 2026: a 48-byte buffer was overrun on a screen with a wallpaper) */
    unsigned char guard[BAR_SHOWN_MAX + 16];
    int keepCount = g_walls.count; LONG keepPick = g_wallPick;
    g_walls.count = 64; g_wallPick = 64;
    lstrcpyA(g_walls.path[63], "Interface\\Glues\\LoadingScreens\\CinderWall64.blp");
    memset(guard, 0xAA, sizeof guard);
    InterlockedExchange(&g_barShown, -1);
    BarShownSince((char *)guard, BAR_SHOWN_MAX);
    int fits = !strcmp((char *)guard, ", its fire the pack's orange, its wallpaper CinderWall64.blp");
    for (int i = BAR_SHOWN_MAX; i < (int)sizeof guard; i++) fits &= guard[i] == 0xAA;
    memset(guard, 0xAA, sizeof guard);
    InterlockedExchange(&g_barShown, -1);
    BarShownSince((char *)guard, 20);
    int cut = strlen((char *)guard) == 19 && guard[19] == 0;
    for (int i = 20; i < (int)sizeof guard; i++) cut &= guard[i] == 0xAA;
    g_walls.count = keepCount; g_wallPick = keepPick;
    Check(fits && cut, "the worker's line: the longest one fits, and a small buffer is cut short, never overrun");
    Check(!strcmp(ForMap(-1), TEX "Loading-BarFill-fel"), "map -1 (no screen of its own): the default screen, 'loading'");
    Check(!strcmp(ForMap(601), TEX "Loading-BarFill-fel") && !strcmp(ForMap(-5), TEX "Loading-BarFill-fel"),
          "a map id past either end of Map.dbc: the default screen");
    Check(!strcmp(ForMap(2), TEX "Loading-BarFill-fel"), "a map with no Map record: the default screen");
    Check(!strcmp(ForMap(3), TEX "Loading-BarFill-fel") && !strcmp(ForMap(4), TEX "Loading-BarFill-fel") &&
          !strcmp(ForMap(5), TEX "Loading-BarFill-fel"), "a LoadingScreenID past the end, negative, or with no record: the default screen");
    Check(!strcmp(ForMap(6), TEX "Loading-BarFill-arcane"), "a screen whose name is too long to be listed: the default family");
    Check(!strcmp(ForMap(600), TEX "Loading-BarFill-aqua"), "the largest map id and LoadingScreenID: in range, as the game's jg");
    Check(!strcmp(ForMap(601), TEX "Loading-BarFill-fel") && !strcmp(ForMap(7), TEX "Loading-BarFill-fel"),
          "one past the largest of either: the default screen, whatever lies there");
    Check(Rd32(kBarMapSet + 5) == BAR_MAP_VA && Rd32(kBarMapSet2 + 1) == BAR_MAP_VA && Rd32(kBarPick + 1) == BAR_MAP_VA &&
          Rd32(kBarPickWalk + 4) == BAR_MAPS_MAX_VA && Rd32(kBarPickWalk + 12) == BAR_MAPS_VA &&
          Rd32(kBarPickWalk + 25) == BAR_MAP_SCREEN && Rd32(kBarPickWalk + 35) == BAR_SCREENS_MAX_VA &&
          Rd32(kBarPickWalk + 43) == BAR_SCREENS_VA && kBarPickWalk[56] == BAR_SCREEN_FILE && Rd32(kBarLoad + BAR_REL_AT) == 0x434FA &&
          BAR_CALL_RVA + 5 + 0x434FA == 0x49D90, "every address the lookup reads is the one in the verified code it mirrors");
    Poke(0xC0DAA8, 0x100);
    Check(!strcmp(ForMap(36), TEX "Loading-BarFill-fel"), "Map.dbc's table unreadable: the default screen, nothing read past it");
    FakeTables();
    Poke(VA(0x864000) + 8, 0x200);
    Check(!strcmp(ForMap(36), TEX "Loading-BarFill-fel"), "a file name the game could not read either: the default screen");
    FakeTables();
    Poke(0x82F00C, 36);
    static const char spark[] = "Interface\\Glues\\LoadingBar\\Loading-BarSpark";
    Check(BarPath(spark) == spark && BarPath(NULL) == NULL && BarPath(TEX "Loading-BarFill-fel") != NULL &&
          !strcmp(BarPath(TEX "Loading-BarFill-fel"), TEX "Loading-BarFill-fel"),
          "a path that is neither of the bar's two: returned as it came");
    Check(!strcmp(BarPath("interface/glues/loadingbar/LOADING-BARFILL"), TEX "Loading-BarFill-fel"),
          "the bar's path in other case or slashes: still the bar's");
    g_barsOn = 0;
    Check(ForMap(36) == GameFill(), "with the redirect off, the game's own path back");
    g_barsOn = 1;

    Start();
    Check(LogSays("fire bar already on") && (unsigned)Call() == rel, "a second start in the same process: already on, left as it is");

    /* any one place changed: no redirect, the log names it and what it found */
    int stock = 1;
    for (int i = 0; i < BAR_CODE_COUNT; i++) {
        Fresh("firebar-other");
        PackWithBars(kBars);
        shim_module[kBarCode[i].rva + (unsigned)kBarCode[i].n - 1] ^= 0x40;
        Start();
        char words[200], there[200];
        snprintf(words, sizeof words, "fire bar off: other bytes at RVA 0x%lX (%s)", (unsigned long)kBarCode[i].rva, kBarCode[i].what);
        int len = snprintf(there, sizeof there, "there:");
        for (int k = 0; k < 8 && k < kBarCode[i].n; k++) len += snprintf(there + len, sizeof there - (size_t)len, " %02X", shim_module[kBarCode[i].rva + (unsigned)k]);
        int callKept = kBarCode[i].rva == BAR_LOAD_RVA ? Same(shim_module + BAR_LOAD_RVA, kBarLoad, (int)sizeof kBarLoad - 1)
                                                       : CallStock();
        stock &= callKept && !g_barsOn && LogSays(words) && LogSays(there) && On() && BarWide() && shim_threads == 1 &&
                 LogSays("fire sound on");
        if (!(callKept && LogSays(words))) printf("  (place %d)\n", i);
    }
    Check(stock, "any one of the seven places changed: the call left alone, the log names it and its bytes; screens, bar, sound as ever");
    Fresh("firebar-other-rel");
    PackWithBars(kBars);
    shim_module[BAR_CALL_RVA + 2] = 0x35;
    Start();
    Check(shim_module[BAR_CALL_RVA + 2] == 0x35 && Rd32(shim_module + BAR_CALL_RVA + 1) == 0x000435FA &&
          LogSays("RVA 0x6863 (the bar's textures being loaded)"), "the call going somewhere else already: left alone");

    /* never without our screens and the switch */
    Fresh("firebar-no-screens");
    Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(CallStock() && !LogSays("fire bar"), "no loading screens of ours: the call never touched");
    Fresh("firebar-other-exe");
    PackWithBars(kBars);
    shim_module[PATCH_RVA + 1] = 0x11;
    Start();
    Check(CallStock() && !On() && !LogSays("fire bar"), "a client the switch is not for: the call never touched");
    Fresh("firebar-foreign");
    PackWithBars(kBars);
    Put(ACTIVE, "FOREIGN");
    Start();
    Check(CallStock() && !LogSays("fire bar"), "someone else's patch-~.MPQ: the call never touched");

    /* bars.txt missing, broken or with nothing the pack carries: no redirect, logged */
    Fresh("firebar-no-bars");
    PackWithBars(NULL);
    Start();
    Check(CallStock() && On() && BarWide() && shim_threads == 1 && LogSays("Data\\patch-~.MPQ holds no CinderLoad\\bars.txt"),
          "a pack with no bars.txt: no redirect, logged, all else as ever");
    Fresh("firebar-not-mpq");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(CallStock() && On() && LogSays("could not be read from Data\\patch-~.MPQ (malformed archive header"), "a pack the reader cannot open: no redirect");
    Fresh("firebar-garbage");
    PackWithBars("\x01\x02\x03 \x7F\xFF\xFE\nonly-one-word\n\n");
    Start();
    Check(CallStock() && LogSays("2 lines skipped") && LogSays("no family bars.txt names"), "a bars.txt of garbage: no redirect");
    Fresh("firebar-no-files");
    PackWithBars("loadscreendeadmines orange\ndefault green\n");
    Start();
    Check(CallStock() && LogSays("families in the pack: none") && LogSays("no family bars.txt names"),
          "families whose textures are not in the pack: no redirect");
    Fresh("firebar-too-big");
    {
        static char big[BAR_TEXT_MAX + 100];
        memset(big, '#', sizeof big - 1);
        PackWithBars(big);
    }
    Start();
    Check(CallStock() && LogSays("is over 64 KB"), "a bars.txt over 64 KB: refused, no redirect");
    Fresh("firebar-half");
    {
        const char *names[] = { "CinderLoad\\bars.txt", TEX "Loading-BarFill-fel.blp", TEX "Loading-BarFill-aqua.blp",
                                TEX "Loading-BarBorder-aqua.blp" };
        const char *data[] = { "loadscreendeadmines fel\ndefault aqua\n", "BLP2", "BLP2", "BLP2" };
        MakeMpq(UW, 4, names, data, NULL);
        Put("WTF/Config.wtf", RES("5120x2160"));
    }
    Start();
    FakeTables();
    Check(LogSays("fire bar on") && LogSays("in the pack: aqua; named but not in it (their screens keep the pack's orange): fel") &&
          ForMap(36) == GameFill() && BarPath(GameBorder()) == GameBorder() && !strcmp(ForMap(0), TEX "Loading-BarFill-aqua"),
          "a family with its fill but no border: never returned, its screens keep the pack's orange");

    /* bars.txt, read tolerantly */
    static BarTable t;
    BarParse(&t, kBars, (unsigned)strlen(kBars));
    Check(t.screens == 4 && t.families == 4 && t.skipped == 0 && t.fallback == 3 && !strcmp(t.screen[1].name, "loadscreenmoltencore") &&
          !strcmp(t.family[t.screen[1].family].name, "aqua"), "bars.txt: comments, blank lines, CRLF, case and a .blp ending");
    static const char made[] =                       /* make_pack.py's own output, as it writes bars.txt */
        "# CinderLoad: the fire of each loading screen's bar, \"<screen> <family>\"; \"default\" is every screen\n"
        "# not listed. A family whose two textures are not in this archive keeps the orange bar. (make_pack.py)\n"
        "loading orange\nloadscreendeadmines fel\nloadscreenmoltencore aqua\ndefault orange\n";
    BarParse(&t, made, (unsigned)strlen(made));
    Check(t.screens == 3 && t.families == 3 && t.skipped == 0 && t.fallback == 0, "bars.txt as make_pack.py writes it");
    static const char odd[] = "loadscreena fel\n"
                              "loadscreenb fel-green\n"                       /* not letters and digits */
                              "loadscreenc fel green\n"                       /* three words */
                              "loadscreend\n"                                 /* one */
                              "loadscreene sixteencharacters\n"               /* a family name over 15 */
                              "loadscreenaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa fel\n"   /* a name over 47 */
                              "loadscreenf\tAQUA\n"
                              "loadscreena aqua\n"                            /* a later line wins */
                              "loadscreeng fel";                              /* no line end at the end */
    BarParse(&t, odd, (unsigned)strlen(odd));
    Check(t.screens == 3 && t.skipped == 5 && t.fallback == -1 && !strcmp(t.family[t.screen[0].family].name, "aqua") &&
          !strcmp(t.screen[2].name, "loadscreeng"), "bars.txt: bad lines skipped and counted, a later line wins, no default");
    static char longLine[600];
    snprintf(longLine, sizeof longLine, "loadscreena fel # %0*d\ndefault fel\n", 300, 0);
    BarParse(&t, longLine, (unsigned)strlen(longLine));
    Check(t.screens == 0 && t.skipped == 1 && t.fallback == 0, "bars.txt: a line over 200 characters skipped whole, never cut");
    static const char nul[] = "loadscreena fe\0l\ndefault fel\n";
    BarParse(&t, nul, (unsigned)sizeof nul - 1);
    Check(t.screens == 0 && t.skipped == 1 && t.fallback == 0, "bars.txt: a NUL inside a line: that line skipped");
    static char many[40000];
    int len = 0;
    for (int i = 0; i < 300; i++) len += snprintf(many + len, sizeof many - (size_t)len, "screen%d f%d\n", i, i % 10);
    BarParse(&t, many, (unsigned)len);
    Check(t.families == BAR_FAMILIES && t.screens <= BAR_SCREENS && t.skipped == 300 - t.screens,
          "bars.txt: more families or screens than there is room for: the rest skipped, nothing overrun");

    /* no default line: a screen it does not list keeps the pack's orange */
    Fresh("firebar-no-default");
    PackWithBars("loadscreendeadmines fel\n");
    Start();
    FakeTables();
    Check(LogSays("fire bar on") && LogSays("no default") && !strcmp(ForMap(36), TEX "Loading-BarFill-fel") &&
          ForMap(0) == GameFill() && ForMap(-1) == GameFill(), "no default line: unlisted screens and the default one keep the pack's orange");
}

/* A real client's WoW.exe: the file offset of an RVA, from its section table. -1 when no section holds it. */
static long ExeOffset(FILE *x, unsigned rva)
{
    unsigned char h[64], sec[40];
    if (fseek(x, 0, SEEK_SET) || fread(h, 1, 64, x) != 64) return -1;
    unsigned pe = Rd32(h + 60);
    unsigned char fh[24];
    if (fseek(x, (long)pe, SEEK_SET) || fread(fh, 1, 24, x) != 24 || memcmp(fh, "PE\0\0", 4)) return -1;
    unsigned count = (unsigned)fh[6] | (unsigned)fh[7] << 8, opt = (unsigned)fh[20] | (unsigned)fh[21] << 8;
    for (unsigned i = 0; i < count; i++) {
        if (fseek(x, (long)(pe + 24 + opt + 40 * i), SEEK_SET) || fread(sec, 1, 40, x) != 40) return -1;
        unsigned va = Rd32(sec + 12), raw = Rd32(sec + 16), ptr = Rd32(sec + 20);
        if (rva >= va && rva < va + raw) return (long)(ptr + rva - va);
    }
    return -1;
}
static void FireBarRealClient(const char *client)
{
    char exe[1024];
    snprintf(exe, sizeof exe, "%s/WoW.exe", client);
    FILE *x = fopen(exe, "rb");
    int match = x != NULL;
    for (int i = 0; x && i < BAR_CODE_COUNT; i++) {
        unsigned char got[64];
        long off = ExeOffset(x, kBarCode[i].rva);
        match &= off >= 0 && !fseek(x, off, SEEK_SET) && fread(got, 1, (size_t)kBarCode[i].n, x) == (size_t)kBarCode[i].n &&
                 !memcmp(got, kBarCode[i].bytes, (size_t)kBarCode[i].n);
        if (!match) { printf("FAIL: RVA 0x%lX (%s) differs in %s\n", (unsigned long)kBarCode[i].rva, kBarCode[i].what, exe); break; }
    }
    for (int i = 0; x && match && i <= REVEAL_CODE_COUNT; i++) {   /* the reveal's five places, then the wallpaper call */
        DWORD rva = i < REVEAL_CODE_COUNT ? kRevealCode[i].rva : WALL_LOAD_RVA;
        int len = i < REVEAL_CODE_COUNT ? kRevealCode[i].n : (int)sizeof kWallLoad;
        const unsigned char *want = i < REVEAL_CODE_COUNT ? kRevealCode[i].bytes : kWallLoad;
        unsigned char got[64];
        long off = ExeOffset(x, rva);
        match &= off >= 0 && !fseek(x, off, SEEK_SET) && fread(got, 1, (size_t)len, x) == (size_t)len && !memcmp(got, want, (size_t)len);
        if (!match) printf("FAIL: RVA 0x%lX differs in %s\n", (unsigned long)rva, exe);
    }
    if (x) fclose(x);
    Check(match, "a real client: all seven places hold the bytes the fire bar expects, in its WoW.exe");
}

/* The mock's timeline (render_frames.py KEYS) through the sound built from a real client's files, polled as the game
 * would be: the fill changes every 20 ms. Written as 16-bit stereo for listening; a few checks on the way. */
static void FireRealClient(const char *client)
{
    static const float keys[][2] = { { 0, 0 }, { 0.6f, 0.02f }, { 1.6f, 0.15f }, { 2.8f, 0.18f }, { 3.1f, 0.42f }, { 5.8f, 0.55f },
                                     { 6.2f, 0.80f }, { 9.6f, 0.94f }, { 11.2f, 1.04f }, { 12.5f, 1.04f } };
    /* the places the sound relies on, against the real WoW.exe on disk (its .text sits at file offset = RVA) */
    char exe[1024];
    snprintf(exe, sizeof exe, "%s/WoW.exe", client);
    FILE *x = fopen(exe, "rb");
    int match = x != NULL;
    for (int i = 0; x && i < FIRE_CODE_COUNT; i++) {
        unsigned char got[32];
        match &= !fseek(x, (long)kFireCode[i].rva, SEEK_SET) && fread(got, 1, (size_t)kFireCode[i].n, x) == (size_t)kFireCode[i].n &&
                 !memcmp(got, kFireCode[i].bytes, (size_t)kFireCode[i].n);
        if (!match) { printf("FAIL: RVA 0x%lX (%s) differs in %s\n", (unsigned long)kFireCode[i].rva, kFireCode[i].what, exe); break; }
    }
    if (x) fclose(x);
    Check(match, "a real client: all eight places hold the bytes the sound expects, in its WoW.exe");

    Fresh("fire-real");
    GameDir();                                              /* the log goes to this folder */
    char from[1024], to[1024];
    const char *arch[] = { "patch.MPQ", "sound.MPQ" };
    for (int i = 0; i < 2; i++) {
        snprintf(from, sizeof from, "%s/Data/%s", client, arch[i]);
        snprintf(to, sizeof to, "%sData/%s", shim_gamedir, arch[i]);
        if (symlink(from, to)) { printf("FAIL: could not link %s\n", from); fails++; return; }
    }
    memset(&g_fire, 0, sizeof g_fire);
    int built = FireBuild(44100), all = 1;
    for (int i = 0; i < FIRE_LOOPS; i++) all &= g_fire.mix.loop[i] != NULL;
    Check(built && all && LogSays("6 of 6 loops"), "a real client: all six loops made from its own files");
    if (!all) return;
    FireReset(&g_fire.mix);
    int total = (int)(12.5 * 44100), pollFrames = 882, done = 0;
    static short pcm[2 * 12 * 44100 + 2 * 44100];
    short peak = 0;
    for (int poll = 0; done < total; poll++) {
        float t = poll * 0.02f, fill = 0;
        for (int k = 0; k + 1 < 10; k++)
            if (t >= keys[k][0] && t <= keys[k + 1][0]) fill = keys[k][1] + (keys[k + 1][1] - keys[k][1]) * (t - keys[k][0]) / (keys[k + 1][0] - keys[k][0]);
        int n = total - done < pollFrames ? total - done : pollFrames;
        FireRender(&g_fire.mix, t < 11.6f, fill, pcm + 2 * done, n);
        done += n;
    }
    for (int i = 0; i < 2 * total; i++) if (abs(pcm[i]) > peak) peak = (short)abs(pcm[i]);
    Check(peak > 8000 && peak < 32767, "a real client: the timeline is loud as the mock and never clips");
    char path[1024];
    snprintf(path, sizeof path, "%s/fire-timeline.wav", g_root);
    static unsigned char out[sizeof pcm + 64];
    size_t len = Wav(out, 1, 2, 16, 44100, pcm, total);
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(out, 1, len, f); fclose(f); printf("wrote %s (peak %d)\n", path, peak); }
}

/* ---- tonight's three: the fire revealed with a soft, live front, and the continents' wallpapers ---- */
static void PutFloat(unsigned va, float f) { memcpy(shim_module + va - 0x400000u, &f, 4); }
static void PackLive(int reveal, int walls)
{
    const char *names[16], *data[16];
    int n = 0;
    names[n] = "Interface\\Glues\\LoadingScreens\\LoadScreenDeadmines.blp"; data[n++] = "BLP2screen";
    names[n] = "CinderLoad\\bars.txt";
    data[n++] = walls ? "cinderwall01 aqua\r\ncinderwall02 fel\r\ndefault arcane\r\n" : kBars;
    static const char *const fam[] = { "fel", "aqua", "arcane" };
    static char fills[3][80], borders[3][80];
    for (int f = 0; f < 3; f++) {
        snprintf(fills[f], sizeof fills[f], TEX "Loading-BarFill-%s.blp", fam[f]);
        snprintf(borders[f], sizeof borders[f], TEX "Loading-BarBorder-%s.blp", fam[f]);
        names[n] = fills[f]; data[n++] = "BLP2f";
        names[n] = borders[f]; data[n++] = "BLP2b";
    }
    if (reveal) { names[n] = "CinderLoad\\reveal.txt"; data[n++] = "revealed"; }
    if (walls) {
        names[n] = "Interface\\Glues\\LoadingScreens\\CinderWall01.blp"; data[n++] = "BLP2wall1";
        names[n] = "Interface\\Glues\\LoadingScreens\\CinderWall02.blp"; data[n++] = "BLP2wall2";
    }
    MakeMpq(UW, n, names, data, NULL);
    Put("WTF/Config.wtf", RES("5120x2160"));
}
static int SetupCall(void) { return Rd32(shim_module + 0x725E + 1); }
static int StripCall(void) { return Rd32(shim_module + 0x7288 + 1); }
static int StretchKept(void)
{
    return Same(shim_module + REVEAL_DRAW_RVA, kRevealDraw, (int)sizeof kRevealDraw) &&
           Same(shim_module + REVEAL_STRIP_RVA, kRevealStrip, (int)sizeof kRevealStrip);
}

static void LiveTests(void)
{
    /* the reveal: both calls ours, the fill as tall as the log */
    Fresh("reveal-on");
    PackLive(1, 0);
    Start();
    unsigned setup = (unsigned)((unsigned long)RevealStub - (unsigned long)(shim_module + 0x725E + 5));
    unsigned strip = (unsigned)((unsigned long)RevealStripStub - (unsigned long)(shim_module + 0x7288 + 5));
    Check(LogSays("fire reveal on") && (unsigned)SetupCall() == setup && (unsigned)StripCall() == strip &&
          shim_module[0x725E] == 0xE8 && shim_module[0x7288] == 0xE8 &&
          g_drawSetup == shim_module + 0x18A2A0 && g_drawStrip == shim_module + 0x18A2E0,
          "reveal on: RVA 0x725E to the setup stub, 0x7288 to the strip stub, each going on to the game's own (0x58A2A0, 0x58A2E0)");
    Check(Same(shim_module + BAR_FILL_RVA, kBorderWide, 16) && Same(shim_module + BAR_BORDER_RVA, kBorderWide, 16) &&
          LogSays("its fire as tall as the log") && LogSays("fire bar on"),
          "reveal on: the fill takes the log's own size, the fire bar still on");
    Start();
    Check(LogSays("fire reveal already on") && (unsigned)SetupCall() == setup, "a second start: already on, nothing written");

    Fresh("reveal-asked-not");
    PackLive(0, 0);
    Start();
    Check(!LogSays("fire reveal") && StretchKept() && Same(shim_module + BAR_FILL_RVA, kFillWide, 16) && LogSays("fire bar on"),
          "a pack without reveal.txt: stretched as before, the fill its old height");

    int quiet = 1;
    for (int i = 0; i < REVEAL_CODE_COUNT; i++) {
        Fresh("reveal-other");
        PackLive(1, 0);
        shim_module[kRevealCode[i].rva + (unsigned)kRevealCode[i].n - 1] ^= 0x40;
        Start();
        char words[160];
        snprintf(words, sizeof words, "fire reveal off: other bytes at RVA 0x%lX (%s)", (unsigned long)kRevealCode[i].rva,
                 kRevealCode[i].what);
        int kept = kRevealCode[i].rva == REVEAL_DRAW_RVA || kRevealCode[i].rva == REVEAL_STRIP_RVA
                       ? Same(shim_module + REVEAL_DRAW_RVA, kRevealDraw, (int)sizeof kRevealDraw - (kRevealCode[i].rva == REVEAL_DRAW_RVA)) &&
                         Same(shim_module + REVEAL_STRIP_RVA, kRevealStrip, (int)sizeof kRevealStrip - (kRevealCode[i].rva == REVEAL_STRIP_RVA))
                       : StretchKept();
        quiet &= LogSays(words) && kept && Same(shim_module + BAR_FILL_RVA, kFillWide, 16) && LogSays("fire bar on");
    }
    Check(quiet, "any of the reveal's five places changed: logged, neither call touched, the fill stretched at its old height");

    /* the live strip */
    float v[12] = { 0.0f, 0.0f, 0.5f,  0.5f, 0.0f, 0.5f,  0.0f, 0.09f, 0.5f,  0.5f, 0.09f, 0.5f };
    PutFloat(0x882BE4, 0.5f);
    g_barsOn = 1;
    RevealFill(v);
    int n = g_fillCount / 2, rising = 1;
    for (int k = 1; k < n; k++) rising &= g_fillVerts[6 * k] > g_fillVerts[6 * (k - 1)];
    float uSpan = g_fillUV[4 * (n - 1)] - g_fillUV[0];
    Check(g_fillCount % 2 == 0 && n >= 3 && n <= FILL_COLUMNS && rising && g_fillVerts[0] == 0.0f &&
          g_fillVerts[6 * (n - 1)] == 0.5f && g_fillVerts[1] == 0.09f && g_fillVerts[4] == 0.0f && g_fillVerts[2] == 0.5f,
          "the strip: columns from the left edge to the front in order, top then bottom, at the game's height and depth");
    Check(Near(uSpan, 0.25, 1e-4) && g_fillUV[1] == 0 && g_fillUV[3] == 1,
          "the lava: half its texture spans the whole bar, so half a bar's progress shows a quarter of it");
    unsigned front = g_fillColor[2 * (n - 1)], left = g_fillColor[0];
    int grey = 0, bright = 0;
    for (int k = 0; k < n; k++) {
        float d = (0.5f - g_fillVerts[6 * k]) / 1.0f;
        int gk = (int)(g_fillColor[2 * k] & 0xFF);
        if (d > 0.4f && gk > grey) grey = gk;
        if (d > 0.04f && d < 0.1f && gk > bright) bright = gk;
    }
    Check((front >> 24) == 0 && (left >> 24) == 255 && bright > grey && grey >= 70 && g_fillColor[1] == g_fillColor[0],
          "the heat: the front fades in from nothing, brightest just behind it, cooling (but still alight) far behind");
    PutFloat(0x882BE4, 0.0f);
    float v0[12] = { 0.0f, 0.0f, 0.5f,  0.0f, 0.0f, 0.5f,  0.0f, 0.09f, 0.5f,  0.0f, 0.09f, 0.5f };
    RevealFill(v0);
    Check(g_fillCount >= 2 && g_fillCount <= 2 * FILL_COLUMNS, "an empty bar: a strip of nothing, never past its arrays");
    PutFloat(0x882BE4, 2.0f);
    float v1[12] = { 0.0f, 0.0f, 0.5f,  1.0f, 0.0f, 0.5f,  0.0f, 0.09f, 0.5f,  1.0f, 0.09f, 0.5f };
    RevealFill(v1);
    Check(g_fillCount <= 2 * FILL_COLUMNS && g_fillVerts[6 * (g_fillCount / 2 - 1)] == 1.0f, "a progress past 1 is taken as 1");

    /* the wallpapers */
    Fresh("walls-on");
    PackLive(1, 1);
    Start();
    unsigned wallRel = (unsigned)((unsigned long)BarStub - (unsigned long)(shim_module + 0x6EBA + 5));
    Check(LogSays("wallpapers: 2 in the pack") && LogSays("wallpapers on") && Rd32(shim_module + 0x6EBA + 1) == wallRel &&
          shim_module[0x6EBA] == 0xE8 && Same(shim_module + 0x6EAF, kWallLoad, 12),
          "wallpapers on: the picture's load at RVA 0x6EBA goes to the bar's stub, every other byte kept");
    FakeTables();
    const char *ekPath = "Interface\\Glues\\LoadingScreens\\LoadScreenEasternKingdom.blp";
    int ok = 1, last = -1, changed = 1;
    for (int i = 0; i < 6; i++) {
        const char *fill = ForMap(0);
        const char *pic = BarPath(ekPath);
        int w = strstr(pic, "CinderWall01") ? 1 : (strstr(pic, "CinderWall02") ? 2 : 0);
        ok &= w && !strcmp(fill, w == 1 ? TEX "Loading-BarFill-aqua" : TEX "Loading-BarFill-fel");
        if (last >= 0) changed &= w != last;
        last = w;
    }
    Check(ok && changed, "Eastern Kingdoms: a wallpaper drawn at the fill, its fire with it, the picture then that same wallpaper, never twice in a row");
    const char *dmPath = "Interface\\Glues\\LoadingScreens\\LoadScreenDeadmines.blp";
    const char *dmFill = ForMap(36);
    Check(BarPath(dmPath) == dmPath && !strcmp(dmFill, TEX "Loading-BarFill-arcane"),
          "a dungeon (the Deadmines): its own picture, its fire from bars.txt");

    Fresh("walls-none");
    PackLive(1, 0);
    Start();
    Check(!LogSays("wallpapers") && Same(shim_module + WALL_LOAD_RVA, kWallLoad, (int)sizeof kWallLoad),
          "no wallpapers in the pack: the picture's load is never touched");

    Fresh("walls-other");
    PackLive(1, 1);
    shim_module[WALL_LOAD_RVA + 1] ^= 0x40;
    Start();
    Check(LogSays("wallpapers off: other bytes at RVA 0x6EAF") && LogSays("fire bar on") &&
          shim_module[WALL_LOAD_RVA + 1] == (0x4E ^ 0x40) && Same(shim_module + WALL_CALL_RVA, kWallLoad + WALL_REL_AT - 1, 5),
          "the picture's load holding other bytes: logged, untouched, the fire bar still on");
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: test_switch <scratch dir>\n"); return 2; }
    g_root = argv[1];

    Fresh("nothing");
    Start();
    Check(Gone(ACTIVE) && !On(), "no archives: nothing put in place, switch off");

    Fresh("first");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(Is(ACTIVE, "AAAA") && Is(MARK, "21x9 4") && On(), "5120x2160: the ultrawide set put in place, switch on");
    memcpy(shim_module + PATCH_RVA, kStock, sizeof kStock);
    Start();
    Check(Is(ACTIVE, "AAAA") && On() && LogSays("are in place"), "next start: already in place, switch on again");
    Put("WTF/Config.wtf", RES("1920x1080"));
    memcpy(shim_module + PATCH_RVA, kStock, sizeof kStock);
    Start();
    Check(Gone(ACTIVE) && Gone(MARK) && !On(), "changed to 1920x1080 with no 16x9 set: ours removed, switch off");

    Fresh("foreign");
    Put(UW, "AAAA"); Put(ACTIVE, "FOREIGN"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(Is(ACTIVE, "FOREIGN") && !On() && LogSays("not ours"), "someone else's patch-~.MPQ: left alone, switch off");

    Fresh("stale-marker");
    Put(UW, "AAAA"); Put(ACTIVE, "FOREIGNXX"); Put(MARK, "21x9 4"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(Is(ACTIVE, "FOREIGNXX") && !On(), "a file that replaced ours (other size than recorded): left alone");

    Fresh("update");
    Put(UW, "BBBBBB"); Put(ACTIVE, "AAAA"); Put(MARK, "21x9 4"); Put("WTF/Config.wtf", RES("3440x1440"));
    Start();
    Check(Is(ACTIVE, "BBBBBB") && Is(MARK, "21x9 6") && On(), "a newer pack: ours replaced by the new one");

    Fresh("monitor");
    Put(UW, "AAAA");
    Start();
    Check(Is(ACTIVE, "AAAA") && On() && LogSays("the monitor"), "no Config.wtf: the monitor's 3440x1440 picks 21x9");

    Fresh("other-exe");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    shim_module[PATCH_RVA + 1] = 0x11;
    Start();
    Check(Gone(ACTIVE) && shim_module[PATCH_RVA] == 0xE8 && shim_module[PATCH_RVA + 1] == 0x11 &&
          LogSays("not the client this was made for"), "another WoW.exe: nothing put in place, its code never changed");

    Fresh("already-wide");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    memcpy(shim_module + PATCH_RVA, kWide, sizeof kWide);
    Start();
    Check(On() && LogSays("already on"), "an exe already changed for wide screens: left as it is");

    Fresh("two-shapes");
    Put(UW, "AAAA"); Put(HD, "HHHHH"); Put("WTF/Config.wtf", RES("2560x1440"));
    Start();
    Check(Is(ACTIVE, "HHHHH") && Is(MARK, "16x9 5") && On(), "2560x1440 with both sets: the 16x9 one");
    Put("WTF/Config.wtf", RES("3440x1440"));
    Start();
    Check(Is(ACTIVE, "AAAA") && Is(MARK, "21x9 4") && On(), "then 3440x1440: swapped to the 21x9 one");

    Fresh("ours-by-content");
    Put(UW, "AAAA"); Put(HD, "HHHHH"); Put(ACTIVE, "HHHHH"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(Is(ACTIVE, "AAAA") && On(), "ours with no marker (same bytes as one of our sets): replaced");

    Fresh("four-three");
    Put(UW, "AAAA"); Put(ACTIVE, "AAAA"); Put(MARK, "21x9 4"); Put("WTF/Config.wtf", RES("1024x768"));
    Start();
    Check(Gone(ACTIVE) && !On(), "4:3: no set for it, ours removed, the game's own screens as it shows them");

    Fresh("bar");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(On() && BarWide() && LogSays("bar on"), "our set in place: the bar spans the whole width, twice as tall");
    Start();
    Check(BarWide() && LogSays("bar already full width"), "next start: the bar already full width, left as it is");

    Fresh("bar-no-set");
    Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(!On() && BarStock(), "no set for this screen: the bar keeps the game's own size");

    Fresh("bar-foreign");
    Put(UW, "AAAA"); Put(ACTIVE, "FOREIGN"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(!On() && BarStock(), "someone else's patch-~.MPQ: the bar keeps the game's own size too");

    Fresh("bar-other-exe");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    shim_module[BAR_BORDER_RVA + 8] = 0x11;
    Start();
    Check(!On() && Gone(ACTIVE) && shim_module[BAR_BORDER_RVA + 8] == 0x11 && Same(shim_module + BAR_FILL_RVA, kFillStock, 16) &&
          LogSays("not the client this was made for"), "a bar size table with other numbers: nothing put in place, never changed");

    Fresh("from-0.1.2");
    Put(UW, "AAAA"); Put(ACTIVE, "AAAA"); Put(OLDMARK, "21x9 4"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(On() && Is(MARK, "21x9 4") && Gone(OLDMARK), "0.1.2's record in Data/CinderLoad: moved beside the archive");

    Fresh("from-0.1.2-update");
    Put(UW, "BBBBBB"); Put(ACTIVE, "AAAA"); Put(OLDMARK, "21x9 4"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(On() && Is(ACTIVE, "BBBBBB") && Is(MARK, "21x9 6") && Gone(OLDMARK), "0.1.2's record still makes ours known: updated");

    Fresh("packs-removed");
    Put(ACTIVE, "AAAA"); Put(MARK, "21x9 4"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    Check(!On() && Gone(ACTIVE) && Gone(MARK), "packs removed, DLL still there: ours taken out, the game's own");

    Fresh("no-temp-left");
    Put(UW, "AAAA"); Put("WTF/Config.wtf", RES("5120x2160"));
    Start();
    {
        char t[64];
        snprintf(t, sizeof t, "Data/CinderLoad/pending-%lu.tmp", (unsigned long)getpid());
        Check(On() && Is(ACTIVE, "AAAA") && Gone(t), "put in place through its own temporary file, none left behind");
    }

    Fresh("copy-fails");
    Put(UW, "AAAA"); Put(HD, "HHHHH"); Put(ACTIVE, "HHHHH"); Put(MARK, "16x9 5"); Put("WTF/Config.wtf", RES("5120x2160"));
    {
        char p[700];
        snprintf(p, sizeof p, "%s%s", shim_gamedir, UW);
        chmod(p, 0);
        Start();
        chmod(p, 0644);
    }
    Check(!On() && Gone(ACTIVE) && Gone(MARK) && LogSays("could not put"),
          "the copy fails: the old shape's screens taken out too, never left squeezed");

    FireTests();
    FireBarTests();
    LiveTests();
    if (argc > 2) {
        FireBarRealClient(argv[2]);
        FireRealClient(argv[2]);
    }

    if (fails) printf("%d of %d checks FAILED\n", fails, checks); else printf("all %d checks passed\n", checks);
    return fails != 0;
}
