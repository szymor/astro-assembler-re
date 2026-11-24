/* Input, frontend-neutral part: the DirectInput-style state the core reads. */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

/*
 * Debugging/testing aid: ASTRO_KEYS="frame:dik[:frames],..." holds DirectInput
 * key `dik` (hex) from presented frame `frame` for `frames` frames (default 10).
 */
static void scripted_keys(uint8_t state[256])
{
    static const char *spec;
    static bool checked;
    if (!checked) { spec = getenv("ASTRO_KEYS"); checked = true; }
    if (!spec) return;
    unsigned long now = video_frame_count();
    const char *p = spec;
    while (*p) {
        char *end;
        unsigned long at = strtoul(p, &end, 10), dik = 0, dur = 10;
        if (*end == ':') dik = strtoul(end + 1, &end, 16);
        if (*end == ':') dur = strtoul(end + 1, &end, 10);
        if (now >= at && now < at + dur && dik < 256) state[dik] = 0x80;
        p = *end == ',' ? end + 1 : end;
        if (end == p && *p) break;
    }
}

/* Input_PollKeyboard (0x4071b0): 256-byte DirectInput key state. */
void input_fill_keystate(void)
{
    uint8_t state[256] = {0};
    plat_keys(state);
    scripted_keys(state);
    replay_keys(state);
    memcpy(&mem[G_KEY_STATE], state, sizeof state);
}

/*
 * Input_PollJoystick (0x406f40): joystick `index` into the 2-bit histories of
 * player `slot` (right, left, up, down, button 0, button 1; previous state in
 * bit 1). The original set the axis range to -1000..1000 with a 50% dead zone
 * (0x406c90) and only looked at the sign, so a stick counts beyond half
 * deflection; the d-pad acts like the stick. With no device the original
 * updated nothing.
 */
void input_poll_joystick(int index, int slot)
{
    if (index < 0 || index > 1) return;
    int ax, ay, dx, dy, bits = -1;
    bool b0, b1;
    if (plat_joystick(index, &ax, &ay, &dx, &dy, &b0, &b1)) {
        int x = ax < -16384 ? -1 : ax > 16383 ? 1 : 0;
        int y = ay < -16384 ? -1 : ay > 16383 ? 1 : 0;
        if (dx) x = dx;
        if (dy) y = dy;
        bits = (x > 0) | (x < 0) << 1 | (y < 0) << 2 | (y > 0) << 3 | b0 << 4 | b1 << 5;
    }
    bits = replay_joystick(index, slot, bits);
    if (bits < 0) return;
    const uint32_t base = slot == 0 ? 0x55c980u : 0x55c998u;
    /* right, left, up, down, button 0, button 1 */
    for (int i = 0; i < 6; i++) {
        const uint32_t va = base + 4u * (uint32_t)i;
        wr32(va, (rd32(va) & 1) << 1 | ((bits >> i) & 1));
    }
}

/* USB HID keyboard usage (= SDL2/SDL3 scancode) -> DirectInput DIK code
 * (PC set-1 scancode, extended keys | 0x80). */
uint8_t input_dik_from_usb(int sc)
{
    static const uint8_t letters[26] = {
        0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (sc >= 4 && sc <= 29) return letters[sc - 4];          /* A..Z    */
    if (sc >= 30 && sc <= 38) return (uint8_t)(0x02 + (sc - 30)); /* 1..9 */
    if (sc >= 58 && sc <= 67) return (uint8_t)(0x3B + (sc - 58)); /* F1..F10 */
    switch (sc) {
    case 39: return 0x0B;  /* 0 */
    case 40: return 0x1C;  /* Return */
    case 41: return 0x01;  /* Escape */
    case 42: return 0x0E;  /* Backspace */
    case 43: return 0x0F;  /* Tab */
    case 44: return 0x39;  /* Space */
    case 45: return 0x0C;  /* - */
    case 46: return 0x0D;  /* = */
    case 47: return 0x1A;  /* [ */
    case 48: return 0x1B;  /* ] */
    case 49: return 0x2B;  /* \ */
    case 51: return 0x27;  /* ; */
    case 52: return 0x28;  /* ' */
    case 53: return 0x29;  /* ` */
    case 54: return 0x33;  /* , */
    case 55: return 0x34;  /* . */
    case 56: return 0x35;  /* / */
    case 57: return 0x3A;  /* Caps Lock */
    case 68: return 0x57;  /* F11 */
    case 69: return 0x58;  /* F12 */
    case 73: return 0xD2;  /* Insert */
    case 74: return 0xC7;  /* Home */
    case 75: return 0xC9;  /* Page Up */
    case 76: return 0xD3;  /* Delete */
    case 77: return 0xCF;  /* End */
    case 78: return 0xD1;  /* Page Down */
    case 79: return 0xCD;  /* Right */
    case 80: return 0xCB;  /* Left */
    case 81: return 0xD0;  /* Down */
    case 82: return 0xC8;  /* Up */
    case 84: return 0xB5;  /* KP / */
    case 85: return 0x37;  /* KP * */
    case 86: return 0x4A;  /* KP - */
    case 87: return 0x4E;  /* KP + */
    case 88: return 0x9C;  /* KP Enter */
    case 89: return 0x4F;  /* KP 1 */
    case 90: return 0x50;  /* KP 2 */
    case 91: return 0x51;  /* KP 3 */
    case 92: return 0x4B;  /* KP 4 */
    case 93: return 0x4C;  /* KP 5 */
    case 94: return 0x4D;  /* KP 6 */
    case 95: return 0x47;  /* KP 7 */
    case 96: return 0x48;  /* KP 8 */
    case 97: return 0x49;  /* KP 9 */
    case 98: return 0x52;  /* KP 0 */
    case 99: return 0x53;  /* KP . */
    case 224: return 0x1D; /* Left Ctrl */
    case 225: return 0x2A; /* Left Shift */
    case 226: return 0x38; /* Left Alt */
    case 228: return 0x9D; /* Right Ctrl */
    case 229: return 0x36; /* Right Shift */
    case 230: return 0xB8; /* Right Alt */
    default: return 0;
    }
}
