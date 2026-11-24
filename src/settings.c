/*
 * Settings of the port itself (not part of the original game), kept in
 * astro-port.ini next to the game's ASTRO_CC.CFG:
 *
 *   filter = sharp | nearest | smooth   scaling of the picture (default sharp)
 *   fullscreen = 0 | 1                  start in fullscreen (Alt+Enter toggles)
 *   resolution = 640x480 | 320x240      detail of the picture handed to the
 *                                       frontend (default 640x480); 320x240
 *                                       averages the 640x480 menus down
 *   gameplay_aspect = 4:3 | 16:10       shape of the gameplay picture: 4:3 as on
 *                                       the original's CRTs (tall pixels), or
 *                                       16:10 = square pixels (wider on 16:9
 *                                       screens); menus stay 4:3
 *   exclusive = 0 | 1                   fullscreen switches the display to the
 *                                       picture's size, if the display offers it
 *
 *                     4:3                        16:10
 *   640x480   the original's look (default)  square gameplay pixels (640X400X32)
 *   320x240   gameplay stretched to 240 lines  gameplay 1:1 between black bars
 *
 * ASTRO_RESOLUTION and ASTRO_GAMEPLAY_ASPECT override `resolution` and
 * `gameplay_aspect` (also for the headless frontend) without being saved.
 */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

port_settings settings = {FILTER_SHARP, false, RES_640X480, ASPECT_4_3, false};

static const char *const k_filters[] = {"sharp", "nearest", "smooth"};
static const char *const k_resolutions[] = {"640x480", "320x240"};
static const char *const k_aspects[] = {"4:3", "16:10"};

/* Saved back instead of environment overrides. */
static out_resolution g_file_resolution;
static gameplay_aspect g_file_aspect;

static void parse_aspect(const char *value)
{
    for (int i = 0; i < 2; i++)
        if (!strcmp(value, k_aspects[i])) { settings.gameplay_aspect = (gameplay_aspect)i; return; }
    astro_log("gameplay_aspect: unknown value %s, using %s", value, k_aspects[settings.gameplay_aspect]);
}

static void parse_resolution(const char *value)
{
    for (int i = 0; i < 2; i++)
        if (!astro_strcasecmp(value, k_resolutions[i])) { settings.resolution = (out_resolution)i; return; }
    astro_log("resolution: unknown value %s, using %s", value, k_resolutions[settings.resolution]);
}

static void settings_path(char *out, size_t size)
{
    snprintf(out, size, "%sastro-port.ini", astro_pref_dir());
}

static void load_file(void)
{
    if (astro_no_user_files) return;
    char path[1200], line[256];
    settings_path(path, sizeof path);
    FILE *f = astro_fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char key[32], value[32];
        if (line[0] == '#' || sscanf(line, " %31[a-z_] = %31s", key, value) != 2) continue;
        if (!strcmp(key, "filter")) {
            for (int i = 0; i < 3; i++)
                if (!astro_strcasecmp(value, k_filters[i])) settings.filter = (scale_filter)i;
        } else if (!strcmp(key, "fullscreen")) {
            settings.fullscreen = atoi(value) != 0;
        } else if (!strcmp(key, "resolution")) {
            parse_resolution(value);
        } else if (!strcmp(key, "gameplay_aspect")) {
            parse_aspect(value);
        } else if (!strcmp(key, "exclusive")) {
            settings.exclusive = atoi(value) != 0;
        }
    }
    fclose(f);
}

void settings_load(void)
{
    load_file();
    g_file_resolution = settings.resolution;
    g_file_aspect = settings.gameplay_aspect;
    const char *res = getenv("ASTRO_RESOLUTION");
    if (res) parse_resolution(res);
    const char *asp = getenv("ASTRO_GAMEPLAY_ASPECT");
    if (asp) parse_aspect(asp);
}

void settings_save(void)
{
    if (astro_no_user_files) return;
    char path[1200];
    settings_path(path, sizeof path);
    FILE *f = astro_fopen(path, "w");
    if (!f) return;
    fprintf(f,
            "# Astro Assembler port settings\n"
            "# filter: sharp (default), nearest or smooth\n"
            "filter = %s\n"
            "fullscreen = %d\n"
            "#\n"
            "# resolution: 640x480 (default) or 320x240 (menus reduced, low-resolution look)\n"
            "# gameplay_aspect: 4:3 (default, as on the original's screens) or 16:10 (square pixels)\n"
            "#            4:3                             16:10\n"
            "# 640x480    the original's look             square gameplay pixels\n"
            "# 320x240    gameplay stretched to 240 lines gameplay 1:1 between black bars\n"
            "resolution = %s\n"
            "gameplay_aspect = %s\n"
            "#\n"
            "# exclusive: 1 = fullscreen switches the display to the picture's size (if available)\n"
            "exclusive = %d\n",
            k_filters[settings.filter], settings.fullscreen ? 1 : 0,
            k_resolutions[g_file_resolution], k_aspects[g_file_aspect], settings.exclusive ? 1 : 0);
    fclose(f);
}
