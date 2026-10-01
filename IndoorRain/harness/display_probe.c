/* display_probe.c: native Linux, for harness/run.sh, which runs it with the environment it gives Wine and starts
 * no Wine unless it passes. It asks what Wine's two display drivers would ask. winex11 calls XOpenDisplay(NULL),
 * which reads DISPLAY and nothing else. winewayland calls wl_display_connect(NULL), which takes WAYLAND_SOCKET,
 * else WAYLAND_DISPLAY, and without either tries wayland-0 in XDG_RUNTIME_DIR: on a Wayland desktop that is the
 * user's own compositor, so unsetting WAYLAND_DISPLAY alone is not headless. run.sh also disables both drivers in
 * Wine itself; this checks the environment under that, so neither layer rests on the other.
 * The libraries are opened at run time: a machine without one of them builds and runs this all the same, and
 * Wine could not use the one it lacks either. A display it reaches is disconnected at once; it opens no window.
 * Exit code: 0 when no display is reachable, 1 when one is (the output says which).
 *   gcc -O2 -Wall -o display_probe display_probe.c -ldl   (run.sh builds it into the run folder, outside the repo)
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static void *sym(void *lib, const char *name)
{
    return lib ? dlsym(lib, name) : NULL;
}
static void said(const char *what, int reached, const char *env)
{
    if (reached) printf("%-8s REACHED a display (%s=%s)\n", what, env, getenv(env) ? getenv(env) : "unset, so the default");
    else printf("%-8s no display reachable\n", what);
}

int main(void)
{
    int reached = 0, r;
    void *x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL), *wl = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
    /* dlsym hands back object pointers; POSIX guarantees they convert to function pointers */
    void *(*x_open)(const char *); int (*x_close)(void *);
    void *(*wl_connect)(const char *); void (*wl_disconnect)(void *);
    *(void **)&x_open = sym(x11, "XOpenDisplay"); *(void **)&x_close = sym(x11, "XCloseDisplay");
    *(void **)&wl_connect = sym(wl, "wl_display_connect"); *(void **)&wl_disconnect = sym(wl, "wl_display_disconnect");
    if (!x_open || !x_close) printf("%-8s no libX11.so.6 here, so Wine has no X11 driver either\n", "X11:");
    else { void *d = x_open(NULL); r = d != NULL; if (d) x_close(d); said("X11:", r, "DISPLAY"); reached |= r; }
    if (!wl_connect || !wl_disconnect) printf("%-8s no libwayland-client.so.0 here, so Wine has no Wayland driver either\n", "Wayland:");
    else { void *d = wl_connect(NULL); r = d != NULL; if (d) wl_disconnect(d); said("Wayland:", r, "WAYLAND_DISPLAY"); reached |= r; }
    if (reached) printf("XDG_RUNTIME_DIR=%s\n", getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
    return reached;
}
