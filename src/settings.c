/*
 * Settings of the port itself (not part of the original game), kept in
 * astro-port.ini next to the game's ASTRO_CC.CFG:
 *
 *   filter = sharp | nearest | smooth   scaling of the picture (default sharp)
 *   fullscreen = 0 | 1                  start in fullscreen (Alt+Enter toggles)
 *   resolution = 640x480 | 320x240 | 320x200
 *                                       size of the picture (default 640x480):
 *                                       reduced sizes average the 640x480 menus
 *                                       down; 320x240 stretches the 320x200
 *                                       gameplay frame to 240 lines
 *   letterbox = 0 | 1                   320x240: show the 320x200 gameplay frame
 *                                       1:1 between black bars instead of
 *                                       stretching it (pixel-exact, but flatter
 *                                       than the original on a 4:3 screen)
 *   exclusive = 0 | 1                   fullscreen switches the display to the
 *                                       resolution, if the display offers it
 *
 * ASTRO_RESOLUTION and ASTRO_LETTERBOX override `resolution` and `letterbox`
 * (also for the headless frontend) without being saved.
 */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

port_settings settings = {FILTER_SHARP, false, RES_640X480, false, false};

static const char *const k_filters[] = {"sharp", "nearest", "smooth"};
static const char *const k_resolutions[] = {"640x480", "320x240", "320x200"};

/* Saved back instead of ASTRO_RESOLUTION / ASTRO_LETTERBOX overrides. */
static out_resolution g_file_resolution;
static bool g_file_letterbox;

static void parse_resolution(const char *value)
{
    for (int i = 0; i < 3; i++)
        if (!astro_strcasecmp(value, k_resolutions[i])) settings.resolution = (out_resolution)i;
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
        } else if (!strcmp(key, "letterbox")) {
            settings.letterbox = atoi(value) != 0;
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
    const char *res = getenv("ASTRO_RESOLUTION");
    if (res) parse_resolution(res);
    g_file_letterbox = settings.letterbox;
    const char *lb = getenv("ASTRO_LETTERBOX");
    if (lb) settings.letterbox = atoi(lb) != 0;
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
            "# resolution: 640x480 (default), 320x240 or 320x200\n"
            "resolution = %s\n"
            "# letterbox: 1 = at 320x240, gameplay 1:1 between black bars instead of stretched\n"
            "letterbox = %d\n"
            "# exclusive: 1 = fullscreen switches the display to that resolution (if available)\n"
            "exclusive = %d\n",
            k_filters[settings.filter], settings.fullscreen ? 1 : 0,
            k_resolutions[g_file_resolution], g_file_letterbox ? 1 : 0, settings.exclusive ? 1 : 0);
    fclose(f);
}
