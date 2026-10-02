/* Runs CinderLoad.dll's own code (dll/cinderload.c) against fake game folders, on POSIX through tests/shim:
 *   gcc -std=c99 -Wall -Wno-unused-function -Itests/shim -o /tmp/test_switch tests/test_switch.c && /tmp/test_switch <scratch dir>
 * Each case is a fresh folder under <scratch dir>; nothing outside it is touched. */
#include "../dll/cinderload.c"
#include <stdio.h>
#include <sys/stat.h>

char shim_gamedir[512];
unsigned char shim_module[0x400000];
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
    shim_screen_w = 3440; shim_screen_h = 1440;
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
#define MARK "Data/CinderLoad/installed.txt"
#define RES(w) "SET gxWindow \"1\"\r\nSET gxResolution \"" w "\"\r\n"

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
    Check(Is(ACTIVE, "AAAA") && shim_module[PATCH_RVA] == 0xE8 && shim_module[PATCH_RVA + 1] == 0x11 &&
          LogSays("not the client this was made for"), "another WoW.exe: its code is never changed");

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
    Check(On() && shim_module[BAR_BORDER_RVA + 8] == 0x11 && Same(shim_module + BAR_FILL_RVA, kFillStock, 16) &&
          LogSays("holds other numbers"), "a size table with other numbers: never changed");

    if (fails) printf("%d of %d checks FAILED\n", fails, checks); else printf("all %d checks passed\n", checks);
    return fails != 0;
}
