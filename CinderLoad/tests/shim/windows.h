/* A stand-in for <windows.h> with just what cinderload.c calls, on POSIX files, so the DLL's own code runs in
 * tests/test_switch.c. Paths with backslashes are turned into slashes; the game folder and the module's code are
 * set by the test. */
#ifndef SHIM_WINDOWS_H
#define SHIM_WINDOWS_H
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE, *HINSTANCE, *LPVOID, *HMODULE;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef struct { unsigned short wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct { void *BaseAddress, *AllocationBase; DWORD AllocationProtect; size_t RegionSize; DWORD State, Protect, Type; } MEMORY_BASIC_INFORMATION;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define DLL_PROCESS_ATTACH 1
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFF)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define FILE_APPEND_DATA 4
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define CREATE_ALWAYS 2
#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 1
#define PAGE_GUARD 0x100
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

extern char shim_gamedir[];                 /* "/tmp/x/" */
extern unsigned char shim_module[0x8000];   /* WoW.exe's first pages */
extern int shim_screen_w, shim_screen_h;

static void ShimPath(char *out, const char *in) { strcpy(out, in); for (char *p = out; *p; p++) if (*p == '\\') *p = '/'; }
static HANDLE ToHandle(int fd) { return fd < 0 ? INVALID_HANDLE_VALUE : (HANDLE)(long)(fd + 1); }
static int Fd(HANDLE h) { return (int)(long)h - 1; }

static DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD n) { (void)m; snprintf(out, n, "%sWoW.exe", shim_gamedir); return (DWORD)strlen(out); }
static HMODULE GetModuleHandleA(const char *n) { (void)n; return (HMODULE)shim_module; }
static HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *sa, DWORD disp, DWORD flags, HANDLE t)
{
    (void)share; (void)sa; (void)flags; (void)t;
    char p[1024]; ShimPath(p, path);
    int o = access == GENERIC_READ ? O_RDONLY : (access == FILE_APPEND_DATA ? O_WRONLY | O_APPEND : O_WRONLY);
    if (disp == OPEN_ALWAYS) o |= O_CREAT;
    if (disp == CREATE_ALWAYS) o |= O_CREAT | O_TRUNC;
    return ToHandle(open(p, o, 0644));
}
static BOOL CloseHandle(HANDLE h) { return close(Fd(h)) == 0; }
static DWORD GetFileSize(HANDLE h, DWORD *hi) { (void)hi; struct stat st; return fstat(Fd(h), &st) ? INVALID_FILE_SIZE : (DWORD)st.st_size; }
static BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov) { (void)ov; long r = read(Fd(h), buf, n); *got = r < 0 ? 0 : (DWORD)r; return r >= 0; }
static BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *put, void *ov) { (void)ov; long r = write(Fd(h), buf, n); *put = r < 0 ? 0 : (DWORD)r; return r >= 0; }
static DWORD GetFileAttributesA(const char *path) { char p[1024]; ShimPath(p, path); struct stat st; return stat(p, &st) ? INVALID_FILE_ATTRIBUTES : (S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL); }
static BOOL DeleteFileA(const char *path) { char p[1024]; ShimPath(p, path); return unlink(p) == 0; }
static BOOL CopyFileA(const char *from, const char *to, BOOL failIfExists)
{
    (void)failIfExists; char a[1024], b[1024]; ShimPath(a, from); ShimPath(b, to);
    FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL; char buf[4096]; size_t n;
    if (!in || !out) { if (in) fclose(in); return FALSE; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out); return TRUE;
}
static DWORD GetLastError(void) { return 0; }
static void GetLocalTime(SYSTEMTIME *t) { memset(t, 0, sizeof *t); }
static HANDLE GetProcessHeap(void) { return (HANDLE)1; }
static void *HeapAlloc(HANDLE h, DWORD f, size_t n) { (void)h; (void)f; return malloc(n); }
static BOOL HeapFree(HANDLE h, DWORD f, void *p) { (void)h; (void)f; free(p); return TRUE; }
static int lstrlenA(const char *s) { return (int)strlen(s); }
static char *lstrcpyA(char *d, const char *s) { return strcpy(d, s); }
static char *lstrcatA(char *d, const char *s) { return strcat(d, s); }
static int wsprintfA(char *out, const char *fmt, ...) { va_list a; va_start(a, fmt); int n = vsprintf(out, fmt, a); va_end(a); return n; }
static int GetSystemMetrics(int i) { return i == SM_CXSCREEN ? shim_screen_w : shim_screen_h; }
static size_t VirtualQuery(const void *at, MEMORY_BASIC_INFORMATION *m, size_t n)
{
    (void)at; memset(m, 0, n); m->BaseAddress = shim_module; m->RegionSize = sizeof shim_module;
    m->State = MEM_COMMIT; m->Protect = PAGE_EXECUTE_READ; return n;
}
static BOOL VirtualProtect(void *at, size_t n, DWORD prot, DWORD *old) { (void)at; (void)n; (void)prot; *old = PAGE_EXECUTE_READ; return TRUE; }
static BOOL FlushInstructionCache(HANDLE p, const void *at, size_t n) { (void)p; (void)at; (void)n; return TRUE; }
static HANDLE GetCurrentProcess(void) { return (HANDLE)1; }
static BOOL DisableThreadLibraryCalls(HINSTANCE h) { (void)h; return TRUE; }
#endif
