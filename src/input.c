/* SDL events and the DirectInput-style keyboard state the core reads. */
#include "astro.h"

#include <SDL3/SDL.h>

bool input_quit_requested;
bool input_focused = true;

/* ---- gamepads as the original's DirectInput joysticks -------------------- */

static SDL_Gamepad *g_pads[2];

static void open_pad(SDL_JoystickID id)
{
    for (int i = 0; i < 2; i++) if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) return;
    for (int i = 0; i < 2; i++) {
        if (!g_pads[i]) {
            g_pads[i] = SDL_OpenGamepad(id);
            if (g_pads[i]) SDL_Log("joystick %d: %s", i + 1, SDL_GetGamepadName(g_pads[i]));
            return;
        }
    }
}

/*
 * Testing aid: ASTRO_VPAD="frame:control[:frames],..." attaches a virtual SDL
 * gamepad and holds `control` (a, b, left, right, up, down) from presented
 * frame `frame` for `frames` frames (default 10).
 */
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
    if (!g_vpad) SDL_Log("virtual gamepad: %s", SDL_GetError());
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

int input_init_gamepads(void)
{
    vpad_attach();
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) open_pad(ids[i]);
    SDL_free(ids);
    return (g_pads[0] != NULL) + (g_pads[1] != NULL);
}

/*
 * Input_PollJoystick (0x406f40): joystick `index` into the 2-bit histories of
 * player `slot` (left, right, up, down, button 0, button 1; previous state in
 * bit 1). The original set the axis range to -1000..1000 with a 50% dead zone
 * (0x406c90) and only looked at the sign, so a stick counts beyond half
 * deflection. Button 0 = south (A/cross), button 1 = east (B/circle); the
 * d-pad acts like the stick. With no device the original updated nothing.
 */
void input_poll_joystick(int index, int slot)
{
    if (index < 0 || index > 1 || !g_pads[index]) return;
    SDL_Gamepad *p = g_pads[index];
    const uint32_t base = slot == 0 ? 0x55c980u : 0x55c998u;
    /* slot 0: right 980, left 984, up 988, down 98c, b0 990, b1 994 */
    uint32_t right = base, left = base + 4, up = base + 8, down = base + 12, b0 = base + 16, b1 = base + 20;
    const uint32_t vars[6] = {right, left, up, down, b0, b1};
    for (int i = 0; i < 6; i++) wr32(vars[i], (rd32(vars[i]) & 1) << 1);
    int x = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY);
    x = x < -16384 ? -1 : x > 16383 ? 1 : 0;
    y = y < -16384 ? -1 : y > 16383 ? 1 : 0;
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) x = -1;
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) x = 1;
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_UP)) y = -1;
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) y = 1;
    if (x < 0) wr32(left, rd32(left) | 1); else if (x > 0) wr32(right, rd32(right) | 1);
    if (y < 0) wr32(up, rd32(up) | 1); else if (y > 0) wr32(down, rd32(down) | 1);
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)) wr32(b0, rd32(b0) | 1);
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST)) wr32(b1, rd32(b1) | 1);
}

/* SDL scancode -> DirectInput DIK code (PC set-1 scancodes, 0xE0 keys | 0x80). */
static uint8_t dik_from_sdl(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const uint8_t letters[26] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return letters[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (uint8_t)(0x02 + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return (uint8_t)(0x3B + (sc - SDL_SCANCODE_F1));
    switch (sc) {
    case SDL_SCANCODE_0: return 0x0B;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN: return 0x1C;
    case SDL_SCANCODE_LCTRL: return 0x1D;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_KP_ENTER: return 0x9C;
    case SDL_SCANCODE_RCTRL: return 0x9D;
    case SDL_SCANCODE_KP_DIVIDE: return 0xB5;
    case SDL_SCANCODE_RALT: return 0xB8;
    case SDL_SCANCODE_HOME: return 0xC7;
    case SDL_SCANCODE_UP: return 0xC8;
    case SDL_SCANCODE_PAGEUP: return 0xC9;
    case SDL_SCANCODE_LEFT: return 0xCB;
    case SDL_SCANCODE_RIGHT: return 0xCD;
    case SDL_SCANCODE_END: return 0xCF;
    case SDL_SCANCODE_DOWN: return 0xD0;
    case SDL_SCANCODE_PAGEDOWN: return 0xD1;
    case SDL_SCANCODE_INSERT: return 0xD2;
    case SDL_SCANCODE_DELETE: return 0xD3;
    default: return 0;
    }
}

static void handle(const SDL_Event *e);

void input_pump(void)
{
    vpad_update();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) handle(&ev);
}

/* Like App_PumpMessages (0x408150): while the game is not the active window it
 * waits for messages, i.e. the whole game pauses. */
void input_wait_while_unfocused(void)
{
    SDL_Event ev;
    while (!input_focused && !input_quit_requested && SDL_WaitEvent(&ev)) handle(&ev);
}

static void handle(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_EVENT_QUIT:
        input_quit_requested = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        input_focused = false;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        input_focused = true;
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        open_pad(ev->gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (int i = 0; i < 2; i++) {
            if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == ev->gdevice.which) {
                SDL_CloseGamepad(g_pads[i]);
                g_pads[i] = NULL;
            }
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (ev->key.scancode == SDL_SCANCODE_RETURN && (ev->key.mod & SDL_KMOD_ALT) && !ev->key.repeat)
            video_toggle_fullscreen();
        break;
    default:
        break;
    }
}

/*
 * Debugging/testing aid: ASTRO_KEYS="frame:dik[:frames],..." holds DirectInput
 * key `dik` (hex) from presented frame `frame` for `frames` frames (default 10).
 */
static void scripted_keys(uint8_t state[256])
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = SDL_getenv("ASTRO_KEYS"); checked = true; }
    if (!spec) return;
    unsigned long now = video_frame_count();
    const char *p = spec;
    while (*p) {
        char *end;
        unsigned long at = SDL_strtoul(p, &end, 10), dik = 0, dur = 10;
        if (*end == ':') dik = SDL_strtoul(end + 1, &end, 16);
        if (*end == ':') dur = SDL_strtoul(end + 1, &end, 10);
        if (now >= at && now < at + dur && dik < 256) state[dik] = 0x80;
        p = *end == ',' ? end + 1 : end;
        if (end == p && *p) break;
    }
}

void input_fill_keystate(void)
{
    int n = 0;
    const bool *keys = SDL_GetKeyboardState(&n);
    uint8_t state[256] = {0};
    for (int sc = 0; sc < n; sc++) {
        if (!keys[sc]) continue;
        uint8_t dik = dik_from_sdl((SDL_Scancode)sc);
        if (dik) state[dik] = 0x80;
    }
    scripted_keys(state);
    memcpy(&mem[G_KEY_STATE], state, sizeof state);
}
