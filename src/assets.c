/* Loading files from the packaged asset directory (tools/package_assets.py). */
#include "astro.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

static char g_dir[1024] = "assets";

void assets_set_dir(const char *dir) { SDL_strlcpy(g_dir, dir, sizeof g_dir); }
const char *assets_dir(void) { return g_dir; }

void *assets_read(const char *rel, size_t *size)
{
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", g_dir, rel);
    void *data = SDL_LoadFile(path, size);
    if (!data) SDL_Log("cannot read %s: %s", path, SDL_GetError());
    return data;
}

bool assets_load_to_mem(const char *rel, uint32_t va, uint32_t *size_out)
{
    size_t size = 0;
    void *data = assets_read(rel, &size);
    if (!data) return false;
    if ((uint64_t)va + size > RT_MEM_SIZE) {
        SDL_Log("%s does not fit at %08x", rel, va);
        SDL_free(data);
        return false;
    }
    memcpy(&mem[va], data, size);
    SDL_free(data);
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
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", g_dir, rel);
    SDL_Surface *s = SDL_LoadPNG(path);
    if (!s) { SDL_Log("cannot load %s: %s", path, SDL_GetError()); return false; }
    SDL_Surface *x = SDL_ConvertSurface(s, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(s);
    if (!x) return false;
    if (x->w != SCREEN_W || x->h != SCREEN_H) {
        SDL_Log("%s is %dx%d, expected 640x480", rel, x->w, x->h);
        SDL_DestroySurface(x);
        return false;
    }
    for (int y = 0; y < SCREEN_H; y++) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)x->pixels + (size_t)y * x->pitch);
        uint8_t *dst = &mem[va + (uint32_t)(y * SCREEN_W * SCREEN_BPP)];
        for (int i = 0; i < SCREEN_W; i++) {
            uint32_t p = src[i] & 0x00ffffffu;
            memcpy(dst + 4 * i, &p, 4);
        }
    }
    SDL_DestroySurface(x);
    return true;
}
