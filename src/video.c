/* SDL3 window, presentation of the emulated primary surface, frame pacing. */
#include "astro.h"

#include <SDL3/SDL.h>

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Texture *g_game_texture;   /* 320x200 gameplay frame */
static Uint64 g_next_frame_ns;

/* The original synchronised to the display's vertical blank at the driver's
 * default refresh rate (most likely 60 Hz, see docs/plan.md). */
#define FRAME_NS (SDL_NS_PER_SECOND / 60)

bool video_init(void)
{
    if (!SDL_CreateWindowAndRenderer("Astro Assembler", SCREEN_W, SCREEN_H, SDL_WINDOW_RESIZABLE,
                                     &g_window, &g_renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(g_renderer, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  SCREEN_W, SCREEN_H);
    if (!g_texture) { SDL_Log("texture: %s", SDL_GetError()); return false; }
    SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    g_game_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       GAME_W, GAME_H);
    if (!g_game_texture) { SDL_Log("texture: %s", SDL_GetError()); return false; }
    /* TODO: decide how to reproduce the original's upscaling (docs/plan.md). */
    SDL_SetTextureScaleMode(g_game_texture, SDL_SCALEMODE_NEAREST);
    SDL_HideCursor();
    g_next_frame_ns = SDL_GetTicksNS();
    return true;
}

void video_shutdown(void)
{
    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
}

static unsigned long g_frames;

unsigned long video_frame_count(void) { return g_frames; }

/* Debugging aid: ASTRO_DUMP=<dir> saves every 60th presented frame as BMP. */
static void dump_frame(void)
{
    static const char *dir;
    static bool checked;
    unsigned long frame = g_frames++;
    if (!checked) { dir = SDL_getenv("ASTRO_DUMP"); checked = true; }
    if (!dir || frame % 60) return;
    SDL_Surface *s = SDL_CreateSurfaceFrom(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_XRGB8888, &mem[VA_PRIMARY],
                                           SCREEN_W * SCREEN_BPP);
    if (!s) return;
    char path[1024];
    SDL_snprintf(path, sizeof path, "%s/frame%05lu.bmp", dir, frame);
    SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
}

void video_present_primary(void)
{
    dump_frame();
    SDL_UpdateTexture(g_texture, NULL, &mem[VA_PRIMARY], SCREEN_W * SCREEN_BPP);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

/*
 * Gameplay: the core renders an 8-bit 320x200 frame. With the palette flag the
 * port sets, the core also keeps the current palette as 256 PALETTEENTRY
 * (r, g, b, flags; already scaled to 8 bits) at G_PALETTE_ENTRIES.
 */
void video_present_game(uint32_t frame_va)
{
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t *e = &mem[G_PALETTE_ENTRIES + 4u * (uint32_t)i];
        pal[i] = (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
    }
    void *pixels;
    int pitch;
    if (SDL_LockTexture(g_game_texture, NULL, &pixels, &pitch)) {
        for (int y = 0; y < GAME_H; y++) {
            uint32_t *dst = (uint32_t *)((uint8_t *)pixels + (size_t)y * (size_t)pitch);
            const uint8_t *src = &mem[frame_va + (uint32_t)(y * GAME_W)];
            for (int x = 0; x < GAME_W; x++) dst[x] = pal[src[x]];
        }
        SDL_UnlockTexture(g_game_texture);
    }
    const char *dir = SDL_getenv("ASTRO_DUMP");
    if (dir && g_frames % 60 == 0) {
        static uint32_t rgb[GAME_W * GAME_H];
        for (int i = 0; i < GAME_W * GAME_H; i++) rgb[i] = pal[mem[frame_va + (uint32_t)i]];
        SDL_Surface *s = SDL_CreateSurfaceFrom(GAME_W, GAME_H, SDL_PIXELFORMAT_XRGB8888, rgb, GAME_W * 4);
        if (s) {
            char path[1024];
            SDL_snprintf(path, sizeof path, "%s/game%05lu.bmp", dir, g_frames);
            SDL_SaveBMP(s, path);
            SDL_DestroySurface(s);
            /* raw indices + PALETTEENTRY palette for analysis */
            SDL_snprintf(path, sizeof path, "%s/game%05lu.raw", dir, g_frames);
            SDL_IOStream *io = SDL_IOFromFile(path, "wb");
            if (io) {
                SDL_WriteIO(io, &mem[frame_va], GAME_W * GAME_H);
                SDL_WriteIO(io, &mem[G_PALETTE_ENTRIES], 1024);
                SDL_CloseIO(io);
            }
        }
    }
    g_frames++;
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderTexture(g_renderer, g_game_texture, NULL, NULL);
    SDL_RenderPresent(g_renderer);
}

void video_wait_frame(void)
{
    Uint64 now = SDL_GetTicksNS();
    g_next_frame_ns += FRAME_NS;
    if (g_next_frame_ns > now) SDL_DelayPrecise(g_next_frame_ns - now);
    else if (now - g_next_frame_ns > 4 * FRAME_NS) g_next_frame_ns = now;   /* fell behind: resync */
}

void video_toggle_fullscreen(void)
{
    bool fs = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(g_window, !fs);
}
