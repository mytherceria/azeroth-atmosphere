/* Wine/Windows harness for IndoorRain.dll. Imports fmod.dll STATICALLY (libfmod.a), so the DLL's
 * import-table redirection of FSOUND_Close, FSOUND_Init and FSOUND_Stream_Open runs for real; fakes the
 * client's globals in one page it maps (a test build of the DLL reads them there, see the README) and a
 * fake CVar table of ten names, the storm CVar's value in a page of its own as on the client's heap;
 * walks a scenario counting playing channels; exit code = number of failed checks. It starts by
 * deleting IndoorRain.ini and Logs\IndoorRain.log in its own folder: the checks count log lines and
 * need the ini's defaults. Never run it in a game folder: it refuses a folder holding WoW.exe or Data.
 *
 * Since 0.14 the DLL builds its sounds from a client's archives, and the run folder holds no sound file.
 * The harness names the Data folder its test build reads (data_dir in the ini, a switch only the test build
 * has); the DLL opens those archives read-only, and the harness never opens them at all. One scenario per
 * run, since the DLL starts once per process:
 *   harness.exe <Data folder>   the full walk, on sounds built from that client's archives (a Wine path such
 *                               as Z:\home\...\Data; without it, data_dir from an IndoorRain.ini already here).
 *                               It knows two clients, and tells them apart by the DLL's scan line: RavenCraft's,
 *                               with patch-S.mpq (its 45.5 s heavy rain splice and three gusts), and a plain 1.12.1
 *                               client without one (patch.MPQ's heavy loop, 23.5 s once seamless, and no gusts);
 *                               checks 19, 20c and 20i expect what the one found has
 *   harness.exe --no-data       the same DLL on an empty Data folder the harness makes (no-data\): every build
 *                               fails and is logged once, and nothing plays, nothing is swapped, nothing crashes;
 *                               from the storm on, the folder holds a patch-S.mpq the harness keeps open with no
 *                               sharing, as another program would, so the DLL meets a real sharing violation (N5g)
 * FMOD's file callbacks are the harness's own, counting their calls by thread (check 25): the DLL plays only
 * from memory, so no call may come from any thread but this one, which stands for the game's. In the client
 * those callbacks are the game's file layer, whose cache has no lock. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

__declspec(dllimport) signed char __stdcall FSOUND_SetOutput(int);
__declspec(dllimport) signed char __stdcall FSOUND_Init(int, int, unsigned int);
__declspec(dllimport) void __stdcall FSOUND_Close(void);
__declspec(dllimport) int __stdcall FSOUND_GetChannelsPlaying(void);
__declspec(dllimport) void *__stdcall FSOUND_Sample_Load(int index, const char *name, unsigned int mode, int offset, int length);
__declspec(dllimport) signed char __stdcall FSOUND_Sample_SetDefaults(void *sample, int deffreq, int defvol, int defpan, int defpri);
__declspec(dllimport) int __stdcall FSOUND_PlaySound(int channel, void *sample);
__declspec(dllimport) signed char __stdcall FSOUND_IsPlaying(int channel);
__declspec(dllimport) signed char __stdcall FSOUND_StopSound(int channel);
__declspec(dllimport) signed char __stdcall FSOUND_GetReserved(int channel);
__declspec(dllimport) void *__stdcall FSOUND_Stream_Open(const char *name, unsigned int mode, int offset, int length);
__declspec(dllimport) signed char __stdcall FSOUND_Stream_Close(void *stream);
__declspec(dllimport) int __stdcall FSOUND_Stream_GetLengthMs(void *stream);
typedef void *(__stdcall *cb_open_t)(const char *name);
typedef void (__stdcall *cb_close_t)(void *handle);
typedef int (__stdcall *cb_read_t)(void *buffer, int size, void *handle);
typedef int (__stdcall *cb_seek_t)(void *handle, int pos, signed char mode);
typedef int (__stdcall *cb_tell_t)(void *handle);
__declspec(dllimport) signed char __stdcall FSOUND_File_SetCallbacks(cb_open_t, cb_close_t, cb_read_t, cb_seek_t, cb_tell_t);

#define ADDR_CMAPWEATHER_PTR 0x00C7B100u
#define ADDR_WEATHER_SOUND_ID 0x00C7B200u
#define ADDR_WEATHER_GATE 0x00C7B584u
#define ADDR_OBJMGR_PTR 0x00C7B300u
#define ADDR_FAKE_CVARS 0x00C7B700u
#define STORM_TEMPLATE "IRS1:k0:l0:n000:d00"

static int fails = 0;
static char cv_enabled[16] = "1", cv_volume[16] = "60", cv_indoors[16] = "0", cv_master[24] = "1", cv_ambience[24] = "1", cv_enable_ambience[8] = "1";
/* The addon's storm switch starts off: random thunder and gusts would count as playing channels in the
   channel checks before 20. Check 5b turns it on while the mod is off, and nothing may play. */
static char cv_storms[8] = "0", cv_session[8] = "42", cv_thunder[8] = "0";
static char *cv_storm;   /* IndoorRain_Storm's value, in a page of its own so check 24 can make it read-only */

static void *reserve(DWORD page)
{
    void *p = VirtualAlloc((LPVOID)(UINT_PTR)page, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!p) printf("could not reserve page %08lx (error %lu)\n", (unsigned long)page, GetLastError());
    return p;
}
static void expect(const char *stage, int want_playing)
{
    int n = FSOUND_GetChannelsPlaying();
    int ok = want_playing ? (n > 0) : (n == 0);
    printf("%-56s channels = %d, expected %s  %s\n", stage, n, want_playing ? ">0" : "0", ok ? "ok" : "FAIL");
    if (!ok) fails++;
}
static int last_logged_channel(void)
{
    FILE *f = fopen("Logs\\IndoorRain.log", "r"); char line[1024]; int ch = -1; char *p;
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) if ((p = strstr(line, "stream started")) && (p = strstr(p, "channel "))) ch = atoi(p + 8);
    fclose(f); return ch;
}
static int log_count(const char *needle)
{
    FILE *f = fopen("Logs\\IndoorRain.log", "r"); char line[1024]; int n = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) if (strstr(line, needle)) n++;
    fclose(f); return n;
}
/* the first log line holding needle, from the needle on; empty when there is none */
static void log_first(const char *needle, char *out, int cap)
{
    FILE *f = fopen("Logs\\IndoorRain.log", "r"); char line[1024]; char *p;
    out[0] = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f)) if ((p = strstr(line, needle))) { strncpy(out, p, (size_t)cap - 1); out[cap - 1] = 0; break; }
    fclose(f);
}
static void expect_log(const char *stage, const char *needle, int min, int max)
{
    int n = log_count(needle); int ok = n >= min && n <= max;
    printf("%-56s log \"%s\" x%d, expected %d..%d  %s\n", stage, needle, n, min, max, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}
static void ini_set(const char *key, const char *value)
{
    char ini[MAX_PATH]; GetFullPathNameA("IndoorRain.ini", MAX_PATH, ini, NULL);
    WritePrivateProfileStringA("IndoorRain", key, value, ini);
}
static void storm_reset(const char *value) { memset(cv_storm, 0, 64); strcpy(cv_storm, value); }

/* FMOD's file access, counted by thread (check 25). FMOD calls these for every sound opened by name; a memory
   image (FSOUND_LOADMEMORY) must never reach them. They do the real work with stdio, as FMOD's own sample
   code does (0 from fseek is success). */
static DWORD main_tid;
static volatile LONG cb_main = 0, cb_other = 0, cb_other_tid = 0;
static void cb_count(void)
{
    if (GetCurrentThreadId() == main_tid) InterlockedIncrement(&cb_main);
    else { InterlockedIncrement(&cb_other); InterlockedExchange(&cb_other_tid, (LONG)GetCurrentThreadId()); }
}
static void *__stdcall cb_open(const char *name) { cb_count(); return name ? fopen(name, "rb") : NULL; }
static void __stdcall cb_close(void *h) { cb_count(); if (h) fclose((FILE *)h); }
static int __stdcall cb_read(void *buf, int size, void *h) { cb_count(); return h && size > 0 ? (int)fread(buf, 1, (size_t)size, (FILE *)h) : 0; }
static int __stdcall cb_seek(void *h, int pos, signed char mode) { cb_count(); return h ? fseek((FILE *)h, pos, mode) : -1; }
static int __stdcall cb_tell(void *h) { cb_count(); return h ? (int)ftell((FILE *)h) : -1; }

/* The flood sample for checks 18 and 22: a second of quiet noise, a 16-bit mono WAV made here and loaded from
   memory (0.14 ships no sound file the harness could borrow; its length does not matter, it loops). */
static unsigned char flood_wav[44 + 22050 * 2] __attribute__((aligned(4)));
static void flood_make(void)
{
    unsigned int n = 22050 * 2, i, x = 12345;
    memcpy(flood_wav, "RIFF", 4); *(unsigned int *)(flood_wav + 4) = 36 + n; memcpy(flood_wav + 8, "WAVEfmt ", 8);
    *(unsigned int *)(flood_wav + 16) = 16; *(unsigned short *)(flood_wav + 20) = 1; *(unsigned short *)(flood_wav + 22) = 1;
    *(unsigned int *)(flood_wav + 24) = 22050; *(unsigned int *)(flood_wav + 28) = 44100;
    *(unsigned short *)(flood_wav + 32) = 2; *(unsigned short *)(flood_wav + 34) = 16;
    memcpy(flood_wav + 36, "data", 4); *(unsigned int *)(flood_wav + 40) = n;
    for (i = 0; i < n / 2; i++) { x = x * 1103515245u + 12345u; ((short *)(flood_wav + 44))[i] = (short)((int)((x >> 16) & 0x7FF) - 1024); }
}

/* The start, in place of 0.13's fixed sleep: wait for the DLL's build line (up to 90 s: a first run under Wine
   reads a cold disk), and meanwhile, in the full walk (probe), as soon as the hook is in, ask for the game's rain
   loop as the game would.
   0e: the hook answers at once, from this thread, and passes the call through (the swap switch only opens at
   the worker's first poll, after the build), so a rain loop the game opens during the build costs it nothing.
   Only the full walk asks, and only it scores 0e. On an empty Data folder there is no build to ask during: the
   build line follows the hook line by a few ms, so a poll every 25 ms mostly misses the gap and 0e failed against
   a correct DLL, and an ask that landed after the worker's first poll had turned the swap on added a second
   "not built" line to the one N3b counts. N3 asks there instead, with the swap on. */
static void wait_build(int probe)
{
    DWORD t0 = GetTickCount(), asked_ms = 0; int probed = 0, during = 0, ok; void *st = NULL; char line[1024];
    while (GetTickCount() - t0 < 90000 && log_count("sounds built at start:") <= 0) {
        if (probe && !probed && log_count("outdoor rain swap: installed") > 0) {
            DWORD a = GetTickCount();
            st = FSOUND_Stream_Open("Sound\\Ambience\\Weather\\RainHeavyLoop.wav", 0x2002, 0, 0);
            asked_ms = GetTickCount() - a; probed = 1;
            during = log_count("sounds built at start:") <= 0;
            if (st) FSOUND_Stream_Close(st);
        }
        Sleep(25);
    }
    log_first("sounds built at start:", line, sizeof line);
    printf("   %s%s", line[0] ? line : "(no build line within 90 s)\n", line[0] && !strchr(line, '\n') ? "\n" : "");
    if (!probe) return;
    ok = probed && during && !st && asked_ms < 250;
    printf("%-56s %s, %s in %lu ms, expected NULL at once  %s\n", "0e a rain loop asked for during the build passes through",
           !probed ? "never asked (no hook line)" : (during ? "asked during the build" : "asked after it (inconclusive)"), st ? "stream" : "NULL",
           (unsigned long)asked_ms, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

int main(int argc, char **argv)
{
    char data[MAX_PATH] = ""; int no_data, cb_control, raven = 1;
    /* run.sh sends this to a file and stops a walk that hangs; the C runtime fills a 4 KB buffer before it writes to
       a file (and on Windows line buffering is the same), so buffered, the lines before a hang would be lost with it
       and the file would stop at an earlier check than the one that hung */
    setvbuf(stdout, NULL, _IONBF, 0);
    no_data = argc > 1 && strcmp(argv[1], "--no-data") == 0;
    main_tid = GetCurrentThreadId();
    if (GetFileAttributesA("WoW.exe") != INVALID_FILE_ATTRIBUTES || GetFileAttributesA("Data") != INVALID_FILE_ATTRIBUTES) {
        printf("refusing to run here: this folder holds WoW.exe or a Data folder, and the harness deletes files in its own folder\n");
        return 93;
    }
    /* the held archive of N5g is gone before the scan, even if a run died holding it: N1b wants every archive missing */
    if (no_data) { CreateDirectoryA("no-data", NULL); DeleteFileA("no-data\\patch-S.mpq"); GetFullPathNameA("no-data\\", MAX_PATH, data, NULL); }
    else if (argc > 1) { strncpy(data, argv[1], MAX_PATH - 1); data[MAX_PATH - 1] = 0; }
    else { char ini[MAX_PATH]; GetFullPathNameA("IndoorRain.ini", MAX_PATH, ini, NULL); GetPrivateProfileStringA("IndoorRain", "data_dir", "", data, MAX_PATH, ini); }
    { DWORD a = data[0] ? GetFileAttributesA(data) : INVALID_FILE_ATTRIBUTES;
      if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
          printf("usage: harness.exe <the Data folder of RavenCraft's client or of a plain 1.12.1 one, e.g. Z:\\home\\...\\Data> | --no-data   (no such folder: \"%s\")\n", data);
          return 94;
      } }
    DeleteFileA("IndoorRain.ini"); DeleteFileA("Logs\\IndoorRain.log");   /* the ini's defaults, and a log that counts from zero */
    ini_set("data_dir", data);   /* the test build reads its archives there, read-only; the harness never opens them */
    printf("data_dir: %s%s\n", data, no_data ? " (empty, made here)" : "");
    if (!reserve(0x00C7B000)) return 99;
    DWORD *weather_ptr = (DWORD *)ADDR_CMAPWEATHER_PTR, *sound_id = (DWORD *)ADDR_WEATHER_SOUND_ID, *mgr_ptr = (DWORD *)ADDR_OBJMGR_PTR;
    BYTE *gate = (BYTE *)ADDR_WEATHER_GATE;
    char **cvars = (char **)ADDR_FAKE_CVARS;
    cv_storm = (char *)VirtualAlloc(NULL, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    if (!cv_storm) { printf("could not allocate the storm CVar's page\n"); return 98; }
    storm_reset(STORM_TEMPLATE);
    /* the order of the DLL's harness table (cvar_find) */
    cvars[0] = cv_enabled; cvars[1] = cv_volume; cvars[2] = cv_indoors; cvars[3] = cv_master; cvars[4] = cv_ambience; cvars[5] = cv_enable_ambience;
    cvars[6] = cv_storm; cvars[7] = cv_storms; cvars[8] = cv_session; cvars[9] = cv_thunder;
    unsigned char *weather = (unsigned char *)VirtualAlloc(NULL, 0x130, MEM_COMMIT, PAGE_READWRITE);
    unsigned char *mgr = (unsigned char *)VirtualAlloc(NULL, 0x100, MEM_COMMIT, PAGE_READWRITE);
    float *intensity = (float *)(weather + 0x00); int *wtype = (int *)(weather + 0x20); DWORD *rainfx = (DWORD *)(weather + 0x28);
    DWORD *guid = (DWORD *)(mgr + 0xC0);
    *weather_ptr = 0; *sound_id = 0; *gate = 0; *mgr_ptr = 0; *guid = 0; *intensity = 0.0f; *wtype = 0; *rainfx = 0;

    /* The Goldshire inn, in memory from the moment the world comes up (check 2), so a building probe that ran
       by default would name it long before check 21 turns the probe on (0d). The chain is the one the client's
       zone text walks: player -> map entity -> the group def flagged 0x10 -> the placed building -> its model
       (path, header with the model id). */
    unsigned char *m = (unsigned char *)VirtualAlloc(NULL, 0x4000, MEM_COMMIT, PAGE_READWRITE);
    unsigned char *player = m, *ent = m + 0x400, *link = m + 0x600, *def = m + 0x700, *rootlink = m + 0x800,
                  *mdef = m + 0x900, *model = m + 0xC00, *hdr = m + 0x1000, *grp = m + 0x1100, *other = m + 0x1400;
    *(DWORD *)(mgr + 0xA4) = 0x10;                                   /* list link offset in each object */
    *(DWORD *)(mgr + 0xAC) = (DWORD)(UINT_PTR)other;                 /* first object: someone else */
    *(DWORD *)(other + 0x30) = 0x9999; *(DWORD *)(other + 0x10 + 4) = (DWORD)(UINT_PTR)player;
    *(DWORD *)(player + 0x30) = 0x1234; *(DWORD *)(player + 0x10 + 4) = 1;   /* end of list */
    *(DWORD *)(player + 0xE0) = (DWORD)(UINT_PTR)ent;
    *(DWORD *)(ent + 0x18) = 0x10; *(DWORD *)(ent + 0x20) = (DWORD)(UINT_PTR)link;
    *(DWORD *)(link + 0x08) = (DWORD)(UINT_PTR)def; *(DWORD *)(link + 0x10 + 4) = 1;
    *(DWORD *)(def + 0x08) = 0x10; *(DWORD *)(def + 0x20) = (DWORD)(UINT_PTR)rootlink; *(DWORD *)(def + 0x7C) = 2;
    *(DWORD *)(rootlink + 0x08) = (DWORD)(UINT_PTR)mdef;
    *(DWORD *)(mdef + 0x118) = (DWORD)(UINT_PTR)model; *(DWORD *)(mdef + 0x128) = 0;
    strcpy((char *)(model + 0x1C), "World\\wmo\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo");
    *(DWORD *)(model + 0x120) = (DWORD)(UINT_PTR)hdr; *(DWORD *)(hdr + 0x20) = 53;
    *(DWORD *)(model + 0x1F4 + 2 * 4) = (DWORD)(UINT_PTR)grp; *(DWORD *)(grp + 0x14C) = 1932;
    *(DWORD *)(ent + 0x90) = 1;                                      /* the entity knows where it is */

    /* Load the DLL BEFORE the engine starts, as VanillaFixes does: the DLL must see this Init through its hook. */
    HMODULE dll = LoadLibraryA("IndoorRain.dll");
    if (!dll) { printf("IndoorRain.dll failed to load (error %lu)\n", GetLastError()); return 95; }
    printf("IndoorRain.dll loaded\n");
    Sleep(1500);
    FSOUND_SetOutput(0);
    if (!FSOUND_Init(44100, 12, 0)) { printf("FSOUND_Init failed\n"); return 96; }
    /* our file callbacks, then a named open of a file that is here: the control that shows FMOD now calls them */
    FSOUND_File_SetCallbacks(cb_open, cb_close, cb_read, cb_seek, cb_tell);
    { void *st = FSOUND_Stream_Open("IndoorRain.ini", 0x2002, 0, 0); if (st) FSOUND_Stream_Close(st); cb_control = (int)cb_main; }
    printf("fmod up (nosound), through the import table\n");
    wait_build(!no_data);   /* the DLL idles until the engine is up, waits two more seconds, then builds its sounds */
    expect_log("0 both hooks installed after the engine came up", "close installed, init installed", 1, 1);
    expect_log("0b the first Init was NOT intercepted (by design)", "sound engine started", 0, 0);
    expect_log("0c the rain-loop swap is installed (0.13)", "outdoor rain swap: installed", 1, 1);

    if (no_data) {
        /* N: an empty Data folder. Every build fails and says so once; the swap passes the game's own call
           through; nothing plays; nothing crashes. */
        expect_log("N1 nothing was built, and the log says so", "sounds built at start: 0 of 6", 1, 1);
        expect_log("N1b the scan names every archive missing", "patch.MPQ missing, patch-S.mpq missing, sound.MPQ missing", 1, 1);
        expect_log("N1c one line per rain source (the storm's wait)", "cannot build", 3, 3);
        *(DWORD *)(mgr + 0xAC) = 1;                                  /* no object list: the probe is off anyway */
        *mgr_ptr = (DWORD)(UINT_PTR)mgr; *guid = 0x1234; *weather_ptr = (DWORD)(UINT_PTR)weather;
        Sleep(4000);                                          expect("N2 in world, dry, outdoors (bridge connects)", 0);
        *sound_id = 8535; *wtype = 1; *intensity = 1.0f; *rainfx = 1;
        Sleep(1500);
        { void *st = FSOUND_Stream_Open("Sound\\Ambience\\Weather\\RainHeavyLoop.wav", 0x2002, 0, 0);
          printf("%-56s stream %s, expected NULL  %s\n", "N3 swap on, loop unbuilt: the game's own call passes", st ? "opened" : "NULL", st ? "FAIL" : "ok");
          if (st) { fails++; FSOUND_Stream_Close(st); }
          Sleep(700);
          expect_log("N3b and the worker says why", "heavy outdoor loop is not built, so the game plays its own", 1, 1); }
        strcpy(cv_indoors, "1");
        Sleep(2500);                                          expect("N4 heavy rain indoors: nothing to play", 0);
        expect_log("N4b the indoor loop's failure is logged once", "could not open indoor_rain_heavy", 1, 1);
        /* N5g, N5h: an archive another program holds. From here the harness holds a patch-S.mpq of its own with no
           sharing, as a program writing it would; it appears only now, so the scan and the start still found the
           folder empty (N1b, N1c). The storm's builds then meet a real sharing violation in the DLL's open: its
           Windows error is logged once, and the close thunder and the gusts, which patch-S.mpq holds, wait for a
           retry a minute later rather than fail for the session or come from sound.MPQ's older crack. */
        HANDLE held = CreateFileA("no-data\\patch-S.mpq", GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (held == INVALID_HANDLE_VALUE) printf("could not make no-data\\patch-S.mpq to hold (error %lu)\n", GetLastError());
        ini_set("storm_rate", "1000"); strcpy(cv_storms, "1"); strcpy(cv_indoors, "0");
        Sleep(8000);                                          expect("N5 a storm with nothing built plays nothing", 0);
        expect_log("N5b strikes still land (the flash is the addon's)", "storm: strike", 1, 1000);
        expect_log("N5c no thunder played", "storm: thunder", 0, 0);
        expect_log("N5d no gust played", "storm: gust", 0, 0);
        expect_log("N5e the missing thunder is logged once", "storm: could not open", 1, 1);
        expect_log("N5f the storm's 8 sources, each logged once", "cannot build", 11, 11);
        expect_log("N5g an archive another program holds: its error once", "cannot open patch-S.mpq in the Data folder just now (Windows error 32)", 1, 1);
        expect_log("N5h what it holds waits a minute (close, 3 gusts)", "its archive could not be read just now", 4, 4);
        if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
        DeleteFileA("no-data\\patch-S.mpq");   /* so a retry, a minute on, finds no such archive */
        strcpy(cv_storms, "0"); ini_set("storm_rate", "100");
        *wtype = 2; *sound_id = 8537; *rainfx = 0; *(DWORD *)(weather + 0x2C) = 1; strcpy(cv_indoors, "1");
        Sleep(3500);                                          expect("N6 snow indoors: its loop cannot be built either", 0);
        expect_log("N6b and that is logged once", "cannot build indoor_snow_medium", 1, 1);
        FSOUND_Close();
        Sleep(1500);
        expect_log("N7 the engine closes under a DLL with nothing built", "FSOUND_Close intercepted", 1, 1);
        printf("%-56s still alive  ok\n", "N8 no crash");
        goto end;
    }
    /* Which client: the DLL's scan line names patch-S.mpq with its size when it is there. RavenCraft's has it, and
       with it the heavy rain splice and the gusts; a plain 1.12.1 client has neither (checks 19, 20c, 20i). */
    { char scan[1024]; log_first("sound sources:", scan, sizeof scan); raven = strstr(scan, "patch-S.mpq (") != NULL;
      printf("   client: %s\n", raven ? "RavenCraft's (patch-S.mpq in the scan): heavy rain 45.5 s, three gusts"
                                       : "a plain 1.12.1 one (no patch-S.mpq in the scan): heavy rain 23.5 s, no gusts"); }
    /* 0f and 0f2 need only the archives and the DLL's own code. Since the 0.14 review nothing is built that cannot
       play: the storm needs the addon connected, so its thunder and gusts wait for its switch (20h, 20i). */
    expect_log("0f the outdoor rain loops were built", "built outdoor_rain_", 3, 3);
    expect_log("0f2 the indoor rain loops were built", "built indoor_rain_", 3, 3);
    /* 0f4: each built at the rate of the mixer, FSOUND_Init(44100) above, so FMOD steps one frame per frame: it holds
       each frame of a slower stereo voice instead of interpolating (the second 0.14 review). The text ends in the
       colon only the line for a rate read from FMOD has there: 44,100 Hz is also the DLL's default, and the line it
       writes when it gets no rate says the rate is not known (the third 0.14 review: the needle matched that one too).
       20j and 20k show the rate is read, not assumed: the engine comes back at 48,000 Hz at check 17. */
    expect_log("0f4 the DLL reads the mixer's rate from FMOD", "the game's mixer runs at 44100 Hz: each", 1, 1);
    expect_log("0f5 and builds the six rain loops at it", "frames at 44100 Hz, 2 ch", 6, 6);
    expect_log("0f3 no thunder before the storm comes on", "built thunder_", 0, 0);
    expect_log("0g no gust before the storm comes on", "built gust_", 0, 0);
    expect_log("0h nothing failed to build: 6 of 6", "sounds built at start: 6 of 6", 1, 1);

    Sleep(700);                                               expect("1 no world", 0);
    *mgr_ptr = (DWORD)(UINT_PTR)mgr; *guid = 0x1234; *weather_ptr = (DWORD)(UINT_PTR)weather;
    Sleep(4000);                                              expect("2 in world, dry, outdoors (bridge connects)", 0);
    *sound_id = 8534; *wtype = 1; *intensity = 0.55f; *rainfx = 1;
    Sleep(1500);                                              expect("3 raining, addon says outdoors", 0);
    /* The DLL's one write into the client: digits in place in IndoorRain_Storm, the rain level and the echo. */
    { const char *want = "IRS1:k0:l2:n420:d00"; int ok = strcmp(cv_storm, want) == 0;
      printf("%-56s \"%s\", expected \"%s\"  %s\n", "3b the DLL answers session 42 with rain level 2", cv_storm, want, ok ? "ok" : "FAIL");
      if (!ok) fails++; }
    strcpy(cv_indoors, "1");
    Sleep(2500);                                              expect("4 addon says indoors (play)", 1);
    strcpy(cv_enabled, "0");
    Sleep(2500);                                              expect("5 /indoorrain off (stop)", 0);
    /* 5b, 5c: off means off. The storm switched on at ten times the rate, outdoors in the rain, while the mod
       is off: no strike, nothing playing, and the game's rain loop is its own (not in this folder: NULL). */
    { int strikes = log_count("storm: strike"), n, playing; void *st;
      ini_set("storm_rate", "1000"); strcpy(cv_storms, "1"); strcpy(cv_indoors, "0");
      Sleep(8000);
      n = log_count("storm: strike") - strikes; playing = FSOUND_GetChannelsPlaying();
      printf("%-56s strikes %d, channels %d, expected 0 and 0  %s\n", "5b off: storm on at storm_rate 1000, nothing plays", n, playing, (n == 0 && playing == 0) ? "ok" : "FAIL");
      if (n != 0 || playing != 0) fails++;
      expect_log("5b2 nothing of the storm built while off", "built thunder_", 0, 0);
      st = FSOUND_Stream_Open("Sound\\Ambience\\Weather\\RainHeavyLoop.wav", 0x2002, 0, 0);
      printf("%-56s stream %s, expected NULL  %s\n", "5c off: the game's rain loop is not swapped", st ? "opened" : "NULL", st ? "FAIL" : "ok");
      if (st) { fails++; FSOUND_Stream_Close(st); }
      strcpy(cv_storms, "0"); strcpy(cv_indoors, "1"); ini_set("storm_rate", "100");
      Sleep(2500); }
    strcpy(cv_enabled, "1");
    Sleep(2500);                                              expect("6 /indoorrain on (play)", 1);
    strcpy(cv_indoors, "0");
    Sleep(2500);                                              expect("7 stepped outside (stop)", 0);
    strcpy(cv_indoors, "1"); strcpy(cv_volume, "30");
    Sleep(2500);                                              expect("8 back inside at volume 30 (play)", 1);
    *sound_id = 8535;
    Sleep(3500);                                              expect("9 rain turned heavy (other file, restarted)", 1);
    /* Indoors plays at volume% of the weather outside: 255 x the track's gain x AmbienceVolume x volume. */
    expect_log("9b heavy rain at 30% plays at 53/255", "volume: rain heavy at 53/255 (ambience 100%, volume 30%", 1, 1);
    strcpy(cv_ambience, "0.5");
    Sleep(1500);                                              expect_log("9c the ambience slider scales it", "volume: rain heavy at 27/255 (ambience 50%, volume 30%", 1, 1);
    strcpy(cv_master, "0.49999998");
    Sleep(1500);                                              expect("9d master is the engine's, not ours (still playing)", 1);
    expect_log("9d2 so the level did not change", "volume: rain heavy at 27/255 (ambience 50%, volume 30%", 1, 1);
    strcpy(cv_master, "0");
    Sleep(2500);                                              expect("9d3 master at zero: no loop at all (stop)", 0);
    strcpy(cv_master, "1");
    Sleep(2500);                                              expect("9d4 master back (play)", 1);
    strcpy(cv_enable_ambience, "0");
    Sleep(2500);                                              expect("9d5 Ambient Sounds off: the storm outside is silent (stop)", 0);
    strcpy(cv_enable_ambience, "1");
    Sleep(2500);                                              expect("9d6 Ambient Sounds back on (play)", 1);
    strcpy(cv_ambience, "0");
    Sleep(2500);                                              expect("9e ambience at zero: no loop at all (stop)", 0);
    strcpy(cv_ambience, "1");
    Sleep(2500);                                              expect("9f slider back up (play)", 1);
    expect_log("9g back at 53/255", "volume: rain heavy at 53/255 (ambience 100%, volume 30%", 2, 2);
    { int before = log_count("stream started"); strcpy(cv_volume, "0");
      Sleep(3000);                                            expect("10 volume 0 while playing (stop, no churn)", 0);
      int after = log_count("stream started");
      printf("%-56s starts during volume 0: %d, expected 0  %s\n", "10b no start/stop churn at volume 0", after - before, after - before == 0 ? "ok" : "FAIL");
      if (after - before != 0) fails++; }
    strcpy(cv_volume, "60");
    Sleep(2500);                                              expect("11 volume back to 60 (play)", 1);
    *sound_id = 0; *wtype = 0; *intensity = 0.0f; *rainfx = 0;
    Sleep(2500);                                              expect("12 rain stopped while indoors (stop)", 0);
    /* What a real storm looked like in Goldshire, 27 Sep: the type field reads -1 while sound 8535 plays. */
    { int before = log_count("stream started: rain heavy");
      *wtype = -1; *sound_id = 8535; *intensity = 1.0f; *rainfx = 0;
      Sleep(3500);                                            expect("12b real storm: type -1, sound 8535 (play)", 1);
      int started = log_count("stream started: rain heavy") - before;
      printf("%-56s heavy rain starts: %d, expected 1  %s\n", "12c it is the heavy rain track", started, started == 1 ? "ok" : "FAIL");
      if (started != 1) fails++; }
    *wtype = 2; *(DWORD *)(weather + 0x2C) = 0;   /* no snow effect object: the type vetoes the rain id and the fallback has nothing */
    *sound_id = 8533;                             /* light rain id (written after the type) against heavy intensity: 12d3 shows the fallback's level does not come from the id */
    Sleep(3500);                                              expect("12d type names snow, sound says rain (stop)", 0);
    { int before = log_count("stream started: snow heavy");
      *(DWORD *)(weather + 0x2C) = 1;                         /* the snow effect object appears: the fallback plays the type's weather */
      Sleep(3500);                                            expect("12d2 type snow with its effect: the type decides (play)", 1);
      int started = log_count("stream started: snow heavy") - before;
      printf("%-56s heavy snow starts: %d, expected 1  %s\n", "12d3 it is the heavy snow track", started, started == 1 ? "ok" : "FAIL");
      if (started != 1) fails++; }
    *intensity = 0.0f; *wtype = -1; *(DWORD *)(weather + 0x2C) = 0;
    Sleep(3500);                                              expect("12e rain sound left over, no intensity (stop)", 0);
    *wtype = -1; *sound_id = 0; Sleep(300); *intensity = 1.0f;   /* a full poll between: no read can pair the old id with the new intensity */
    Sleep(3500);                                              expect("12f storm over: sound 0 (stays silent)", 0);
    *wtype = 0; *intensity = 0.0f;
    *wtype = 2; *sound_id = 8537; *rainfx = 0; *(DWORD *)(weather + 0x2C) = 1;
    Sleep(3500);                                              expect("13 snow indoors (plays the snow track)", 1);
    expect_log("13b it is the snow track", "stream started: snow medium", 1, 1);
    *wtype = 3; *sound_id = 8558; *(DWORD *)(weather + 0x2C) = 0; *(DWORD *)(weather + 0x30) = 1;
    Sleep(3500);                                              expect("13c sandstorm indoors (plays the sand track)", 1);
    expect_log("13d it is the heavy sand track", "stream started: sand heavy", 1, 1);
    *wtype = 2; *sound_id = 8536; *(DWORD *)(weather + 0x30) = 0; *(DWORD *)(weather + 0x2C) = 1; *intensity = 0.3f;
    Sleep(3500);                                              expect("13e light snow (other file, restarted)", 1);
    /* 0.14: snow and sand are built the first time they are wanted (12d2 heavy snow, 13 medium, 13c heavy sand, 13e light snow) */
    expect_log("13f each snow and sand loop built when first wanted", "built indoor_s", 4, 4);
    *(DWORD *)(weather + 0x2C) = 0;
    *wtype = 1; *sound_id = 8533; *rainfx = 1; *intensity = 0.3f;
    Sleep(3500);                                              expect("14 light rain indoors (play)", 1);
    expect_log("14b light rain has its own track now", "stream started: rain light", 1, 1);
    expect_log("14c light rain plays far quieter than heavy", "volume: rain light at 21/255 (ambience 100%, volume 60%", 1, 1);
    *guid = 0;
    Sleep(2500);                                              expect("15 logged out (stop)", 0);
    *guid = 0x1234;
    Sleep(2500);                                              expect("16 back in the rain, indoors (play)", 1);
    FSOUND_Close();   /* through the import table: the DLL's redirected slot must run first */
    Sleep(1500);
    expect_log("16b close intercepted through the import table", "FSOUND_Close intercepted", 1, 1);
    expect_log("16c stream dropped, not closed twice", "dropping the stream", 1, 1);
    printf("%-56s still alive after the engine closed under the stream  ok\n", "16d no crash");
    /* Close then Init, at another rate: a restart WoW 1.12.1 itself never makes (it starts its engine once, at launch,
       and closes it once, at exit, so SoundMixRate takes effect at the next launch; the fourth 0.14 review, from its
       WoW.exe), made here to prove that the DLL takes the rate from FMOD, not from an assumption: the storm's sounds,
       first built after it, must be built for 48,000 Hz (20j, 20k). */
    FSOUND_SetOutput(0); FSOUND_Init(48000, 12, 0);
    Sleep(3000);                                              expect("17 engine re-initialised: rain resumes (play)", 1);
    expect_log("17b the re-init was intercepted", "sound engine started", 1, 1);

    /* 19: the game asks for its own rain loops; the swap hands it IndoorRain's outdoor loops, built in memory. The
       client's files are not in this folder, so a stream coming back at all is the swap, and its length says it
       is the built loop: 20 s light, 30 s medium, 45.5 s heavy (RavenCraft's splice). A poll between each, so the
       worker logs each one. */
    { static const char *const lvl[3] = { "Light", "Medium", "Heavy" }; int want_ms[3] = { 20000, 30000, 45500 };
      char name[96], stage[64]; int i;
      if (!raven) want_ms[2] = 23500;   /* patch.MPQ's 25 s heavy loop, 1.5 s of it crossfaded into its start */
      for (i = 0; i < 3; i++) {
          void *st; int ms = -1, ok;
          sprintf(name, "Sound\\Ambience\\Weather\\Rain%sLoop.wav", lvl[i]);
          st = FSOUND_Stream_Open(name, 0x2002, 0, 0);
          if (st) { ms = FSOUND_Stream_GetLengthMs(st); FSOUND_Stream_Close(st); }
          ok = st && ms >= want_ms[i] - 50 && ms <= want_ms[i] + 50;
          sprintf(stage, "19 the game's %s rain loop opens as the built one", lvl[i]);
          printf("%-56s stream %s, %d ms, expected %d  %s\n", stage, st ? "opened" : "NULL", ms, want_ms[i], ok ? "ok" : "FAIL");
          if (!ok) fails++;
          Sleep(700);
      }
      void *st = FSOUND_Stream_Open("Sound\\Ambience\\Weather\\SnowLightLoop.wav", 0x2002, 0, 0);
      printf("%-56s stream %s, expected NULL  %s\n", "19b any other file goes through untouched", st ? "opened" : "NULL", st ? "FAIL" : "ok");
      if (st) { fails++; FSOUND_Stream_Close(st); }
      expect_log("19c the swaps are logged by the worker", "the game's heavy rain loop is IndoorRain's outdoor loop", 1, 1);
      expect_log("19d (light and medium too)", "rain loop is IndoorRain's outdoor loop", 3, 3); }
    /* 20: a heavy storm outdoors at ten times the rate, the addon's storm switch on: strikes, thunder and gusts. */
    { int d0 = cv_storm[14] - '0', s0 = log_count("storm: strike");
      ini_set("storm_rate", "1000"); strcpy(cv_storms, "1");
      *wtype = 1; *sound_id = 8535; *rainfx = 1; *intensity = 1.0f; strcpy(cv_indoors, "0");
      Sleep(12000);
      expect_log("20 strikes in a heavy storm", "storm: strike", 2, 1000);
      expect_log("20b thunder played outdoors", "storm: thunder", 1, 1000);
      if (raven) expect_log("20c gusts played outdoors", "storm: gust", 1, 1000);
      else expect_log("20c no gust: this client has none to build", "storm: gust", 0, 0);
      expect_log("20d every storm sound opened", "could not open", 0, 0);
      expect_log("20h the ten thunders built as the storm came on", "built thunder_", 10, 10);
      if (raven) expect_log("20i each gust decoded through fmod, built then", "built gust_", 3, 3);
      else expect_log("20i no gust built: this client has none", "built gust_", 0, 0);
      /* the engine came back at 48,000 Hz at check 17, after the rain loops were built at 44,100 */
      expect_log("20j the engine's new rate read from FMOD, logged once", "the game's mixer now runs at 48000 Hz:", 1, 1);
      expect_log("20k and the storm's sounds built at it (mono)", "frames at 48000 Hz, 1 ch", raven ? 13 : 10, raven ? 13 : 10);
      strcpy(cv_indoors, "1"); Sleep(12000);
      expect_log("20e indoors the thunder is the muffled one", "(indoors, muffled)", 1, 1000);
      ini_set("storm_rate", "100");
      Sleep(2500);                                            expect("20f heavy rain indoors again (play)", 1);
      /* 20g: every strike reached the addon. The storm is switched off first, so no strike lands between
         reading the digit and counting the log lines; the digit counts strikes modulo 10. */
      strcpy(cv_storms, "0"); Sleep(1000);
      { int n = log_count("storm: strike") - s0, d1 = cv_storm[14] - '0', dark = log_count("no lightning");
        int ok = n >= 2 && d1 == (d0 + n) % 10 && dark == 0 && strlen(cv_storm) == 19 && strncmp(cv_storm, "IRS1:k", 6) == 0;
        printf("%-56s digit %d -> %d after %d strikes, %d without lightning  %s\n", "20g the strike digit counts every strike", d0, d1, n, dark, ok ? "ok" : "FAIL");
        if (!ok) fails++; } }

    /* 0d: the inn has been in memory since check 2, and the default ini keeps the probe off. */
    expect_log("0d a default ini runs no building probe", "building:", 0, 0);
    /* 21: the probe, turned on in the ini as a tester would, names the inn; 21b: stepping out. */
    { ini_set("building_probe", "1");
      Sleep(3000);
      expect_log("21 the building probe names the inn", "building: World\\wmo\\Azeroth\\Buildings\\GoldshireInn\\GoldshireInn.wmo (model 53, name set 0, group 1932)", 1, 1);
      *(DWORD *)(def + 0x08) = 0;                                      /* stepped out: no group contains you */
      Sleep(1500);
      expect_log("21b stepping out is logged", "building: none", 1, 1000);
      *(DWORD *)(mgr + 0xAC) = 1; }                                    /* empty list again for what follows */

    /* 18: the busy inn. Twelve channels, all wanted by top-priority game sounds while the loop plays. */
    { int before_cut = log_count("cut off"); int ch = last_logged_channel(); int i, played = 0, voice[14];
      void *smp;
      flood_make();
      smp = FSOUND_Sample_Load(-1, (const char *)flood_wav, 0x2002 | 0x8000, 0, (int)sizeof flood_wav);   /* FSOUND_LOADMEMORY */
      if (!smp) {
        printf("%-56s could not load the synthetic flood sample  FAIL\n", "18 flood setup"); fails++;
        printf("%-56s not run, they need the flood  FAIL\n", "18b 22 23 24 24b"); fails += 5;
      }
      else {
        FSOUND_Sample_SetDefaults(smp, -1, -1, -1, 255);
        for (i = 0; i < 14; i++) if ((voice[i] = FSOUND_PlaySound(-1, smp)) >= 0) played++;
        Sleep(1500);
        printf("%-56s reserved=%d playing=%d flood voices=%d  %s\n", "18 loop survives a full mixer (reserved channel)", ch >= 0 ? FSOUND_GetReserved(ch) : -1, ch >= 0 ? FSOUND_IsPlaying(ch) : -1, played, (ch >= 0 && FSOUND_IsPlaying(ch) && FSOUND_GetReserved(ch)) ? "ok" : "FAIL");
        if (!(ch >= 0 && FSOUND_IsPlaying(ch) && FSOUND_GetReserved(ch))) fails++;
        expect_log("18b no cut-off retry was needed", "cut off", before_cut, before_cut);

        /* 22: the thunder yields. The mixer full again, now of game sounds at priority 1: a test strike's
           thunder may take none of their channels. At the old priority 200 it took one. */
        for (i = 0; i < 14; i++) if (voice[i] >= 0) FSOUND_StopSound(voice[i]);
        Sleep(300);
        FSOUND_Sample_SetDefaults(smp, -1, -1, -1, 1);
        for (i = 0; i < 14; i++) voice[i] = FSOUND_PlaySound(-1, smp);
        Sleep(500);
        { int busy = FSOUND_GetChannelsPlaying(), alive = 0, still = 0, tests = log_count("storm: test strike"), heard = log_count("storm: thunder"), t, th, ok;
          for (i = 0; i < 14; i++) if (voice[i] >= 0 && FSOUND_IsPlaying(voice[i])) alive++;
          strcpy(cv_thunder, "1");
          Sleep(6000);
          for (i = 0; i < 14; i++) if (voice[i] >= 0 && FSOUND_IsPlaying(voice[i])) still++;
          t = log_count("storm: test strike") - tests; th = log_count("storm: thunder") - heard;
          ok = busy == 12 && t == 1 && th == 0 && still == alive;
          printf("%-56s mixer %d/12, test strikes %d, thunders played %d, game voices %d -> %d  %s\n", "22 a thunder yields to game sounds on a full mixer", busy, t, th, alive, still, ok ? "ok" : "FAIL");
          if (!ok) fails++; }

        /* 23: a value of the wrong shape (one digit short) is never written, whatever the DLL has to say:
           a new session to answer and a test strike. */
        { char before[64]; int bad = log_count("does not hold the template"), same, said;
          storm_reset("IRS1:k0:l0:n000:d0"); memcpy(before, cv_storm, 64);
          strcpy(cv_session, "43"); strcpy(cv_thunder, "2");
          Sleep(6000);
          same = memcmp(before, cv_storm, 64) == 0; said = log_count("does not hold the template") - bad;
          printf("%-56s \"%s\" %s, refusals logged %d  %s\n", "23 a value of the wrong shape stays byte-identical", cv_storm, same ? "unchanged" : "CHANGED", said, (same && said >= 1) ? "ok" : "FAIL");
          if (!(same && said >= 1)) fails++; }

        /* 24: a read-only page is never written, though the value has the right shape. 24b: writable again,
           the answer lands. */
        { char before[64]; DWORD old; int refused = log_count("the write into the game was refused"), same, said, ok;
          strcpy(cv_session, "42"); Sleep(600);           /* 42 is answered already: nothing left to write */
          storm_reset(STORM_TEMPLATE);
          VirtualProtect(cv_storm, 0x1000, PAGE_READONLY, &old);
          memcpy(before, cv_storm, 64);
          strcpy(cv_session, "44"); strcpy(cv_thunder, "3");
          Sleep(6000);
          same = memcmp(before, cv_storm, 64) == 0; said = log_count("the write into the game was refused") - refused;
          printf("%-56s \"%s\" %s, refusals logged %d  %s\n", "24 a read-only page is refused", cv_storm, same ? "unchanged" : "CHANGED", said, (same && said >= 1) ? "ok" : "FAIL");
          if (!(same && said >= 1)) fails++;
          VirtualProtect(cv_storm, 0x1000, PAGE_READWRITE, &old);
          Sleep(1500);
          ok = strncmp(cv_storm + 11, "n44", 3) == 0;
          printf("%-56s \"%s\", expected n44  %s\n", "24b writable again: the DLL answers session 44", cv_storm, ok ? "ok" : "FAIL");
          if (!ok) fails++; }
      }
    }
end:
    /* 25: FMOD's file callbacks. The control: the named open right after they were set reached them on this
       thread. After that, none may have run on any other: the DLL builds from its own reads of the archives and
       opens every sound from memory, and FMOD's stream thread only ever read memory streams here. */
    { char line[1024], *p; int worker = -1, ok = cb_control > 0 && cb_other == 0;
      log_first("(worker thread ", line, sizeof line);
      if ((p = strstr(line, "(worker thread "))) worker = atoi(p + 15);
      printf("%-56s control %d, this thread %ld, others %ld (last %ld; the worker is %d)  %s\n", "25 no FMOD file callback off the game's thread",
             cb_control, (long)cb_main, (long)cb_other, (long)cb_other_tid, worker, ok ? "ok" : "FAIL");
      if (!ok) fails++; }
    printf("%d check(s) failed\n", fails);
    ExitProcess((UINT)fails);
}
