/*
 * Settings of the port itself (not part of the original game), kept in
 * astro-port.ini next to the game's ASTRO_CC.CFG:
 *
 *   filter = sharp | nearest | smooth   scaling of the picture (default sharp)
 *   fullscreen = 0 | 1                  start in fullscreen (Alt+Enter toggles)
 */
#include "astro.h"

#include <SDL3/SDL.h>

port_settings settings = {FILTER_SHARP, false};

static const char *const k_filters[] = {"sharp", "nearest", "smooth"};

static void settings_path(char *out, size_t size)
{
    char *pref = SDL_GetPrefPath("", "Astro Assembler");
    SDL_snprintf(out, size, "%sastro-port.ini", pref ? pref : "");
    SDL_free(pref);
}

void settings_load(void)
{
    char path[1200];
    settings_path(path, sizeof path);
    size_t size = 0;
    char *text = SDL_LoadFile(path, &size);
    if (!text) return;
    for (char *line = text, *next; line && *line; line = next) {
        next = SDL_strchr(line, '\n');
        if (next) *next++ = 0;
        char key[32], value[32];
        if (line[0] == '#' || SDL_sscanf(line, " %31[a-z_] = %31s", key, value) != 2) continue;
        if (!SDL_strcmp(key, "filter")) {
            for (int i = 0; i < 3; i++)
                if (!SDL_strcasecmp(value, k_filters[i])) settings.filter = (scale_filter)i;
        } else if (!SDL_strcmp(key, "fullscreen")) {
            settings.fullscreen = SDL_atoi(value) != 0;
        }
    }
    SDL_free(text);
}

void settings_save(void)
{
    char path[1200];
    settings_path(path, sizeof path);
    SDL_IOStream *io = SDL_IOFromFile(path, "w");
    if (!io) return;
    SDL_IOprintf(io,
                 "# Astro Assembler port settings\n"
                 "# filter: sharp (default), nearest or smooth\n"
                 "filter = %s\n"
                 "fullscreen = %d\n",
                 k_filters[settings.filter], settings.fullscreen ? 1 : 0);
    SDL_CloseIO(io);
}
