/*
 * SDL 1.2 frontend (also runs on sdl12-compat): software surface with the
 * shared software scaler, raw joysticks (axes, hat, buttons 0/1 - the same
 * model as the original's DirectInput joystick), 16-bit callback audio.
 */
#include <SDL.h>
#include <stdlib.h>

#include "../astro.h"
#include "../scale.h"

static SDL_Surface *g_screen;
static int g_desktop_w, g_desktop_h, g_window_w = SCREEN_W, g_window_h = SCREEN_H;
static double g_next_frame;
static bool g_quit, g_focused = true, g_audio_open;
static bool g_exclusive;            /* the display offers a fullscreen mode of the output size */
static SDL_Joystick *g_joy[2];
static plat_mix_fn g_mix;
static uint32_t *g_scaled;          /* scaling target when the surface is not XRGB */
static size_t g_scaled_size;

const char *plat_device_name(void) { return "SDL1"; }

static bool set_mode(int w, int h)
{
    Uint32 flags = SDL_SWSURFACE | (settings.fullscreen ? SDL_FULLSCREEN : SDL_RESIZABLE);
    if (settings.fullscreen && g_exclusive) video_output_size(&w, &h);
    else if (settings.fullscreen && g_desktop_w > 0) { w = g_desktop_w; h = g_desktop_h; }
    SDL_Surface *s = SDL_SetVideoMode(w, h, 32, flags);
    if (!s) { astro_log("SDL_SetVideoMode: %s", SDL_GetError()); return false; }
    g_screen = s;
    return true;
}

/* ---- input ------------------------------------------------------------------- */

static uint8_t dik_from_sdlkey(int k)
{
    static const uint8_t letters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (k >= SDLK_a && k <= SDLK_z) return letters[k - SDLK_a];
    if (k >= SDLK_1 && k <= SDLK_9) return (uint8_t)(0x02 + (k - SDLK_1));
    if (k >= SDLK_F1 && k <= SDLK_F10) return (uint8_t)(0x3B + (k - SDLK_F1));
    switch (k) {
    case SDLK_0: return 0x0B;            case SDLK_RETURN: return 0x1C;
    case SDLK_ESCAPE: return 0x01;       case SDLK_BACKSPACE: return 0x0E;
    case SDLK_TAB: return 0x0F;          case SDLK_SPACE: return 0x39;
    case SDLK_MINUS: return 0x0C;        case SDLK_EQUALS: return 0x0D;
    case SDLK_LEFTBRACKET: return 0x1A;  case SDLK_RIGHTBRACKET: return 0x1B;
    case SDLK_BACKSLASH: return 0x2B;    case SDLK_SEMICOLON: return 0x27;
    case SDLK_QUOTE: return 0x28;        case SDLK_BACKQUOTE: return 0x29;
    case SDLK_COMMA: return 0x33;        case SDLK_PERIOD: return 0x34;
    case SDLK_SLASH: return 0x35;        case SDLK_CAPSLOCK: return 0x3A;
    case SDLK_F11: return 0x57;          case SDLK_F12: return 0x58;
    case SDLK_INSERT: return 0xD2;       case SDLK_HOME: return 0xC7;
    case SDLK_PAGEUP: return 0xC9;       case SDLK_DELETE: return 0xD3;
    case SDLK_END: return 0xCF;          case SDLK_PAGEDOWN: return 0xD1;
    case SDLK_RIGHT: return 0xCD;        case SDLK_LEFT: return 0xCB;
    case SDLK_DOWN: return 0xD0;         case SDLK_UP: return 0xC8;
    case SDLK_KP_DIVIDE: return 0xB5;    case SDLK_KP_MULTIPLY: return 0x37;
    case SDLK_KP_MINUS: return 0x4A;     case SDLK_KP_PLUS: return 0x4E;
    case SDLK_KP_ENTER: return 0x9C;     case SDLK_KP_PERIOD: return 0x53;
    case SDLK_KP0: return 0x52;          case SDLK_KP1: return 0x4F;
    case SDLK_KP2: return 0x50;          case SDLK_KP3: return 0x51;
    case SDLK_KP4: return 0x4B;          case SDLK_KP5: return 0x4C;
    case SDLK_KP6: return 0x4D;          case SDLK_KP7: return 0x47;
    case SDLK_KP8: return 0x48;          case SDLK_KP9: return 0x49;
    case SDLK_LCTRL: return 0x1D;        case SDLK_RCTRL: return 0x9D;
    case SDLK_LSHIFT: return 0x2A;       case SDLK_RSHIFT: return 0x36;
    case SDLK_LALT: return 0x38;         case SDLK_RALT: return 0xB8;
    default: return 0;
    }
}

void plat_keys(uint8_t dik[256])
{
    int n = 0;
    Uint8 *keys = SDL_GetKeyState(&n);
    for (int k = 0; k < n; k++) {
        uint8_t d = keys[k] ? dik_from_sdlkey(k) : 0;
        if (d) dik[d] = 0x80;
    }
}

int plat_joystick_count(void) { return (g_joy[0] != NULL) + (g_joy[1] != NULL); }

bool plat_joystick(int index, int *ax, int *ay, int *dx, int *dy, bool *b0, bool *b1)
{
    SDL_Joystick *j = g_joy[index];
    if (!j) return false;
    *ax = SDL_JoystickNumAxes(j) > 0 ? SDL_JoystickGetAxis(j, 0) : 0;
    *ay = SDL_JoystickNumAxes(j) > 1 ? SDL_JoystickGetAxis(j, 1) : 0;
    Uint8 hat = SDL_JoystickNumHats(j) > 0 ? SDL_JoystickGetHat(j, 0) : SDL_HAT_CENTERED;
    *dx = (hat & SDL_HAT_LEFT) ? -1 : (hat & SDL_HAT_RIGHT) ? 1 : 0;
    *dy = (hat & SDL_HAT_UP) ? -1 : (hat & SDL_HAT_DOWN) ? 1 : 0;
    *b0 = SDL_JoystickNumButtons(j) > 0 && SDL_JoystickGetButton(j, 0);
    *b1 = SDL_JoystickNumButtons(j) > 1 && SDL_JoystickGetButton(j, 1);
    return true;
}

/* ---- lifetime --------------------------------------------------------------- */

bool plat_init(const char *title)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        astro_log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    const SDL_VideoInfo *vi = SDL_GetVideoInfo();     /* before the first mode: desktop size */
    if (vi) { g_desktop_w = vi->current_w; g_desktop_h = vi->current_h; }
    SDL_WM_SetCaption(title, title);
    if (settings.exclusive) {            /* a real display mode of the output size, if offered */
        int ow, oh;
        video_output_size(&ow, &oh);
        SDL_Rect **modes = SDL_ListModes(NULL, SDL_FULLSCREEN | SDL_SWSURFACE);
        if (modes == (SDL_Rect **)-1) g_exclusive = true;            /* any size */
        for (int i = 0; modes && modes != (SDL_Rect **)-1 && modes[i]; i++)
            if (modes[i]->w == ow && modes[i]->h == oh) g_exclusive = true;
        if (!g_exclusive) astro_log("display has no %dx%d mode: fullscreen uses the desktop mode", ow, oh);
    }
    video_window_size(g_desktop_w, g_desktop_h, &g_window_w, &g_window_h);
    if (!set_mode(g_window_w, g_window_h)) return false;
    SDL_ShowCursor(SDL_DISABLE);
    for (int i = 0; i < 2 && i < SDL_NumJoysticks(); i++) {
        g_joy[i] = SDL_JoystickOpen(i);
        if (g_joy[i]) astro_log("joystick %d: %s", i + 1, SDL_JoystickName(i));
    }
    g_next_frame = SDL_GetTicks();
    return true;
}

void plat_shutdown(void)
{
    if (!g_screen) return;
    for (int i = 0; i < 2; i++) if (g_joy[i]) SDL_JoystickClose(g_joy[i]);
    free(g_scaled);
    g_screen = NULL;
    SDL_Quit();
}

void plat_error(const char *message) { astro_log("%s", message); }

/* ---- video ------------------------------------------------------------------- */

void plat_present(const uint32_t *xrgb, int sw, int sh)
{
    SDL_Surface *s = g_screen;
    if (!s) return;
    int x = 0, y = 0, w = s->w, h = s->h;   /* exclusive mode: the display shows it as 4:3 */
    if (!(settings.fullscreen && g_exclusive)) scale_aspect_rect(s->w, s->h, &x, &y, &w, &h);
    SDL_FillRect(s, NULL, 0);
    bool direct = s->format->BytesPerPixel == 4 && s->format->Rmask == 0xff0000 &&
                  s->format->Gmask == 0x00ff00 && s->format->Bmask == 0x0000ff;
    if (SDL_MUSTLOCK(s) && SDL_LockSurface(s) != 0) return;
    if (direct) {
        scale_xrgb(xrgb, sw, sh, (uint32_t *)s->pixels, s->pitch / 4, x, y, w, h, settings.filter);
    } else {                                          /* other pixel format: convert */
        size_t need = (size_t)w * (size_t)h;
        if (need > g_scaled_size) { free(g_scaled); g_scaled = malloc(need * 4); g_scaled_size = g_scaled ? need : 0; }
        if (g_scaled) {
            scale_xrgb(xrgb, sw, sh, g_scaled, w, 0, 0, w, h, settings.filter);
            for (int r = 0; r < h; r++) {
                Uint8 *row = (Uint8 *)s->pixels + (size_t)(y + r) * s->pitch + (size_t)x * s->format->BytesPerPixel;
                for (int c = 0; c < w; c++) {
                    uint32_t p = g_scaled[(size_t)r * (size_t)w + (size_t)c];
                    Uint32 v = SDL_MapRGB(s->format, (Uint8)(p >> 16), (Uint8)(p >> 8), (Uint8)p);
                    memcpy(row + (size_t)c * s->format->BytesPerPixel, &v, s->format->BytesPerPixel);
                }
            }
        }
    }
    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    /* ASTRO_DUMP_OUTPUT=<dir>: save the scaled window image every 300th gameplay frame */
    const char *odir = getenv("ASTRO_DUMP_OUTPUT");
    unsigned long frame = video_frame_count();
    if (odir && video_is_game() && frame % 300 == 1) {
        char path[1100];
        snprintf(path, sizeof path, "%s/out%05lu.bmp", odir, frame - 1);
        SDL_SaveBMP(s, path);
    }
    SDL_Flip(s);
}

void plat_wait_frame(void)
{
    g_next_frame += 1000.0 / 60.0;
    double now = SDL_GetTicks();
    if (g_next_frame > now) SDL_Delay((Uint32)(g_next_frame - now));
    else if (now - g_next_frame > 4 * 1000.0 / 60.0) g_next_frame = now;   /* fell behind: resync */
}

/* ---- events ------------------------------------------------------------------ */

static void handle(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT: g_quit = true; break;
    case SDL_ACTIVEEVENT:
        if (ev->active.state & SDL_APPINPUTFOCUS) g_focused = ev->active.gain != 0;
        break;
    case SDL_VIDEORESIZE:
        if (!settings.fullscreen) {
            g_window_w = ev->resize.w; g_window_h = ev->resize.h;
            set_mode(g_window_w, g_window_h);
        }
        break;
    case SDL_KEYDOWN:
        if (ev->key.keysym.sym == SDLK_RETURN && (ev->key.keysym.mod & KMOD_ALT)) {
            settings.fullscreen = !settings.fullscreen;
            if (!set_mode(g_window_w, g_window_h)) {          /* fall back to the other mode */
                settings.fullscreen = !settings.fullscreen;
                set_mode(g_window_w, g_window_h);
            }
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

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    static float buf[2 * 2048];
    int16_t *out = (int16_t *)stream;
    int frames = len / (int)(2 * sizeof(int16_t));
    while (frames > 0) {
        int n = frames > 2048 ? 2048 : frames;
        g_mix(buf, n);
        for (int i = 0; i < 2 * n; i++) out[i] = (int16_t)(buf[i] * 32767.0f);   /* mix is within -1..1 */
        out += 2 * n;
        frames -= n;
    }
}

bool plat_audio_open(int rate, plat_mix_fn mix)
{
    SDL_AudioSpec want;
    memset(&want, 0, sizeof want);
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    g_mix = mix;
    if (SDL_OpenAudio(&want, NULL) != 0) {           /* NULL: SDL converts to the device */
        astro_log("no audio device: %s", SDL_GetError());
        return false;
    }
    g_audio_open = true;
    SDL_PauseAudio(0);
    return true;
}

void plat_audio_close(void)
{
    if (g_audio_open) SDL_CloseAudio();
    g_audio_open = false;
}

void plat_audio_lock(void) { if (g_audio_open) SDL_LockAudio(); }
void plat_audio_unlock(void) { if (g_audio_open) SDL_UnlockAudio(); }

int main(int argc, char **argv) { return astro_main(argc, argv); }
