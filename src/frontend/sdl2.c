/*
 * SDL2 frontend: GPU-scaled presentation (SDL_Renderer, needs SDL 2.0.12+ for
 * per-texture scale modes), game controllers, callback audio.
 */
#include <SDL.h>

#include "../astro.h"
#include "../scale.h"

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_tex[2], *g_prescaled;
static int g_tex_w[2], g_tex_h[2];
static bool g_exclusive;           /* a display mode of the output size is set for fullscreen */
static int g_prescaled_w, g_prescaled_h;
static Uint64 g_next_frame, g_frame_ticks;
static bool g_quit, g_focused = true;
static SDL_GameController *g_pads[2];
static SDL_AudioDeviceID g_audio;
static plat_mix_fn g_mix;

const char *plat_device_name(void) { return "SDL2"; }

/* ---- controllers ------------------------------------------------------------- */

static void open_pad(int device_index)
{
    if (!SDL_IsGameController(device_index)) return;
    SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(device_index);
    for (int i = 0; i < 2; i++)
        if (g_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == id) return;
    for (int i = 0; i < 2; i++) {
        if (!g_pads[i]) {
            g_pads[i] = SDL_GameControllerOpen(device_index);
            if (g_pads[i]) astro_log("joystick %d: %s", i + 1, SDL_GameControllerName(g_pads[i]));
            return;
        }
    }
}

int plat_joystick_count(void) { return (g_pads[0] != NULL) + (g_pads[1] != NULL); }

bool plat_joystick(int index, int *ax, int *ay, int *dx, int *dy, bool *b0, bool *b1)
{
    SDL_GameController *p = g_pads[index];
    if (!p) return false;
    *ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
    *ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
    *dx = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? -1 :
          SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 1 : 0;
    *dy = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP) ? -1 :
          SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1 : 0;
    *b0 = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A) != 0;   /* south */
    *b1 = SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B) != 0;   /* east  */
    return true;
}

void plat_keys(uint8_t dik[256])
{
    int n = 0;
    const Uint8 *keys = SDL_GetKeyboardState(&n);
    for (int sc = 0; sc < n; sc++) {
        uint8_t d = keys[sc] ? input_dik_from_usb(sc) : 0;   /* SDL2 scancodes are USB usages */
        if (d) dik[d] = 0x80;
    }
}

static void set_fullscreen(bool on)
{
    SDL_SetWindowFullscreen(g_window, !on ? 0 : g_exclusive ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

/* ---- lifetime --------------------------------------------------------------- */

bool plat_init(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        astro_log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    SDL_DisplayMode desktop = {0};
    SDL_GetDesktopDisplayMode(0, &desktop);
    int ww, wh;
    video_window_size(desktop.w, desktop.h, &ww, &wh);
    g_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    g_renderer = g_window ? SDL_CreateRenderer(g_window, -1, SDL_RENDERER_TARGETTEXTURE) : NULL;
    if (!g_renderer) { astro_log("window: %s", SDL_GetError()); return false; }
    if (settings.exclusive) {            /* a real display mode of the output size, if offered */
        int ow, oh;
        video_output_size(&ow, &oh);
        SDL_DisplayMode want = {0}, mode;
        want.w = ow; want.h = oh;
        if (SDL_GetClosestDisplayMode(0, &want, &mode) && mode.w == ow && mode.h == oh &&
            SDL_SetWindowDisplayMode(g_window, &mode) == 0) {
            g_exclusive = true;
            astro_log("fullscreen display mode %dx%d", mode.w, mode.h);
        } else {
            astro_log("display has no %dx%d mode: fullscreen uses the desktop mode", ow, oh);
        }
    }
    set_fullscreen(settings.fullscreen);
    SDL_ShowWindow(g_window);
    SDL_ShowCursor(SDL_DISABLE);
    for (int i = 0; i < SDL_NumJoysticks(); i++) open_pad(i);
    g_frame_ticks = SDL_GetPerformanceFrequency() / 60;
    g_next_frame = SDL_GetPerformanceCounter();
    return true;
}

void plat_shutdown(void)
{
    if (!g_window) return;
    if (g_prescaled) SDL_DestroyTexture(g_prescaled);
    for (int i = 0; i < 2; i++) if (g_tex[i]) SDL_DestroyTexture(g_tex[i]);
    SDL_DestroyRenderer(g_renderer);
    SDL_DestroyWindow(g_window);
    g_window = NULL;
    SDL_Quit();
}

void plat_error(const char *message)
{
    astro_log("%s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Astro Assembler", message, g_window);
}

/* ---- video ------------------------------------------------------------------- */

/* Streaming texture of the picture's size (two sizes are in use at most). */
static SDL_Texture *texture_for(int w, int h)
{
    for (int i = 0; i < 2; i++)
        if (g_tex[i] && g_tex_w[i] == w && g_tex_h[i] == h) return g_tex[i];
    if (g_tex[1]) SDL_DestroyTexture(g_tex[1]);
    g_tex[1] = g_tex[0]; g_tex_w[1] = g_tex_w[0]; g_tex_h[1] = g_tex_h[0];
    g_tex[0] = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    g_tex_w[0] = w; g_tex_h[0] = h;
    if (!g_tex[0]) astro_log("texture: %s", SDL_GetError());
    return g_tex[0];
}

void plat_present(const uint32_t *xrgb, int sw, int sh)
{
    SDL_Texture *src = texture_for(sw, sh);
    if (!src) return;
    SDL_UpdateTexture(src, NULL, xrgb, sw * 4);
    SDL_SetRenderTarget(g_renderer, NULL);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    int ow = SCREEN_W, oh = SCREEN_H;
    SDL_GetRendererOutputSize(g_renderer, &ow, &oh);
    SDL_Rect dst = {0, 0, ow, oh};       /* exclusive mode: the display shows it as 4:3 */
    if (!g_exclusive || !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN))
        scale_aspect_rect(ow, oh, &dst.x, &dst.y, &dst.w, &dst.h);
    int kx = dst.w / sw > 1 ? dst.w / sw : 1, ky = dst.h / sh > 1 ? dst.h / sh : 1;
    bool exact = dst.w == sw * kx && dst.h == sh * ky;

    if (settings.filter == FILTER_SMOOTH) {
        SDL_SetTextureScaleMode(src, SDL_ScaleModeLinear);
        SDL_RenderCopy(g_renderer, src, NULL, &dst);
    } else if (settings.filter == FILTER_NEAREST || exact) {
        SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
        SDL_RenderCopy(g_renderer, src, NULL, &dst);
    } else {   /* sharp: integer nearest into a target, then linear for the rest */
        int pw = sw * kx, ph = sh * ky;
        if (!g_prescaled || g_prescaled_w != pw || g_prescaled_h != ph) {
            if (g_prescaled) SDL_DestroyTexture(g_prescaled);
            g_prescaled = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, pw, ph);
            g_prescaled_w = pw; g_prescaled_h = ph;
            if (g_prescaled) SDL_SetTextureScaleMode(g_prescaled, SDL_ScaleModeLinear);
        }
        SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
        if (g_prescaled && SDL_SetRenderTarget(g_renderer, g_prescaled) == 0) {
            SDL_RenderCopy(g_renderer, src, NULL, NULL);
            SDL_SetRenderTarget(g_renderer, NULL);
            SDL_RenderCopy(g_renderer, g_prescaled, NULL, &dst);
        } else {
            SDL_RenderCopy(g_renderer, src, NULL, &dst);
        }
    }
    /* ASTRO_DUMP_OUTPUT=<dir>: save the scaled window image every 300th gameplay frame */
    const char *odir = SDL_getenv("ASTRO_DUMP_OUTPUT");
    unsigned long frame = video_frame_count();
    if (odir && video_is_game() && frame % 300 == 1) {
        SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_RGB888);
        if (shot && SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_RGB888, shot->pixels, shot->pitch) == 0) {
            char path[1100];
            SDL_snprintf(path, sizeof path, "%s/out%05lu.bmp", odir, frame - 1);
            SDL_SaveBMP(shot, path);
        }
        if (shot) SDL_FreeSurface(shot);
    }
    SDL_RenderPresent(g_renderer);
}

void plat_wait_frame(void)
{
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    g_next_frame += g_frame_ticks;
    if (g_next_frame > now) {
        Uint64 ms = (g_next_frame - now) * 1000 / freq;
        if (ms > 1) SDL_Delay((Uint32)(ms - 1));                  /* sleep, then spin the rest */
        while (SDL_GetPerformanceCounter() < g_next_frame) {}
    } else if (now - g_next_frame > 4 * g_frame_ticks) {
        g_next_frame = now;                                      /* fell behind: resync */
    }
}

/* ---- events ------------------------------------------------------------------ */

static void handle(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT: g_quit = true; break;
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST) g_focused = false;
        if (ev->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) g_focused = true;
        break;
    case SDL_CONTROLLERDEVICEADDED: open_pad(ev->cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED:
        for (int i = 0; i < 2; i++) {
            if (g_pads[i] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[i])) == ev->cdevice.which) {
                SDL_GameControllerClose(g_pads[i]);
                g_pads[i] = NULL;
            }
        }
        break;
    case SDL_KEYDOWN:
        if (ev->key.keysym.scancode == SDL_SCANCODE_RETURN && (ev->key.keysym.mod & KMOD_ALT) && !ev->key.repeat) {
            settings.fullscreen = !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN);
            set_fullscreen(settings.fullscreen);
        }
        break;
    default: break;
    }
}

void plat_pump(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) handle(&ev);
}

bool plat_quit_requested(void) { return g_quit; }
bool plat_focused(void) { return g_focused; }

void plat_wait_focus(void)
{
    SDL_Event ev;
    while (!g_focused && !g_quit && SDL_WaitEvent(&ev)) handle(&ev);
}

/* ---- audio ------------------------------------------------------------------- */

static void SDLCALL audio_cb(void *ud, Uint8 *stream, int len)
{
    g_mix((float *)stream, len / (int)(2 * sizeof(float)));
}

bool plat_audio_open(int rate, plat_mix_fn mix)
{
    SDL_AudioSpec want = {0}, have;
    want.freq = rate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    g_mix = mix;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* SDL converts if needed */
    if (!g_audio) { astro_log("no audio device: %s", SDL_GetError()); return false; }
    SDL_PauseAudioDevice(g_audio, 0);
    return true;
}

void plat_audio_close(void)
{
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    g_audio = 0;
}

void plat_audio_lock(void) { if (g_audio) SDL_LockAudioDevice(g_audio); }
void plat_audio_unlock(void) { if (g_audio) SDL_UnlockAudioDevice(g_audio); }

int main(int argc, char **argv) { return astro_main(argc, argv); }
