/* Common entry point; each frontend's main() calls astro_main(). */
#include "astro.h"

#include <stdio.h>
#include <stdlib.h>

/* The asset folder: the first argument, else "assets" next to the executable
 * (a package started from a file manager or shortcut), else "assets" in the
 * current directory (development builds run from the source tree). */
static const char *find_assets(int argc, char **argv)
{
    static char path[1100];
    if (argc > 1) return argv[1];
    snprintf(path, sizeof path, "%sassets", astro_exe_dir());
    char probe[1200];
    snprintf(probe, sizeof probe, "%s/manifest.json", path);
    FILE *f = astro_exe_dir()[0] ? astro_fopen(probe, "rb") : NULL;
    if (f) { fclose(f); return path; }
    return "assets";
}

int astro_main(int argc, char **argv)
{
    const char *assets = find_assets(argc, argv);

    mem = calloc(RT_MEM_SIZE, 1);
    if (!mem) { astro_log("out of memory"); return 1; }

    settings_load();
    if (!plat_init("Astro Assembler")) return 1;
    if (!shell_init(assets)) {
        plat_error("Could not load the game data. Put the \"assets\" folder next to the "
                   "program, or pass its location as the first argument (it is built "
                   "with tools/package_assets.py).");
        plat_shutdown();
        return 1;
    }
#ifdef RT_WATCH
    /* Debug builds: ASTRO_WATCH=<byte-hex> watches writes of that byte into the
     * 320x200 frame buffer, or ASTRO_WATCH_RANGE=<lo>:<hi> (offsets from the
     * level load base); break on rt_watch in a debugger. */
    if (getenv("ASTRO_WATCH")) {
        rt_watch_lo = rd32(G_ARENA_BASE);
        rt_watch_hi = rt_watch_lo + GAME_W * GAME_H;
        const char *r = getenv("ASTRO_WATCH_RANGE");
        if (r) {
            char *end;
            uint32_t lo = (uint32_t)strtoul(r, &end, 16), hi = lo + 0x10000;
            if (*end == ':') hi = (uint32_t)strtoul(end + 1, NULL, 16);
            rt_watch_lo = rd32(G_LEVEL_LOAD_BASE) + lo;
            rt_watch_hi = rd32(G_LEVEL_LOAD_BASE) + hi;
        }
        rt_watch_byte = (int)strtol(getenv("ASTRO_WATCH"), NULL, 16);
    }
#endif
    shell_run();                         /* returns when the menu quits; config saved */
    audio_shutdown();
    plat_shutdown();
    return 0;
}

/* Leave from inside the game (window closed): save like the normal exit path. */
void astro_exit(void)
{
    config_save();
    settings_save();
    audio_shutdown();
    plat_shutdown();
    exit(0);                             /* runs atexit handlers (debug reports) */
}
