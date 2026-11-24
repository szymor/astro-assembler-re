/*
 * Settings of the port itself (not part of the original game), kept in
 * astro-port.ini next to the game's ASTRO_CC.CFG:
 *
 *   filter = sharp | nearest | smooth   scaling of the picture (default sharp)
 *   fullscreen = 0 | 1                  start in fullscreen (Alt+Enter toggles)
 */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

port_settings settings = {FILTER_SHARP, false};

static const char *const k_filters[] = {"sharp", "nearest", "smooth"};

static void settings_path(char *out, size_t size)
{
    snprintf(out, size, "%sastro-port.ini", astro_pref_dir());
}

void settings_load(void)
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
        }
    }
    fclose(f);
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
            "fullscreen = %d\n",
            k_filters[settings.filter], settings.fullscreen ? 1 : 0);
    fclose(f);
}
