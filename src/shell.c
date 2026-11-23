/*
 * Replacement for the original C++ shell's WinMain flow (0x4023e0): prepares
 * the emulated memory the way the shell did and drives the core through its
 * commands. See docs/modules.md ("Program flow").
 */
#include "astro.h"
#include "gen_funcs.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

/* Run one core command (Core_Dispatch) from the outside, as a CALL would. */
void core_command(uint32_t cmd)
{
    const uint32_t ret = 0xC0DEF00Du;
    uint32_t esp0 = VA_STACK_TOP - 16;
    wr32(G_CORE_COMMAND, cmd);
    WR_REG(R_ESP, 4, esp0 - 4);
    wr32(esp0 - 4, ret);
    f_408c44();
    if (cpu.ret_addr != ret || ESP != esp0) {
        SDL_Log("core command %u returned badly (ret %08x, esp %08x)", cmd, cpu.ret_addr, ESP);
        abort();
    }
}

/* ---- display mode list ---------------------------------------------------- */

/* Font used by the in-game mode menu (Font_CharToIndex, 0x403f50) and the glyph
 * widths DD_Init uses to size the menu entries. */
static const char k_font_chars[] = " !()-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const int k_glyph_width[45] = {
    10, 7, 12, 12, 18, 7, 24, 22, 21, 22, 22, 22, 22, 22, 21, 23, 22, 7, 21, 22, 22, 21, 22,
    21, 21, 22, 22, 7, 22, 24, 21, 26, 24, 22, 22, 22, 23, 22, 21, 22, 24, 26, 23, 22, 22};

static int font_index(char c)
{
    for (int i = 0; k_font_chars[i]; i++)
        if (k_font_chars[i] == c) return i;
    return 0;
}

struct mode { int w, h, bpp; };

/*
 * The original enumerated the DirectDraw modes (640x400..1024x768, 16/24/32 bpp)
 * into g_modeTable and built text records for the options menu. The port has a
 * single logical game mode; the 320x200 frame is scaled by SDL.
 * TODO: verify the record layout against DD_Init's disassembly when the
 * options menu is reached.
 */
static void build_mode_table(void)
{
    static const struct mode modes[] = {{640, 480, 16}};
    const int n = (int)(sizeof modes / sizeof modes[0]);
    uint32_t text = G_MODE_LABEL_BUF;
    int y = ((16 - (n > 10 ? 10 : n)) * 30) / 2;
    for (int i = 0; i < n; i++) {
        uint32_t e = G_MODE_TABLE + 0x48u * (uint32_t)i;
        for (uint32_t k = 0; k < 0x48; k += 4) wr32(e + k, 0);
        wr32(e + 0x00, 0);                      /* device                 */
        wr32(e + 0x04, (uint32_t)modes[i].w);
        wr32(e + 0x08, (uint32_t)modes[i].h);
        wr32(e + 0x0c, 0x20);                   /* DDPIXELFORMAT.dwSize    */
        wr32(e + 0x10, 0x40);                   /* DDPF_RGB                */
        wr32(e + 0x18, (uint32_t)modes[i].bpp); /* dwRGBBitCount           */
        wr32(e + 0x1c, 0xf800); wr32(e + 0x20, 0x07e0); wr32(e + 0x24, 0x001f);

        char label[40];
        SDL_snprintf(label, sizeof label, "%dX%dX%d SDL", modes[i].w, modes[i].h, modes[i].bpp);
        int width = 0;
        for (const char *c = label; *c; c++) width += k_glyph_width[font_index(*c)];
        wr32(G_MODE_LABEL_TEXT + 4u * (uint32_t)i, text);
        wr32(G_MODE_LABEL_RECT + 4u * (uint32_t)i, G_MODE_RECT_BUF + 16u * (uint32_t)i);
        wr32(text, 0x126);
        wr32(text + 4, (uint32_t)y);
        size_t len = strlen(label);
        memcpy(&mem[text + 8], label, len + 1);
        wr32(text + 8 + (uint32_t)len + 1, 0xffffffffu);
        uint32_t r = G_MODE_RECT_BUF + 16u * (uint32_t)i;
        wr32(r, 0x126); wr32(r + 4, (uint32_t)(width + 0x12)); wr32(r + 8, (uint32_t)y); wr32(r + 12, 0x23);
        text += 8 + (uint32_t)len + 5;
        y += 30;
    }
    wr32(G_MODE_LABEL_TEXT + 4u * (uint32_t)n, 0);
    wr32(G_MODE_COUNT, (uint32_t)n);
}

/* What DD_Init / DD_SelectMenuMode leave behind for a 640x480x32 menu mode. */
static void setup_menu_mode_globals(void)
{
    wr32(G_MENU_PIXFMT + 0x00, 0x20);
    wr32(G_MENU_PIXFMT + 0x04, 0x40);
    wr32(G_MENU_PIXFMT + 0x08, 0);
    wr32(G_MENU_BITCOUNT, 32);
    wr32(G_MENU_PIXFMT + 0x10, 0x00ff0000);
    wr32(G_MENU_PIXFMT + 0x14, 0x0000ff00);
    wr32(G_MENU_PIXFMT + 0x18, 0x000000ff);
    wr32(G_MENU_PIXFMT + 0x1c, 0);
    static const uint32_t shifts[6] = {16, 8, 8, 8, 0, 8};
    for (int i = 0; i < 6; i++) wr32(G_PIXSHIFT + 4u * (uint32_t)i, shifts[i]);
    wr32(G_MENU_RBYTE, 2); wr32(G_MENU_GBYTE, 1); wr32(G_MENU_BBYTE, 0);
    wr32(G_MENU_BPP_BYTES, SCREEN_BPP);
    wr32(G_MENU_PITCH, SCREEN_W * SCREEN_BPP);
    wr32(G_MENU_IMG_SIZE, SCREEN_W * SCREEN_H * SCREEN_BPP);
}

/* Config_Validate (0x402d10). */
static void config_validate(void)
{
    wr32(G_CFG_RUNCOUNT, rd32(G_CFG_RUNCOUNT) + 1);
    int32_t idx = (int32_t)rd32(G_CFG_MODE_INDEX), count = (int32_t)rd32(G_MODE_COUNT);
    if (idx < 0 || idx >= count) {
        int found = 0;
        for (int i = 0; i < count && !found; i++) {
            uint32_t e = G_MODE_TABLE + 0x48u * (uint32_t)i;
            if (rd32(e + 4) == 640 && rd32(e + 8) == 480 && rd32(e + 0x18) == 16) { idx = i; found = 1; }
        }
        if (!found) idx = 0;
        wr32(G_CFG_MODE_INDEX, (uint32_t)idx);
    }
    int32_t p1 = (int32_t)rd32(G_CFG_CTRL_P1), p2 = (int32_t)rd32(G_CFG_CTRL_P2);
    int32_t joy = (int32_t)rd32(G_JOYSTICK_COUNT), players = (int32_t)rd32(G_CFG_PLAYERS);
    if (p1 == 0x1c || p2 == 0x1c || (p1 == 4 && joy < 2) || (p2 == 4 && joy < 2) ||
        (p1 == 0 && joy < 1) || (p2 == 0 && joy < 1)) {
        static const int32_t table[16] = {8, 12, 0, 8, 4, 8, 0, 4, 8, 16, 0, 8, 8, 4, 0, 4};
        int i = 0;
        if (joy > 0) i = 2;
        if (joy > 1) i += 4;
        if (players > 1) i += 8;
        wr32(G_CFG_CTRL_P1, (uint32_t)table[i]);
        wr32(G_CFG_CTRL_P2, (uint32_t)table[i + 1]);
    }
}

/* ---- initialisation (WinMain up to the main loop) ----------------------- */

bool shell_init(const char *assets_dir)
{
    assets_set_dir(assets_dir);
    if (!assets_load_to_mem("data/ASTRO_TEXT.BIN", VA_TEXT, NULL) ||
        !assets_load_to_mem("data/ASTRO_DATA.BIN", VA_DATA, NULL))
        return false;

    wr32(G_ALLOC_BASE, VA_ALLOC);                /* Mem_AllocArena            */
    core_command(0);                             /* Core_Cmd00_GetRanges      */

    config_load();                               /* Config_Load               */
    wr32(G_APP_ACTIVE, 1);
    setup_menu_mode_globals();                   /* DD_Init                   */
    build_mode_table();
    wr32(G_JOYSTICK_COUNT, 0);                   /* DI_Init                   */
    wr32(G_SOUND_ENABLED, 1);                    /* DS_Init                   */
    wr32(G_CD_ENABLED, 1);                       /* CD init                   */

    /* Gfx_LoadTitleAssets: MENU.DAT, then the menu images in screen format. */
    uint32_t p = VA_ALLOC, size = 0;
    if (!assets_load_to_mem("data/MENU.DAT", p, &size)) return false;
    p += size;
    static const char *const images[] = {"images/PORTADA.png", "images/FONDO1.png", "images/FONDO2.png",
                                         "images/NAVES.png", "images/LETRAS.png"};
    const uint32_t img = SCREEN_W * SCREEN_H * SCREEN_BPP;
    for (int i = 0; i < 5; i++) {
        wr32(G_IMG_PORTADA + 4u * (uint32_t)i, p);
        if (!assets_load_image(images[i], p)) return false;
        p += img;
    }
    wr32(G_IMG_PORTADA + 20, p);                 /* g_imgScreenBackup         */
    uint32_t arena = p + img;
    wr32(G_ARENA_BASE, arena);

    /* GAME.DAT at arena + 0x1ff40; level files will load after it. */
    uint32_t game = arena + 0x1ff40u;
    if (!assets_load_to_mem("data/GAME.DAT", game, &size)) return false;
    wr32(G_LOAD_PTR, game + size);
    wr32(G_LEVEL_LOAD_BASE, game + size);

    /* Gfx_LoadLogos at the level load base. */
    p = game + size;
    static const char *const logos[] = {"images/LOGO1.png", "images/LOGO2.png", "images/LOGO3.png"};
    for (int i = 0; i < 3; i++) {
        wr32(G_IMG_LOGO1 + 4u * (uint32_t)i, p);
        if (!assets_load_image(logos[i], p)) return false;
        p += img;
    }
    if (p > VA_ALLOC + 0x1200000u) { SDL_Log("allocation block overflow"); return false; }

    config_validate();
    audio_init();                                /* DS_Init; game runs without sound if it fails */
    audio_set_volumes(rd32(G_CFG_SFX_VOLUME), rd32(G_CFG_CD_VOLUME));   /* CDVol_Apply */
    core_command(7);                             /* Core_Cmd07_Init           */
    return true;
}

/* Video_SetMenuMode (0x405870): back to the 640x480 menu screen. */
static void set_menu_mode(void)
{
    memset(&mem[VA_PRIMARY], 0, SCREEN_W * SCREEN_H * SCREEN_BPP);
}

/*
 * Video_SetGameMode (0x4052f0). The original picked one of several ways to get
 * the 320x200 frame on screen. The port tells the core it runs in an 8 bpp mode
 * whose palette the core converts itself (mode flag 0x40 set), so the core keeps
 * a PALETTEENTRY palette for the presenter and never runs its 16/24/32 bpp
 * converters.
 */
static void set_game_mode(void)
{
    wr32(G_SCREEN_BPP, 8);
    wr32(G_MODE_FLAG40, 1);
    wr32(G_WAIT_VBLANK, 1);
}

/* Game_RunMenu (0x402ef0). */
static void run_menu(void)
{
    audio_load_set(SOUNDSET_MENU, true);
    core_command(9);
    audio_stop_set(SOUNDSET_MENU);
    audio_load_set(SOUNDSET_MENU, false);
}

/* Gfx_LoadEnding (0x408360) + core command 15. */
static void run_ending(void)
{
    set_menu_mode();
    const uint32_t img = SCREEN_W * SCREEN_H * SCREEN_BPP;
    uint32_t p = rd32(G_LEVEL_LOAD_BASE);
    static const char *const names[] = {"images/FINAL1.png", "images/FINAL2.png"};
    for (int i = 0; i < 2; i++) {
        wr32(G_IMG_FINAL1 + 4u * (uint32_t)i, p);
        if (!assets_load_image(names[i], p)) return;   /* original: skip the ending if loading fails */
        p += img;
    }
    wr32(G_LOAD_PTR, p);
    core_command(15);
}

/* Game_RunLevels (0x402f60). ASTRO_LEVEL=<n> (debugging) starts at level n;
 * the core then lacks the state earlier levels would have left behind. */
static void run_levels(void)
{
    set_game_mode();
    int first = 1;
    const char *dbg = SDL_getenv("ASTRO_LEVEL");
    if (dbg) first = SDL_clamp(SDL_atoi(dbg), 1, 6);
    for (int level = first; level <= 6; level++) {
        char name[32];
        SDL_snprintf(name, sizeof name, "data/LEVEL%d.DAT", level);
        uint32_t base = rd32(G_LEVEL_LOAD_BASE), size = 0;
        if (!assets_load_to_mem(name, base, &size)) exit(1);
        wr32(G_LOAD_PTR, base + size);
        wr32(G_LEVEL_DATA_END, base + size);   /* end of the level data: scratch buffers follow */
        SDL_Log("level %d", level);
        audio_load_set(k_soundset_level[level], true);
        core_command((uint32_t)level);
        audio_stop_set(k_soundset_level[level]);
        audio_load_set(k_soundset_level[level], false);
        if (rd32(G_QUIT_GAME) != 0) return;
    }
    run_ending();
    wr32(G_QUIT_GAME, 1);
}

/* WinMain's main loop; ends when the menu sets g_quitGame = 1 (quit). */
void shell_run(void)
{
    set_menu_mode();
    core_command(22);                       /* Game_SetMenuModeAndInit */
    if (SDL_getenv("ASTRO_ENDING")) {       /* debugging: show the ending directly */
        run_ending();
        return;
    }
    wr32(G_SHELL_PARAM, 0);
    run_menu();
    while (rd32(G_QUIT_GAME) == 0) {
        run_levels();
        wr32(G_SHELL_PARAM, rd32(G_QUIT_GAME) == 1);
        set_menu_mode();
        run_menu();
    }
    config_save();                          /* Config_Save, then App_Shutdown */
}
