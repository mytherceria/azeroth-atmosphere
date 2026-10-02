/* Native tests of CinderLoad's shape choice (dll/shape.h): gcc -std=c99 -Wall -o /tmp/t test_shape.c && /tmp/t */
#include <stdio.h>
#include "../dll/shape.h"

static int fails, checks;
static void Check(int cond, const char *what) { checks++; if (!cond) { printf("FAIL: %s\n", what); fails++; } }

static const char *Pick(int w, int h)
{
    static const int all[SHAPE_COUNT] = { 1, 1, 1, 1 };
    int i = ChooseShape(w, h, all);
    return i < 0 ? "none" : kShapes[i].name;
}

static int Is(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void)
{
    int w = 0, h = 0;
    Check(ParseResolution("SET gxResolution \"5120x2160\"\r\nSET gxWindow \"1\"\r\n", &w, &h) && w == 5120 && h == 2160,
          "his own Config.wtf line");
    w = h = 0;
    Check(ParseResolution("SET gxColorBits \"24\"\nset gxresolution \"3440x1440\"\n", &w, &h) && w == 3440 && h == 1440,
          "lower case, not on the first line");
    w = h = 0;
    Check(ParseResolution("SET gxResolution \"1920x1080\"\nSET gxResolution \"2560x1440\"\n", &w, &h) && w == 2560,
          "the last line wins, as the game reads it");
    Check(!ParseResolution("SET gxRefresh \"165\"\n", &w, &h), "no resolution line");
    Check(!ParseResolution("SET gxResolution \"5120x\"\n", &w, &h), "a broken value");
    Check(!ParseResolution("SET gxResolution \"10x10\"\n", &w, &h), "an impossible size");
    Check(!ParseResolution("# SET gxResolution \"1920x1080\"\n", &w, &h), "not at the start of a line");
    Check(!ParseResolution("", &w, &h), "an empty file");

    Check(Is(Pick(5120, 2160), "21x9"), "5K2K is 21x9");
    Check(Is(Pick(3440, 1440), "21x9"), "Alienware 3440x1440 is 21x9");
    Check(Is(Pick(3840, 1600), "21x9"), "3840x1600 is 21x9");
    Check(Is(Pick(2560, 1080), "21x9"), "2560x1080 is 21x9");
    Check(Is(Pick(1920, 1080), "16x9"), "1080p is 16x9");
    Check(Is(Pick(2560, 1440), "16x9"), "1440p is 16x9");
    Check(Is(Pick(3840, 2160), "16x9"), "4K is 16x9");
    Check(Is(Pick(1920, 1200), "16x10"), "1920x1200 is 16x10");
    Check(Is(Pick(2560, 1600), "16x10"), "2560x1600 is 16x10");
    Check(Is(Pick(5120, 1440), "32x9"), "Samsung G9 5120x1440 is 32x9");
    Check(Is(Pick(1024, 768), "none"), "4:3 gets the game's own");
    Check(Is(Pick(1280, 1024), "none"), "5:4 gets the game's own");
    Check(Is(Pick(0, 0), "none"), "no size");

    int only21[SHAPE_COUNT] = { 0, 0, 1, 0 };
    Check(ChooseShape(1920, 1080, only21) == -1, "16:9 with only the ultrawide archive: none, not a stretched one");
    Check(ChooseShape(3440, 1440, only21) == 2, "ultrawide with only the ultrawide archive");

    if (fails) printf("%d of %d checks FAILED\n", fails, checks); else printf("all %d checks passed\n", checks);
    return fails != 0;
}
