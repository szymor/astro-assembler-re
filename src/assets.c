/* Loading files from the packaged asset directory (tools/package_assets.py). */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

static char g_dir[1024] = "assets";

void assets_set_dir(const char *dir) { snprintf(g_dir, sizeof g_dir, "%s", dir); }
const char *assets_dir(void) { return g_dir; }

FILE *assets_open(const char *rel)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_dir, rel);
    return astro_fopen(path, "rb");
}

void *assets_read(const char *rel, size_t *size)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_dir, rel);
    void *data = astro_read_file(path, size);
    if (!data) astro_log("cannot read %s", path);
    return data;
}

bool assets_load_to_mem(const char *rel, uint32_t va, uint32_t *size_out)
{
    size_t size = 0;
    void *data = assets_read(rel, &size);
    if (!data) return false;
    if ((uint64_t)va + size > RT_MEM_SIZE) {
        astro_log("%s does not fit at %08x", rel, va);
        free(data);
        return false;
    }
    memcpy(&mem[va], data, size);
    free(data);
    if (size_out) *size_out = (uint32_t)size;
    return true;
}

/*
 * The original shell (Gfx_LoadBitmap) blits each 640x480 BMP into a surface of
 * the menu mode's pixel format and copies the lines into the arena. The port's
 * menu mode is 32 bpp XRGB with the unused byte 0, as DirectDraw left it.
 */
bool assets_load_image(const char *rel, uint32_t va)
{
    size_t size = 0;
    void *file = assets_read(rel, &size);
    if (!file) return false;
    int w = 0, h = 0;
    uint32_t *px = png_decode_xrgb(file, size, &w, &h);
    free(file);
    if (!px) { astro_log("cannot decode %s", rel); return false; }
    if (w != SCREEN_W || h != SCREEN_H) {
        astro_log("%s is %dx%d, expected 640x480", rel, w, h);
        free(px);
        return false;
    }
    memcpy(&mem[va], px, (size_t)SCREEN_W * SCREEN_H * 4);
    free(px);
    return true;
}
