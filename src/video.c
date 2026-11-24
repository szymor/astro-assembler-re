/*
 * Presentation, frontend-neutral part: hands the menu surface or the converted
 * gameplay frame to the frontend, counts frames, and writes debug dumps.
 */
#include "astro.h"

#include <stdlib.h>

static unsigned long g_frames;

unsigned long video_frame_count(void) { return g_frames; }

/* ASTRO_DUMP=<dir>: save every 60th presented frame as BMP
 * (frameNNNNN.bmp = menus, gameNNNNN.bmp/.raw = gameplay). */
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
    plat_present(px, SCREEN_W, SCREEN_H);
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
    plat_present(rgb, GAME_W, GAME_H);
}
