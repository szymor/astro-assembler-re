/*
 * Configuration file, in the original binary format (Config_Load 0x402900,
 * Config_Save 0x402b30). The original kept it as %WINDIR%\ASTRO_CC.CFG; the
 * port keeps it in the user's preference directory, under the same name, so a
 * file from the original can be copied over.
 *
 * Layout (little-endian, read and written in this order):
 *   'RHO!' magic, run counter, players, control P1, control P2,
 *   0x20 + 0x1a + 0x40 bytes (game settings / high-score data),
 *   display mode index, wave volume, CD volume, difficulty.
 */
#include "astro.h"

#include <SDL3/SDL.h>

#define CFG_MAGIC 0x214f4852u   /* "RHO!" */

static const struct { uint32_t va, size; } k_fields[] = {
    {0x55cbe0, 4}, {0x46f440, 4}, {0x46f444, 4}, {0x46f448, 4},
    {0x46f470, 0x20}, {0x46f450, 0x1a}, {0x46f498, 0x40},
    {0x46ec04, 4}, {0x46ec08, 4}, {0x46ec0c, 4}, {0x556abc, 4},
};

static char g_path[1200];
static bool g_save_enabled;   /* DAT_00556830: false if an existing file was not ours */

static const char *config_path(void)
{
    if (!g_path[0]) {
        char *pref = SDL_GetPrefPath("", "Astro Assembler");
        SDL_snprintf(g_path, sizeof g_path, "%sASTRO_CC.CFG", pref ? pref : "");
        SDL_free(pref);
    }
    return g_path;
}

void config_load(void)
{
    g_save_enabled = true;
    SDL_IOStream *io = SDL_IOFromFile(config_path(), "rb");
    if (!io) return;                       /* no file yet: defaults, like a first run */
    uint32_t magic = 0;
    if (SDL_ReadIO(io, &magic, 4) != 4 || magic != CFG_MAGIC) {
        /* the original refused to use (or overwrite) a foreign file */
        SDL_Log("%s is not a configuration file; settings will not be saved", g_path);
        g_save_enabled = false;
        SDL_CloseIO(io);
        return;
    }
    for (size_t i = 0; i < sizeof k_fields / sizeof k_fields[0]; i++) {
        uint8_t buf[0x40];
        if (SDL_ReadIO(io, buf, k_fields[i].size) != k_fields[i].size) break;   /* partial file: stop */
        memcpy(&mem[k_fields[i].va], buf, k_fields[i].size);
    }
    SDL_CloseIO(io);
}

void config_save(void)
{
    if (!g_save_enabled) return;
    SDL_IOStream *io = SDL_IOFromFile(config_path(), "wb");
    if (!io) { SDL_Log("cannot write %s: %s", g_path, SDL_GetError()); return; }
    uint32_t magic = CFG_MAGIC;
    bool ok = SDL_WriteIO(io, &magic, 4) == 4;
    for (size_t i = 0; ok && i < sizeof k_fields / sizeof k_fields[0]; i++)
        ok = SDL_WriteIO(io, &mem[k_fields[i].va], k_fields[i].size) == k_fields[i].size;
    if (!SDL_CloseIO(io) || !ok) SDL_Log("writing %s failed", g_path);
}
