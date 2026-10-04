/* CinderLoad.dll: loading screens at the shape of the screen, for the 32-bit 1.12.1 client.
 *
 * The loading screens come in one archive per screen shape, Data\CinderLoad\LoadingScreens-<shape>.MPQ (the game
 * reads no archive in that folder). As the game starts, before it opens its own archives, this DLL reads the
 * resolution the game saved (SET gxResolution in WTF\Config.wtf; the monitor's size when there is none yet), picks
 * the nearest shape (shape.h) and copies that archive to Data\patch-~.MPQ, which the game then loads over its own
 * loading screens. A resolution changed in the game counts from the next start.
 *
 * Why patch-~: the client loads Data\patch-?.MPQ (one character, the pattern in WoW.exe) in sorted order and a later
 * archive wins. Every letter after M (the Project Reforged loading screens) is taken by a launcher pack, Ultra HD at U
 * among them, and '~' sorts after every letter and digit whichever way the names are compared, so ours load last and
 * no other pack is ever mistaken for this one.
 *
 * The stock client keeps every loading screen at 4:3. At RVA 0x6AC4 it calls a function whose result it then
 * compares (FCOM) to decide that; putting FLD1 (1.0) and four NOPs in place of the call lets the loading screen
 * fill the screen. That five-byte change is the community's widescreen edit of WoW.exe, published with its stock
 * and changed bytes in VanillaWideLoadScreens (github.com/Seraphic8x2244/VanillaWideLoadScreens); this file is
 * written from those published bytes, not from its code. It is made only when our loading screens for this shape
 * are in place, since the game's own square ones would be stretched, and only when the eleven bytes there (the
 * call and the compare after it) are exactly the stock ones. WoW.exe on disk is never touched.
 *
 * A Data\patch-~.MPQ that is not ours (not the size Data\CinderLoad\installed.txt recorded when we put it there,
 * and none of our archives) is left alone, and the switch stays off. Each start adds a line or two to Logs\CinderLoad.log.
 *
 * While a loading screen shows, the bar burns with a sound: the game's own fire loops, read from the player's client
 * archives, panned after the fill's front (fire.h, and "the fire sound" below). It is played through the client's
 * own fmod.dll from a thread of ours that only reads the client's memory; nothing in the client is changed for it.
 *
 * The bar's fire takes the colour of the picture's (orange, fel green, arcane, aqua: "the fire bar" below). The game
 * loads the bar's two textures once per screen; one call there is sent through this DLL, which names the textures
 * of that screen's fire family in our pack instead. Only with our loading screens in place, and only when every place
 * it relies on holds the bytes it was made for; otherwise the bar is the pack's orange one, as before.
 *
 * Built like IndoorRain.dll, with no C runtime (build.sh), and with IndoorRain's MPQ reader (mpq.c, inflate.c).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "shape.h"
#include "fire.h"
#include "mpq.h"

#define DLL_NAME  "CinderLoad"
#define PATCH_RVA 0x6AC4

static const unsigned char kStock[11] = { 0xE8, 0xA7, 0x42, 0x01, 0x00,           /* call (the 4:3 decision)     */
                                          0xD8, 0x15, 0xD8, 0xF9, 0x7F, 0x00 };   /* fcom dword [0x7FF9D8] after */
static const unsigned char kWide[5]   = { 0xD9, 0xE8, 0x90, 0x90, 0x90 };         /* fld1, then four nops        */

static char g_dir[MAX_PATH];             /* the game's folder, ending in a backslash */

static int Same(const unsigned char *a, const unsigned char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

static int Join(char *out, const char *a, const char *b)
{
    if (lstrlenA(a) + lstrlenA(b) >= MAX_PATH) return 0;
    lstrcpyA(out, a);
    lstrcatA(out, b);
    return 1;
}

static int GameDir(void)
{
    DWORD n = GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    if (!n || n >= MAX_PATH) { g_dir[0] = 0; return 0; }
    char *dir = g_dir;
    for (char *p = g_dir; *p; p++)
        if (*p == '\\' || *p == '/') dir = p + 1;
    *dir = 0;
    return 1;
}

/* One line, appended to Logs\CinderLoad.log next to WoW.exe. */
static void Log(const char *msg)
{
    char path[MAX_PATH];
    if (!g_dir[0] || !Join(path, g_dir, "Logs\\" DLL_NAME ".log")) return;
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[400];
    int len = wsprintfA(line, "%04d-%02d-%02d %02d:%02d:%02d %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                        t.wSecond, msg);
    DWORD wrote;
    WriteFile(h, line, (DWORD)len, &wrote, NULL);
    CloseHandle(h);
}

static int Exists(const char *path)
{
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int VariantPath(char *out, int shape)
{
    return Join(out, g_dir, "Data\\CinderLoad\\LoadingScreens-") && lstrlenA(out) + 16 < MAX_PATH &&
           lstrcatA(out, kShapes[shape].name) && lstrcatA(out, ".MPQ");
}

/* A small text file, NUL-terminated, on the process heap (HeapFree it); NULL when missing or over limit bytes. */
static char *ReadText(const char *path, DWORD limit)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *buf = (size == INVALID_FILE_SIZE || size > limit) ? NULL : HeapAlloc(GetProcessHeap(), 0, size + 1);
    if (buf && !ReadFile(f, buf, size, &got, NULL)) got = 0;
    CloseHandle(f);
    if (buf) buf[got] = 0;
    return buf;
}

/* 1 when the two files hold the same bytes. */
static int SameContent(const char *a, const char *b)
{
    HANDLE fa = CreateFileA(a, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE fb = CreateFileA(b, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    int same = 0;
    if (fa != INVALID_HANDLE_VALUE && fb != INVALID_HANDLE_VALUE) {
        DWORD sa = GetFileSize(fa, NULL), sb = GetFileSize(fb, NULL);
        enum { CHUNK = 65536 };
        unsigned char *ba = HeapAlloc(GetProcessHeap(), 0, CHUNK), *bb = HeapAlloc(GetProcessHeap(), 0, CHUNK);
        if (sa == sb && sa != INVALID_FILE_SIZE && ba && bb) {
            same = 1;
            for (DWORD left = sa; same && left;) {
                DWORD want = left < CHUNK ? left : CHUNK, ga = 0, gb = 0;
                if (!ReadFile(fa, ba, want, &ga, NULL) || !ReadFile(fb, bb, want, &gb, NULL) || ga != want || gb != want ||
                    !Same(ba, bb, (int)want))
                    same = 0;
                left -= want;
            }
        }
        if (ba) HeapFree(GetProcessHeap(), 0, ba);
        if (bb) HeapFree(GetProcessHeap(), 0, bb);
    }
    if (fa != INVALID_HANDLE_VALUE) CloseHandle(fa);
    if (fb != INVALID_HANDLE_VALUE) CloseHandle(fb);
    return same;
}

static DWORD FileSize(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return INVALID_FILE_SIZE;
    DWORD size = GetFileSize(f, NULL);
    CloseHandle(f);
    return size;
}

/* The ownership record says which shape we put in place and how big that file was: "21x9 98765432". It lives beside
 * the archive, as Data\patch-~.cinderload, so it stays with the file it describes even if Data\CinderLoad is removed
 * (0.1.2 and earlier kept it at Data\CinderLoad\installed.txt, still read once and then moved). It counts only while
 * Data\patch-~.MPQ is still that size, so a file that replaced ours is never taken for ours. */
static int MarkerMatches(const char *marker, const char *dst)
{
    char *text = ReadText(marker, 256);
    if (!text) return 0;
    const char *p = text, *num = NULL;
    for (; *p; p++)
        if (*p == ' ') num = p + 1;
    DWORD want = 0;
    int digits = 0;
    for (p = num; p && *p >= '0' && *p <= '9' && digits < 10; p++, digits++) want = want * 10 + (DWORD)(*p - '0');
    HeapFree(GetProcessHeap(), 0, text);
    return digits > 0 && want == FileSize(dst);
}

static void WriteText(const char *path, const char *text)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wrote;
    WriteFile(f, text, (DWORD)lstrlenA(text), &wrote, NULL);
    CloseHandle(f);
}

static int ClientIsOurs(void);

/* Takes our archive out: only once it is really gone is the ownership record dropped, so a file that could not be
 * deleted (another client has it open) is still known as ours next time. */
static void TakeOut(const char *dst, const char *marker, const char *oldMarker)
{
    if (!Exists(dst) || DeleteFileA(dst) || !Exists(dst)) {
        DeleteFileA(marker);
        DeleteFileA(oldMarker);
    }
}

/* Puts the loading screens for this screen's shape in place as Data\patch-~.MPQ. 1 when ours are in place, so the
 * switch can go on. */
static int ChooseScreens(void)
{
    char dst[MAX_PATH], marker[MAX_PATH], oldMarker[MAX_PATH], cfg[MAX_PATH], src[MAX_PATH], tmp[MAX_PATH], m[320];
    char pending[48];
    wsprintfA(pending, "Data\\CinderLoad\\pending-%lu.tmp", GetCurrentProcessId());
    if (!Join(dst, g_dir, "Data\\patch-~.MPQ") || !Join(marker, g_dir, "Data\\patch-~.cinderload") ||
        !Join(oldMarker, g_dir, "Data\\CinderLoad\\installed.txt") || !Join(cfg, g_dir, "WTF\\Config.wtf") ||
        !Join(tmp, g_dir, pending))
        return 0;

    int avail[SHAPE_COUNT], any = 0;
    for (int i = 0; i < SHAPE_COUNT; i++) {
        avail[i] = VariantPath(src, i) && Exists(src);
        any |= avail[i];
    }
    int w = 0, h = 0;
    const char *from = "Config.wtf";
    char *text = ReadText(cfg, 1 << 20);
    if (!text || !ParseResolution(text, &w, &h)) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
        from = "the monitor";
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    int pick = any ? ChooseShape(w, h, avail) : -1;
    int client = ClientIsOurs();
    if (!client) pick = -1;              /* never put widened screens in place where the switch cannot go on */

    int haveDst = Exists(dst);
    if (pick >= 0 && haveDst && VariantPath(src, pick) && SameContent(src, dst)) {
        if (!MarkerMatches(marker, dst)) {             /* moved beside the archive from 0.1.2's place, or rewritten */
            char note[64];
            wsprintfA(note, "%s %lu", kShapes[pick].name, FileSize(dst));
            WriteText(marker, note);
        }
        DeleteFileA(oldMarker);
        wsprintfA(m, "screen %dx%d (from %s): the %s loading screens are in place", w, h, from, kShapes[pick].name);
        Log(m);
        return 1;
    }
    int ours = !haveDst || MarkerMatches(marker, dst) || MarkerMatches(oldMarker, dst);
    for (int i = 0; !ours && i < SHAPE_COUNT; i++)
        if (avail[i] && VariantPath(src, i) && SameContent(src, dst)) ours = 1;
    if (!ours) {
        wsprintfA(m, "screen %dx%d (from %s): Data\\patch-~.MPQ is not ours, so it is left alone and the switch stays off",
                  w, h, from);
        Log(m);
        return 0;
    }
    if (pick < 0) {
        TakeOut(dst, marker, oldMarker);
        if (!client)
            wsprintfA(m, "screen %dx%d (from %s): this WoW.exe is not the client this was made for, so the game's own "
                      "loading screens, as it shows them", w, h, from);
        else
            wsprintfA(m, "screen %dx%d (from %s): no loading screens for this shape here, so the game's own, as it "
                      "shows them", w, h, from);
        Log(m);
        return 0;
    }
    /* Copied beside the archive first, then moved over it in one step: a copy cut short (the game killed while it
     * starts, a power cut) never leaves a broken Data\patch-~.MPQ, and a second client starting at the same moment
     * copies to its own file. */
    if (!VariantPath(src, pick) || !CopyFileA(src, tmp, FALSE) ||
        !MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD err = GetLastError();
        DeleteFileA(tmp);
        TakeOut(dst, marker, oldMarker);
        wsprintfA(m, "screen %dx%d (from %s): could not put the %s loading screens in place (error %lu); the game's own",
                  w, h, from, kShapes[pick].name, err);
        Log(m);
        return 0;
    }
    char note[64];
    wsprintfA(note, "%s %lu", kShapes[pick].name, FileSize(dst));
    WriteText(marker, note);
    DeleteFileA(oldMarker);
    wsprintfA(m, "screen %dx%d (from %s): put the %s loading screens in place", w, h, from, kShapes[pick].name);
    Log(m);
    return 1;
}

/* The loading bar. The client draws it from a table of {texture, is fill, cx, cy, w, h} in screen fractions measured
 * from the bottom (VA 0x7FFD34, two entries of 0x18 bytes): the fill at 0.5, 0.075, 0.525 x 0.025 and the border
 * (the charred log in our packs) at 0.5, 0.075, 0.6 x 0.05; the fill is drawn from its left edge to progress * w.
 * With our set in place the bar spans the whole width at 0.09 of the height, flush with the bottom of the screen; the
 * fill runs wall to wall too, 0.052 high, the log's solid band, where
 * the cracks the fire shows through run (our packs carry a border drawn for exactly these proportions). */
#define BAR_FILL_RVA   0x3FFD3C   /* cx, cy, w, h of Loading-BarFill */
#define BAR_BORDER_RVA 0x3FFD54   /* cx, cy, w, h of Loading-BarBorder */
static const unsigned char kFillStock[16]   = { 0x00, 0x00, 0x00, 0x3F, 0x9A, 0x99, 0x99, 0x3D,   /* 0.5, 0.075 */
                                                0x66, 0x66, 0x06, 0x3F, 0xCD, 0xCC, 0xCC, 0x3C }; /* 0.525, 0.025 */
static const unsigned char kBorderStock[16] = { 0x00, 0x00, 0x00, 0x3F, 0x9A, 0x99, 0x99, 0x3D,   /* 0.5, 0.075 */
                                                0x9A, 0x99, 0x19, 0x3F, 0xCD, 0xCC, 0x4C, 0x3D }; /* 0.6, 0.05 */
static const unsigned char kFillWide[16]    = { 0x00, 0x00, 0x00, 0x3F, 0xEC, 0x51, 0x38, 0x3D,   /* 0.5, 0.045 */
                                                0x00, 0x00, 0x80, 0x3F, 0xF4, 0xFD, 0x54, 0x3D }; /* 1.0, 0.052 */
static const unsigned char kBorderWide[16]  = { 0x00, 0x00, 0x00, 0x3F, 0xEC, 0x51, 0x38, 0x3D,   /* 0.5, 0.045 */
                                                0x00, 0x00, 0x80, 0x3F, 0xEC, 0x51, 0xB8, 0x3D }; /* 1.0, 0.09 */

/* Read only, before anything is put in place: is this the client CinderLoad was made for? Both places it changes must
 * hold the stock bytes (or ours, from an earlier start in this process). */
static int Readable(const unsigned char *at, int n)
{
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(at, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (const unsigned char *)mbi.BaseAddress + mbi.RegionSize >= at + n;
}
static int ClientIsOurs(void)
{
    const unsigned char *base = (const unsigned char *)GetModuleHandleA(NULL);
    const unsigned char *code = base + PATCH_RVA, *fill = base + BAR_FILL_RVA, *border = base + BAR_BORDER_RVA;
    if (!Readable(code, (int)sizeof kStock) || !Readable(fill, (int)(border + 16 - fill))) return 0;
    int codeOk = Same(code, kStock, sizeof kStock) ||
                 (Same(code, kWide, sizeof kWide) && Same(code + 5, kStock + 5, (int)sizeof kStock - 5));
    int barOk = (Same(fill, kFillStock, 16) && Same(border, kBorderStock, 16)) ||
                (Same(fill, kFillWide, 16) && Same(border, kBorderWide, 16));
    return codeOk && barOk;
}

static void WideBar(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    unsigned char *fill = base + BAR_FILL_RVA, *border = base + BAR_BORDER_RVA;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(fill, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        (unsigned char *)mbi.BaseAddress + mbi.RegionSize < border + 16) {
        Log("bar left as it is: its size table could not be read");
        return;
    }
    if (Same(fill, kFillWide, 16) && Same(border, kBorderWide, 16)) {
        Log("bar already full width: nothing to do");
        return;
    }
    if (!Same(fill, kFillStock, 16) || !Same(border, kBorderStock, 16)) {
        Log("bar left as it is: its size table holds other numbers, not the client this was made for");
        return;
    }
    DWORD old;
    if (!VirtualProtect(fill, (SIZE_T)(border + 16 - fill), PAGE_READWRITE, &old)) {
        Log("bar left as it is: its size table could not be made writable");
        return;
    }
    for (int i = 0; i < 16; i++) {
        fill[i] = kFillWide[i];
        border[i] = kBorderWide[i];
    }
    VirtualProtect(fill, (SIZE_T)(border + 16 - fill), old, &old);
    Log(Same(fill, kFillWide, 16) && Same(border, kBorderWide, 16)
            ? "bar on: the whole width, flush with the bottom (its size table, in memory only)"
            : "bar left as it is: the numbers read back differ");
}

static int Apply(void)
{
    unsigned char *at = (unsigned char *)GetModuleHandleA(NULL) + PATCH_RVA;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(at, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        (unsigned char *)mbi.BaseAddress + mbi.RegionSize < at + sizeof kStock) {
        Log("switch left off: the code there could not be read");
        return 0;
    }
    if (Same(at, kWide, sizeof kWide) && Same(at + 5, kStock + 5, (int)sizeof kStock - 5)) {
        Log("switch already on: nothing to do");
        return 1;
    }
    if (!Same(at, kStock, sizeof kStock)) {
        char m[160];
        wsprintfA(m, "switch left off: other bytes at RVA 0x%X (%02X %02X %02X %02X %02X), not the client this was made for",
                  PATCH_RVA, at[0], at[1], at[2], at[3], at[4]);
        Log(m);
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(at, sizeof kWide, PAGE_EXECUTE_READWRITE, &old)) {
        Log("switch left off: the code could not be made writable");
        return 0;
    }
    for (int i = 0; i < (int)sizeof kWide; i++)
        at[i] = kWide[i];
    VirtualProtect(at, sizeof kWide, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, sizeof kWide);
    int on = Same(at, kWide, sizeof kWide);
    Log(on ? "switch on: loading screens fill the screen (5 bytes at RVA 0x6AC4, in memory only)"
           : "switch left off: the bytes read back differ");
    return on;
}

/* The switch failed after our screens went in (the code could not be made writable): take them out again, before the
 * game opens its archives, so it never shows widened screens squeezed into 4:3. */
static void TakeOutAfterAll(void)
{
    char dst[MAX_PATH], marker[MAX_PATH], oldMarker[MAX_PATH];
    if (!Join(dst, g_dir, "Data\\patch-~.MPQ") || !Join(marker, g_dir, "Data\\patch-~.cinderload") ||
        !Join(oldMarker, g_dir, "Data\\CinderLoad\\installed.txt"))
        return;
    if (MarkerMatches(marker, dst)) TakeOut(dst, marker, oldMarker);
    Log("switch off, so our loading screens were taken out again: the game's own");
}

/* ---- the fire sound ----
 * Read from WoW.exe (1.12.1, build 5875) on 2 Oct 2026:
 * - The loading screen is a layer the client keeps at VA 0x882BE0: made when a loading screen begins (RVA 0x6800),
 *   released and set to 0 when it ends (RVA 0x7E80), and the client's own "is a loading screen up?" (RVA 0x7E70) is
 *   that pointer being non-zero. The sound plays while it is, and never otherwise.
 * - The bar's fill is the float at VA 0x882BE4, worked out each frame by RVA 0x6920 from the three loading stages and
 *   held to 0..1; the bar is drawn from it (RVA 0x7217: fill width = it * the fill's width). RVA 0x6920 first stores
 *   0 there and then the sum, so a read from another thread can catch the 0: FireFill keeps the largest.
 * - The client's sound engine is up while the byte at VA 0xCF55E8 is non-zero: set after FSOUND_Init (RVA 0x3A4797),
 *   tested before FSOUND_Close is called (RVA 0x3A4A59, the call at RVA 0x3A4A89), cleared after it (RVA 0x3A4AB6).
 * Each of those places must hold exactly these bytes when CinderLoad starts, or there is no sound at all and the log
 * says which one differed; the loading screens and the bar work the same either way.
 *
 * A thread of ours reads those three numbers every 20 ms; nothing in the client is hooked or changed for the sound.
 * While a loading screen shows it plays one stereo stream through the client's fmod.dll (FMOD 3.75), a stream FMOD
 * asks us to fill (FSOUND_Stream_Create): every sample is mixed here (fire.h), from loops built once from the client's
 * own sound files, so the equal-power pan does not depend on how FMOD pans. The thread calls FMOD only to start the
 * stream as a loading screen comes up and to close it once it has faded out after the screen has gone, and only while
 * the client's sound engine is up: with the sound off in the game, or the engine closed, it makes no FMOD call at all.
 * FMOD's own master volume (the game's Master Volume) applies to it as to every sound the game plays. */
#define FIRE_LAYER_RVA    0x482BE0   /* DWORD: the loading screen's layer, 0 when none shows */
#define FIRE_PROGRESS_RVA 0x482BE4   /* float: the bar's fill, 0..1 */
#define FIRE_SOUNDUP_RVA  0x8F55E8   /* BYTE: non-zero while the client's sound engine is up */
#define FIRE_POLL_MS      20
#define FIRE_FADE_WAIT_MS 1500       /* after a screen has gone: close the stream once silent, or after this at most */
#define FIRE_BLOCK_FRAMES 4096       /* what FMOD asks us to fill at a time: 93 ms at 44.1 kHz */
#define FIRE_RATE_MAX     48000      /* a faster mixer gets the sound at this rate, which FMOD then takes up */
#define FIRE_SCREEN_LOGS  10         /* one line per loading screen, for the first this many of a session */

static const struct { DWORD rva; int n; unsigned char bytes[26]; const char *what; } kFireCode[] = {
    { 0x007E70, 14, { 0x8B, 0x0D, 0xE0, 0x2B, 0x88, 0x00, 0x33, 0xC0, 0x85, 0xC9, 0x0F, 0x95, 0xC0, 0xC3 },
      "the loading screen check" },                           /* mov ecx,[882BE0]; xor eax,eax; test; setne al; ret */
    { 0x007E80, 25, { 0x8B, 0x0D, 0xE0, 0x2B, 0x88, 0x00, 0x57, 0x33, 0xFF, 0x3B, 0xCF, 0x74, 0x05, 0xE8, 0x3E, 0x30,
                      0x01, 0x00, 0x56, 0x89, 0x3D, 0xE0, 0x2B, 0x88, 0x00 },
      "the loading screen's end" },                           /* ... mov [882BE0],edi (0) */
    { 0x006920, 16, { 0xD9, 0x05, 0x7C, 0xFD, 0x7F, 0x00, 0xC7, 0x05, 0xE4, 0x2B, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00 },
      "the bar's progress" },                                 /* fld [7FFD7C]; mov dword [882BE4],0 */
    { 0x007217, 6, { 0xD9, 0x05, 0xE4, 0x2B, 0x88, 0x00 }, "the bar's drawing" },   /* fld dword [882BE4] */
    { 0x3A4797, 6, { 0x88, 0x1D, 0xE8, 0x55, 0xCF, 0x00 }, "the sound engine's start" },   /* mov [CF55E8],bl */
    { 0x3A4A59, 7, { 0xA0, 0xE8, 0x55, 0xCF, 0x00, 0x84, 0xC0 }, "the sound engine's close" },   /* mov al,[CF55E8]; test */
    { 0x3A4A82, 12, { 0x6A, 0xFD, 0xE8, 0xBB, 0x90, 0x05, 0x00, 0xE8, 0xB0, 0x90, 0x05, 0x00 },
      "the sound engine's close" },                           /* push -3; call; call FSOUND_Close */
    { 0x3A4AB6, 7, { 0xC6, 0x05, 0xE8, 0x55, 0xCF, 0x00, 0x00 }, "the sound engine's close" },   /* mov byte [CF55E8],0 */
};
#define FIRE_CODE_COUNT ((int)(sizeof kFireCode / sizeof kFireCode[0]))

/* -1 when every place holds its bytes, else the index of the first that does not. */
static int FireCodeDiffers(const unsigned char *base)
{
    for (int i = 0; i < FIRE_CODE_COUNT; i++)
        if (!Readable(base + kFireCode[i].rva, kFireCode[i].n) || !Same(base + kFireCode[i].rva, kFireCode[i].bytes, kFireCode[i].n))
            return i;
    return -1;
}

/* FMOD 3.75 (the client's fmod.dll), by decorated name; its callbacks are __stdcall like its calls (the client's own
 * file callbacks for FMOD all return with ret N). */
#define FSOUND_FREE       (-1)
#define FSOUND_STEREOPAN  (-1)
#define FSOUND_16BITS     0x00000010u
#define FSOUND_STEREO     0x00000040u
#define FSOUND_SIGNED     0x00000100u
#define FSOUND_2D         0x00002000u
#define FIRE_PRIORITY     255         /* a game sound must not take the channel while the screen shows */
typedef signed char (WINAPI *fn_StreamCallback)(void *stream, void *buff, int len, void *user);
static struct {
    void *(WINAPI *Stream_Create)(fn_StreamCallback cb, int lenbytes, unsigned int mode, int rate, void *user);
    void *(WINAPI *Stream_GetSample)(void *stream);
    signed char (WINAPI *Sample_SetDefaults)(void *sample, int freq, int vol, int pan, int pri);
    int (WINAPI *Stream_PlayEx)(int channel, void *stream, void *dsp, signed char paused);
    signed char (WINAPI *SetPriority)(int channel, int pri);
    signed char (WINAPI *SetReserved)(int channel, signed char reserved);
    signed char (WINAPI *SetPan)(int channel, int pan);
    signed char (WINAPI *SetVolume)(int channel, int vol);
    signed char (WINAPI *SetPaused)(int channel, signed char paused);
    signed char (WINAPI *StopSound)(int channel);
    signed char (WINAPI *Stream_Close)(void *stream);
    int (WINAPI *GetOutputRate)(void);
    float (WINAPI *GetVersion)(void);
    int (WINAPI *GetError)(void);
} F;

static void *Proc(HMODULE h, const char *name) { return (void *)GetProcAddress(h, name); }
static int FireResolve(void)
{
    HMODULE h = GetModuleHandleA("fmod.dll");
    if (!h) return 0;
    F.Stream_Create = Proc(h, "_FSOUND_Stream_Create@20");
    F.Stream_GetSample = Proc(h, "_FSOUND_Stream_GetSample@4");
    F.Sample_SetDefaults = Proc(h, "_FSOUND_Sample_SetDefaults@20");
    F.Stream_PlayEx = Proc(h, "_FSOUND_Stream_PlayEx@16");
    F.SetPriority = Proc(h, "_FSOUND_SetPriority@8");
    F.SetReserved = Proc(h, "_FSOUND_SetReserved@8");
    F.SetPan = Proc(h, "_FSOUND_SetPan@8");
    F.SetVolume = Proc(h, "_FSOUND_SetVolume@8");
    F.SetPaused = Proc(h, "_FSOUND_SetPaused@8");
    F.StopSound = Proc(h, "_FSOUND_StopSound@4");
    F.Stream_Close = Proc(h, "_FSOUND_Stream_Close@4");
    F.GetOutputRate = Proc(h, "_FSOUND_GetOutputRate@0");
    F.GetVersion = Proc(h, "_FSOUND_GetVersion@0");
    F.GetError = Proc(h, "_FSOUND_GetError@0");
    return F.Stream_Create && F.Stream_PlayEx && F.SetPan && F.SetVolume && F.SetPaused && F.StopSound &&
           F.Stream_Close && F.GetVersion;
}

static struct {
    const unsigned char *base;
    FireMix mix;                       /* the callback's alone while a stream is open; the thread's otherwise */
    volatile LONG gate, fillBits, quiet;
    FireFill fill;
    void *stream;
    int channel, playing, built, screens, complaints;
    DWORD since, gone;
    float reached;
} g_fire;

static signed char WINAPI FireCallback(void *stream, void *buff, int len, void *user)
{
    (void)stream; (void)user;
    union { LONG bits; float f; } fill;
    fill.bits = g_fire.fillBits;
    int frames = len > 0 ? len / 4 : 0;
    int on = FireRender(&g_fire.mix, (int)g_fire.gate, fill.f, (short *)buff, frames);
    for (int i = frames * 4; i < len; i++) ((unsigned char *)buff)[i] = 0;
    InterlockedExchange(&g_fire.quiet, !on);
    return 1;
}
static void FirePublish(float fill)
{
    union { LONG bits; float f; } v;
    v.f = fill;
    InterlockedExchange(&g_fire.fillBits, v.bits);
}

/* The client's archives, read with plain file calls on this thread (mpq.c), never through the client's own. */
static void *IoAlloc(void *ctx, unsigned size) { (void)ctx; return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }
static void IoRelease(void *ctx, void *p) { (void)ctx; if (p) HeapFree(GetProcessHeap(), 0, p); }
static void *IoOpen(void *ctx, const char *archive)
{
    (void)ctx;
    char path[MAX_PATH];
    if (!Join(path, g_dir, "Data\\") || lstrlenA(path) + lstrlenA(archive) >= MAX_PATH) return NULL;
    lstrcatA(path, archive);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return h == INVALID_HANDLE_VALUE ? NULL : (void *)h;
}
static unsigned IoReadAt(void *ctx, void *fh, unsigned long long off, void *buf, unsigned len)
{
    (void)ctx;
    unsigned done = 0;
    while (done < len) {
        OVERLAPPED ov;
        DWORD got = 0;
        unsigned long long at = off + done;
        ov.Internal = 0; ov.InternalHigh = 0; ov.Offset = (DWORD)at; ov.OffsetHigh = (DWORD)(at >> 32); ov.hEvent = NULL;
        if (!ReadFile((HANDLE)fh, (unsigned char *)buf + done, len - done, &got, &ov) || !got) break;
        done += got;
    }
    return done;
}
static unsigned long long IoSize(void *ctx, void *fh)
{
    (void)ctx;
    DWORD hi = 0, lo = GetFileSize((HANDLE)fh, &hi);
    return lo == INVALID_FILE_SIZE ? 0 : ((unsigned long long)hi << 32) | lo;
}
static void IoClose(void *ctx, void *fh) { (void)ctx; CloseHandle((HANDLE)fh); }
static const mpq_io kIo = { IoAlloc, IoRelease, IoOpen, IoReadAt, IoSize, IoClose, NULL };

/* The archives the loops are read from, the one the client lets win first: the six are the original files in the
 * client's sound.MPQ, and UndeadFireLarge the newer one in its patch.MPQ, as in the mock. A pack in a patch-?.MPQ that
 * changes a campfire's sound changes the campfire, not this fire: the sound stays the one that was approved. */
static const char *const kFireArchives[] = { "patch.MPQ", "sound.MPQ" };
#define FIRE_ARCHIVES ((int)(sizeof kFireArchives / sizeof kFireArchives[0]))

static void FireFree(void)
{
    for (int i = 0; i < FIRE_LOOPS; i++) {
        if (g_fire.mix.loop[i]) HeapFree(GetProcessHeap(), 0, g_fire.mix.loop[i]);
        g_fire.mix.loop[i] = NULL;
        g_fire.mix.len[i] = 0;
    }
}

/* The six loops at the mixer's rate, from the client's own files. Only while no stream is open. 1 when at least one
 * could be made (a missing one is logged and the rest play), 0 when none. */
static int FireBuild(int rate)
{
    char m[400];
    int made = 0;
    FireFree();
    FireSetRate(&g_fire.mix, rate);
    for (int i = 0; i < FIRE_LOOPS; i++) {
        const char *file = kFireLoops[i].path, *why = "in none of the client's archives";
        for (const char *p = kFireLoops[i].path; *p; p++)
            if (*p == '\\') file = p + 1;
        for (int a = 0; !g_fire.mix.loop[i] && a < FIRE_ARCHIVES; a++) {
            const char *name = kFireArchives[a];
            unsigned char *wav = NULL;
            unsigned len = 0, frames = 0;
            int r = mpq_read(&kIo, name, kFireLoops[i].path, &wav, &len), srate = 0, channels = 0;
            const unsigned char *data = NULL;
            if (r == MPQ_E_NOT_FOUND || r == MPQ_E_NO_ARCHIVE || r == MPQ_E_EMPTY) continue;
            if (r != MPQ_OK) {
                wsprintfA(m, "fire sound: %s in %s could not be read (%s); trying older copies", file, name, mpq_error_text(r));
                Log(m);
                continue;
            }
            float *src = NULL, *out = NULL;
            unsigned period = 0;
            if (!FireWavInfo(wav, len, &srate, &channels, &data, &frames)) why = "not a 16-bit PCM sound";
            else if (!(src = HeapAlloc(GetProcessHeap(), 0, frames * sizeof(float))) ||
                     !(out = HeapAlloc(GetProcessHeap(), 0, (FireResampledLength(frames, srate, rate) + 1) * sizeof(float))))
                why = "out of memory";
            else {
                FireWavRead(data, channels, frames, src);
                period = FireMakeLoop(src, frames, srate, rate, kFireLoops[i].offset * (rate / 100) / (FIRE_MOCK_RATE / 100), out);
                if (!period) why = "too short to loop";
            }
            if (src) HeapFree(GetProcessHeap(), 0, src);
            HeapFree(GetProcessHeap(), 0, wav);
            if (period) {
                g_fire.mix.loop[i] = out;
                g_fire.mix.len[i] = period;
                made++;
            } else {
                if (out) HeapFree(GetProcessHeap(), 0, out);
                wsprintfA(m, "fire sound: %s in %s is %s; trying older copies", file, name, why);
                Log(m);
                why = "in no archive as a sound it can use";
            }
        }
        if (!g_fire.mix.loop[i]) {
            wsprintfA(m, "fire sound: %s is %s, so the fire plays without it", file, why);
            Log(m);
        }
    }
    if (made) wsprintfA(m, "fire sound: %d of %d loops made from the client's own files, at %d Hz", made, FIRE_LOOPS, rate);
    else wsprintfA(m, "fire sound off: none of its %d loops could be made from the client's files", FIRE_LOOPS);
    Log(m);
    return made > 0;
}

static int FireRate(void)
{
    int rate = F.GetOutputRate ? F.GetOutputRate() : 0;
    if (rate < 8000 || rate > 192000) rate = 44100;
    return rate > FIRE_RATE_MAX ? FIRE_RATE_MAX : rate;
}

/* A loading screen has come up: one stream, started paused, then let go. 1 when it plays. */
static int FireOpen(float fill)
{
    char m[160];
    int rate = FireRate();
    if (g_fire.built && rate != g_fire.mix.rate) g_fire.built = 0;      /* the engine was restarted at another rate */
    if (!g_fire.built) g_fire.built = FireBuild(rate) ? 1 : -1;
    if (g_fire.built < 0) return 0;
    FireReset(&g_fire.mix);
    FireFillStart(&g_fire.fill, fill);
    FirePublish(g_fire.fill.fill);
    InterlockedExchange(&g_fire.gate, 1);
    InterlockedExchange(&g_fire.quiet, 0);
    void *st = F.Stream_Create(FireCallback, FIRE_BLOCK_FRAMES * 4, FSOUND_16BITS | FSOUND_SIGNED | FSOUND_STEREO | FSOUND_2D,
                               g_fire.mix.rate, NULL);
    if (!st) {
        if (g_fire.complaints++ >= 3) return 0;
        wsprintfA(m, "fire sound: the sound engine would not make the stream (FMOD error %d); trying at the next screen",
                  F.GetError ? F.GetError() : -1);
        Log(m);
        return 0;
    }
    void *sample = F.Stream_GetSample ? F.Stream_GetSample(st) : NULL;
    if (sample && F.Sample_SetDefaults) F.Sample_SetDefaults(sample, -1, -1, -1, FIRE_PRIORITY);
    int ch = F.Stream_PlayEx(FSOUND_FREE, st, NULL, 1);
    if (ch < 0) {
        F.Stream_Close(st);
        if (g_fire.complaints++ < 3) Log("fire sound: no free sound channel; trying at the next screen");
        return 0;
    }
    if (F.SetPriority) F.SetPriority(ch, FIRE_PRIORITY);
    if (F.SetReserved) F.SetReserved(ch, 1);
    F.SetPan(ch, FSOUND_STEREOPAN);             /* our own left and right, at full level: the pan is mixed in */
    F.SetVolume(ch, 255);
    F.SetPaused(ch, 0);
    g_fire.stream = st;
    g_fire.channel = ch;
    return 1;
}
static void FireClose(void)
{
    if (g_fire.channel >= 0) {
        if (F.SetReserved) F.SetReserved(g_fire.channel, 0);
        F.StopSound(g_fire.channel);
    }
    F.Stream_Close(g_fire.stream);
    g_fire.stream = NULL;
    g_fire.channel = -1;
}

static void BarShownSince(char *out);

/* One look at the client, every FIRE_POLL_MS. */
static void FireStep(DWORD now)
{
    const unsigned char *base = g_fire.base;
    int screen = *(volatile const unsigned int *)(base + FIRE_LAYER_RVA) != 0;
    float read = *(volatile const float *)(base + FIRE_PROGRESS_RVA);
    int up = *(volatile const unsigned char *)(base + FIRE_SOUNDUP_RVA) != 0;
    char m[220];

    if (!up) {                                   /* the engine is closed: it took our stream with it, so no call */
        if (g_fire.stream) Log("fire sound: the game closed its sound engine; the stream went with it");
        g_fire.stream = NULL;
        g_fire.channel = -1;
        g_fire.playing = 0;
        return;
    }
    if (!g_fire.built && !screen)                /* made once the engine is up, before the first loading screen */
        g_fire.built = FireBuild(FireRate()) ? 1 : -1;
    if (screen && !g_fire.playing) {             /* a loading screen has come up, or come back while fading */
        if (!g_fire.stream && !FireOpen(read)) {
            g_fire.playing = -1;                 /* not this screen */
            return;
        }
        if (g_fire.stream) {
            FireFillStart(&g_fire.fill, read);
            FirePublish(g_fire.fill.fill);
            InterlockedExchange(&g_fire.gate, 1);
        }
        g_fire.playing = 1;
        g_fire.since = now;
        g_fire.reached = g_fire.fill.fill;
        return;
    }
    if (screen && g_fire.playing > 0) {
        float fill = FireFillRead(&g_fire.fill, read);
        FirePublish(fill);
        if (fill > g_fire.reached) g_fire.reached = fill;
        return;
    }
    if (!screen && g_fire.playing) {             /* the screen has gone: fade out */
        if (g_fire.playing > 0 && g_fire.screens < FIRE_SCREEN_LOGS) {
            g_fire.screens++;
            DWORD ms = now - g_fire.since;
            char bar[48];
            BarShownSince(bar);
            wsprintfA(m, "loading screen: the fire burned for %lu.%lu s, the bar reached %d%%%s", ms / 1000, ms % 1000 / 100,
                      (int)(g_fire.reached * 100 + 0.5f), bar);
            Log(m);
        }
        InterlockedExchange(&g_fire.gate, 0);
        g_fire.playing = 0;
        g_fire.gone = now;
        return;
    }
    if (g_fire.stream && (g_fire.quiet || now - g_fire.gone > FIRE_FADE_WAIT_MS)) FireClose();
}

static DWORD WINAPI FireThread(LPVOID arg)
{
    (void)arg;
    for (int tries = 0; !FireResolve(); tries++) {
        if (tries == 30) {
            Log("fire sound off: the game's fmod.dll is not there or lacks the calls it needs");
            return 0;
        }
        Sleep(1000);
    }
    float v = F.GetVersion();
    if (v < 3.7f || v >= 3.8f) {                 /* the stream callback's form is FMOD 3.7's */
        char m[120];
        wsprintfA(m, "fire sound off: the game's fmod.dll is version %d.%02d, not the 3.7x this was made for",
                  (int)v, (int)(v * 100 + 0.5f) % 100);
        Log(m);
        return 0;
    }
    g_fire.channel = -1;
    for (;;) {
        FireStep(GetTickCount());
        Sleep(FIRE_POLL_MS);
    }
}

/* At start, read only: the sound's thread is started when every place it relies on holds the bytes it was made for. */
static void FireStart(void)
{
    const unsigned char *base = (const unsigned char *)GetModuleHandleA(NULL);
    int bad = FireCodeDiffers(base);
    char m[300];
    if (bad >= 0) {
        int len = wsprintfA(m, "fire sound off: other bytes at RVA 0x%lX (%s) than in the client it was made for, so no "
                            "sound; there:", (unsigned long)kFireCode[bad].rva, kFireCode[bad].what);
        if (Readable(base + kFireCode[bad].rva, kFireCode[bad].n))
            for (int i = 0; i < kFireCode[bad].n; i++)
                len += wsprintfA(m + len, " %02X", base[kFireCode[bad].rva + (DWORD)i]);
        Log(m);
        return;
    }
    if (!Readable(base + FIRE_LAYER_RVA, 8) || !Readable(base + FIRE_SOUNDUP_RVA, 1)) {
        Log("fire sound off: the loading screen's numbers could not be read");
        return;
    }
    g_fire.base = base;
    g_fire.channel = -1;
    HANDLE t = CreateThread(NULL, 0, FireThread, NULL, 0, NULL);
    if (!t) {
        Log("fire sound off: its thread could not be started");
        return;
    }
    CloseHandle(t);
    Log("fire sound on: the bar will burn while loading screens show");
}

/* ---- the fire bar ----
 * Each loading screen's bar burns in the colour of its picture's fire. Beside the orange Loading-BarFill.blp and
 * Loading-BarBorder.blp the game loads for every screen, our pack carries Loading-BarFill-<family>.blp and
 * Loading-BarBorder-<family>.blp for each fire family the owner's art kit found (fel, arcane, aqua, ...), and
 * CinderLoad\bars.txt: one line per screen, "<the screen file's base name> <family>", and "default <family>" for the
 * screens it does not list (make_pack.py writes it).
 *
 * Read from WoW.exe (1.12.1, build 5875) on 2 Oct 2026:
 * - The DWORD at VA 0x82F00C is the map the loading screen is for, stored just before a screen begins (RVA 0x67E3, and
 *   RVA 0x72D3 on the other way in); -1 when there is none, and the game then shows Interface\Glues\loading.
 * - The game picks the picture at RVA 0x6E20, on the screen's first frame: Map.dbc's record for that map, its
 *   LoadingScreenID (+0x98), LoadingScreens.dbc's record for that, and its file name (+8); anything missing on the way
 *   is the default screen. BarScreen walks the same tables the same way to the same name. (A third way into begin,
 *   RVA 0x7DE0, stores no map but -1, so the screen is for the map stored last; the picture is then chosen from that
 *   same number, and so is the bar. One difference: the game also shows the default screen when the picture's file
 *   is in none of its archives, which BarScreen does not ask; every screen our pack carries is there.)
 * - The bar's two textures are loaded inside the screen's begin (RVA 0x6800), once per screen, and released at its end
 *   (RVA 0x7E80). For each entry of the bar's table (VA 0x7FFD34) with no texture yet, ecx = the entry's path and a call
 *   from RVA 0x6891 to the texture loader at VA 0x449D90, which takes edx and three stack arguments too and pops them.
 * The map is stored microseconds before the textures are loaded, on the game's own thread: a thread of ours cannot get
 * in between. So that one call is the one change: it goes to BarStub, which asks BarPath which texture to load and
 * goes on to the loader with the stack and every register as the game left them, but ecx. Every place above must hold
 * exactly these bytes, or the call is left alone and the log says which differed; the screens, the bar's size and the
 * fire sound work the same either way. The client's tables are only read, on the thread the game reads them on.
 */
#define BAR_CALL_RVA        0x006891   /* the call to the texture loader: E8 FA 34 04 00 */
#define BAR_LOAD_RVA        0x006863   /* the loop around it */
#define BAR_FILL_PATH_RVA   0x42F0A0   /* "Interface\Glues\LoadingBar\Loading-BarFill" */
#define BAR_BORDER_PATH_RVA 0x42F070   /* "Interface\Glues\LoadingBar\Loading-BarBorder" */
#define BAR_MAP_VA          0x82F00C   /* int: the loading screen's map, -1 for none */
#define BAR_MAPS_VA         0xC0DAA8   /* Map.dbc's records by id (an array of pointers) */
#define BAR_MAPS_MAX_VA     0xC0DAAC   /* its largest id */
#define BAR_SCREENS_VA      0xC0DB0C   /* LoadingScreens.dbc's records by id */
#define BAR_SCREENS_MAX_VA  0xC0DB10   /* its largest id */
#define BAR_MAP_SCREEN      0x98       /* in a Map record: its LoadingScreenID */
#define BAR_SCREEN_FILE     8          /* in a LoadingScreens record: its file name */
#define BAR_FAMILIES        8          /* fire families one pack may carry */
#define BAR_FAMILY_MAX      15         /* letters and digits in a family's name */
#define BAR_SCREENS         256        /* screens bars.txt may name (the client has 45) */
#define BAR_NAME_MAX        47         /* characters in a screen's base name */
#define BAR_LINE_MAX        200        /* a longer line in bars.txt is skipped */
#define BAR_TEXT_MAX        65536      /* a bigger bars.txt is refused */
#define BAR_PATH_MAX        260        /* characters of a screen's file name read from the client */

static const char kBarFillPath[]   = "Interface\\Glues\\LoadingBar\\Loading-BarFill";
static const char kBarBorderPath[] = "Interface\\Glues\\LoadingBar\\Loading-BarBorder";
static const unsigned char kBarMapSet[] = { 0x8B, 0x55, 0xEC, 0x89, 0x15, 0x0C, 0xF0, 0x82, 0x00,   /* mov [82F00C],edx */
                                            0xE8, 0x0F, 0x00, 0x00, 0x00 };                     /* call begin (6800) */
static const unsigned char kBarMapSet2[] = { 0xA3, 0x0C, 0xF0, 0x82, 0x00 };                     /* mov [82F00C],eax */
static const unsigned char kBarPick[] = { 0xA1, 0x0C, 0xF0, 0x82, 0x00, 0x8D };                  /* mov eax,[82F00C] */
static const unsigned char kBarPickWalk[] = {
    0x7C, 0x72, 0x3B, 0x05, 0xAC, 0xDA, 0xC0, 0x00, 0x7F, 0x6A,             /* id < 0 or > [C0DAAC]: the default */
    0x8B, 0x15, 0xA8, 0xDA, 0xC0, 0x00, 0x8B, 0x04, 0x82, 0x85, 0xC0, 0x74, 0x5D,   /* map = [C0DAA8][id], or none */
    0x8B, 0x80, 0x98, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x7C, 0x53,             /* ls = map->LoadingScreenID, not < 0 */
    0x3B, 0x05, 0x10, 0xDB, 0xC0, 0x00, 0x7F, 0x4B,                         /* nor > [C0DB10] */
    0x8B, 0x0D, 0x0C, 0xDB, 0xC0, 0x00, 0x8B, 0x34, 0x81, 0x85, 0xF6, 0x74, 0x3E,   /* rec = [C0DB0C][ls], or none */
    0x8B, 0x56, 0x08, 0x52, 0xE8, 0x1F };                                   /* push rec->file name; call */
static const unsigned char kBarLoad[] = {
    0xBE, 0x34, 0xFD, 0x7F, 0x00, 0x83, 0x3F, 0x00, 0x75, 0x2B,             /* esi = the bar's table; a texture yet? */
    0x6A, 0x00, 0x6A, 0x01, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x00, 0x6A, 0x01,
    0x8D, 0x4D, 0xFC, 0xE8, 0xFB, 0x40, 0x18, 0x00, 0x8B, 0x10,
    0x8B, 0x0E,                                                             /* ecx = the entry's path */
    0x6A, 0x00, 0x6A, 0x01, 0x52, 0x8D, 0x55, 0xD8,                         /* three arguments; edx = a local */
    0xE8, 0xFA, 0x34, 0x04, 0x00,                                           /* call the loader (449D90): ours */
    0x89 };
#define BAR_REL_AT (BAR_CALL_RVA + 1 - BAR_LOAD_RVA)   /* where in kBarLoad the call's rel32 sits */

static const struct { DWORD rva; int n; const unsigned char *bytes; const char *what; } kBarCode[] = {
    { 0x0067E3, (int)sizeof kBarMapSet, kBarMapSet, "the loading screen's map" },
    { 0x0072D3, (int)sizeof kBarMapSet2, kBarMapSet2, "the loading screen's map" },
    { 0x006E2C, (int)sizeof kBarPick, kBarPick, "the loading screen's picture" },
    { 0x006E52, (int)sizeof kBarPickWalk, kBarPickWalk, "the loading screen's picture" },
    { BAR_LOAD_RVA, (int)sizeof kBarLoad, kBarLoad, "the bar's textures being loaded" },
    { BAR_FILL_PATH_RVA, (int)sizeof kBarFillPath, (const unsigned char *)kBarFillPath, "the bar's fill texture" },
    { BAR_BORDER_PATH_RVA, (int)sizeof kBarBorderPath, (const unsigned char *)kBarBorderPath, "the bar's border texture" },
};
#define BAR_CODE_COUNT ((int)(sizeof kBarCode / sizeof kBarCode[0]))

/* What bars.txt says, with each family's two texture paths in the DLL's own memory, for BarPath to hand the game. */
typedef struct {
    int families, screens, fallback, skipped;          /* fallback: the "default" line's family, or -1 */
    struct { char name[BAR_FAMILY_MAX + 1]; int ok; char fill[80], border[80]; } family[BAR_FAMILIES];
    struct { char name[BAR_NAME_MAX + 1]; int family; } screen[BAR_SCREENS];
} BarTable;
static BarTable g_bars;
static volatile LONG g_barsOn;                         /* 1 once g_bars is complete and the call is ours */
static volatile LONG g_barShown;                       /* BarPath's last fill: 0 none since the worker looked, -1 the
                                                          pack's orange, else the family's index + 1 */

/* Called from the asm stub below, so neither may be static, nor dropped as unused. */
const char *WINAPI BarPath(const char *path) __attribute__((used));
__attribute__((used)) void *g_barLoader;               /* the game's texture loader, VA 0x449D90 */

#if defined(__i386__)
/* The game's call lands here with ecx = the path, edx = the loader's own argument and the loader's three arguments on
 * the stack above the return address. eax and edx are kept around the call to BarPath (stdcall: it pops its one
 * argument and keeps ebx, esi, edi and ebp, as the game's own code expects of a call), ecx becomes the path it
 * returns, and the jump reaches the loader with the stack exactly as the game set it: the loader returns straight to
 * the game, popping its three arguments itself. A naked function rather than asm at the top of the file: the PE
 * assembler has no way to switch to .text and back, and the compiler then places it in .text itself. */
__attribute__((naked, used)) static void BarStub(void)
{
    __asm__("pushl %eax\n\t"
            "pushl %edx\n\t"
            "pushl %ecx\n\t"
            "call _BarPath@4\n\t"
            "movl %eax, %ecx\n\t"
            "popl %edx\n\t"
            "popl %eax\n\t"
            "jmp *_g_barLoader\n\t");
}
#else
static void BarStub(void) {}                           /* the tests' stand-in, never called: its address is all they use */
#endif

/* A client address as a pointer. The client's code above holds absolute addresses, so with every place verified the
 * client sits at its own base and its addresses are real ones. The tests define this to read a fake image instead. */
#ifndef ClientAt
#define ClientAt(va) ((const unsigned char *)(ULONG_PTR)(va))
#endif
static int ClientWord(unsigned va, unsigned *out)
{
    const unsigned char *p = ClientAt(va);
    if (!p || !Readable(p, 4)) return 0;
    *out = *(volatile const unsigned int *)p;
    return 1;
}
/* A NUL-terminated string of the client's into out: its length, -1 when it cannot be read, -2 when it is longer than
 * size - 1. Checked page by page, so a string at the end of its block is read no further than its NUL. */
static int ClientText(unsigned va, char *out, int size)
{
    const unsigned char *p = ClientAt(va);
    if (!p) return -1;
    for (int i = 0; i < size; i++) {
        if ((i == 0 || ((ULONG_PTR)(p + i) & 0xFFF) == 0) && !Readable(p + i, 1)) return -1;
        if (!(out[i] = (char)p[i])) return i;
    }
    return -2;
}

static int BarSame(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
/* A path the game names a texture by, against one of its own: case does not matter, nor which slash. */
static int BarSamePath(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a == '/' ? '\\' : Lower(*a), y = *b == '/' ? '\\' : Lower(*b);
        if (x != y) return 0;
        if (!x) return 1;
    }
}

/* A screen's file as bars.txt names it: after the last \ or /, before the last dot, in lower case ("Interface\Glues\
 * LoadingScreens\LoadScreenDeadmines.blp" is "loadscreendeadmines"). 0 when that is empty or over BAR_NAME_MAX. */
static int BarBaseName(const char *s, int len, char *out)
{
    int from = 0, to = len;
    for (int i = 0; i < len; i++)
        if (s[i] == '\\' || s[i] == '/') from = i + 1;
    for (int i = len - 1; i >= from; i--)
        if (s[i] == '.') { to = i; break; }
    if (to - from < 1 || to - from > BAR_NAME_MAX) return 0;
    for (int i = from; i < to; i++) out[i - from] = Lower(s[i]);
    out[to - from] = 0;
    return 1;
}

/* One line of bars.txt, without its line end. Blank lines and comments (#) are passed over; a line that is not two
 * words, a screen's name and a family's (letters and digits), is skipped and counted. A later line for the same
 * screen wins. */
static void BarLine(BarTable *t, const char *s, int n)
{
    const char *word[3];
    int len[3], words = 0;
    for (int i = 0; i < n && words < 3;) {
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= n || s[i] == '#') break;
        word[words] = s + i;
        while (i < n && s[i] != ' ' && s[i] != '\t') {
            if ((unsigned char)s[i] < 0x20 || s[i] == 0x7F) { t->skipped++; return; }
            i++;
        }
        len[words] = (int)(s + i - word[words]);
        words++;
    }
    if (!words) return;
    char name[BAR_NAME_MAX + 1], family[BAR_FAMILY_MAX + 1];
    if (words != 2 || !BarBaseName(word[0], len[0], name) || len[1] > BAR_FAMILY_MAX) { t->skipped++; return; }
    for (int i = 0; i < len[1]; i++) {
        family[i] = Lower(word[1][i]);
        if (!((family[i] >= 'a' && family[i] <= 'z') || (family[i] >= '0' && family[i] <= '9'))) { t->skipped++; return; }
    }
    family[len[1]] = 0;
    int f = 0;
    while (f < t->families && !BarSame(t->family[f].name, family)) f++;
    if (f == t->families) {
        if (f == BAR_FAMILIES) { t->skipped++; return; }
        lstrcpyA(t->family[f].name, family);
        t->families++;
    }
    if (BarSame(name, "default")) { t->fallback = f; return; }
    int k = 0;
    while (k < t->screens && !BarSame(t->screen[k].name, name)) k++;
    if (k == t->screens) {
        if (k == BAR_SCREENS) { t->skipped++; return; }
        lstrcpyA(t->screen[k].name, name);
        t->screens++;
    }
    t->screen[k].family = f;
}
/* bars.txt, len bytes (not NUL-terminated), into t. LF or CRLF; a line over BAR_LINE_MAX is skipped. */
static void BarParse(BarTable *t, const char *text, unsigned len)
{
    unsigned char *z = (unsigned char *)t;
    for (unsigned i = 0; i < sizeof *t; i++) z[i] = 0;
    t->fallback = -1;
    for (unsigned at = 0; at < len;) {
        unsigned end = at;
        while (end < len && text[end] != '\n') end++;
        int n = (int)(end - at);
        if (n > BAR_LINE_MAX) t->skipped++;
        else BarLine(t, text + at, n > 0 && text[at + (unsigned)n - 1] == '\r' ? n - 1 : n);
        at = end + 1;
    }
}

/* The loading screen's picture as the game picks it (RVA 0x6E20), into name: its file's base name, "loading" for the
 * default screen. 0 when the file's name is longer than any bars.txt can list. */
static int BarScreen(char *name)
{
    unsigned id, idMax, maps, map, ls, lsMax, recs, rec, file;
    lstrcpyA(name, "loading");
    if (!ClientWord(BAR_MAP_VA, &id) || (int)id < 0 || !ClientWord(BAR_MAPS_MAX_VA, &idMax) || (int)id > (int)idMax ||
        !ClientWord(BAR_MAPS_VA, &maps) || !ClientWord(maps + id * 4, &map) || !map ||
        !ClientWord(map + BAR_MAP_SCREEN, &ls) || (int)ls < 0 || !ClientWord(BAR_SCREENS_MAX_VA, &lsMax) ||
        (int)ls > (int)lsMax || !ClientWord(BAR_SCREENS_VA, &recs) || !ClientWord(recs + ls * 4, &rec) || !rec ||
        !ClientWord(rec + BAR_SCREEN_FILE, &file))
        return 1;
    char path[BAR_PATH_MAX + 1];
    int n = ClientText(file, path, (int)sizeof path);
    if (n == -1) return 1;                             /* no name to be read there: as the default screen */
    return n >= 0 && BarBaseName(path, n, name);
}

/* The family whose textures this screen's bar gets, or -1 for the pack's orange ones. */
static int BarFamily(const BarTable *t)
{
    char name[BAR_NAME_MAX + 1];
    int f = t->fallback;
    if (BarScreen(name))
        for (int k = 0; k < t->screens; k++)
            if (BarSame(t->screen[k].name, name)) { f = t->screen[k].family; break; }
    return f >= 0 && t->family[f].ok ? f : -1;
}

/* On the game's thread, inside its loop over the bar's table: the texture to load in place of path. Anything that is
 * not one of the bar's two paths, and any screen whose family is not in the pack, gets path back as it came. Reads
 * only; no allocation, no file, no log. */
const char *WINAPI BarPath(const char *path)
{
    int border = path && BarSamePath(path, kBarBorderPath);
    if (!g_barsOn || !path || (!border && !BarSamePath(path, kBarFillPath))) return path;
    int f = BarFamily(&g_bars);
    if (!border) InterlockedExchange(&g_barShown, f >= 0 ? f + 1 : -1);
    if (f < 0) return path;
    return border ? g_bars.family[f].border : g_bars.family[f].fill;
}

/* For the worker's line about a screen that has gone: which bar it had, if the bar was ours to choose. */
static void BarShownSince(char *out)
{
    LONG shown = InterlockedExchange(&g_barShown, 0);
    out[0] = 0;
    if (g_barsOn && shown > 0 && shown <= g_bars.families)
        wsprintfA(out, ", its fire %s", g_bars.family[shown - 1].name);
    else if (g_barsOn && shown < 0)
        lstrcpyA(out, ", its fire the pack's orange");
}

/* The rel32 that sends the call at RVA 0x6891 to BarStub. */
static unsigned BarRel(const unsigned char *base)
{
    return (unsigned)((ULONG_PTR)BarStub - (ULONG_PTR)(base + BAR_CALL_RVA + 5));
}
static unsigned Rd32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }

/* Place i holds its bytes; the call's rel32 may already be ours, from an earlier start in this process. */
static int BarPlaceHolds(const unsigned char *base, int i)
{
    const unsigned char *at = base + kBarCode[i].rva, *want = kBarCode[i].bytes;
    int n = kBarCode[i].n;
    if (!Readable(at, n)) return 0;
    if (Same(at, want, n)) return 1;
    return kBarCode[i].rva == BAR_LOAD_RVA && Same(at, want, BAR_REL_AT) &&
           Same(at + BAR_REL_AT + 4, want + BAR_REL_AT + 4, n - BAR_REL_AT - 4) && Rd32(at + BAR_REL_AT) == BarRel(base);
}

/* Both of a family's textures are in our archive: read whole, so a damaged one is found here and not by the game. */
static int BarHasTexture(const char *path)
{
    char name[100];
    if (lstrlenA(path) + 5 > (int)sizeof name) return 0;
    lstrcpyA(name, path);
    lstrcatA(name, ".blp");
    unsigned char *data = NULL;
    unsigned len = 0;
    int r = mpq_read(&kIo, "patch-~.MPQ", name, &data, &len);
    if (data) HeapFree(GetProcessHeap(), 0, data);
    return r == MPQ_OK;
}

/* bars.txt is ours, but a file in the player's folder all the same: nothing bigger is even read in. */
static void *IoAllocSmall(void *ctx, unsigned size) { return size > 4 * BAR_TEXT_MAX ? NULL : IoAlloc(ctx, size); }
static const mpq_io kIoSmall = { IoAllocSmall, IoRelease, IoOpen, IoReadAt, IoSize, IoClose, NULL };

/* At start, after our screens and the switch are in place: reads bars.txt and the families out of Data\patch-~.MPQ,
 * then sends the one call to BarStub. 1 when the bar's fire follows the screens. */
static int FireBar(void)
{
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    char m[400];
    for (int i = 0; i < BAR_CODE_COUNT; i++) {
        if (BarPlaceHolds(base, i)) continue;
        int len = wsprintfA(m, "fire bar off: other bytes at RVA 0x%lX (%s) than in the client it was made for, so "
                            "every bar is the pack's orange; there:", (unsigned long)kBarCode[i].rva, kBarCode[i].what);
        if (Readable(base + kBarCode[i].rva, kBarCode[i].n))
            for (int k = 0; k < kBarCode[i].n; k++)
                len += wsprintfA(m + len, " %02X", base[kBarCode[i].rva + (DWORD)k]);
        else
            lstrcpyA(m + len, " (unreadable)");
        Log(m);
        return 0;
    }
    unsigned char *at = base + BAR_CALL_RVA;
    if (Rd32(at + 1) == BarRel(base)) {
        Log("fire bar already on: nothing to do");
        return 1;
    }
    InterlockedExchange(&g_barsOn, 0);

    unsigned char *text = NULL;
    unsigned len = 0;
    int r = mpq_read(&kIoSmall, "patch-~.MPQ", "CinderLoad\\bars.txt", &text, &len);
    if (r != MPQ_OK || len > BAR_TEXT_MAX) {
        if (text) HeapFree(GetProcessHeap(), 0, text);
        if (r == MPQ_E_NOT_FOUND)
            Log("fire bar off: Data\\patch-~.MPQ holds no CinderLoad\\bars.txt, so every bar is the pack's orange");
        else if (r == MPQ_OK || r == MPQ_E_NOMEM)
            Log("fire bar off: CinderLoad\\bars.txt is over 64 KB, so every bar is the pack's orange");
        else {
            wsprintfA(m, "fire bar off: CinderLoad\\bars.txt could not be read from Data\\patch-~.MPQ (%s), so every "
                      "bar is the pack's orange", mpq_error_text(r));
            Log(m);
        }
        return 0;
    }
    BarParse(&g_bars, (const char *)text, len);
    HeapFree(GetProcessHeap(), 0, text);

    int have = 0, miss = 0;
    char haveList[BAR_FAMILIES * (BAR_FAMILY_MAX + 2) + 1], missList[BAR_FAMILIES * (BAR_FAMILY_MAX + 2) + 1];
    haveList[0] = missList[0] = 0;
    for (int f = 0; f < g_bars.families; f++) {
        lstrcpyA(g_bars.family[f].fill, kBarFillPath);
        lstrcatA(g_bars.family[f].fill, "-");
        lstrcatA(g_bars.family[f].fill, g_bars.family[f].name);
        lstrcpyA(g_bars.family[f].border, kBarBorderPath);
        lstrcatA(g_bars.family[f].border, "-");
        lstrcatA(g_bars.family[f].border, g_bars.family[f].name);
        g_bars.family[f].ok = BarHasTexture(g_bars.family[f].fill) && BarHasTexture(g_bars.family[f].border);
        char *list = g_bars.family[f].ok ? haveList : missList;
        int *count = g_bars.family[f].ok ? &have : &miss;
        if ((*count)++) lstrcatA(list, ", ");
        lstrcatA(list, g_bars.family[f].name);
    }
    wsprintfA(m, "fire bar: bars.txt names %d screens, %s, %d lines skipped", g_bars.screens,
              g_bars.fallback < 0 ? "no default (the screens it does not list keep the pack's orange)"
                                  : "and a default for the rest", g_bars.skipped);
    Log(m);
    wsprintfA(m, "fire bar: families in the pack: %s; named but not in it (their screens keep the pack's orange): %s",
              have ? haveList : "none", miss ? missList : "none");
    Log(m);
    if (!have) {
        Log("fire bar off: no family bars.txt names has both its textures in the pack");
        return 0;
    }

    /* Everything BarPath reads is in place before the call can reach it. */
    g_barLoader = at + 5 + (int)Rd32(at + 1);          /* where the game's call went: the loader, VA 0x449D90 */
    InterlockedExchange(&g_barsOn, 1);
    unsigned rel = BarRel(base);
    DWORD old;
    if (!VirtualProtect(at + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
        InterlockedExchange(&g_barsOn, 0);
        Log("fire bar off: the code could not be made writable");
        return 0;
    }
    for (int i = 0; i < 4; i++)
        at[1 + i] = (unsigned char)(rel >> (8 * i));
    VirtualProtect(at + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 5);
    if (Rd32(at + 1) != rel) {
        InterlockedExchange(&g_barsOn, 0);           /* BarPath then hands every path back as it came */
        Log("fire bar off: the bytes read back differ");
        return 0;
    }
    Log("fire bar on: each loading screen's bar burns in its picture's fire (one call at RVA 0x6891, in memory only)");
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        if (GameDir() && ChooseScreens()) {
            if (Apply()) {
                WideBar();
                FireBar();
            } else {
                TakeOutAfterAll();
            }
        }
        if (g_dir[0]) FireStart();
    }
    return TRUE;
}

BOOL WINAPI DllMainCRTStartup(HINSTANCE inst, DWORD reason, LPVOID reserved) { return DllMain(inst, reason, reserved); }
