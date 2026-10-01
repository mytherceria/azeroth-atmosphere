/* IndoorRain.dll: muffled rain, snow and sandstorm while you are under a roof, for the WoW 1.12.1 client.
 *
 * Loaded by VanillaFixes from dlls.txt. Every poll it reads the client's weather state and
 * the player's whereabouts from memory and plays a looping muffled weather stream through the
 * client's own fmod.dll, fading in when there is weather and you are indoors and fading out
 * the moment either stops. Whether you are indoors comes from the IndoorRain addon, which
 * runs on the game's own thread and writes the CVar IndoorRain_Indoors; the DLL does not
 * work that out from the client's object lists. One optional read does walk them: the
 * building probe (building_probe=1 in the ini, off by default) finds the player in the object
 * manager's list and reads which building model and group the player stands in, read-only,
 * and only logs it, as groundwork for the coming roof sounds by building. No detours, no
 * packet handlers, no Lua from this side.
 *
 * Storms (0.13): while it rains, thunder at random distances and times, wind gusts outdoors, and the
 * game's own outdoor rain loops swapped for louder ones with no thunder baked in. One import-table
 * slot (FSOUND_Stream_Open) is redirected for the swap; the thunder and gusts are short one-shot
 * streams. The DLL tells the addon about each strike through one fixed-width CVar it overwrites in
 * place (IndoorRain_Storm), so the addon can flash the sky before the thunder and thicken the fog.
 *
 * Sounds (0.14): the addon ships no sound files. Every sound is built in memory from the player's own
 * client files, read out of the MPQs in the Data folder next to WoW.exe (mpq.c, inflate.c) and run through
 * the recipes the 0.13 files were made with (sounds.c, dsp.c); the gusts, which are Ogg Vorbis, are
 * decoded by the client's own fmod.dll. See "the sounds, built from the player's own client files" below.
 *
 * Settings: CVars IndoorRain_Enabled and IndoorRain_Volume (set by the addon, saved by the
 * client), with IndoorRain.ini next to WoW.exe as the fallback and for the test switches.
 * Log: Logs\IndoorRain.log next to WoW.exe (kept under 1 MB).
 *
 * Built with i686-w64-mingw32-gcc, no C runtime; imports only KERNEL32 and USER32. GPL-3.0.
 */
#include <windows.h>
#include "addresses.h"
#include "mpq.h"
#include "sounds.h"

#define DLL_NAME     "IndoorRain"
#define DLL_VERSION  "0.14"
#define LOG_MAX_BYTES 1000000
#define START_RETRY_MS 10000

/* ---- FMOD 3.75 (the client's fmod.dll), resolved at run time by decorated name ---- */
typedef void *FSOUND_STREAM;
typedef FSOUND_STREAM *(__stdcall *fn_Stream_Open)(const char *name, unsigned int mode, int offset, int length);
typedef int   (__stdcall *fn_Stream_Play)(int channel, FSOUND_STREAM *stream);
typedef signed char (__stdcall *fn_Stream_Close)(FSOUND_STREAM *stream);
typedef signed char (__stdcall *fn_SetVolume)(int channel, int vol);
typedef signed char (__stdcall *fn_StopSound)(int channel);
typedef signed char (__stdcall *fn_IsPlaying)(int channel);
typedef float (__stdcall *fn_GetVersion)(void);
typedef int   (__stdcall *fn_Stream_PlayEx)(int channel, FSOUND_STREAM *stream, void *dsp, signed char startpaused);
typedef signed char (__stdcall *fn_SetPaused)(int channel, signed char paused);
typedef int   (__stdcall *fn_GetMaxChannels)(void);
typedef void *(__stdcall *fn_Stream_GetSample)(FSOUND_STREAM *stream);
typedef signed char (__stdcall *fn_Sample_SetDefaults)(void *sample, int deffreq, int defvol, int defpan, int defpri);
typedef signed char (__stdcall *fn_SetPriority)(int channel, int priority);
#define RAIN_CHANNEL_PRIORITY 255   /* the client runs a 12-channel mixer; a busy inn must not silence the rain */
#define STOLEN_RETRY_MS 1000          /* the game took our channel: try again soon, not in ten seconds */
#define STOLEN_STREAK_LIMIT 6         /* after this many quick deaths in a row, fall back to the long retry */
typedef signed char (__stdcall *fn_SetReserved)(int channel, signed char reserved);
typedef signed char (__stdcall *fn_SetPan)(int channel, int pan);
typedef signed char (__stdcall *fn_SetFrequency)(int channel, int freq);
typedef int   (__stdcall *fn_GetFrequency)(int channel);
/* 0.14: the gusts are Ogg Vorbis, and only fmod.dll can decode them: into an unmanaged sample whose PCM we copy out */
typedef void *(__stdcall *fn_Sample_Load)(int index, const char *name_or_data, unsigned int mode, int offset, int length);
typedef signed char (__stdcall *fn_Sample_Lock)(void *sample, int offset, int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2);
typedef signed char (__stdcall *fn_Sample_Unlock)(void *sample, void *ptr1, void *ptr2, unsigned int len1, unsigned int len2);
typedef unsigned int (__stdcall *fn_Sample_GetLength)(void *sample);   /* in frames */
typedef unsigned int (__stdcall *fn_Sample_GetMode)(void *sample);
typedef signed char (__stdcall *fn_Sample_GetDefaults)(void *sample, int *deffreq, int *defvol, int *defpan, int *defpri);
typedef void  (__stdcall *fn_Sample_Free)(void *sample);
typedef int   (__stdcall *fn_GetError)(void);
typedef int   (__stdcall *fn_GetOutputRate)(void);   /* the mixer's rate, which every built sound is made for (sounds.c) */
#define FSOUND_FREE          (-1)
#define FSOUND_UNMANAGED     (-2)
#define FSOUND_LOOP_OFF      0x00000001u
#define FSOUND_LOOP_NORMAL   0x00000002u
#define FSOUND_8BITS         0x00000008u
#define FSOUND_16BITS        0x00000010u
#define FSOUND_STEREO        0x00000040u
#define FSOUND_2D            0x00002000u
#define FSOUND_LOADMEMORY    0x00008000u
#define FSOUND_LOADRAW       0x00010000u
#define FSOUND_IMAADPCM      0x00400000u

static struct {
    fn_Stream_Open Stream_Open; fn_Stream_Play Stream_Play; fn_Stream_Close Stream_Close;
    fn_SetVolume SetVolume; fn_StopSound StopSound; fn_IsPlaying IsPlaying; fn_GetVersion GetVersion;
    fn_Stream_PlayEx Stream_PlayEx; fn_SetPaused SetPaused; fn_GetMaxChannels GetMaxChannels;
    fn_Stream_GetSample Stream_GetSample; fn_Sample_SetDefaults Sample_SetDefaults; fn_SetPriority SetPriority;
    fn_SetReserved SetReserved; fn_SetPan SetPan; fn_SetFrequency SetFrequency; fn_GetFrequency GetFrequency;
    /* optional: without them there are no gusts, and everything else still plays */
    fn_Sample_Load Sample_Load; fn_Sample_Lock Sample_Lock; fn_Sample_Unlock Sample_Unlock; fn_Sample_GetLength Sample_GetLength;
    fn_Sample_GetMode Sample_GetMode; fn_Sample_GetDefaults Sample_GetDefaults; fn_Sample_Free Sample_Free; fn_GetError GetError;
    fn_GetOutputRate GetOutputRate;   /* without it the sounds are built for 44,100 Hz, the game's default */
} F;


static void *proc(HMODULE h, const char *name) { return (void *)GetProcAddress(h, name); }
static int fmod_alive(void);

/* CVar* __fastcall Lookup(const char* name): name in ecx, edx unused */
typedef void *(__fastcall *fn_cvar_lookup)(const char *name, void *unused);

/* ---- settings ---- */
static struct {
    int enabled, volume, fade_ms, poll_ms;
    int force_indoors;   /* 1 = always indoors, -1 = never, 0 = ask the addon's CVar */
    int force_weather;   /* 0 = read the client; 1 rain, 2 snow, 3 sand = pretend that weather (medium) */
    char track[4][4][MAX_PATH];   /* [kind 1..3][level 1..3] */
    int track_id[4][4];  /* the built sound (SND_*) each track names, or -1 for a custom file (0.14, load_ini) */
    int storm;           /* thunder and gusts while it rains (the addon's IndoorRain_Storms overrides it) */
    int outdoor_rain;    /* hand the game our louder, thunder-free loops for its outdoor rain */
    int thunder_volume, gust_volume;   /* percent */
    int storm_rate;      /* percent: 200 = strikes and gusts twice as often */
    int building_probe;  /* log the building you stand in (0.13, off by default: proves the chain before the roof sound uses it) */
} cfg;
static volatile LONG g_outdoor_on = 0;   /* outdoor_rain with the mod on, set by the worker every poll; read on the game's thread */
static const char *KIND_NAME[4] = { "none", "rain", "snow", "sand" };
static const char *LEVEL_NAME[4] = { "", "light", "medium", "heavy" };

static char g_gamedir[MAX_PATH], g_ini[MAX_PATH], g_log[MAX_PATH];
static WCHAR g_gamedirW[MAX_PATH];   /* the same folder in UTF-16: every sound is read through it (sound_bytes) */
static WCHAR g_dataW[MAX_PATH * 2];  /* the client's Data folder, ending in a backslash: the archives the sounds are built from */
static volatile LONG g_stop = 0;

/* ---- tiny helpers, no CRT ---- */
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scat(char *d, int cap, const char *s) { int n = slen(d), i = 0; while (s[i] && n + 1 < cap) d[n++] = s[i++]; d[n] = 0; }
static void scpy(char *d, int cap, const char *s) { d[0] = 0; scat(d, cap, s); }
static int satoi(const char *s, int dflt)
{
    int i = 0, neg = 0, v = 0, any = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (s[i] == '-') { neg = 1; i++; } else if (s[i] == '+') i++;
    while (s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); any = 1; i++; if (v > 1000000) break; }
    if (!any) return dflt;
    return neg ? -v : v;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
#if HARNESS_CVARS
static int seq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
#endif
/* "0.69999998807907" -> 700: a 0..1 slider value in thousandths, rounded; dflt when unreadable */
static int spermille(const char *s, int dflt)
{
    int i = 0, whole = 0, frac = 0, scale = 1000, any = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (s[i] == '-') return 0;
    if (s[i] == '+') i++;
    while (s[i] >= '0' && s[i] <= '9') { whole = whole * 10 + (s[i] - '0'); any = 1; i++; if (whole > 10) break; }
    if (s[i] == '.') {
        i++;
        while (s[i] >= '0' && s[i] <= '9' && scale > 1) { scale /= 10; frac += (s[i] - '0') * scale; any = 1; i++; }
        if (s[i] >= '5' && s[i] <= '9') frac++;   /* round on the fourth digit */
        while (s[i] >= '0' && s[i] <= '9') i++;
    }
    if (!any) return dflt;
    /* "1e-05": the client writes a slider below 0.0001 in exponent form; that is zero for us */
    if (s[i] == 'e' || s[i] == 'E') return s[i + 1] == '-' ? 0 : 1000;
    return clampi(whole * 1000 + frac, 0, 1000);
}

static void logf_(const char *fmt, ...)
{
    char line[1024], stamp[48];
    SYSTEMTIME t; GetLocalTime(&t);
    wsprintfA(stamp, "%04d-%02d-%02d %02d:%02d:%02d.%03d [%u] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, (unsigned)GetCurrentThreadId());
    va_list ap; va_start(ap, fmt); wvsprintfA(line, fmt, ap); va_end(ap);
    HANDLE h = CreateFileA(g_log, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    if (GetFileSize(h, NULL) > LOG_MAX_BYTES) { CloseHandle(h); return; }   /* never grow past the cap */
    DWORD w;
    WriteFile(h, stamp, (DWORD)slen(stamp), &w, NULL);
    WriteFile(h, line, (DWORD)slen(line), &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

/* Read client memory only when the whole range sits in one committed, readable region. */
static int readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (VirtualQuery(p, &mbi, sizeof mbi) != sizeof mbi) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if (mbi.Protect == PAGE_EXECUTE) return 0;
    if ((const char *)p + n > (const char *)mbi.BaseAddress + mbi.RegionSize) return 0;
    return 1;
}
/* readable() is only a cheap filter: the client's allocator can release a region between that check and a
 * plain load, and nothing here catches the fault (found in the 0.13 re-review; every read since 0.8 did it).
 * So each read is a copy through ReadProcessMemory on our own process, which copies or fails cleanly. */
static int safe_copy(DWORD addr, void *out, SIZE_T n)
{
    SIZE_T got = 0;
    if (!addr || !readable((const void *)(UINT_PTR)addr, n)) return 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(UINT_PTR)addr, out, n, &got) && got == n;
}
static int read32(DWORD addr, DWORD *out) { return safe_copy(addr, out, 4); }
static int read8(DWORD addr, BYTE *out)   { return safe_copy(addr, out, 1); }
static int readf(DWORD addr, float *out)  { return safe_copy(addr, out, 4); }
/* Copy a short NUL-terminated string out of client memory: 16 bytes at a time, byte by byte where a
   16-byte copy would run off the end of a readable region. */
static int readstr(DWORD addr, char *out, int cap)
{
    int i = 0, j;
    while (i + 1 < cap) {
        int n = cap - 1 - i < 16 ? cap - 1 - i : 16;
        if (!safe_copy(addr + (DWORD)i, out + i, (SIZE_T)n)) {
            for (j = 0; j < n; j++) if (!read8(addr + (DWORD)(i + j), (BYTE *)&out[i + j])) { out[0] = 0; return 0; } else if (!out[i + j]) return 1;
        }
        for (j = 0; j < n; j++) if (!out[i + j]) return 1;
        i += n;
    }
    out[i] = 0; return 1;
}

/* ---- settings from the ini ---- */
static void ini_default(const char *key, const char *def)
{
    char buf[8];
    if (!GetPrivateProfileStringA(DLL_NAME, key, "", buf, sizeof buf, g_ini)) WritePrivateProfileStringA(DLL_NAME, key, def, g_ini);
}
/* What a track key names (0.14). "builtin", the default, is the sound the DLL builds for that weather from the
   client's own files. The nine paths 0.10 to 0.13 wrote as defaults (Interface\AddOns\IndoorRain\indoor_<kind>_<level>.ogg)
   name files the addon no longer ships, so each still means the built sound it was made as, even if an old copy
   is still in the folder: every ini written before keeps working unedited, at the same level. Anything else is
   a custom file, played exactly as in 0.13 (sound_bytes). */
#define TRACK_BUILTIN "builtin"
static int indoor_sound(int kind, int level) { return SND_INDOOR_RAIN_LIGHT + (clampi(kind, 1, 3) - 1) * 3 + clampi(level, 1, 3) - 1; }
/* paths compared the way Windows compares them: ASCII case folded, '/' read as '\\' */
static int path_is(const char *a, const char *b)
{
    int i;
    for (i = 0; a[i] && b[i]; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32); else if (x == '/') x = '\\';
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32); else if (y == '/') y = '\\';
        if (x != y) return 0;
    }
    return a[i] == b[i];
}
static int track_builtin(int kind, int level, const char *v)
{
    char legacy[96]; int k, l;
    if (path_is(v, TRACK_BUILTIN)) return indoor_sound(kind, level);
    for (k = 1; k <= 3; k++) for (l = 1; l <= 3; l++) {
        wsprintfA(legacy, "Interface\\AddOns\\IndoorRain\\indoor_%s_%s.ogg", KIND_NAME[k], LEVEL_NAME[l]);
        if (path_is(v, legacy)) return indoor_sound(k, l);
    }
    return -1;
}
static void load_ini(void)
{
    int k, l; char key[32];
    ini_default("enabled", "1"); ini_default("volume", "70"); ini_default("fade_ms", "1000"); ini_default("poll_ms", "250");
    ini_default("force_indoors", "0"); ini_default("force_weather", "0");
    ini_default("storm", "1"); ini_default("outdoor_rain", "1"); ini_default("thunder_volume", "100");
    ini_default("gust_volume", "100"); ini_default("storm_rate", "100"); ini_default("building_probe", "0");
    for (k = 1; k <= 3; k++) for (l = 1; l <= 3; l++) {
        wsprintfA(key, "track_%s_%s", KIND_NAME[k], LEVEL_NAME[l]);
        ini_default(key, TRACK_BUILTIN);
    }
    cfg.enabled = GetPrivateProfileIntA(DLL_NAME, "enabled", 1, g_ini) ? 1 : 0;
    cfg.volume = clampi(GetPrivateProfileIntA(DLL_NAME, "volume", 70, g_ini), 0, 100);
    cfg.fade_ms = clampi(GetPrivateProfileIntA(DLL_NAME, "fade_ms", 1000, g_ini), 0, 10000);
    cfg.poll_ms = clampi(GetPrivateProfileIntA(DLL_NAME, "poll_ms", 250, g_ini), 50, 5000);
    cfg.force_indoors = clampi(GetPrivateProfileIntA(DLL_NAME, "force_indoors", 0, g_ini), -1, 1);
    cfg.force_weather = clampi(GetPrivateProfileIntA(DLL_NAME, "force_weather", 0, g_ini), 0, 3);
    if (!cfg.force_weather && GetPrivateProfileIntA(DLL_NAME, "force_rain", 0, g_ini)) cfg.force_weather = 1;   /* the 0.8 name still works */
    cfg.storm = GetPrivateProfileIntA(DLL_NAME, "storm", 1, g_ini) ? 1 : 0;
    cfg.outdoor_rain = GetPrivateProfileIntA(DLL_NAME, "outdoor_rain", 1, g_ini) ? 1 : 0;
    cfg.thunder_volume = clampi(GetPrivateProfileIntA(DLL_NAME, "thunder_volume", 100, g_ini), 0, 100);
    cfg.gust_volume = clampi(GetPrivateProfileIntA(DLL_NAME, "gust_volume", 100, g_ini), 0, 100);
    cfg.storm_rate = clampi(GetPrivateProfileIntA(DLL_NAME, "storm_rate", 100, g_ini), 25, 1000);
    cfg.building_probe = GetPrivateProfileIntA(DLL_NAME, "building_probe", 0, g_ini) ? 1 : 0;
    for (k = 1; k <= 3; k++) for (l = 1; l <= 3; l++) {
        wsprintfA(key, "track_%s_%s", KIND_NAME[k], LEVEL_NAME[l]);
        GetPrivateProfileStringA(DLL_NAME, key, "", cfg.track[k][l], MAX_PATH, g_ini);
        cfg.track_id[k][l] = cfg.track[k][l][0] ? track_builtin(k, l, cfg.track[k][l]) : -1;
    }
}
#if HARNESS_CVARS
/* Test switch, test build only: the harness's folder holds no client files, so it names a client's Data folder
   in the ini (data_dir), which this build reads, read-only, instead of the one next to its exe. Read once, at
   start. The release DLL has no such switch: it only ever reads the Data folder next to WoW.exe. */
static void data_dir_from_ini(void)
{
    char a[MAX_PATH]; WCHAR w[MAX_PATH + 2]; int n, i;
    if (!GetPrivateProfileStringA(DLL_NAME, "data_dir", "", a, sizeof a, g_ini)) return;
    n = MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);   /* counts the terminator */
    if (n < 2) return;
    if (w[n - 2] != L'\\' && w[n - 2] != L'/') { w[n - 1] = L'\\'; w[n] = 0; }
    for (i = 0; w[i]; i++) g_dataW[i] = w[i];
    g_dataW[i] = 0;
    logf_("data_dir (test switch): the sounds are built from the archives in %s", a);
}
#endif

/* ---- the CVar bridge ---- */
/* missing: the lookup has refused (the addon is not loaded) and no lookup has succeeded since; unlike
   refused, it is not cleared on leaving the world, so the game keeps its own rain loops meanwhile */
static struct { int ready; int refused; int missing; int head_bad; int sound_tried; DWORD enabled, volume, indoors, master, ambience, enable_ambience;
                DWORD storm, storms, session, thunder; } cv;

#if !HARNESS_CVARS
static int cvar_lookup_is_the_expected_code(void)
{
    static const BYTE head[CVAR_LOOKUP_HEAD_LEN] = CVAR_LOOKUP_HEAD_BYTES;
    int i;
    for (i = 0; i < CVAR_LOOKUP_HEAD_LEN; i++) { BYTE b; if (!read8(ADDR_CVAR_LOOKUP + (DWORD)i, &b) || b != head[i]) return 0; }
    return 1;
}
#endif
static DWORD cvar_find(const char *name)
{
#if HARNESS_CVARS
    /* harness: a table of char* at a fake address, in this order (harness.c fills it the same way) */
    static const char *const names[10] = { CVAR_ENABLED, CVAR_VOLUME, CVAR_INDOORS, CVAR_MASTER, CVAR_AMBIENCE, CVAR_ENABLE_AMBIENCE,
                                           CVAR_STORM, CVAR_STORMS, CVAR_SESSION, CVAR_THUNDER };
    int idx;
    for (idx = 0; idx < 10; idx++)
        if (seq(name, names[idx])) return HARNESS_CVARS + (DWORD)idx * 4u - OFF_CVAR_VALUE;   /* +OFF_CVAR_VALUE lands on the char* */
    return 0;
#else
    fn_cvar_lookup lookup = (fn_cvar_lookup)(UINT_PTR)ADDR_CVAR_LOOKUP;
    return (DWORD)(UINT_PTR)lookup(name, NULL);
#endif
}
static void cvar_connect(void)
{
    DWORD ready = 0;
    if (cv.ready || cv.refused || cv.head_bad) return;
#if !HARNESS_CVARS
    if (!cvar_lookup_is_the_expected_code()) { cv.head_bad = 1; logf_("CVar lookup at the expected address does not match this exe; settings come from the ini only"); return; }
    if (!read32(ADDR_CVAR_READY, &ready) || ready == 0xFFFFFFFFu) return;
#else
    (void)ready;
#endif
    if (!cv.sound_tried) {
        cv.sound_tried = 1;
        cv.master = cvar_find(CVAR_MASTER); cv.ambience = cvar_find(CVAR_AMBIENCE); cv.enable_ambience = cvar_find(CVAR_ENABLE_AMBIENCE);
        if (cv.master && cv.ambience && cv.enable_ambience) logf_("sound settings found: the loop follows AmbienceVolume and Ambient Sounds, and stops with MasterVolume at zero");
        else logf_("sound settings not found (MasterVolume %s, AmbienceVolume %s, EnableAmbience %s); a missing one is read as full",
                   cv.master ? "found" : "missing", cv.ambience ? "found" : "missing", cv.enable_ambience ? "found" : "missing");
    }
    cv.enabled = cvar_find(CVAR_ENABLED); cv.volume = cvar_find(CVAR_VOLUME); cv.indoors = cvar_find(CVAR_INDOORS);
    if (cv.enabled && cv.volume && cv.indoors) {
        cv.ready = 1; cv.missing = 0; logf_("CVar bridge connected (the IndoorRain addon is loaded)");
        cv.storm = cvar_find(CVAR_STORM); cv.storms = cvar_find(CVAR_STORMS); cv.session = cvar_find(CVAR_SESSION); cv.thunder = cvar_find(CVAR_THUNDER);
        if (cv.storm && cv.storms && cv.session && cv.thunder) logf_("storm CVars found: lightning, fog and the storm switch follow the addon");
        else logf_("storm CVars not all registered (an addon older than 0.5?): thunder follows the ini, no lightning or fog");
    }
    else { cv.refused = 1; cv.missing = 1; logf_("IndoorRain CVars not registered; is the IndoorRain addon enabled? Settings come from the ini only, and there is no storm without it"); }
}
/* -1 when the CVar cannot be read this poll */
static int cvar_int(DWORD cvar, int dflt)
{
    DWORD sp; char buf[16];
    if (!cvar || !read32(cvar + OFF_CVAR_VALUE, &sp) || !sp || !readstr(sp, buf, sizeof buf)) return dflt;
    return satoi(buf, dflt);
}
static int cvar_permille(DWORD cvar, int dflt)
{
    DWORD sp; char buf[24];
    if (!cvar || !read32(cvar + OFF_CVAR_VALUE, &sp) || !sp || !readstr(sp, buf, sizeof buf)) return dflt;
    return spermille(buf, dflt);
}

/* Indoors the loop plays at volume% of what the same weather sounds like outdoors. Outdoors the
   client plays each weather file at its SoundEntries.dbc volume (0.69 for all nine) times the
   AmbienceVolume slider, and nothing at all with Ambient Sounds off. MasterVolume is applied by
   the sound engine itself to every channel, ours included (FSOUND_SetSFXMasterVolume), so it is
   not part of the target: a master at zero only means there is no loop to play. The shipped loops are filtered copies whose loudness
   differs from the originals (the rain loops were loudness-normalised when they were made, so
   light rain came out as loud as heavy), so each track carries the gain that brings it back to
   its original: 10^(dB/20) * 0.69, where dB is the original's integrated loudness (EBU R128)
   minus the loop's, measured 27 Sep 2026 with ffmpeg ebur128 on the client's
   Sound\Ambience\Weather files and the addon's loops. */
static const float TRACK_GAIN[4][4] = {
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.1377f, 0.2654f, 0.6980f },   /* rain light, medium, heavy: -14.0, -8.3, +0.1 dB */
    { 0.0f, 0.7061f, 0.7566f, 0.7922f },   /* snow: +0.2, +0.8, +1.2 dB */
    { 0.0f, 0.7832f, 1.0687f, 0.9201f },   /* sand: +1.1, +3.8, +2.5 dB */
};
static int level_target(int kind, int level, int volume, int ambience_pm)
{
    float t;
    if (kind < 1 || kind > 3 || level < 1 || level > 3) return 0;
    t = 255.0f * TRACK_GAIN[kind][level] * ((float)ambience_pm / 1000.0f) * ((float)volume / 100.0f);
    return clampi((int)(t + 0.5f), 0, 255);
}

/* ---- client state ---- */
typedef struct { int in_world, kind, level, indoors, gate, type, intensity_pct, fx; DWORD sound_id; } state_t;
#define OFF_WEATHER_SNOW_FX 0x2Cu
#define OFF_WEATHER_SAND_FX 0x30u
#define SOUND_SNOW_LIGHT 8536u
#define SOUND_SNOW_HEAVY 8538u
#define SOUND_SAND_LIGHT 8556u
#define SOUND_SAND_HEAVY 8558u

static void read_state(state_t *s)
{
    DWORD mgr = 0, guid = 0, wptr = 0, sid = 0, type = 0, fx = 0; BYTE gate = 0; float inten = 0.0f;
    s->in_world = 0; s->kind = 0; s->level = 0; s->indoors = 0; s->gate = 0; s->type = -1; s->intensity_pct = 0; s->fx = 0; s->sound_id = 0;
    if (!read32(ADDR_OBJMGR_PTR, &mgr) || !mgr || !read32(mgr + OFF_OBJMGR_PLAYER_GUID, &guid) || !guid) return;
    if (!read32(ADDR_CMAPWEATHER_PTR, &wptr) || !wptr) return;
    s->in_world = 1;
    if (read8(ADDR_WEATHER_GATE, &gate)) s->gate = gate ? 1 : 0;
    if (read32(ADDR_WEATHER_SOUND_ID, &sid)) s->sound_id = sid;
    if (read32(wptr + OFF_WEATHER_TYPE, &type)) s->type = (int)type;
    if (s->type >= 1 && s->type <= 3 && read32(wptr + OFF_WEATHER_RAIN_FX + 4u * (DWORD)(s->type - 1), &fx)) s->fx = fx ? 1 : 0;
    if (readf(wptr + OFF_WEATHER_INTENSITY, &inten)) { if (!(inten >= 0.0f)) inten = 0.0f; if (inten > 1.0f) inten = 1.0f; s->intensity_pct = (int)(inten * 100.0f + 0.5f); }
    /* The weather sound id the client holds decides; it stays set under a roof although the client mutes
       the sound there. A type field at +0x20 naming the same weather is enough, but in game it did so only
       on the first poll: in a real heavy storm in Goldshire (27 Sep) it read 1 once and then -1 while sound
       8535 kept playing outdoors, so a type naming no weather lets the id count while the storm has
       intensity. A type naming another weather overrides the id, and the fallback below decides. */
    int sid_kind = 0, sid_level = 0;
    if (sid >= SOUND_RAIN_LIGHT && sid <= SOUND_RAIN_HEAVY) { sid_kind = 1; sid_level = (int)(sid - SOUND_RAIN_LIGHT) + 1; }
    else if (sid >= SOUND_SNOW_LIGHT && sid <= SOUND_SNOW_HEAVY) { sid_kind = 2; sid_level = (int)(sid - SOUND_SNOW_LIGHT) + 1; }
    else if (sid >= SOUND_SAND_LIGHT && sid <= SOUND_SAND_HEAVY) { sid_kind = 3; sid_level = (int)(sid - SOUND_SAND_LIGHT) + 1; }
    if (cfg.force_weather) { s->kind = cfg.force_weather; s->level = 2; }
    else if (sid_kind && (s->type == sid_kind || ((s->type < 1 || s->type > 3) && s->intensity_pct > 0))) { s->kind = sid_kind; s->level = sid_level; }
    else if (s->type >= 1 && s->type <= 3 && s->fx && s->intensity_pct >= 27) { s->kind = s->type; s->level = s->intensity_pct < 40 ? 1 : (s->intensity_pct < 70 ? 2 : 3); }
    if (s->kind && !cfg.track[s->kind][s->level][0]) s->kind = 0;   /* no track configured for this weather: stay silent */
    if (cfg.force_indoors > 0) s->indoors = 1;
    else if (cfg.force_indoors < 0) s->indoors = 0;
}

/* ---- the building you are in (0.13 probe) ----
 * Read-only, the same walk the client's zone-text update makes (addresses.h). It finds the active
 * player in the object manager's list once and then only checks that the cached object still has the
 * player's GUID, so the list is walked again only after a loading screen. Every read is guarded; a
 * list the game changes under the walk costs a wrong answer for one poll, never a crash. It only logs,
 * and it is off unless building_probe=1 is set in the ini: it proves the chain on a real client (it
 * named the Goldshire inn, a dwarven inn and Stormwind by group on 29 Sep) before the roof sounds by
 * building follow it. */
typedef struct { DWORD wmo, nameset, group; char path[160]; } building_t;
static DWORD g_player_obj = 0, g_player_mgr = 0, g_player_seen = 0;   /* a loading screen frees both, so the cache is keyed on the manager too */

static int find_player(DWORD mgr, DWORD glo, DWORD ghi, DWORD *out)
{
    DWORD obj, linkoff, lo, hi; int n;
    /* a freed object keeps its GUID bytes, so the cache is also re-proved by a walk of the list every 5 s */
    if (g_player_obj && GetTickCount() - g_player_seen > 5000) g_player_obj = 0;
    if (g_player_obj && mgr == g_player_mgr && read32(g_player_obj + OFF_OBJ_GUID, &lo) && read32(g_player_obj + OFF_OBJ_GUID + 4, &hi) && lo == glo && hi == ghi) { *out = g_player_obj; return 1; }
    g_player_obj = 0;
    if (!read32(mgr + OFF_OBJMGR_FIRST_OBJ, &obj) || !read32(mgr + OFF_OBJMGR_NEXT_LINK, &linkoff) || linkoff > 0x10000u) return 0;
    for (n = 0; n < 20000 && obj && !(obj & 1u); n++) {
        if (read32(obj + OFF_OBJ_GUID, &lo) && read32(obj + OFF_OBJ_GUID + 4, &hi) && lo == glo && hi == ghi) { g_player_obj = obj; g_player_mgr = mgr; g_player_seen = GetTickCount(); *out = obj; return 1; }
        if (!read32(obj + linkoff + 4, &obj)) return 0;
    }
    return 0;
}
/* 1 = inside a building (b filled), 0 = not inside one, or not readable */
static int read_building(building_t *b)
{
    DWORD mgr, glo, ghi = 0, player, ent, flags, link, linkoff, def, dflags, rootlink, mdef, model, hdr, gidx, grp; int n;
    b->wmo = 0; b->nameset = 0; b->group = 0xFFFFFFFFu; b->path[0] = 0;
    if (!read32(ADDR_OBJMGR_PTR, &mgr) || !mgr || !read32(mgr + OFF_OBJMGR_PLAYER_GUID, &glo) || !glo) return 0;
    read32(mgr + OFF_OBJMGR_PLAYER_GUID + 4, &ghi);
    if (!find_player(mgr, glo, ghi, &player)) return 0;
    if (!read32(player + OFF_UNIT_MAP_ENTITY, &ent) || !ent || !read32(ent + OFF_ENTITY_FLAGS, &flags) || !(flags & 1u)) return 0;
    if (!read32(ent + OFF_ENTITY_LINKS, &link) || !read32(ent + OFF_ENTITY_LINKOFF, &linkoff) || linkoff > 0x10000u) return 0;
    for (n = 0; n < 64 && link && !(link & 1u); n++) {
        if (!read32(link + OFF_LINK_OBJ, &def)) return 0;
        if (def && read32(def + OFF_GROUPDEF_FLAGS, &dflags) && (dflags & 0x10u)) {
            if (!read32(def + OFF_GROUPDEF_ROOTLINK, &rootlink) || !rootlink || (rootlink & 1u)) return 0;
            if (!read32(rootlink + OFF_LINK_OBJ, &mdef) || !mdef || !read32(mdef + OFF_MAPOBJDEF_MODEL, &model) || !model) return 0;
            if (read32(model + OFF_MAPOBJ_HEADER, &hdr) && hdr) read32(hdr + OFF_MOHD_WMOID, &b->wmo);
            read32(mdef + OFF_MAPOBJDEF_NAMESET, &b->nameset);
            if (read32(def + OFF_GROUPDEF_INDEX, &gidx) && gidx < 512u && read32(model + OFF_MAPOBJ_GROUPS + gidx * 4u, &grp) && grp) read32(grp + OFF_GROUP_ID, &b->group);
            readstr(model + OFF_MAPOBJ_PATH, b->path, sizeof b->path);
            return 1;
        }
        if (!read32(link + linkoff + 4, &link)) return 0;
    }
    return 0;
}
static int same_building(const building_t *x, const building_t *y)
{
    int i = 0;
    if (x->wmo != y->wmo || x->nameset != y->nameset || x->group != y->group) return 0;
    while (x->path[i] && x->path[i] == y->path[i]) i++;
    return x->path[i] == y->path[i];
}

/* ---- sounds held in the DLL's own memory ----
 * The client routes FMOD's file access through its own file layer (FSOUND_File_SetCallbacks), and that
 * layer's open and close keep a cache with no lock: the game only ever opens and closes sounds on its own
 * thread. FMOD calls those callbacks on whichever thread opens or closes a stream, so a stream this worker
 * opened by name could corrupt the game's cache if the game opened a sound at the same moment (found in the
 * 0.13 hostile review; 0.12's indoor loop had the same race, rarer). So every sound the DLL plays is in
 * memory it owns, opened with FSOUND_LOADMEMORY, which FMOD serves without calling the client at all. Since
 * 0.14 that is the sounds it builds (g_snd, below) and, here, a custom track from the ini, read once with
 * plain Win32 calls. Buffers live until the process ends, never freed under a stream that might still
 * read them. */
#define MEM_SLOTS 64
static struct { char name[MAX_PATH]; void *data; DWORD size; int ok; } g_mem[MEM_SLOTS];
static int g_mem_n = 0;
static const char *g_mem_why = "";   /* why the last open failed, for the log */
static int str_eq(const char *a, const char *b) { int i = 0; while (a[i] && a[i] == b[i]) i++; return a[i] == b[i]; }
/* A file is read from the game folder as a loose file (not from the game's archives), at most 32 MB.
 * The path is built and opened in UTF-16 (GetModuleFileNameW, CreateFileW): through the ANSI calls, a
 * game folder with a character outside the ANSI code page turns into '?' and no sound would load. */
static const void *sound_bytes(const char *rel, DWORD *size, int *slot)
{
    WCHAR path[MAX_PATH * 2]; HANDLE h; DWORD sz, got; void *buf; int i, n = 0;
    for (i = 0; i < g_mem_n; i++) if (str_eq(g_mem[i].name, rel)) { *size = g_mem[i].size; *slot = i; return g_mem[i].data; }
    if (g_mem_n >= MEM_SLOTS || slen(rel) >= MAX_PATH) { g_mem_why = "too many sound files held in memory"; return NULL; }
    if (!(rel[0] && rel[1] == ':')) while (n < MAX_PATH && g_gamedirW[n]) { path[n] = g_gamedirW[n]; n++; }
    /* the name itself comes from the code or the ini, which the ini calls read in the ANSI code page */
    if (!MultiByteToWideChar(CP_ACP, 0, rel, -1, path + n, MAX_PATH * 2 - n)) { g_mem_why = "the file name could not be converted"; return NULL; }
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { g_mem_why = "no such file in the game folder"; return NULL; }
    sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); g_mem_why = "the file is empty or unreadable"; return NULL; }
    if (sz > 32u * 1024u * 1024u) { CloseHandle(h); g_mem_why = "over the 32 MB limit for a sound held in memory; convert it to ogg"; return NULL; }
    buf = VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) { CloseHandle(h); g_mem_why = "out of memory"; return NULL; }
    if (!ReadFile(h, buf, sz, &got, NULL) || got != sz) { VirtualFree(buf, 0, MEM_RELEASE); CloseHandle(h); g_mem_why = "the file could not be read"; return NULL; }
    CloseHandle(h);
    scpy(g_mem[g_mem_n].name, MAX_PATH, rel); g_mem[g_mem_n].data = buf; g_mem[g_mem_n].size = sz; g_mem[g_mem_n].ok = 0;
    *size = sz; *slot = g_mem_n++;
    return buf;
}
static FSOUND_STREAM *open_from_memory(const char *rel, unsigned int mode)
{
    DWORD size; int slot; FSOUND_STREAM *st; const void *p = sound_bytes(rel, &size, &slot);
    if (!p) return NULL;
    st = F.Stream_Open((const char *)p, mode | FSOUND_LOADMEMORY, 0, (int)size);
    if (st) { g_mem[slot].ok = 1; return st; }
    g_mem_why = "not a sound the engine can play (mp3, ogg or wav)";
    if (!g_mem[slot].ok) {   /* never opened, so no stream reads it: drop it, and a fixed file is read afresh next time */
        VirtualFree(g_mem[slot].data, 0, MEM_RELEASE);
        g_mem[slot] = g_mem[--g_mem_n];   /* streams hold the data pointer, never the slot */
    }
    return NULL;
}

/* The sounds the DLL builds (0.14), one entry per SND_* output: a complete 16-bit PCM WAV image in memory,
 * opened with FSOUND_LOADMEMORY. An entry is written once, by the worker, and never changed or freed after it
 * is ready, because a stream opened on it (the game's own outdoor rain, one of ours) reads it in place, from
 * FMOD's stream thread, for as long as that stream is open: until the process ends, for all we know. The
 * builder is further down (sounds_build). */
#define BUILD_NONE     0      /* not tried yet */
#define BUILD_READY    1      /* built: data and size never change again */
#define BUILD_FAILED   (-1)   /* could not be built (no such archive or file, damaged or unfit data, or out of memory
                                 BUILD_NOMEM_TRIES times): not tried again this session */
#define BUILD_LATER    (-2)   /* ran out of memory, or its archive could not be opened or read just then: tried again
                                 BUILD_RETRY_MS later, when memory may have come free or the archive be readable */
#define BUILD_RETRY_MS 60000
/* A lack of memory is tried this many times in all, a minute apart, and then given up for the session: each try reads
   and decodes its source and asks for its buffers again, so a client at the end of its address space would otherwise
   be taken to that end every minute, the game's own allocations with it (0.14 review). An archive that could not be
   opened or read costs a failed open or read a minute, and is tried for as long as it lasts. */
#define BUILD_NOMEM_TRIES 3
typedef struct { const void *data; DWORD size; int state; DWORD retry_at; int later_logged; int nomem_tries; } built_t;
static built_t g_snd[SND_COUNT];
/* the three outdoor rain loops for the Stream_Open hook, [level 1..3]: each pointer is published once, with
   InterlockedExchangePointer, after its entry is complete, and the hook reads nothing else of ours */
static built_t *volatile g_outdoor_img[4];
static int built_ok(int id) { return id >= 0 && id < SND_COUNT && g_snd[id].state == BUILD_READY; }

/* ---- the stream ---- */
static FSOUND_STREAM *g_stream = NULL;
static int g_channel = -1, g_kind_playing = 0, g_level_playing = 0, g_vol_cur = 0;
static DWORD g_last_fail = 0, g_started_tick = 0; static int g_fail_logged = 0; static DWORD g_retry_ms = START_RETRY_MS; static int g_quick_deaths = 0;

static const char *track_for(int kind, int level) { return cfg.track[clampi(kind, 1, 3)][clampi(level, 1, 3)]; }
static int track_id(int kind, int level) { return cfg.track_id[clampi(kind, 1, 3)][clampi(level, 1, 3)]; }
/* Whether two weathers play the same sound, so a change between them keeps it playing: two built sounds when
   they are the same one, two custom files when the paths match, a built one and a custom one never. (One
   keyword for all nine built sounds and a plain string compare would keep the medium loop playing through a
   heavier storm.) */
static int same_track(int k1, int l1, int k2, int l2)
{
    int a = track_id(k1, l1), b = track_id(k2, l2);
    if (a >= 0 || b >= 0) return a == b;
    return str_eq(track_for(k1, l1), track_for(k2, l2));
}

static int start_stream(int kind, int level)
{
    DWORD now = GetTickCount();
    const char *track = track_for(kind, level);
    int id = track_id(kind, level);
    const char *name = id >= 0 ? SND_OUTPUTS[id].name : track;
    void *sample;
    if (g_last_fail && now - g_last_fail < g_retry_ms) return 0;
    /* never through the client's file layer: see above */
    if (id < 0) g_stream = open_from_memory(track, FSOUND_LOOP_NORMAL | FSOUND_2D);
    else if (!built_ok(id)) { g_stream = NULL; g_mem_why = "it could not be built from the client's files; the build lines in this log say why"; }
    else if (!(g_stream = F.Stream_Open((const char *)g_snd[id].data, FSOUND_LOOP_NORMAL | FSOUND_2D | FSOUND_LOADMEMORY, 0, (int)g_snd[id].size)))
        g_mem_why = "the sound engine refused the built sound";
    if (!g_stream) { g_last_fail = now; g_retry_ms = START_RETRY_MS; if (!g_fail_logged) { g_fail_logged = 1; logf_("could not open %s (%s); retrying every %d s", name, g_mem_why, START_RETRY_MS / 1000); } return 0; }
    /* a high priority so the loop wins a channel on the client's 12-channel mixer instead of a footstep */
    sample = F.Stream_GetSample ? F.Stream_GetSample(g_stream) : NULL;
    if (sample && F.Sample_SetDefaults) F.Sample_SetDefaults(sample, -1, -1, -1, RAIN_CHANNEL_PRIORITY);
    g_channel = F.Stream_PlayEx(FSOUND_FREE, g_stream, NULL, 1);   /* start paused, so the first mixed block is silent */
    if (g_channel < 0) {
        /* no free channel right now: try again next poll, not in ten seconds */
        F.Stream_Close(g_stream); g_stream = NULL;
        if (!g_fail_logged) { g_fail_logged = 1; logf_("no free sound channel for %s; retrying every poll", name); }
        return 0;
    }
    if (F.SetPriority) F.SetPriority(g_channel, RAIN_CHANNEL_PRIORITY);
    if (F.SetReserved) F.SetReserved(g_channel, 1);   /* the game may not steal this channel while the loop plays */
    g_vol_cur = 0; F.SetVolume(g_channel, 0); F.SetPaused(g_channel, 0);
    g_kind_playing = kind; g_level_playing = level; g_last_fail = 0; g_started_tick = GetTickCount();
    logf_("stream started: %s %s, channel %d, %s%s", KIND_NAME[clampi(kind, 1, 3)], LEVEL_NAME[clampi(level, 1, 3)], g_channel, name,
          id >= 0 ? ", built from the client's files" : "");
    return 1;
}
/* Forget the handles without touching fmod: used once the engine has been closed under us. */
static void drop_stream(void) { if (g_stream) logf_("sound engine closed; dropping the stream"); g_stream = NULL; g_channel = -1; g_kind_playing = 0; g_level_playing = 0; g_vol_cur = 0; }
static void stop_stream(void)
{
    if (!g_stream) return;
    if (!fmod_alive()) { drop_stream(); return; }
    if (g_channel >= 0) { if (F.SetReserved) F.SetReserved(g_channel, 0); F.StopSound(g_channel); }
    F.Stream_Close(g_stream);
    logf_("stream stopped");
    g_stream = NULL; g_channel = -1; g_kind_playing = 0; g_level_playing = 0; g_vol_cur = 0;
}
static void set_volume(int v) { v = clampi(v, 0, 255); if (g_channel >= 0 && v != g_vol_cur) { F.SetVolume(g_channel, v); g_vol_cur = v; } }

/* ---- the storm: thunder and gusts while it rains ----
 * RavenCraft's medium and heavy rain loops carry thunder on a fixed cycle (a roll every 31 s, two in
 * 75 s), so the sky rumbled like clockwork. The game now gets thunder-free loops (hooked_stream_open)
 * and the thunder comes from here, at random: a strike lands at a distance, the addon hears of it at
 * once through IndoorRain_Storm and flashes the sky, and the thunder follows after that distance's
 * delay, muffled under a roof. Gusts come and go outdoors. Each is a short one-shot stream on a
 * channel the game may take back, unlike the reserved indoor loop. */
#define SHOT_SLOTS    3
/* The lowest priority there is. FSOUND_FREE takes a busy channel only from a sound at or below the new
 * sound's priority, so on a full mixer a thunder or gust can never push out a game sound (only one the
 * game itself plays at priority 0), and any game sound can take a shot's channel back: the shot is
 * dropped or cut short instead. The indoor loop keeps its reserved channel at 255 (start_stream). */
#define SHOT_PRIORITY 0
#define SHOT_MAX_MS   20000
static struct { FSOUND_STREAM *stream; int channel; DWORD started; } g_shot[SHOT_SLOTS];
static int g_shot_fail_logged = 0;

static void shots_drop(void) { int i; for (i = 0; i < SHOT_SLOTS; i++) { g_shot[i].stream = NULL; g_shot[i].channel = -1; } }
/* Close the one-shots that have finished, or all of them. Stream_Close stops the stream's own channel,
   so a channel the game has since reused for its own sound is never touched. */
static void shots_reap(int all)
{
    int i; DWORD now = GetTickCount();
    if (!fmod_alive()) { shots_drop(); return; }
    for (i = 0; i < SHOT_SLOTS; i++) {
        if (!g_shot[i].stream) continue;
        if (all || !F.IsPlaying(g_shot[i].channel) || now - g_shot[i].started > SHOT_MAX_MS) {
            F.Stream_Close(g_shot[i].stream); g_shot[i].stream = NULL; g_shot[i].channel = -1;
        }
    }
}
/* id: a built sound (SND_THUNDER_*, SND_GUST_*), opened from its image in memory like every sound the DLL plays */
static int shot_play(int id, int vol, int pan, int pitch_pct)
{
    FSOUND_STREAM *st = NULL; void *sample; int i, ch, f; const char *name = SND_OUTPUTS[id].name;
    if (!fmod_alive() || vol < 1) return 0;
    for (i = 0; i < SHOT_SLOTS && g_shot[i].stream; i++) ;
    if (i == SHOT_SLOTS) { logf_("storm: all %d sound slots busy, %s skipped", SHOT_SLOTS, name); return 0; }
    if (built_ok(id)) st = F.Stream_Open((const char *)g_snd[id].data, FSOUND_LOOP_OFF | FSOUND_2D | FSOUND_LOADMEMORY, 0, (int)g_snd[id].size);
    if (!st) {
        if (!g_shot_fail_logged) { g_shot_fail_logged = 1; logf_("storm: could not open %s (%s)", name,
            built_ok(id) ? "the sound engine refused the built sound" : "it could not be built from the client's files; the build lines in this log say why"); }
        return 0;
    }
    sample = F.Stream_GetSample ? F.Stream_GetSample(st) : NULL;
    if (sample && F.Sample_SetDefaults) F.Sample_SetDefaults(sample, -1, -1, -1, SHOT_PRIORITY);
    ch = F.Stream_PlayEx(FSOUND_FREE, st, NULL, 1);
    if (ch < 0) { F.Stream_Close(st); return 0; }
    if (F.SetPriority) F.SetPriority(ch, SHOT_PRIORITY);
    F.SetVolume(ch, clampi(vol, 0, 255));
    if (F.SetPan) F.SetPan(ch, clampi(pan, 0, 255));
    if (F.SetFrequency && F.GetFrequency && pitch_pct != 100 && (f = F.GetFrequency(ch)) > 0) F.SetFrequency(ch, f / 100 * pitch_pct);
    F.SetPaused(ch, 0);
    g_shot[i].stream = st; g_shot[i].channel = ch; g_shot[i].started = GetTickCount();
    return 1;
}

/* IndoorRain_Storm, the one thing the DLL tells the addon: "IRS1:k<distance>:l<rain level>:n<echo><strike>:d<delay>".
 * The addon registers it at exactly this width and never sets it again. Before every write the DLL
 * re-reads the value pointer and checks the whole template, then changes single digit bytes in place:
 * never the length, never the terminator, so the string the client owns keeps its size. The echo is
 * the addon's session code handed back, which tells the addon this DLL is running and the rain level
 * is current. The strike digit goes last and is one byte, so the addon sees either the old strike or
 * the new one with its distance and delay already in place. */
static const char STORM_TEMPLATE[] = "IRS1:k0:l0:n000:d00";
#define STORM_LEN 19
/* One digit group into the client's string: WriteProcessMemory on our own process fails cleanly if the
   region has gone, where a plain store would fault. */
static int poke(DWORD at, const char *src, SIZE_T n)
{
    SIZE_T put = 0;
    return WriteProcessMemory(GetCurrentProcess(), (LPVOID)(UINT_PTR)at, src, n, &put) && put == n;
}
/* 1 = written; 0 = the addon's CVar is not there; -1 = its value is not the template; -2 = the write was refused */
#define STORM_NO_CVAR 0
#define STORM_BAD_VALUE (-1)
#define STORM_REFUSED (-2)
static const char *storm_why(int r) { return r == STORM_REFUSED ? "the write into the game was refused" : (r == STORM_BAD_VALUE ? "the addon's storm CVar does not hold the template" : "the addon's storm CVar is not there"); }
static int still_at(DWORD sp) { DWORD now; return read32(cv.storm + OFF_CVAR_VALUE, &now) && now == sp; }   /* the client has not replaced the string */
static int storm_cvar_write(int k, int d, int l, int echo, int strike)
{
    DWORD sp; char p[STORM_LEN + 1], c[2]; int i; MEMORY_BASIC_INFORMATION mbi;
    if (!cv.storm || !read32(cv.storm + OFF_CVAR_VALUE, &sp) || !sp) return STORM_NO_CVAR;
    if (VirtualQuery((const void *)(UINT_PTR)sp, &mbi, sizeof mbi) != sizeof mbi) return STORM_BAD_VALUE;
    /* WriteProcessMemory writes through an execute-read protection (it is how a debugger sets a breakpoint),
       so the page's own protection is checked first: a page the client did not make writable is never written */
    if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) return STORM_REFUSED;
    if (!safe_copy(sp, p, STORM_LEN + 1)) return STORM_BAD_VALUE;
    for (i = 0; i < STORM_LEN; i++) {
        char ch = p[i];
        if (STORM_TEMPLATE[i] == '0' ? (ch < '0' || ch > '9') : ch != STORM_TEMPLATE[i]) return STORM_BAD_VALUE;
    }
    if (p[STORM_LEN] != 0) return STORM_BAD_VALUE;
    /* the same order as before: distance and delay, rain level, echo, and the strike digit alone and last */
    if (k >= 0) {
        d = clampi(d, 0, 99);
        c[0] = (char)('0' + clampi(k, 0, 9)); if (!still_at(sp)) return STORM_BAD_VALUE; if (!poke(sp + 6, c, 1)) return STORM_REFUSED;
        c[0] = (char)('0' + d / 10); c[1] = (char)('0' + d % 10); if (!still_at(sp)) return STORM_BAD_VALUE; if (!poke(sp + 17, c, 2)) return STORM_REFUSED;
    }
    if (l >= 0) { c[0] = (char)('0' + clampi(l, 0, 9)); if (!still_at(sp)) return STORM_BAD_VALUE; if (!poke(sp + 9, c, 1)) return STORM_REFUSED; }
    if (echo >= 0) { echo = clampi(echo, 0, 99); c[0] = (char)('0' + echo / 10); c[1] = (char)('0' + echo % 10); if (!still_at(sp)) return STORM_BAD_VALUE; if (!poke(sp + 12, c, 2)) return STORM_REFUSED; }
    if (strike > 0) { c[0] = (char)('0' + (p[14] - '0' + 1) % 10); if (!still_at(sp)) return STORM_BAD_VALUE; if (!poke(sp + 14, c, 1)) return STORM_REFUSED; }   /* a bump of the digit already there: a relaunch never repeats it */
    return 1;
}

/* how often, by rain level (none/test, light, medium, heavy), in ms at storm_rate 100 */
static const int STRIKE_MS[4][2] = { { 30000, 90000 }, { 45000, 150000 }, { 25000, 90000 }, { 12000, 50000 } };
static const int GUST_MS[4][2]   = { { 0, 0 }, { 40000, 90000 }, { 25000, 70000 }, { 15000, 45000 } };
/* how far away a strike lands, percent by rain level: close, near, mid, far, farther */
static const int CLASS_WEIGHT[4][5] = { { 20, 20, 20, 20, 20 }, { 0, 10, 25, 35, 30 }, { 8, 20, 32, 25, 15 }, { 18, 27, 30, 15, 10 } };
static const int CLASS_DELAY[5][2] = { { 2, 5 }, { 6, 12 }, { 13, 22 }, { 23, 32 }, { 33, 40 } };   /* tenths of a second, flash to thunder */
static const int CLASS_GAIN[5] = { 100, 88, 74, 58, 46 };   /* percent */
static const char *const CLASS_NAME[5] = { "close", "near", "mid", "far", "far2" };
static const int GUST_GAIN[4] = { 0, 70, 85, 100 };

static struct {
    int level;                    /* the rain level the storm runs at, 0 = none */
    int pending, cls;             /* a strike whose thunder has not sounded yet */
    DWORD due, next_strike, next_gust;
    int l_written, echoed;        /* last rain level and session code written, -1 = none yet */
    int test_prev, test_known;
    unsigned int rng;
} sm;

static void storm_init(void) { sm.l_written = -1; sm.echoed = -1; sm.rng = GetTickCount() ^ (GetCurrentThreadId() << 16) ^ 0x9E3779B9u; }
static unsigned int rnd_next(void) { unsigned int x = sm.rng ? sm.rng : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; sm.rng = x; return x; }
static int rnd(int lo, int hi) { return hi <= lo ? lo : lo + (int)(rnd_next() % (unsigned int)(hi - lo + 1)); }
static DWORD rate_ms(int ms) { return (DWORD)(ms / cfg.storm_rate * 100); }
static int after(DWORD now, DWORD t) { return (int)(now - t) >= 0; }

static void storm_strike(int level)
{
    int w = rnd(1, 100), acc = 0, cls, d, told;   /* told: a storm_cvar_write result */
    for (cls = 0; cls < 4; cls++) { acc += CLASS_WEIGHT[level][cls]; if (w <= acc) break; }
    d = rnd(CLASS_DELAY[cls][0], CLASS_DELAY[cls][1]);
    told = storm_cvar_write(cls + 1, d, -1, -1, 1);
    sm.pending = 1; sm.cls = cls; sm.due = GetTickCount() + (DWORD)d * 100u;
    if (told == 1) logf_("storm: strike %s, thunder in %d.%d s", CLASS_NAME[cls], d / 10, d % 10);
    else logf_("storm: strike %s, thunder in %d.%d s (no lightning: %s)", CLASS_NAME[cls], d / 10, d % 10, storm_why(told));
}

/* What the addon needs to know, written every poll whether or not the sound engine is up: the rain level
   (the fog follows it) and the echo of the addon's session code. No FMOD call. */
static void storm_bridge(const state_t *s)
{
    static int last_fail = 1;
    int rain = (s->in_world && s->kind == 1) ? clampi(s->level, 1, 3) : 0, code = cvar_int(cv.session, -1), r;
    if (rain != sm.l_written) {
        r = storm_cvar_write(-1, 0, rain, -1, -1);
        if (r == 1) sm.l_written = rain;
        else if (cv.storm && r != last_fail) logf_("storm: could not tell the addon the rain level (%s)", storm_why(r));
        last_fail = r;
    }
    if (code >= 10 && code <= 99 && code != sm.echoed) {
        r = storm_cvar_write(-1, 0, -1, code, -1);
        if (r == 1) { sm.echoed = code; logf_("storm: the addon's session %d answered (rain level %d)", code, rain); }
        else if (cv.storm && r != last_fail) logf_("storm: could not answer the addon (%s)", storm_why(r));
        last_fail = r;
    }
}

/* One step of the storm, under the fmod lock. Returns the ms until a thunder is due, or -1.
   live: the mod is on and the addon connected; storms_on is never 1 without it. */
static int storm_tick(const state_t *s, int storms_on, int live, int volume, int ambience_pm, int master_pm)
{
    DWORD now = GetTickCount();
    int rain = (s->in_world && s->kind == 1) ? clampi(s->level, 1, 3) : 0;
    int level = (rain && storms_on) ? rain : 0;
    int test = cvar_int(cv.thunder, -1);
    /* leaving the world, or the mod turned off, stops the storm's sounds at once and drops a thunder not yet heard */
    shots_reap(!s->in_world || !live);
    if (!s->in_world || !live) sm.pending = 0;
    if (level != sm.level) {
        if (level && !sm.level) {
            sm.next_strike = now + rate_ms(rnd(STRIKE_MS[level][0] / 3, STRIKE_MS[level][1] / 2));
            sm.next_gust = now + rate_ms(rnd(GUST_MS[level][0] / 2, GUST_MS[level][1] / 2));
        }
        if (level) logf_("storm: %s rain, a strike every %d to %d s", LEVEL_NAME[level], (int)rate_ms(STRIKE_MS[level][0]) / 1000, (int)rate_ms(STRIKE_MS[level][1]) / 1000);
        else logf_(rain ? "storm: switched off" : "storm: over");
        sm.level = level;
    }
    /* the storm grew, or storm_rate went up: no long wait left over from before */
    if (sm.level && !sm.pending && (int)(sm.next_strike - now) > (int)rate_ms(STRIKE_MS[sm.level][1])) sm.next_strike = now + rate_ms(rnd(STRIKE_MS[sm.level][0], STRIKE_MS[sm.level][1]));
    if (sm.level && (int)(sm.next_gust - now) > (int)rate_ms(GUST_MS[sm.level][1])) sm.next_gust = now + rate_ms(rnd(GUST_MS[sm.level][0], GUST_MS[sm.level][1]));
    if (test >= 0 && !sm.pending) {
        if (sm.test_known && test != sm.test_prev && s->in_world && live) { logf_("storm: test strike (/indoorrain thunder)"); storm_strike(sm.level); }
        sm.test_prev = test; sm.test_known = 1;
    }
    if (sm.level && !sm.pending && after(now, sm.next_strike)) {
        storm_strike(sm.level);
        if (sm.level >= 2 && rnd(1, 100) <= 15) sm.next_strike = sm.due + (DWORD)rnd(4000, 10000);   /* now and then a second strike close behind */
        else sm.next_strike = now + rate_ms(rnd(STRIKE_MS[sm.level][0], STRIKE_MS[sm.level][1]));
        if (!after(sm.next_strike, sm.due + 1000)) sm.next_strike = sm.due + 1000;
    }
    if (sm.pending && after(now, sm.due)) {
        float v = 255.0f * (float)CLASS_GAIN[sm.cls] / 100.0f * (float)cfg.thunder_volume / 100.0f * (float)ambience_pm / 1000.0f;
        if (s->indoors) v = v * (float)volume / 100.0f;
        sm.pending = 0;
        /* SND_THUNDER_CLOSE.. and SND_THUNDER_CLOSE_IN.. run in CLASS_NAME's order */
        if (master_pm > 0 && shot_play((s->indoors ? SND_THUNDER_CLOSE_IN : SND_THUNDER_CLOSE) + sm.cls, (int)(v + 0.5f), rnd(48, 208), rnd(88, 104)))
            logf_("storm: thunder %s%s at %d/255", CLASS_NAME[sm.cls], s->indoors ? " (indoors, muffled)" : "", (int)(v + 0.5f));
    }
    if (sm.level && after(now, sm.next_gust)) {
        sm.next_gust = now + rate_ms(rnd(GUST_MS[sm.level][0], GUST_MS[sm.level][1]));
        if (!s->indoors && master_pm > 0) {
            /* one of the gusts that could be built: a client without one of the wind sounds has fewer, or none */
            int ids[3], n = 0, i;
            float v = 255.0f * (float)GUST_GAIN[sm.level] / 100.0f * (float)cfg.gust_volume / 100.0f * (float)ambience_pm / 1000.0f;
            for (i = SND_GUST_1; i <= SND_GUST_3; i++) if (built_ok(i)) ids[n++] = i;
            if (n && shot_play(ids[rnd(0, n - 1)], (int)(v + 0.5f), rnd(32, 224), rnd(90, 110))) logf_("storm: gust at %d/255", (int)(v + 0.5f));
        }
    }
    return sm.pending ? (int)(sm.due - now) : -1;
}

static int resolve_fmod(void)
{
    HMODULE h = GetModuleHandleA("fmod.dll");
    if (!h) return 0;
    F.Stream_Open = (fn_Stream_Open)proc(h, "_FSOUND_Stream_Open@16");
    F.Stream_Play = (fn_Stream_Play)proc(h, "_FSOUND_Stream_Play@8");
    F.Stream_Close = (fn_Stream_Close)proc(h, "_FSOUND_Stream_Close@4");
    F.SetVolume = (fn_SetVolume)proc(h, "_FSOUND_SetVolume@8");
    F.StopSound = (fn_StopSound)proc(h, "_FSOUND_StopSound@4");
    F.IsPlaying = (fn_IsPlaying)proc(h, "_FSOUND_IsPlaying@4");
    F.GetVersion = (fn_GetVersion)proc(h, "_FSOUND_GetVersion@0");
    F.Stream_PlayEx = (fn_Stream_PlayEx)proc(h, "_FSOUND_Stream_PlayEx@16");
    F.SetPaused = (fn_SetPaused)proc(h, "_FSOUND_SetPaused@8");
    F.GetMaxChannels = (fn_GetMaxChannels)proc(h, "_FSOUND_GetMaxChannels@0");
    F.Stream_GetSample = (fn_Stream_GetSample)proc(h, "_FSOUND_Stream_GetSample@4");
    F.Sample_SetDefaults = (fn_Sample_SetDefaults)proc(h, "_FSOUND_Sample_SetDefaults@20");
    F.SetPriority = (fn_SetPriority)proc(h, "_FSOUND_SetPriority@8");
    F.SetReserved = (fn_SetReserved)proc(h, "_FSOUND_SetReserved@8");
    F.SetPan = (fn_SetPan)proc(h, "_FSOUND_SetPan@8");
    F.SetFrequency = (fn_SetFrequency)proc(h, "_FSOUND_SetFrequency@8");
    F.GetFrequency = (fn_GetFrequency)proc(h, "_FSOUND_GetFrequency@4");
    F.Sample_Load = (fn_Sample_Load)proc(h, "_FSOUND_Sample_Load@20");
    F.Sample_Lock = (fn_Sample_Lock)proc(h, "_FSOUND_Sample_Lock@28");
    F.Sample_Unlock = (fn_Sample_Unlock)proc(h, "_FSOUND_Sample_Unlock@20");
    F.Sample_GetLength = (fn_Sample_GetLength)proc(h, "_FSOUND_Sample_GetLength@4");
    F.Sample_GetMode = (fn_Sample_GetMode)proc(h, "_FSOUND_Sample_GetMode@4");
    F.Sample_GetDefaults = (fn_Sample_GetDefaults)proc(h, "_FSOUND_Sample_GetDefaults@20");
    F.Sample_Free = (fn_Sample_Free)proc(h, "_FSOUND_Sample_Free@4");
    F.GetError = (fn_GetError)proc(h, "_FSOUND_GetError@0");
    F.GetOutputRate = (fn_GetOutputRate)proc(h, "_FSOUND_GetOutputRate@0");
    return F.Stream_Open && F.Stream_Play && F.Stream_Close && F.SetVolume && F.StopSound && F.IsPlaying
        && F.Stream_PlayEx && F.SetPaused && F.GetMaxChannels;
}

/* The client closes its sound engine on exit, and no FMOD 3.75 query reveals that afterwards
 * (measured: GetMaxChannels, GetOutput, GetError all keep their values). So the DLL intercepts
 * the client's own FSOUND_Close: one pointer in WoW.exe's import table is redirected to a
 * function that takes the same lock the worker holds around every FMOD call, marks the engine
 * closed, and then runs the real close. Nothing else in the client is touched. */
typedef void (__stdcall *fn_Close)(void);
typedef signed char (__stdcall *fn_Init)(int mixrate, int maxchannels, unsigned int flags);
static fn_Close g_real_close = NULL;
static fn_Init g_real_init = NULL;
static DWORD_PTR *g_slot_close = NULL, *g_slot_init = NULL;   /* the two import-table slots we redirect */
static CRITICAL_SECTION g_fmod_cs;
static volatile LONG g_fmod_closed = 0;
static int g_close_hooked = 0, g_init_hooked = 0;
static int fmod_alive(void) { return !g_fmod_closed; }
static void drop_stream(void);
static void __stdcall hooked_close(void)
{
    EnterCriticalSection(&g_fmod_cs);
    InterlockedExchange(&g_fmod_closed, 1);
    drop_stream();
    shots_drop();
    LeaveCriticalSection(&g_fmod_cs);
    logf_("sound engine closing: FSOUND_Close intercepted, no more sound calls until it starts again");
    if (g_real_close) g_real_close();
}
static signed char __stdcall hooked_init(int mixrate, int maxchannels, unsigned int flags)
{
    signed char ok = g_real_init ? g_real_init(mixrate, maxchannels, flags) : 0;
    if (ok) {
        EnterCriticalSection(&g_fmod_cs);
        InterlockedExchange(&g_fmod_closed, 0);
        LeaveCriticalSection(&g_fmod_cs);
        logf_("sound engine started: FSOUND_Init(%d, %d) intercepted", mixrate, maxchannels);
    } else logf_("sound engine failed to start (FSOUND_Init returned 0)");
    return ok;
}
/* Find the main module's import-table slot that holds *target* (the resolved export of fmod.dll)
 * and point it at *replacement*. Matching by pointer works for name and ordinal imports alike. */
static DWORD_PTR *hook_import_slot(void *target, void *replacement)
{
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)exe;
    IMAGE_NT_HEADERS *nt; IMAGE_DATA_DIRECTORY dir; IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!exe || !target || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)((BYTE *)exe + dir.VirtualAddress); imp->Name; imp++) {
        const char *name = (const char *)exe + imp->Name;
        IMAGE_THUNK_DATA *iat;
        if (lstrcmpiA(name, "fmod.dll") != 0) continue;
        for (iat = (IMAGE_THUNK_DATA *)((BYTE *)exe + imp->FirstThunk); iat->u1.Function; iat++) {
            if ((void *)(UINT_PTR)iat->u1.Function == target) {
                DWORD old;
                if (!VirtualProtect(&iat->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) return NULL;
                iat->u1.Function = (DWORD_PTR)(UINT_PTR)replacement;
                VirtualProtect(&iat->u1.Function, sizeof(void *), old, &old);
                return (DWORD_PTR *)&iat->u1.Function;
            }
        }
    }
    return NULL;
}
static void unhook_import_slot(DWORD_PTR *slot, void *original)
{
    DWORD old;
    if (!slot || !original) return;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return;
    *slot = (DWORD_PTR)(UINT_PTR)original;
    VirtualProtect(slot, sizeof(void *), old, &old);
}
/* The game opens its outdoor rain loop through fmod.dll like every other sound. With the import-table
 * slot for FSOUND_Stream_Open redirected here, a request for one of the three rain loops opens
 * IndoorRain's louder, thunder-free loop instead; anything else, and any failure, goes to the real
 * call unchanged. This runs on the game's own thread, so it takes no lock and touches only constants
 * and single-word flags; the worker does the logging.
 * Since 0.14 the loop is the one the worker built in memory (g_outdoor_img). The hook never builds, waits,
 * allocates or logs: until that level's loop is built (the worker builds all three before it opens the swap
 * switch, at start or at the poll the switch turns on), or when it could not be built, the game's own call
 * goes through untouched and it plays its own loop. Every call gets a fresh stream on the same image, which the
 * game owns and closes; the image itself is never freed. */
static fn_Stream_Open g_real_stream_open = NULL;
static DWORD_PTR *g_slot_open = NULL;
static int g_open_hooked = 0;
static volatile LONG g_swapped = 0, g_swap_failed = 0, g_swap_level = 0, g_swap_unbuilt = 0, g_unbuilt_level = 0;
static int rain_loop_level(const char *name)
{
    static const char *const loop[4] = { "", "rainlightloop.wav", "rainmediumloop.wav", "rainheavyloop.wav" };
    const char *b = name; int i, lvl;
    for (i = 0; i < MAX_PATH && name[i]; i++) if (name[i] == '\\' || name[i] == '/') b = name + i + 1;
    if (i >= MAX_PATH) return 0;
    for (lvl = 1; lvl <= 3; lvl++) {
        const char *w = loop[lvl]; int j;
        for (j = 0; w[j]; j++) { char c = b[j]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != w[j]) break; }
        if (!w[j] && !b[j]) return lvl;
    }
    return 0;
}
static FSOUND_STREAM *__stdcall hooked_stream_open(const char *name, unsigned int mode, int offset, int length)
{
    if (g_outdoor_on && name && !(mode & (FSOUND_LOADMEMORY | FSOUND_LOADRAW)) && offset == 0 && length == 0) {
        int lvl = rain_loop_level(name);
        if (lvl) {
            const built_t *img = g_outdoor_img[lvl];   /* one read of a pointer published after its image was complete */
            if (img) {
                /* the game's own mode bits (loop, 2D), plus LOADMEMORY; FMOD 3.75 takes a memory image's size as the length */
                FSOUND_STREAM *s = g_real_stream_open((const char *)img->data, mode | FSOUND_LOADMEMORY, 0, (int)img->size);
                if (s) { InterlockedExchange(&g_swap_level, lvl); InterlockedIncrement(&g_swapped); return s; }
                InterlockedIncrement(&g_swap_failed);
            } else { InterlockedExchange(&g_unbuilt_level, lvl); InterlockedIncrement(&g_swap_unbuilt); }
        }
    }
    return g_real_stream_open(name, mode, offset, length);
}

static void install_hooks(void)
{
    HMODULE fm = GetModuleHandleA("fmod.dll");
    void *close_fn = proc(fm, "_FSOUND_Close@0"), *init_fn = proc(fm, "_FSOUND_Init@12"), *open_fn = proc(fm, "_FSOUND_Stream_Open@16");
    g_real_stream_open = (fn_Stream_Open)open_fn;
    g_slot_open = hook_import_slot(open_fn, (void *)hooked_stream_open);
    if (g_slot_open) g_open_hooked = 1;
    logf_("outdoor rain swap: %s", g_open_hooked ? "installed (the game's rain loops become IndoorRain's outdoor loops)" : "NOT installed (no matching fmod import slot); the game plays its own rain");
    /* the real pointers are saved BEFORE either slot is redirected: the client's main thread can call
     * through a redirected slot the instant it is written, and hooked_init must never find NULL */
    g_real_close = (fn_Close)close_fn; g_real_init = (fn_Init)init_fn;
    g_slot_close = hook_import_slot(close_fn, (void *)hooked_close);
    if (g_slot_close) g_close_hooked = 1;
    g_slot_init = hook_import_slot(init_fn, (void *)hooked_init);
    if (g_slot_init) g_init_hooked = 1;
    logf_("sound engine hooks: close %s, init %s", g_close_hooked ? "installed" : "NOT installed (no matching fmod import slot in the main module)",
          g_init_hooked ? "installed" : "NOT installed");
    if (!g_close_hooked) logf_("without the close hook, an exit while it rains indoors relies on the world-exit stop alone");
}
#if HARNESS_CVARS
__declspec(dllexport) void __stdcall IndoorRain_HarnessClose(void) { hooked_close(); }
#endif

/* ---- the sounds, built from the player's own client files (0.14) ----
 * Up to 0.13 the addon shipped its 25 sounds as .ogg files, made offline from the client's own weather,
 * lightning and wind sounds by tools/make_loops.sh and tools/make_storm_assets.sh. Those are copies of
 * Blizzard audio and cannot go on a public site, so the addon ships none now. The DLL reads the same client
 * files out of the MPQs in the Data folder next to WoW.exe (mpq.c and inflate.c, read-only, through the plain
 * Win32 file calls below, on this thread; never through the client's own archive code, whose file layer
 * belongs to the game's thread), runs the recipes those scripts ran (sounds.c, on the blocks of dsp.c), and
 * keeps each result in g_snd as a WAV image, opened with FSOUND_LOADMEMORY like every sound the DLL plays.
 * Each image is made at the rate the game's mixer runs at (mix_rate_now), or for snow and sand half of it: the
 * mixer holds each frame of a stereo voice slower than itself instead of interpolating, and at a lower rate the
 * muffled rain came back with a hiss over it (sounds.c). The thunder and the gusts, which shot_play pitches at random,
 * are made at the least multiple of that rate that is 44.1 kHz or more (snd_oneshot_rate): at a slower mixer's own
 * rate, the straight line the mixer draws between the frames of a pitched mono voice dulled and imaged them.
 *
 * When. The rain loops the settings want are built once at start, right after the hooks go in and before the
 * first poll: the three outdoor loops while the swap would be on, and the indoor loops the ini's rain tracks
 * name. The game asks for its rain loop within a quarter of a second of entering a rainy world (29 Sep log),
 * and the swap must have its loop by then. Nothing is built that cannot play (0.14 review: all nineteen were
 * built whatever the settings): nothing while the mod is off, no outdoor loop with outdoor_rain=0, and the
 * storm's ten thunders and three gusts only while its switch is on, which needs the addon connected, so they
 * are built at the poll the storm comes on, before its first strike is due (the thunders also for a test
 * strike, which needs no storm). A snow or sand loop is built the first time it is wanted indoors: most
 * sessions never hear one, and the six are the largest. After that, each poll builds whatever the settings
 * newly want (the mod, the storm or outdoor_rain switched on, a track set back to builtin) and retries a build
 * that has to wait. A build runs outside g_fmod_cs, so the game's thread never waits for one (hooked_close
 * takes that lock); only the gusts' decode through FMOD takes it, for a few ms.
 *
 * Failure. A missing archive or file, damaged data, or a file the recipes were not made for (over 90 s, over
 * 32 MB as samples, or an Ogg whose length cannot be known from its pages: refused before a sample is decoded,
 * sounds.h) is logged once and costs that sound for the session. An archive that is there but could not be opened
 * or read just then (another program holding it, a denied open, a short read; its Windows error is logged once per
 * archive) is logged once and tried again every minute, quietly, for as long as it lasts. A lack of the DLL's own
 * memory (ir_alloc, below) is logged once, tried again a minute later, and given up after BUILD_NOMEM_TRIES tries in
 * all, logged once more. Meanwhile nothing is built from another archive's copy instead (source_read). Either way it
 * costs that sound, never the game: a sound is published only once it is complete, and a build that fails frees
 * only its own buffers.
 * Memory that FMOD allocates is another matter. The client hands FMOD its own heap (SMem, through
 * FSOUND_SetMemorySystem before FSOUND_Init; the client's Sound.log says "SMem wrappers"), and that heap ends
 * the process when it cannot supply a block instead of returning NULL, for the game's own sounds as for ours.
 * So the gusts' decode (ogg_decode: about 0.4 MB of samples a gust, for a moment, and the decoder's state) and
 * every stream the DLL or the swap opens can end the game in a client near the end of its address space, as
 * any sound the game opens can there, and nothing here can turn that into a retry. */

/* Memory for the builds: VirtualAlloc, as 0.13 read its sounds (no C runtime, no heap of our own), which also
   gives each big buffer (a decoded source is up to 29 MB) back to the address space whole the moment it is
   done with. Only the worker calls these. A 16-byte header keeps each block's size, so the log can say what
   the sounds hold and what a build needed at its peak. */
static DWORD g_bytes_now = 0, g_bytes_peak = 0;
static void *ir_alloc(void *ctx, unsigned int n)
{
    BYTE *p;
    (void)ctx;
    if (n > 0x7FFF0000u) return NULL;
    p = (BYTE *)VirtualAlloc(NULL, (SIZE_T)n + 16u, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!p) return NULL;
    *(DWORD *)p = n;
    g_bytes_now += n;
    if (g_bytes_now > g_bytes_peak) g_bytes_peak = g_bytes_now;
    return p + 16;
}
static void ir_release(void *ctx, void *q)
{
    BYTE *p;
    (void)ctx;
    if (!q) return;
    p = (BYTE *)q - 16;
    g_bytes_now -= *(DWORD *)p;
    VirtualFree(p, 0, MEM_RELEASE);
}
static int mb10(DWORD bytes) { return (int)((bytes + 52429u) / 104858u); }   /* tenths of a MB, printed as %d.%d */

/* The archives, for mpq.c: opened read-only and shared every way (the game has them open, and must keep them),
   read at 64-bit offsets (patch.MPQ is 1.9 GB and keeps its tables near its end), closed after each file. The
   names are sounds.c's; a file system that minds case (Wine or macOS can have one) might hold PATCH-S.MPQ, so an
   archive not found as written is looked for in the folder without regard to case.
   mpq.c hears only "no such archive" from an open that returns nothing and "read error" from a short read, and an
   archive another program holds (a sharing violation), a denied open or a short read is neither missing nor
   broken: it may read fine a minute later (0.14 review). So every open, read or size that fails for any reason but
   "no such file" leaves its Windows error here, for source_read. Worker thread only. */
static struct { DWORD err; const char *op; } g_io_trouble;
static void io_trouble(const char *op, DWORD err) { g_io_trouble.err = err ? err : ERROR_GEN_FAILURE; g_io_trouble.op = op; }
static int not_there(DWORD err) { return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND; }
static int wname_is(const WCHAR *w, const char *a)
{
    int i;
    for (i = 0; w[i] && a[i]; i++) {
        WCHAR x = w[i]; unsigned char y = (unsigned char)a[i];
        if (x >= 'A' && x <= 'Z') x = (WCHAR)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        if (x != (WCHAR)y) return 0;
    }
    return !w[i] && !a[i];
}
static HANDLE data_file(const WCHAR *name)
{
    WCHAR path[MAX_PATH * 2]; int n = 0, i;
    while (g_dataW[n] && n < MAX_PATH * 2 - 1) { path[n] = g_dataW[n]; n++; }
    for (i = 0; name[i] && n < MAX_PATH * 2 - 1; i++) path[n++] = name[i];
    path[n] = 0;
    return CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}
static void *io_open(void *ctx, const char *archive)
{
    WCHAR name[64]; HANDLE h; DWORD err = 0; int i;
    (void)ctx;
    for (i = 0; archive[i]; i++) { if (i >= 63) return NULL; name[i] = (WCHAR)(unsigned char)archive[i]; }
    name[i] = 0;
    h = data_file(name);
    if (h == INVALID_HANDLE_VALUE && not_there(err = GetLastError())) {
        WCHAR pat[MAX_PATH * 2]; WIN32_FIND_DATAW fd; HANDLE f; int n = 0;
        while (g_dataW[n] && n < MAX_PATH * 2 - 2) { pat[n] = g_dataW[n]; n++; }
        pat[n++] = L'*'; pat[n] = 0;
        err = ERROR_FILE_NOT_FOUND;
        if ((f = FindFirstFileW(pat, &fd)) != INVALID_HANDLE_VALUE) {
            do if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wname_is(fd.cFileName, archive)) { if ((h = data_file(fd.cFileName)) == INVALID_HANDLE_VALUE) err = GetLastError(); break; }
            while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        if (!not_there(err)) io_trouble("open", err);   /* there, but not openable now: not a missing archive */
        return NULL;
    }
    return (void *)h;
}
static unsigned io_read_at(void *ctx, void *fh, unsigned long long off, void *buf, unsigned len)
{
    unsigned done = 0;
    (void)ctx;
    while (done < len) {
        /* the offset rides in the OVERLAPPED, whole: a signed 32-bit SetFilePointer could not reach past 2 GB */
        OVERLAPPED ov; DWORD got = 0; unsigned long long at = off + done;
        ov.Internal = 0; ov.InternalHigh = 0; ov.Offset = (DWORD)at; ov.OffsetHigh = (DWORD)(at >> 32); ov.hEvent = NULL;
        if (!ReadFile((HANDLE)fh, (BYTE *)buf + done, len - done, &got, &ov)) { io_trouble("read", GetLastError()); break; }
        /* mpq.c never reads past the size it was given, so nothing here is the end of the file: it shrank under us */
        if (got == 0) { io_trouble("read", ERROR_HANDLE_EOF); break; }
        done += got;
    }
    return done;
}
static unsigned long long io_size(void *ctx, void *fh)
{
    DWORD hi = 0, lo = GetFileSize((HANDLE)fh, &hi), err;
    (void)ctx;
    if (lo == INVALID_FILE_SIZE && (err = GetLastError()) != NO_ERROR) { io_trouble("size", err); return 0; }
    return ((unsigned long long)hi << 32) | lo;
}
static void io_close(void *ctx, void *fh) { (void)ctx; CloseHandle((HANDLE)fh); }
static const mpq_io g_io = { ir_alloc, ir_release, io_open, io_read_at, io_size, io_close, NULL };
static const dsp_alloc_t g_dsp = { ir_alloc, ir_release, NULL };

static LARGE_INTEGER g_qpf;   /* QueryPerformanceFrequency, set at start; 0 leaves every time at 0 ms */
static int ms_since(const LARGE_INTEGER *t0)
{
    LARGE_INTEGER t;
    if (!g_qpf.QuadPart || !QueryPerformanceCounter(&t)) return 0;
    return (int)((double)(t.QuadPart - t0->QuadPart) * 1000.0 / (double)g_qpf.QuadPart + 0.5);   /* x87 conversions: no 64-bit division helper, which no C runtime would provide */
}

/* An archive that is there but could not be opened or read gets one line in a session, with its Windows error,
   however often the sounds it holds are tried again (the scan line counts as that line). 1 = not said yet. */
static const char *g_trouble_said[8];
static int g_trouble_n = 0;
static int trouble_first(const char *archive)
{
    int i;
    for (i = 0; i < g_trouble_n; i++) if (path_is(g_trouble_said[i], archive)) return 0;
    if (g_trouble_n < 8) g_trouble_said[g_trouble_n++] = archive;
    return 1;
}

/* One line for the first log of a session: which Data folder, and whether each archive the recipes read is there */
static void data_scan(void)
{
    char dir[MAX_PATH * 2], line[400], part[96]; const char *seen[8]; int n = 0, i, w, k;
    if (!WideCharToMultiByte(CP_ACP, 0, g_dataW, -1, dir, sizeof dir, NULL, NULL)) scpy(dir, sizeof dir, "(a folder whose name the log cannot spell)");
    line[0] = 0;
    for (i = 0; i < SRC_COUNT; i++) for (w = 0; w < SND_WHERE_MAX && SND_SOURCES[i].where[w].archive; w++) {
        const char *a = SND_SOURCES[i].where[w].archive; void *h;
        for (k = 0; k < n && !path_is(seen[k], a); k++) ;
        if (k < n || n == 8) continue;
        seen[n++] = a;
        g_io_trouble.err = 0;
        if ((h = io_open(NULL, a))) { unsigned long long sz = io_size(NULL, h); io_close(NULL, h); wsprintfA(part, "%s%s (%u MB)", line[0] ? ", " : "", a, (unsigned)(sz >> 20)); }
        else if (g_io_trouble.err) { trouble_first(a); wsprintfA(part, "%s%s cannot be opened just now (Windows error %u)", line[0] ? ", " : "", a, (unsigned)g_io_trouble.err); }
        else wsprintfA(part, "%s%s missing", line[0] ? ", " : "", a);
        scat(line, sizeof line, part);
    }
    g_io_trouble.err = 0;
    logf_("sound sources: the client's archives in %s: %s", dir, line);
}

/* The file of source src from the first of its where[] entries whose archive holds it at the size the recipe was
   made for (sounds.c), whatever order the client patches in: RavenCraft's patch-S.mpq holds a medium rain loop with
   thunder baked in, which the list never names, and a heavy loop whose splice times fit that one file only.
   Only "it is not there" moves on to the next entry: no such archive, no such file in it, a stub of size zero, or a
   size other than the recipe's. Anything else stops the walk at that archive, where the client finds the file too
   (0.14 review: a lack of memory while reading patch-S.mpq's heavy loop built the heavy rain from patch.MPQ's older
   loop for the rest of the session, and CallLightning.wav from sound.MPQ's older crack). A lack of memory, or an
   archive that is there but could not be opened or read just then, is tried again later; damaged or refused data
   is not.
   SRC_FOUND (*which set), SRC_NOWHERE, SRC_LATER (the archive, just then), SRC_NOMEM (our memory, just then) or
   SRC_BROKEN. tried collects what each entry said, for the log. */
#define SRC_FOUND   1
#define SRC_NOWHERE 0
#define SRC_LATER   (-1)
#define SRC_BROKEN  (-2)
#define SRC_NOMEM   (-3)
static int source_read(int src, unsigned char **bytes, unsigned *len, int *which, char *tried, int cap)
{
    const snd_source_t *s = &SND_SOURCES[src]; int w, r; char part[160];
    for (w = 0; w < SND_WHERE_MAX && s->where[w].archive; w++) {
        const char *arch = s->where[w].archive;
        g_io_trouble.err = 0;
        r = mpq_read(&g_io, arch, s->path, bytes, len);
        if (r == MPQ_OK && (!s->where[w].size || *len == s->where[w].size)) { *which = w; return SRC_FOUND; }
        if (r == MPQ_OK) {   /* a changed file: a recipe cut at fixed times must never run on it */
            wsprintfA(part, "%s%s holds it at %u bytes, not the %u the recipe was made for", tried[0] ? "; " : "", arch, *len, s->where[w].size);
            ir_release(NULL, *bytes); *bytes = NULL; *len = 0;
            scat(tried, cap, part);
            continue;
        }
        if (g_io_trouble.err) {   /* there, but not openable or readable now: whatever mpq.c made of it */
            wsprintfA(part, "%s%s: could not %s it (Windows error %u)", tried[0] ? "; " : "", arch, g_io_trouble.op, (unsigned)g_io_trouble.err);
            scat(tried, cap, part);
            if (trouble_first(arch))
                logf_("cannot %s %s in the Data folder just now (Windows error %u); the sounds read from it are tried again every minute", g_io_trouble.op, arch, (unsigned)g_io_trouble.err);
            return SRC_LATER;
        }
        wsprintfA(part, "%s%s: %s", tried[0] ? "; " : "", arch, mpq_error_text(r));
        scat(tried, cap, part);
        if (r == MPQ_E_NO_ARCHIVE || r == MPQ_E_NOT_FOUND || r == MPQ_E_EMPTY) continue;
        return r == MPQ_E_NOMEM ? SRC_NOMEM : r == MPQ_E_IO ? SRC_LATER : SRC_BROKEN;
    }
    return SRC_NOWHERE;
}

static unsigned rd_le32(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

/* The gusts are Ogg Vorbis, and this DLL has no Vorbis decoder; the client's fmod.dll has one. It decodes the
 * file from our memory into an unmanaged sample (FSOUND_UNMANAGED: out of the sample slots the game fills from
 * its own thread, and ours to free), a software one (FSOUND_2D without HW2D or HW3D, so Lock hands over plain
 * memory, not a DirectSound buffer), whose 16-bit PCM is copied out before the sample is freed. Never LOADRAW
 * (the Ogg bytes would be taken for PCM), IMAADPCM (it would stay compressed) or NONBLOCKING. Under g_fmod_cs,
 * like every FMOD call of this thread, and only while the engine is up; a few ms a gust. What FMOD hands back
 * is held against what the file says of itself before any of it is used.
 * FMOD takes the decoded sample and the decoder's state from the client's heap, which ends the game rather than
 * fail an allocation (Failure, above): a NULL from Sample_Load is never a lack of memory, and only the copy into
 * our own memory (dsp_new) can come back DSP_NOMEM, to be tried again a minute later. For the same reason a file
 * is held to the length the recipes take (snd_source_ok) by its own word before FMOD sees it, the word being the
 * length the client's vorbisfile will find (snd_ogg_probe reads and checks every page for it), and one whose length
 * cannot be known that way is not decoded at all: FMOD would decode whatever vorbisfile finds into that heap.
 * DSP_OK, DSP_NOMEM, OGG_FAILED (why says why), SND_UNFIT or OGG_LATER (the engine is closed: a later poll). */
#define OGG_LATER  1
#define OGG_FAILED (-20)
static int ogg_decode(const unsigned char *ogg, unsigned n, dsp_buf_t *out, int *peak_db, char *why)
{
    int ch = 0, rate = 0, freq = 0, vol = 0, pan = 0, pri = 0, chans = 1, err = 0, peak = 0, r = OGG_FAILED;
    unsigned granule = 0, frames = 0, mode = 0, bytes = 0, i;
    void *smp, *p1 = NULL, *p2 = NULL; unsigned int l1 = 0, l2 = 0;
    const char *bad = NULL, *not_ogg;
    out->s = NULL; out->frames = 0; out->owned = 0; *peak_db = -99;
    if ((not_ogg = snd_ogg_probe(ogg, n, &ch, &rate, &granule))) { wsprintfA(why, "not an Ogg Vorbis file this can read: %s", not_ogg); return OGG_FAILED; }
    if (!granule) { scpy(why, 200, "an Ogg Vorbis file that does not say its own length"); return OGG_FAILED; }
    if (!snd_source_ok(granule, ch, rate)) { scpy(why, 200, snd_why(SND_UNFIT)); return SND_UNFIT; }
    if (!F.Sample_Load || !F.Sample_Lock || !F.Sample_Unlock || !F.Sample_GetLength || !F.Sample_GetMode || !F.Sample_GetDefaults || !F.Sample_Free) {
        scpy(why, 200, "the client's fmod.dll lacks the sample calls that would decode it"); return OGG_FAILED;
    }
    EnterCriticalSection(&g_fmod_cs);
    if (!fmod_alive()) { LeaveCriticalSection(&g_fmod_cs); return OGG_LATER; }
    smp = F.Sample_Load(FSOUND_UNMANAGED, (const char *)ogg, FSOUND_LOADMEMORY | FSOUND_2D | FSOUND_LOOP_OFF, 0, (int)n);
    if (!smp) { err = F.GetError ? F.GetError() : -1; bad = "fmod could not decode it"; }
    else {
        mode = F.Sample_GetMode(smp); frames = F.Sample_GetLength(smp);
        if (!F.Sample_GetDefaults(smp, &freq, &vol, &pan, &pri)) freq = 0;
        chans = (mode & FSOUND_STEREO) ? 2 : 1;
        bytes = frames * (unsigned)chans * 2u;
        /* FMOD takes the format from the file, whatever the flags asked for: check what it made */
        if (!(mode & FSOUND_16BITS) || (mode & (FSOUND_8BITS | FSOUND_IMAADPCM))) bad = "fmod did not decode it to 16-bit PCM";
        else if (frames == 0 || frames > 60u * 192000u) bad = "fmod gave it a length that cannot be right";
        else if (freq != rate || chans != ch) bad = "fmod decoded it at another rate or channel count than the file says";
        else if (!F.Sample_Lock(smp, 0, (int)bytes, &p1, &p2, &l1, &l2)) bad = "fmod would not hand over the samples";
        else {
            /* a software sample is one span; the second is copied too, should FMOD split it */
            if (!p1 || l1 + l2 != bytes || (l2 && !p2) || (l1 & 1u)) bad = "fmod handed over other than the samples it decoded";
            else if ((r = dsp_new(out, (int)frames, chans, freq, &g_dsp)) != DSP_OK) bad = "out of memory";
            else {
                const short *a = (const short *)p1, *b = (const short *)p2; unsigned na = l1 / 2u;
                for (i = 0; i < frames * (unsigned)chans; i++) {
                    int v = i < na ? a[i] : b[i - na], m = v < 0 ? -v : v;
                    if (m > peak) peak = m;
                    out->s[i] = (float)v * (1.0f / 32768.0f);
                }
            }
            F.Sample_Unlock(smp, p1, p2, l1, l2);
        }
        F.Sample_Free(smp);   /* at once: an unmanaged sample is ours to free */
    }
    LeaveCriticalSection(&g_fmod_cs);
    /* a decoder may pad its last block: a little past the file's own length is kept (and trimmed below), far off is not */
    if (!bad && granule && (frames + 8192u < granule || frames > granule + 65536u)) bad = "fmod decoded a length far from the file's own";
    if (!bad && peak <= 33) bad = "fmod decoded it to silence (peak under -60 dBFS)";
    if (bad) {
        if (err) wsprintfA(why, "%s (FSOUND_GetError %d)", bad, err);
        else wsprintfA(why, "%s (fmod: mode 0x%x, %u frames at %d Hz; the file: %u frames at %d Hz, %d ch)", bad, mode, frames, freq, granule, rate, ch);
        dsp_free(out, &g_dsp);
        return r == DSP_NOMEM ? DSP_NOMEM : OGG_FAILED;
    }
    if (granule && frames > granule) out->frames = (int)granule;
    *peak_db = (int)(20.0 * dsp_log10((double)peak / 32768.0) - 0.5);
    return DSP_OK;
}

/* Why a build did not happen, for build_failed */
#define WHY_FINAL   0   /* no such archive or file, damaged or unfit data: not tried again */
#define WHY_ARCHIVE 1   /* its archive is there but could not be opened or read just then: tried again every minute */
#define WHY_MEMORY  2   /* our own memory ran out: tried again a minute later, BUILD_NOMEM_TRIES times in all */
#define STR_(x) #x
#define STR(x) STR_(x)
/* A build that did not happen: its new state, and the end of the log line it takes, or NULL when it takes none (a
   wait already logged is retried quietly; running out of memory the last time is logged once more, as the end). */
static const char *build_failed(int out, int why)
{
    built_t *b = &g_snd[out];
    if (why == WHY_MEMORY && ++b->nomem_tries >= BUILD_NOMEM_TRIES) {
        b->state = BUILD_FAILED;
        return "; out of memory " STR(BUILD_NOMEM_TRIES) " times a minute apart, so not tried again this session";
    }
    if (why == WHY_FINAL) { b->state = BUILD_FAILED; return ""; }
    b->state = BUILD_LATER;
    b->retry_at = GetTickCount() + BUILD_RETRY_MS;
    if (b->later_logged) return NULL;
    b->later_logged = 1;
    return "; trying again in a minute";
}
/* The x87 control word for the arithmetic of one sound: the thread's default (53-bit precision, round to nearest,
   every exception masked), whatever was left in it, and the caller's put back after. FMOD runs on this thread too
   (the gusts' decode, between two sounds of one build, and every poll), and nothing says what it leaves in the word:
   set once around a whole build (0.14 review), a gust built after FMOD's decode ran on whatever FMOD had left. The
   flags our arithmetic raised (inexact, above all) are cleared before the caller's word goes back, so a word that
   unmasks one cannot fault on our leftovers. */
static unsigned short fpu_enter(void)
{
    unsigned short was, cw = 0x027F;
    __asm__ __volatile__ ("fnstcw %0" : "=m" (was));
    __asm__ __volatile__ ("fldcw %0" : : "m" (cw));
    return was;
}
static void fpu_leave(unsigned short was)
{
    __asm__ __volatile__ ("fnclex");
    __asm__ __volatile__ ("fldcw %0" : : "m" (was));
}
/* One output from its prepared source, published once complete. 1 = built. */
static int build_one(int out, const snd_input_t *in, const char *archive)
{
    void *wav; unsigned int size; int r, ch; LARGE_INTEGER t0; const unsigned char *h; unsigned short cw; const char *tail;
    QueryPerformanceCounter(&t0);
    cw = fpu_enter();
    r = snd_build(out, in, &wav, &size, &g_dsp);
    fpu_leave(cw);
    if (r != DSP_OK) {
        if ((tail = build_failed(out, r == DSP_NOMEM ? WHY_MEMORY : WHY_FINAL))) logf_("cannot build %s: %s%s", SND_OUTPUTS[out].name, snd_why(r), tail);
        return 0;
    }
    h = (const unsigned char *)wav;
    ch = h[22] | h[23] << 8;
    if (ch < 1) ch = 1;
    g_snd[out].data = wav; g_snd[out].size = size; g_snd[out].state = BUILD_READY;
    /* complete: from here the hook may hand it to the game */
    if (out >= SND_OUTDOOR_RAIN_LIGHT && out <= SND_OUTDOOR_RAIN_HEAVY)
        InterlockedExchangePointer((PVOID volatile *)&g_outdoor_img[out - SND_OUTDOOR_RAIN_LIGHT + 1], &g_snd[out]);
    logf_("built %s from %s: %u frames at %u Hz, %d ch, %u bytes, in %d ms", SND_OUTPUTS[out].name, archive, (size - 44u) / (2u * (unsigned)ch), rd_le32(h + 24), ch, size, ms_since(&t0));
    return 1;
}
/* The rate the sounds are built for (sounds.c): the game's mixer's own, FSOUND_GetOutputRate under the lock while the
   engine is up, held to the rates the sounds are built for, SND_MIX_MIN to SND_MIX_MAX. FSOUND_Init takes 4,000 to
   65,535 Hz, and the game's SoundMixRate setting goes past 48,000 only by hand: a mixer that fast plays each frame of a
   48 kHz image once or twice, unevenly (at most 1.37 times on average), which folds images of its band back down to
   12.5 kHz and up, as it would 0.13's 44.1 kHz files; the memory the sounds take grows with the rate they are built
   at (sounds.h), so they are not built past 48 kHz, and the log names both rates. While the engine is closed, the
   last rate read stands; until one is read, or when fmod.dll lacks the call, 44,100 Hz, the game's default for
   SoundMixRate, and the log says the rate is not known (the third 0.14 review: that line began like the one for a rate
   read, so the harness could not tell a DLL that never asked FMOD from one that did). The game sets the rate when it starts
   its engine, and WoW 1.12.1 starts it once, at launch, and closes it once, at exit: it has no restart in the session,
   so SoundMixRate takes effect at the next launch and the rate read at the first build holds for the session (the
   fourth 0.14 review, from the client's WoW.exe). Only the harness restarts the engine, at another rate (its check 17):
   a sound built before that keeps the old rate. Worker thread only; one log line when the rate is first known, or
   found not to be, and one each time the mixer's rate changes. */
static int g_mix_rate = 0, g_mix_read = 0;   /* the rate built for; the mixer's own as last read (0: none yet) */
static int mix_rate_now(void)
{
    int r = 0, was = g_mix_rate, shots; char at[96], also[80];
    if (F.GetOutputRate) {
        EnterCriticalSection(&g_fmod_cs);
        if (fmod_alive()) r = F.GetOutputRate();
        LeaveCriticalSection(&g_fmod_cs);
    }
    if (r <= 0) {
        if (!g_mix_rate) {
            g_mix_rate = 44100;
            logf_("the game's mixer rate is not known (%s): each sound is built at 44100 Hz, the game's default for its SoundMixRate setting, until fmod.dll says",
                  F.GetOutputRate ? "fmod.dll gave no rate" : "this fmod.dll cannot say");
        }
        return g_mix_rate;
    }
    if (r == g_mix_read) return g_mix_rate;
    g_mix_rate = clampi(r, SND_MIX_MIN, SND_MIX_MAX);
    if (r == g_mix_rate) scpy(at, sizeof at, "that rate");
    else wsprintfA(at, "%d Hz, the %s rate they are built for", g_mix_rate, r > g_mix_rate ? "highest" : "lowest");
    /* the thunder and the gusts, pitched at random, at a multiple of a slower mixer's rate (sounds.c) */
    if ((shots = snd_oneshot_rate(g_mix_rate)) != g_mix_rate) wsprintfA(also, ", the thunder and the gusts at %d Hz", shots);
    else also[0] = 0;
    if (!was) logf_("the game's mixer runs at %d Hz: each sound is built at %s%s, and snow and sand at half the mixer's rate when their files have that rate", r, at, also);
    else logf_("the game's mixer %s %d Hz: sounds built from here on are built at %s%s, and those built before, for %d Hz, stay as they are",
               g_mix_read ? "now runs at" : "runs at", r, at, also, was);
    g_mix_read = r;
    return g_mix_rate;
}
/* Reads and decodes source src once, makes it ready for the mixer's rate, and builds each of outs[0..n) from it.
   Returns how many it built. */
static int build_source(int src, const int *outs, int n)
{
    const snd_source_t *s = &SND_SOURCES[src];
    snd_input_t in; unsigned char *bytes = NULL; unsigned len = 0;
    int w = -1, r = DSP_OK, i, built = 0, peak_db = 0, found, why_kind;
    char tried[320], why[320], names[160]; LARGE_INTEGER t0; const char *tail = NULL, *t;   /* why: ogg_decode's reasons run to about 200 characters */
    if (s->format == SND_OGG && !fmod_alive()) return 0;   /* FMOD decodes it, and the engine is closed: a later poll */
    tried[0] = 0; why[0] = 0; names[0] = 0;
    for (i = 0; i < n; i++) { if (i) scat(names, sizeof names, ", "); scat(names, sizeof names, SND_OUTPUTS[outs[i]].name); }
    in.pcm.s = NULL; in.pcm.frames = 0; in.pcm.channels = 0; in.pcm.rate = 0; in.pcm.owned = 0; in.variant = SND_PLAIN; in.mix_rate = 0;
    QueryPerformanceCounter(&t0);
    found = source_read(src, &bytes, &len, &w, tried, sizeof tried);
    if (found == SRC_FOUND) {
        in.variant = s->where[w].variant;
        if (s->format == SND_OGG) r = ogg_decode(bytes, len, &in.pcm, &peak_db, why);   /* held to snd_source_ok before FMOD sees it */
        else {
            unsigned short cw = fpu_enter();
            r = snd_wav_decode(bytes, len, &in.pcm, &g_dsp);   /* a file the recipes were not made for is refused before its samples take any memory */
            fpu_leave(cw);
            if (r != DSP_OK) scpy(why, sizeof why, snd_why(r));
        }
        ir_release(NULL, bytes);   /* for a gust, only now: after FMOD has freed its sample */
        if (r == OGG_LATER) return 0;
        if (r == DSP_OK) {
            unsigned short cw = fpu_enter();
            r = snd_prepare(src, &in, g_mix_rate, &g_dsp);   /* for the mixer's rate; RavenCraft's heavy loop spliced here, once */
            fpu_leave(cw);
            if (r != DSP_OK) { scpy(why, sizeof why, snd_why(r)); dsp_free(&in.pcm, &g_dsp); }
        }
    } else scpy(why, sizeof why, found == SRC_LATER ? "its archive could not be read just now" : found == SRC_NOMEM ? "out of memory reading it just now"
                               : found == SRC_BROKEN ? "the first archive that holds it could not give it" : "not in the client's archives as the recipe needs it");
    if (found != SRC_FOUND || r != DSP_OK) {
        why_kind = found == SRC_NOMEM || r == DSP_NOMEM ? WHY_MEMORY : found == SRC_LATER ? WHY_ARCHIVE : WHY_FINAL;
        for (i = 0; i < n; i++) if ((t = build_failed(outs[i], why_kind)) && !tail) tail = t;
        if (tail) logf_("cannot build %s from %s: %s%s%s%s%s%s", names, s->path, why, tried[0] ? " (" : "", tried, tried[0] ? ")" : "", tail,
                        why_kind == WHY_FINAL && s->optional ? "; the storm plays only the gusts that were built" : "");
        return 0;
    }
    if (s->format == SND_OGG)
        logf_("read %s from %s: %u bytes, decoded by fmod: %d frames at %d Hz, %d ch, peak %d dBFS, in %d ms", s->path, s->where[w].archive, len,
              in.pcm.frames, in.pcm.rate, in.pcm.channels, peak_db, ms_since(&t0));
    else logf_("read %s from %s: %u bytes, %d frames at %d Hz, %d ch, in %d ms%s%s", s->path, s->where[w].archive, len,
               in.pcm.frames, in.pcm.rate, in.pcm.channels, ms_since(&t0), tried[0] ? "; passed over: " : "", tried);
    for (i = 0; i < n && !g_stop; i++) built += build_one(outs[i], &in, s->where[w].archive);
    dsp_free(&in.pcm, &g_dsp);
    return built;
}
static int wants_build(int out, DWORD now) { return g_snd[out].state == BUILD_NONE || (g_snd[out].state == BUILD_LATER && (int)(now - g_snd[out].retry_at) >= 0); }
/* Builds each sound of list[0..n) not built yet (or whose last try has waited its minute), reading and decoding each
   source once for all of its outputs in the list. Worker thread only. Returns how many it built. */
static int sounds_build(const int *list, int n)
{
    int i, j, k, m, built = 0, outs[SND_COUNT]; DWORD now = GetTickCount(), done = 0;
    mix_rate_now();
    for (i = 0; i < n && !g_stop; i++) {
        int src = SND_OUTPUTS[list[i]].source;
        if ((done & (1u << src)) || !wants_build(list[i], now)) continue;
        done |= 1u << src;
        for (m = 0, j = i; j < n; j++) {
            if (SND_OUTPUTS[list[j]].source != src || !wants_build(list[j], now)) continue;
            for (k = 0; k < m && outs[k] != list[j]; k++) ;
            if (k == m) outs[m++] = list[j];
        }
        built += build_source(src, outs, m);
    }
    return built;
}
/* The sounds that can play while the settings stand, and no others (0.14 review: all nineteen were built whatever the
   settings, memory a 32-bit client may not have for sounds that cannot play). Nothing while the mod is off. The three
   outdoor loops while the swap is on (outdoor), the indoor rain loops the ini's rain tracks name, the ten thunders
   while the storm is on or a strike waits for its thunder (thunder: a test strike needs no storm), and the three gusts
   while the storm is on (gusts). Heavy and medium first: on RavenCraft the game's own loops at those levels are the
   ones with thunder on a clock. */
static int wanted_sounds(int *list, int enabled, int outdoor, int thunder, int gusts)
{
    int n = 0, l, id, i;
    if (!enabled) return 0;
    for (l = 3; l >= 1; l--) {
        if (outdoor) list[n++] = SND_OUTDOOR_RAIN_LIGHT + l - 1;
        if ((id = cfg.track_id[1][l]) < 0) continue;
        for (i = 0; i < n && list[i] != id; i++) ;   /* two rain keys may name one sound: listed once, so the log's count is true */
        if (i == n) list[n++] = id;
    }
    if (thunder) for (id = SND_THUNDER_CLOSE; id <= SND_THUNDER_FAR2_IN; id++) list[n++] = id;
    if (gusts) for (id = SND_GUST_1; id <= SND_GUST_3; id++) list[n++] = id;
    return n;
}
/* the outdoor rain swap, as the worker decides it each poll: outdoor_rain with the mod on, while the addon is not
   known to be missing */
static int swap_wanted(int enabled) { return cfg.outdoor_rain && enabled && !cv.head_bad && !cv.missing; }
/* Before the first poll: the rain loops the ini's settings want (the addon's are not readable yet). The storm cannot
   be on before the addon connects, so its thunder and gusts wait for sounds_upkeep. */
static void sounds_at_start(void)
{
    int list[SND_COUNT], n = wanted_sounds(list, cfg.enabled, swap_wanted(cfg.enabled), 0, 0), built, ms; LARGE_INTEGER t0;
    data_scan();
    g_bytes_peak = g_bytes_now;
    QueryPerformanceCounter(&t0);
    built = sounds_build(list, n);
    ms = ms_since(&t0);
    logf_("sounds built at start: %d of %d in %d ms; %d.%d MB held, %d.%d MB at the build's peak; %s", built, n, ms,
          mb10(g_bytes_now) / 10, mb10(g_bytes_now) % 10, mb10(g_bytes_peak) / 10, mb10(g_bytes_peak) % 10,
          cfg.enabled ? "the storm's thunder and gusts are built when it comes on" : "the mod is off in the ini, so nothing is built until it is on");
}
/* Every poll, before the lock: whatever the settings newly want (the mod, the storm or the swap switched on), a retry
   that has waited its minute, and the indoor loop the weather wants now, which is how a snow or sand loop is built the
   first time it is heard. Its loop starts after the build, at this poll: 25 to 50 ms later with the mixer at 44.1 kHz,
   where they keep their files' 22,050 Hz, and 0.26 to 0.39 s at 48 kHz, where they are resampled (on a fast desktop
   CPU; a slower one takes longer). storms: the storm's switch as the worker reads it this poll. Cheap when there is
   nothing to do. */
static void sounds_upkeep(const state_t *s, int enabled, int outdoor, int storms)
{
    int list[SND_COUNT + 1], n = wanted_sounds(list, enabled, outdoor, storms || sm.pending, storms), i, any = 0; DWORD now = GetTickCount();
    if (s->in_world && s->kind && s->indoors && enabled && track_id(s->kind, s->level) >= 0) list[n++] = track_id(s->kind, s->level);
    for (i = 0; i < n && !any; i++) any = wants_build(list[i], now);
    if (!any) return;
    g_bytes_peak = g_bytes_now;
    if (sounds_build(list, n)) logf_("sounds held now: %d.%d MB (%d.%d MB at that build's peak)", mb10(g_bytes_now) / 10, mb10(g_bytes_now) % 10, mb10(g_bytes_peak) / 10, mb10(g_bytes_peak) % 10);
}

/* the swap switch the Stream_Open hook reads, and one log line each time it turns */
static void swap_set(LONG swap)
{
    if (swap == g_outdoor_on) return;
    InterlockedExchange(&g_outdoor_on, swap);
    logf_(swap ? "outdoor rain swap on: the game's rain loops become IndoorRain's outdoor loops"
               : "outdoor rain swap off: the game opens its own rain loops");
}
static DWORD WINAPI worker(LPVOID arg)
{
    state_t prev; int want_prev = 0, enabled_prev = -1, volume_prev = -1, master_prev = -1, ambience_prev = -1, level_target_prev = -1, in_world_since_logged = 0;
    int target_prev = -1, fade_step = 255, storms_prev = -1, building_known = 0;
    building_t building_prev = { 0, 0, 0, { 0 } };
    LONG swapped_prev = 0, swap_failed_prev = 0, swap_unbuilt_prev = 0;
    DWORD last_ini, world_since = 0;
    (void)arg;
    storm_init();
    logf_("%s %s loaded; idle until the client has started its sound engine (worker thread %u)", DLL_NAME, DLL_VERSION, GetCurrentThreadId());
    while (!g_stop && !resolve_fmod()) Sleep(500);
    if (g_stop) return 0;
    /* Do NOTHING while the client starts: no client reads, no ini, no import-table writes. The
     * client's own startup (DXVK, the resolution mod, the addon loader) runs on the main thread and
     * other injected DLLs patch the same import table; this thread stays out of it until the sound
     * engine has demonstrably been initialised (GetMaxChannels is 0 before FSOUND_Init). The first
     * Init is therefore not intercepted; only a later Close and a re-Init are, which is all we need. */
    while (!g_stop && F.GetMaxChannels() <= 0) Sleep(1000);
    if (g_stop) return 0;
    Sleep(2000);
    if (F.GetVersion) { int v = (int)(F.GetVersion() * 100.0f + 0.5f); logf_("fmod %d.%02d running with %d channels; starting up (thread %u)", v / 100, v % 100, F.GetMaxChannels(), GetCurrentThreadId()); }
    install_hooks();
    load_ini();
    logf_("ini: enabled=%d volume=%d fade_ms=%d poll_ms=%d force_indoors=%d force_weather=%d", cfg.enabled, cfg.volume, cfg.fade_ms, cfg.poll_ms, cfg.force_indoors, cfg.force_weather);
    logf_("ini: storm=%d outdoor_rain=%d thunder_volume=%d gust_volume=%d storm_rate=%d building_probe=%d", cfg.storm, cfg.outdoor_rain, cfg.thunder_volume, cfg.gust_volume, cfg.storm_rate, cfg.building_probe);
    /* the rain loops, from the client's own files, before the first poll: the swap switch only opens at that poll,
       so the game is never handed a loop that is not complete */
    QueryPerformanceFrequency(&g_qpf);
#if HARNESS_CVARS
    data_dir_from_ini();
#endif
    sounds_at_start();
    prev.in_world = -1; prev.kind = -1; prev.indoors = -1; prev.level = -1;
    last_ini = GetTickCount();
    for (;;) {
        state_t s; int enabled, volume, want, target, master_pm, ambience_pm, lt, storms_on, wait_ms = -1; LONG swap;
        if (g_stop) break;
        read_state(&s);
        /* connect the CVar bridge only once the world has been up for a while: every addon is loaded by then */
        if (s.in_world) { if (!world_since) world_since = GetTickCount(); if (GetTickCount() - world_since > 3000) cvar_connect(); }
        else { world_since = 0; cv.refused = 0; }   /* CVar pointers live as long as the process; only the "not registered yet" refusal is retried */
        if (s.in_world && cv.ready && cfg.force_indoors == 0) s.indoors = cvar_int(cv.indoors, 0) == 1;
        enabled = cfg.enabled; volume = cfg.volume;
        if (cv.ready) { int e = cvar_int(cv.enabled, -1), v = cvar_int(cv.volume, -1); if (e >= 0) enabled = e ? 1 : 0; if (v >= 0) volume = clampi(v, 0, 100); }
        master_pm = cvar_permille(cv.master, 1000); ambience_pm = cvar_permille(cv.ambience, 1000);
        if (cvar_int(cv.enable_ambience, 1) == 0) ambience_pm = 0;   /* Ambient Sounds off: the weather outside is silent too */
        if (enabled != enabled_prev || volume != volume_prev || master_pm != master_prev || ambience_pm != ambience_prev) {
            logf_("settings: enabled=%d volume=%d%% of the weather outside (%s), master %d/1000, ambience %d/1000",
                  enabled, volume, cv.ready ? "cvars" : "ini", master_pm, ambience_pm);
            enabled_prev = enabled; volume_prev = volume; master_prev = master_pm; ambience_prev = ambience_pm;
        }
        /* Off means off, as in 0.12. The swap follows the mod's switch on every poll, so whenever the mod is off
           (or the addon is known to be missing) the game opens its own rain loops; before the bridge has
           connected it follows the ini's switch, so a rain loop opened at login is already the right one.
           It goes off here at once, and on only after sounds_upkeep below has built the loops it hands over.
           The storm also needs the addon connected: its switch, the flash and the fog live there. */
        swap = swap_wanted(enabled) ? 1 : 0;
        if (!swap) swap_set(0);
        storms_on = 0;
        if (enabled && cv.ready) {
            storms_on = cfg.storm;   /* an addon older than 0.5 has no storm switch */
            if (cv.storms) { int v = cvar_int(cv.storms, -1); if (v >= 0) storms_on = v ? 1 : 0; }
        }
        if (storms_on != storms_prev) {
            logf_("storms %s (%s)", storms_on ? "on" : "off", !enabled ? "the mod is off" : (!cv.ready ? "the addon is not connected" : (cv.storms ? "cvar" : "ini")));
            storms_prev = storms_on;
        }
        {
            LONG sw = g_swapped, sf = g_swap_failed, su = g_swap_unbuilt;
            if (sw != swapped_prev) { logf_("outdoor rain: the game's %s rain loop is IndoorRain's outdoor loop", LEVEL_NAME[clampi(g_swap_level, 1, 3)]); swapped_prev = sw; }
            if (sf != swap_failed_prev) { logf_("outdoor rain: could not open IndoorRain's outdoor loop; the game plays its own"); swap_failed_prev = sf; }
            if (su != swap_unbuilt_prev) { logf_("outdoor rain: IndoorRain's %s outdoor loop is not built, so the game plays its own", LEVEL_NAME[clampi(g_unbuilt_level, 1, 3)]); swap_unbuilt_prev = su; }
        }
        if (!s.in_world) g_player_obj = 0;
        if (cfg.building_probe) {
            building_t bnow; int inside = s.in_world && read_building(&bnow);
            if (!inside) { bnow.wmo = 0; bnow.nameset = 0; bnow.group = 0xFFFFFFFFu; bnow.path[0] = 0; }
            if (!building_known || !same_building(&bnow, &building_prev)) {
                if (inside) logf_("building: %s (model %u, name set %u, group %u), indoors=%d", bnow.path[0] ? bnow.path : "(no name)", bnow.wmo, bnow.nameset, bnow.group, s.indoors);
                else if (building_known) logf_("building: none");
                building_prev = bnow; building_known = 1;
            }
        }
        if (s.in_world != prev.in_world || s.kind != prev.kind || s.indoors != prev.indoors || s.level != prev.level) {
            logf_("state: world=%d gate=%d type=%d intensity=%d%% sound=%u fx=%d weather=%s level=%d indoors=%d",
                  s.in_world, s.gate, s.type, s.intensity_pct, s.sound_id, s.fx, KIND_NAME[clampi(s.kind, 0, 3)], s.level, s.indoors);
            in_world_since_logged = 1;
        }
        /* a sound the settings or the weather newly want is built here, outside the lock: a build takes from a few
           ms to a second, and the game's thread must never wait on one (hooked_close takes that lock) */
        sounds_upkeep(&s, enabled, (int)swap, storms_on);
        if (swap) swap_set(1);
        EnterCriticalSection(&g_fmod_cs);
        if (g_stream && !fmod_alive()) drop_stream();
        if (!s.in_world && g_stream) { logf_("left the world: stopping at once"); stop_stream(); }
        lt = level_target(s.kind, s.level, volume, ambience_pm);
        /* a level the engine would round to nothing (a slider at zero) is no loop at all, not a silent one */
        want = s.in_world && s.kind && s.indoors && enabled && volume > 0 && lt > 0 && (lt * master_pm) / 1000 >= 1;
        if (want && lt != level_target_prev) {
            logf_("volume: %s %s at %d/255 (ambience %d%%, volume %d%% of the weather outside; master %d%% by the engine)",
                  KIND_NAME[clampi(s.kind, 1, 3)], LEVEL_NAME[clampi(s.level, 1, 3)], lt, ambience_pm / 10, volume, master_pm / 10);
            level_target_prev = lt;
        }
        if (!want) level_target_prev = -1;
        if (want != want_prev) { logf_(want ? "weather indoors: fading in" : "fading out"); want_prev = want; }
        /* a level change with a different file: fade out first, the next poll starts the new one */
        if (want && g_stream && (s.kind != g_kind_playing || s.level != g_level_playing) && !same_track(s.kind, s.level, g_kind_playing, g_level_playing)) target = 0;
        else target = want ? lt : 0;
        if (want && !g_stream && fmod_alive()) start_stream(s.kind, s.level);
        if (g_stream && fmod_alive()) {
            if (GetTickCount() - g_started_tick > 2000) { g_fail_logged = 0; g_quick_deaths = 0; g_retry_ms = START_RETRY_MS; }   /* it survived: reset the failure bookkeeping */
            /* fade over fade_ms whatever the level: the step is the distance this fade crosses, per poll,
               fixed when the target changes (recomputing it per poll would shrink it and never arrive) */
            int v = g_vol_cur, step;
            if (target != target_prev) {
                int span = target > g_vol_cur ? target - g_vol_cur : g_vol_cur - target;
                fade_step = cfg.fade_ms > 0 ? (span * cfg.poll_ms) / cfg.fade_ms : 255;
                if (fade_step < 1) fade_step = 1;
                target_prev = target;
            }
            step = fade_step;
            if (v < target) { v += step; if (v > target) v = target; } else if (v > target) { v -= step; if (v < target) v = target; }
            set_volume(v);
            if (target == 0 && g_vol_cur == 0) stop_stream();
            else if (want && !F.IsPlaying(g_channel)) {
                DWORD now = GetTickCount();
                stop_stream();
                if (now - g_started_tick < 2000) {
                    /* gone within two seconds: the game took the channel (busy mixer), or the file is bad */
                    g_quick_deaths++; g_last_fail = now;
                    g_retry_ms = g_quick_deaths < STOLEN_STREAK_LIMIT ? STOLEN_RETRY_MS : START_RETRY_MS;
                    if (g_quick_deaths == 1) logf_("the loop was cut off right after starting (channel taken?); retrying in %d s", STOLEN_RETRY_MS / 1000);
                    else if (g_quick_deaths == STOLEN_STREAK_LIMIT) logf_("cut off %d times in a row; retrying every %d s now", STOLEN_STREAK_LIMIT, START_RETRY_MS / 1000);
                }
                else { g_quick_deaths = 0; logf_("stream ended by itself; restarting"); start_stream(s.kind, s.level); }
            }
        }
        storm_bridge(&s);
        if (fmod_alive()) wait_ms = storm_tick(&s, storms_on, enabled && cv.ready, volume, ambience_pm, master_pm);
        LeaveCriticalSection(&g_fmod_cs);
        prev = s;
        if (GetTickCount() - last_ini > 2000) { load_ini(); last_ini = GetTickCount(); }
        /* a thunder due before the next poll wakes the worker on time: the delay after the flash is the distance */
        Sleep((DWORD)(wait_ms >= 0 && wait_ms < cfg.poll_ms ? (wait_ms > 10 ? wait_ms : 10) : cfg.poll_ms));
    }
    EnterCriticalSection(&g_fmod_cs); stop_stream(); shots_reap(1); LeaveCriticalSection(&g_fmod_cs);
    (void)in_world_since_logged;
    return 0;
}

/* Provided by hand: there is no C runtime in this DLL. */
void *memset(void *d, int c, size_t n) { volatile unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { volatile unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE th; int n;
        static LONG attaches = 0;
        LONG nth = InterlockedIncrement(&attaches);
        if (nth > 1) return TRUE;   /* never start a second worker, whatever the loader does */
        static LONG attached = 0;
        if (InterlockedIncrement(&attached) != 1) return TRUE;   /* never start a second worker, whatever the loader does */
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_fmod_cs);
        GetModuleFileNameA(NULL, g_gamedir, sizeof g_gamedir);
        n = slen(g_gamedir); while (n > 0 && g_gamedir[n - 1] != '\\') n--; g_gamedir[n] = 0;
        {   /* the same folder in UTF-16 for the sounds; one short of the buffer, so the string always ends */
            DWORD w = GetModuleFileNameW(NULL, g_gamedirW, MAX_PATH - 1);
            if (w > MAX_PATH - 1) w = MAX_PATH - 1;
            g_gamedirW[w] = 0;
            while (w > 0 && g_gamedirW[w - 1] != L'\\') w--;
            g_gamedirW[w] = 0;
        }
        {   /* the client's archives: the Data folder next to WoW.exe (0.14) */
            static const WCHAR sub[] = L"Data\\";
            int i = 0, j;
            while (g_gamedirW[i]) { g_dataW[i] = g_gamedirW[i]; i++; }
            for (j = 0; sub[j]; j++) g_dataW[i++] = sub[j];
            g_dataW[i] = 0;
        }
        scpy(g_ini, sizeof g_ini, g_gamedir); scat(g_ini, sizeof g_ini, DLL_NAME ".ini");
        scpy(g_log, sizeof g_log, g_gamedir); scat(g_log, sizeof g_log, "Logs\\" DLL_NAME ".log");
        { char logs[MAX_PATH]; scpy(logs, sizeof logs, g_gamedir); scat(logs, sizeof logs, "Logs"); CreateDirectoryA(logs, NULL); }
        {   /* a log that hit the cap in an earlier session starts over, so this session is never silent */
            HANDLE h = CreateFileA(g_log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) { DWORD size = GetFileSize(h, NULL); CloseHandle(h);
                if (size > LOG_MAX_BYTES) { h = CreateFileA(g_log, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } }
        }
        th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (th) CloseHandle(th);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stop, 1);   /* never wait here */
        if (reserved == NULL) {            /* FreeLibrary, not process exit: leave the import table as we found it */
            unhook_import_slot(g_slot_close, (void *)g_real_close);
            unhook_import_slot(g_slot_init, (void *)g_real_init);
            unhook_import_slot(g_slot_open, (void *)g_real_stream_open);
        }
    }
    return TRUE;
}
BOOL WINAPI DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID reserved) { return DllMain(inst, reason, reserved); }
