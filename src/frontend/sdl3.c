/*
 * SDL3 frontend (the main one): GPU-scaled presentation, gamepads, audio stream.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "../astro.h"
#include "../scale.h"

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_Texture *g_tex[2], *g_prescaled;
static bool g_exclusive;           /* a display mode of the output size is set for fullscreen */
static int g_prescaled_w, g_prescaled_h;
static Uint64 g_next_frame_ns;
static bool g_quit, g_focused = true;
static SDL_Gamepad *g_pads[2];
static SDL_AudioStream *g_stream;
static plat_mix_fn g_mix;

#define FRAME_NS (SDL_NS_PER_SECOND / 60)

const char *plat_device_name(void) { return "SDL3"; }

/* ---- gamepads (+ virtual test pad) --------------------------------------- */

static void open_pad(SDL_JoystickID id)
{
    for (int i = 0; i < 2; i++) if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) return;
    for (int i = 0; i < 2; i++) {
        if (!g_pads[i]) {
            g_pads[i] = SDL_OpenGamepad(id);
            if (g_pads[i]) astro_log("joystick %d: %s", i + 1, SDL_GetGamepadName(g_pads[i]));
            return;
        }
    }
}

/* Testing aid: ASTRO_VPAD="frame:control[:frames],..." attaches a virtual SDL
 * gamepad and holds `control` (a, b, left, right, up, down) from presented
 * frame `frame` for `frames` frames (default 10). */
static SDL_Joystick *g_vpad;
static const char *g_vpad_script;

static void vpad_attach(void)
{
    g_vpad_script = SDL_getenv("ASTRO_VPAD");
    if (!g_vpad_script) return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "Astro test pad";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    g_vpad = id ? SDL_OpenJoystick(id) : NULL;
    if (!g_vpad) astro_log("virtual gamepad: %s", SDL_GetError());
}

static void vpad_update(void)
{
    if (!g_vpad) return;
    unsigned long now = video_frame_count();
    bool a = false, b = false;
    int x = 0, y = 0;
    for (const char *p = g_vpad_script; *p;) {
        char *end;
        unsigned long at = SDL_strtoul(p, &end, 10), dur = 10;
        char name[8] = {0};
        if (*end == ':') {
            end++;
            for (int i = 0; i < 7 && SDL_isalpha(*end); i++) name[i] = *end++;
        }
        if (*end == ':') dur = SDL_strtoul(end + 1, &end, 10);
        if (now >= at && now < at + dur) {
            if (!SDL_strcmp(name, "a")) a = true;
            else if (!SDL_strcmp(name, "b")) b = true;
            else if (!SDL_strcmp(name, "left")) x = -32768;
            else if (!SDL_strcmp(name, "right")) x = 32767;
            else if (!SDL_strcmp(name, "up")) y = -32768;
            else if (!SDL_strcmp(name, "down")) y = 32767;
        }
        if (end == p) break;
        p = *end == ',' ? end + 1 : end;
    }
    SDL_SetJoystickVirtualButton(g_vpad, SDL_GAMEPAD_BUTTON_SOUTH, a);
    SDL_SetJoystickVirtualButton(g_vpad, SDL_GAMEPAD_BUTTON_EAST, b);
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_LEFTX, (Sint16)x);
    SDL_SetJoystickVirtualAxis(g_vpad, SDL_GAMEPAD_AXIS_LEFTY, (Sint16)y);
}

int plat_joystick_count(void) { return (g_pads[0] != NULL) + (g_pads[1] != NULL); }

bool plat_joystick(int index, int *ax, int *ay, int *dx, int *dy, bool *b0, bool *b1)
{
    SDL_Gamepad *p = g_pads[index];
    if (!p) return false;
    *ax = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX);
    *ay = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY);
    *dx = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1 : SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 : 0;
    *dy = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP) ? -1 : SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : 0;
    *b0 = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH);
    *b1 = SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST);
    return true;
}

void plat_keys(uint8_t dik[256])
{
    int n = 0;
    const bool *keys = SDL_GetKeyboardState(&n);
    for (int sc = 0; sc < n; sc++) {
        uint8_t d = keys[sc] ? input_dik_from_usb(sc) : 0;
        if (d) dik[d] = 0x80;
    }
}

/* ---- lifetime --------------------------------------------------------------- */

bool plat_init(const char *title)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        astro_log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
    int ww, wh;
    video_window_size(desktop ? desktop->w : 0, desktop ? desktop->h : 0, &ww, &wh);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    if (!SDL_CreateWindowAndRenderer(title, ww, wh, flags, &g_window, &g_renderer)) {
        astro_log("window: %s", SDL_GetError());
        return false;
    }
    if (settings.exclusive) {            /* a real display mode of the output size, if offered */
        int ow, oh;
        SDL_DisplayMode mode;
        video_output_size(&ow, &oh);
        if (SDL_GetClosestFullscreenDisplayMode(display, ow, oh, 0.0f, false, &mode) && mode.w == ow && mode.h == oh &&
            SDL_SetWindowFullscreenMode(g_window, &mode)) {
            g_exclusive = true;
            astro_log("fullscreen display mode %dx%d", mode.w, mode.h);
        } else {
            astro_log("display has no %dx%d mode: fullscreen uses the desktop mode", ow, oh);
        }
    }
    SDL_SetWindowFullscreen(g_window, settings.fullscreen);
    SDL_ShowWindow(g_window);
    SDL_HideCursor();
    vpad_attach();
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) open_pad(ids[i]);
    SDL_free(ids);
    g_next_frame_ns = SDL_GetTicksNS();
    return true;
}

void plat_shutdown(void)
{
    if (!g_window) return;
    SDL_DestroyTexture(g_prescaled);
    SDL_DestroyTexture(g_tex[0]);
    SDL_DestroyTexture(g_tex[1]);
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

static char g_dump_output[1100];   /* ASTRO_DUMP_OUTPUT: save the scaled image */

/* Streaming texture of the picture's size (two sizes are in use at most). */
static SDL_Texture *texture_for(int w, int h)
{
    for (int i = 0; i < 2; i++)
        if (g_tex[i] && g_tex[i]->w == w && g_tex[i]->h == h) return g_tex[i];
    SDL_DestroyTexture(g_tex[1]);
    g_tex[1] = g_tex[0];
    g_tex[0] = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!g_tex[0]) astro_log("texture: %s", SDL_GetError());
    return g_tex[0];
}

void plat_present(const uint32_t *xrgb, int sw, int sh)
{
    SDL_Texture *src = texture_for(sw, sh);
    if (!src) return;
    SDL_UpdateTexture(src, NULL, xrgb, sw * 4);

    const char *odir = SDL_getenv("ASTRO_DUMP_OUTPUT");
    unsigned long frame = video_frame_count();
    if (odir && video_is_game() && frame % 300 == 1)
        SDL_snprintf(g_dump_output, sizeof g_dump_output, "%s/out%05lu.bmp", odir, frame - 1);

    SDL_SetRenderTarget(g_renderer, NULL);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    int ow = SCREEN_W, oh = SCREEN_H, x, y, w, h;
    SDL_GetCurrentRenderOutputSize(g_renderer, &ow, &oh);
    if (g_exclusive && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN)) {
        x = y = 0; w = ow; h = oh;       /* the display shows the whole mode as 4:3 */
    } else {
        scale_aspect_rect(ow, oh, &x, &y, &w, &h);
    }
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    int kx = SDL_max(1, w / sw), ky = SDL_max(1, h / sh);
    bool exact = w == sw * kx && h == sh * ky;

    if (settings.filter == FILTER_SMOOTH) {
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_LINEAR);
        SDL_RenderTexture(g_renderer, src, NULL, &dst);
    } else if (settings.filter == FILTER_NEAREST || exact) {
        SDL_SetTextureScaleMode(src, SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(g_renderer, src, NULL, &dst);
    } else {   /* sharp: integer nearest into a target, then linear for the rest */
        int pw = sw * kx, ph = sh * ky;
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
            SDL_RenderTexture(g_renderer, src, NULL, &dst);
        }
    }
    if (g_dump_output[0]) {
        SDL_Surface *shot = SDL_RenderReadPixels(g_renderer, NULL);
        if (shot) { SDL_SaveBMP(shot, g_dump_output); SDL_DestroySurface(shot); }
        g_dump_output[0] = 0;
    }
    SDL_RenderPresent(g_renderer);
}

void plat_wait_frame(void)
{
    Uint64 now = SDL_GetTicksNS();
    g_next_frame_ns += FRAME_NS;
    if (g_next_frame_ns > now) SDL_DelayPrecise(g_next_frame_ns - now);
    else if (now - g_next_frame_ns > 4 * FRAME_NS) g_next_frame_ns = now;   /* fell behind: resync */
}

/* ---- events ------------------------------------------------------------------ */

static void handle(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_QUIT: g_quit = true; break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: g_focused = false; break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED: g_focused = true; break;
    case SDL_EVENT_GAMEPAD_ADDED: open_pad(ev->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (int i = 0; i < 2; i++) {
            if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == ev->gdevice.which) {
                SDL_CloseGamepad(g_pads[i]);
                g_pads[i] = NULL;
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (ev->key.scancode == SDL_SCANCODE_RETURN && (ev->key.mod & SDL_KMOD_ALT) && !ev->key.repeat) {
            settings.fullscreen = !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(g_window, settings.fullscreen);
        }
        break;
    default: break;
    }
}

void plat_pump(void)
{
    vpad_update();
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

static void SDLCALL audio_cb(void *ud, SDL_AudioStream *stream, int additional, int total)
{
    static float buf[2 * 1024];
    int frames = additional / (int)(2 * sizeof(float));
    while (frames > 0) {
        int n = frames > 1024 ? 1024 : frames;
        g_mix(buf, n);
        SDL_PutAudioStreamData(stream, buf, (int)(sizeof(float) * 2 * (size_t)n));
        frames -= n;
    }
}

bool plat_audio_open(int rate, plat_mix_fn mix)
{
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, rate};
    g_mix = mix;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_cb, NULL);
    if (!g_stream) { astro_log("no audio device: %s", SDL_GetError()); return false; }
    SDL_ResumeAudioStreamDevice(g_stream);
    return true;
}

void plat_audio_close(void)
{
    if (g_stream) SDL_DestroyAudioStream(g_stream);
    g_stream = NULL;
}

void plat_audio_lock(void) { if (g_stream) SDL_LockAudioStream(g_stream); }
void plat_audio_unlock(void) { if (g_stream) SDL_UnlockAudioStream(g_stream); }

int main(int argc, char **argv) { return astro_main(argc, argv); }
