/*
 * SDL3 window, presentation of the emulated primary surface (menus, 640x480)
 * and the gameplay frame (320x200), frame pacing.
 *
 * The original always filled a 4:3 screen, so both pictures are shown in the
 * largest 4:3 area of the window (the 320x200 frame therefore has the same
 * slightly tall pixels as on the original's screen). Filters (settings.c):
 *  - sharp (default): integer nearest-neighbour upscale into an intermediate
 *    texture, then a linear stretch of the remaining fraction; crisp pixels
 *    without uneven rows (200 -> 480 lines is a 2.4x stretch);
 *  - nearest: plain nearest neighbour;
 *  - smooth: plain bilinear.
 * The original's look depended on the hardware (unfiltered DirectDraw stretch
 * or filtered Direct3D), so there is no single reference.
 */
#include "astro.h"

#include <SDL3/SDL.h>
#include <math.h>

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;        /* 640x480 menu surface   */
static SDL_Texture *g_game_texture;   /* 320x200 gameplay frame */
static SDL_Texture *g_prescaled;      /* intermediate for "sharp" */
static int g_prescaled_w, g_prescaled_h;
static Uint64 g_next_frame_ns;
static unsigned long g_frames;
static char g_dump_output[1100];      /* set when the scaled image should be saved */

/* The original synchronised to the display's vertical blank at the driver's
 * default refresh rate, taken to be 60 Hz (decided, docs/plan.md). */
#define FRAME_NS (SDL_NS_PER_SECOND / 60)

bool video_init(void)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (settings.fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    if (!SDL_CreateWindowAndRenderer("Astro Assembler", SCREEN_W, SCREEN_H, flags, &g_window, &g_renderer)) {
        SDL_Log("window: %s", SDL_GetError());
        return false;
    }
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                  SCREEN_W, SCREEN_H);
    g_game_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       GAME_W, GAME_H);
    if (!g_texture || !g_game_texture) { SDL_Log("texture: %s", SDL_GetError()); return false; }
    SDL_HideCursor();
    g_next_frame_ns = SDL_GetTicksNS();
    return true;
}

void video_shutdown(void)
{
    SDL_DestroyTexture(g_prescaled);
    SDL_DestroyTexture(g_game_texture);
    SDL_DestroyTexture(g_texture);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
}

unsigned long video_frame_count(void) { return g_frames; }

/* Largest 4:3 rectangle centred in the render output. */
static SDL_FRect aspect_rect(void)
{
    int ow = SCREEN_W, oh = SCREEN_H;
    SDL_GetCurrentRenderOutputSize(g_renderer, &ow, &oh);
    float w = (float)ow, h = (float)oh;
    if (w * 3.0f > h * 4.0f) w = h * 4.0f / 3.0f; else h = w * 3.0f / 4.0f;
    w = floorf(w); h = floorf(h);
    return (SDL_FRect){floorf(((float)ow - w) / 2.0f), floorf(((float)oh - h) / 2.0f), w, h};
}

/* Draw a src_w x src_h texture into the 4:3 area with the configured filter. */
static void present_texture(SDL_Texture *src, int src_w, int src_h)
{
    SDL_SetRenderTarget(g_renderer, NULL);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_FRect dst = aspect_rect();
    int kx = SDL_max(1, (int)(dst.w / (float)src_w)), ky = SDL_max(1, (int)(dst.h / (float)src_h));
    bool exact = dst.w == (float)(src_w * kx) && dst.h == (float)(src_h * ky);

    if (settings.filter == FILTER_SMOOTH) {
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_LINEAR);
        SDL_RenderTexture(g_renderer, src, NULL, &dst);
    } else if (settings.filter == FILTER_NEAREST || exact) {
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(g_renderer, src, NULL, &dst);
    } else {
        int pw = src_w * kx, ph = src_h * ky;
        if (!g_prescaled || g_prescaled_w != pw || g_prescaled_h != ph) {
            SDL_DestroyTexture(g_prescaled);
            g_prescaled = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_TARGET, pw, ph);
            g_prescaled_w = pw; g_prescaled_h = ph;
            if (g_prescaled) SDL_SetTextureScaleMode(g_prescaled, SDL_SCALEMODE_LINEAR);
        }
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_NEAREST);
        if (g_prescaled && SDL_SetRenderTarget(g_renderer, g_prescaled)) {
            SDL_RenderTexture(g_renderer, src, NULL, NULL);
            SDL_SetRenderTarget(g_renderer, NULL);
            SDL_RenderTexture(g_renderer, g_prescaled, NULL, &dst);
        } else {
            SDL_RenderTexture(g_renderer, src, NULL, &dst);      /* no render targets: plain nearest */
        }
    }
    if (g_dump_output[0]) {
        SDL_Surface *shot = SDL_RenderReadPixels(g_renderer, NULL);
        if (shot) { SDL_SaveBMP(shot, g_dump_output); SDL_DestroySurface(shot); }
        g_dump_output[0] = 0;
    }
    SDL_RenderPresent(g_renderer);
}

/* Debugging aid: ASTRO_DUMP=<dir> saves every 60th presented frame as BMP. */
static void dump_frame(void)
{
    static const char *dir;
    static bool checked;
    unsigned long frame = g_frames;
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
    g_frames++;
    SDL_UpdateTexture(g_texture, NULL, &mem[VA_PRIMARY], SCREEN_W * SCREEN_BPP);
    present_texture(g_texture, SCREEN_W, SCREEN_H);
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
    /* ASTRO_DUMP_OUTPUT=<dir>: also save the scaled window image every 300th frame */
    const char *odir = SDL_getenv("ASTRO_DUMP_OUTPUT");
    if (odir && g_frames % 300 == 0)
        SDL_snprintf(g_dump_output, sizeof g_dump_output, "%s/out%05lu.bmp", odir, g_frames);
    g_frames++;
    present_texture(g_game_texture, GAME_W, GAME_H);
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
    settings.fullscreen = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) == 0;
    SDL_SetWindowFullscreen(g_window, settings.fullscreen);
}
