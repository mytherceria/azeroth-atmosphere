/* The screen-shape choice of WideLoading.dll, kept free of Windows calls so it can be tested on its own
 * (tests/test_shape.c). Included once, by wideloading.c.
 *
 * The loading screens come in one archive per screen shape, Data\WideLoading\LoadingScreens-<shape>.MPQ. The
 * shape is read from the resolution the game saved (SET gxResolution "WxH" in WTF\Config.wtf) and the nearest
 * shape within 10% is used. The ultrawides are one shape: 2560x1080, 3440x1440, 3840x1600 and 5120x2160 are all
 * within 1.3% of 64:27.
 */
#ifndef WIDELOADING_SHAPE_H
#define WIDELOADING_SHAPE_H

static const struct { const char *name; int num, den; } kShapes[] = {
    { "16x10", 16, 10 },        /* 1.600 */
    { "16x9",  16, 9  },        /* 1.778 */
    { "21x9",  64, 27 },        /* 2.370: 2560x1080, 3440x1440, 3840x1600, 5120x2160 */
    { "32x9",  32, 9  },        /* 3.556: 5120x1440, 7680x2160 */
};
#define SHAPE_COUNT ((int)(sizeof kShapes / sizeof kShapes[0]))
#define SHAPE_TOLERANCE 100     /* per mille: within 10% of a shape, or none */

static char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

/* The resolution in a Config.wtf's text (NUL-terminated): the last SET gxResolution "WxH" line, as the game reads
 * the file top to bottom. 1 and *w, *h set when found and sane. */
static int ParseResolution(const char *text, int *w, int *h)
{
    static const char key[] = "set gxresolution \"";
    int found = 0;
    for (const char *p = text; *p; p++) {
        if (p != text && p[-1] != '\n') continue;          /* only at the start of a line */
        int i = 0;
        while (key[i] && Lower(p[i]) == key[i]) i++;
        if (key[i]) continue;
        const char *q = p + i;
        long a = 0, b = 0;
        int da = 0, db = 0;
        while (*q >= '0' && *q <= '9' && da < 6) { a = a * 10 + (*q++ - '0'); da++; }
        if (*q != 'x' && *q != 'X') continue;
        q++;
        while (*q >= '0' && *q <= '9' && db < 6) { b = b * 10 + (*q++ - '0'); db++; }
        if (*q != '"' || a < 320 || b < 200 || a > 32768 || b > 32768) continue;
        *w = (int)a; *h = (int)b;
        found = 1;
    }
    return found;
}

/* The index of the nearest shape within SHAPE_TOLERANCE of w:h, or -1. available[i] says whether that shape's
 * archive is there; only those count. */
static int ChooseShape(int w, int h, const int *available)
{
    if (w <= 0 || h <= 0) return -1;
    long aspect = (long)w * 1000 / h;
    int best = -1;
    long bestDiff = 0;
    for (int i = 0; i < SHAPE_COUNT; i++) {
        if (!available[i]) continue;
        long s = (long)kShapes[i].num * 1000 / kShapes[i].den;
        long diff = (aspect > s ? aspect - s : s - aspect) * 1000 / s;
        if (diff <= SHAPE_TOLERANCE && (best < 0 || diff < bestDiff)) { best = i; bestDiff = diff; }
    }
    return best;
}

#endif
