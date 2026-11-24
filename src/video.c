/*
 * Presentation, frontend-neutral part: hands the menu surface or the converted
 * gameplay frame to the frontend, counts frames, and writes debug dumps.
 */
#include "astro.h"
#include "scale.h"

#include <stdlib.h>
#include <string.h>

static unsigned long g_frames;
static bool g_game;
static uint32_t g_out[SCREEN_W * SCREEN_H];   /* reduced-resolution picture */

unsigned long video_frame_count(void) { return g_frames; }
bool video_is_game(void) { return g_game; }

void video_output_size(int *w, int *h)
{
    switch (settings.resolution) {
    case RES_320X240: *w = 320; *h = 240; break;
    case RES_320X200: *w = 320; *h = 200; break;
    default: *w = SCREEN_W; *h = SCREEN_H; break;
    }
}

/* 640x480 output: a 640x480 window, as before. Reduced outputs: three times
 * 320x240 (still 4:3), or twice when that does not fit the desktop. */
void video_window_size(int desktop_w, int desktop_h, int *w, int *h)
{
    *w = SCREEN_W; *h = SCREEN_H;
    if (settings.resolution == RES_640X480) return;
    if (desktop_w <= 0 || desktop_h <= 0 || (desktop_w >= 960 && desktop_h >= 800)) { *w = 960; *h = 720; }
}

/* ASTRO_DUMP=<dir>: save every 60th presented frame as BMP
 * (frameNNNNN.bmp = menus, gameNNNNN.bmp/.raw = gameplay). */
/* Reduced output: also dump the picture the frontend gets (<name>-out.bmp). */
static void present_reduced(const char *dir, const char *name, int w, int h)
{
    if (dir) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s%05lu-out.bmp", dir, name, g_frames - 1);
        astro_write_bmp(path, g_out, w, h);
    }
    plat_present(g_out, w, h);
}

static const char *dump_dir(void)
{
    static const char *dir;
    static bool checked;
    if (!checked) { dir = getenv("ASTRO_DUMP"); checked = true; }
    return dir && g_frames % 60 == 0 ? dir : NULL;
}

/* The menus draw straight into the 640x480 32 bpp primary surface. */
void video_present_primary(void)
{
    const uint32_t *px = (const uint32_t *)&mem[VA_PRIMARY];
    const char *dir = dump_dir();
    if (dir) {
        char path[1100];
        snprintf(path, sizeof path, "%s/frame%05lu.bmp", dir, g_frames);
        astro_write_bmp(path, px, SCREEN_W, SCREEN_H);
    }
    g_frames++;
    g_game = false;
    int ow, oh;
    video_output_size(&ow, &oh);
    if (ow == SCREEN_W && oh == SCREEN_H) {
        plat_present(px, SCREEN_W, SCREEN_H);
    } else {   /* reduced output: average the menu picture down (keeps thin font strokes) */
        scale_area(px, SCREEN_W, SCREEN_H, g_out, ow, oh);
        present_reduced(dir, "frame", ow, oh);
    }
}

/*
 * Gameplay: the core renders an 8-bit 320x200 frame. With the palette flag the
 * port sets, the core also keeps the current palette as 256 PALETTEENTRY
 * (r, g, b, flags; already scaled to 8 bits) at G_PALETTE_ENTRIES.
 */
void video_present_game(uint32_t frame_va)
{
    static uint32_t rgb[GAME_W * GAME_H];
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t *e = &mem[G_PALETTE_ENTRIES + 4u * (uint32_t)i];
        pal[i] = (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
    }
    const uint8_t *src = &mem[frame_va];
    for (int i = 0; i < GAME_W * GAME_H; i++) rgb[i] = pal[src[i]];

    const char *dir = dump_dir();
    if (dir) {
        char path[1100];
        snprintf(path, sizeof path, "%s/game%05lu.bmp", dir, g_frames);
        astro_write_bmp(path, rgb, GAME_W, GAME_H);
        snprintf(path, sizeof path, "%s/game%05lu.raw", dir, g_frames);   /* indices + palette */
        FILE *f = astro_fopen(path, "wb");
        if (f) {
            fwrite(src, 1, GAME_W * GAME_H, f);
            fwrite(&mem[G_PALETTE_ENTRIES], 1, 1024, f);
            fclose(f);
        }
    }
    g_frames++;
    g_game = true;
    int ow, oh;
    video_output_size(&ow, &oh);
    if (ow == SCREEN_W || oh == GAME_H) {          /* 640x480 / 320x200: the frame as it is */
        plat_present(rgb, GAME_W, GAME_H);
    } else if (settings.letterbox) {               /* 320x240: 1:1 between black bars */
        int top = (oh - GAME_H) / 2;
        memset(g_out, 0, sizeof(uint32_t) * (size_t)ow * (size_t)oh);
        memcpy(g_out + (size_t)top * (size_t)ow, rgb, sizeof rgb);
        present_reduced(dir, "game", ow, oh);
    } else {                                       /* 320x240: stretch 200 -> 240 lines */
        if (settings.filter == FILTER_SHARP) scale_area(rgb, GAME_W, GAME_H, g_out, ow, oh);
        else scale_xrgb(rgb, GAME_W, GAME_H, g_out, ow, 0, 0, ow, oh, settings.filter);
        present_reduced(dir, "game", ow, oh);
    }
}
