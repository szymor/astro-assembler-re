/* Common entry point; each frontend's main() calls astro_main(). */
#include "astro.h"

#include <stdlib.h>

int astro_main(int argc, char **argv)
{
    const char *assets = argc > 1 ? argv[1] : "assets";

    mem = calloc(RT_MEM_SIZE, 1);
    if (!mem) { astro_log("out of memory"); return 1; }

    settings_load();
    if (!plat_init("Astro Assembler")) return 1;
    if (!shell_init(assets)) {
        plat_error("Could not load the game data. Build it with tools/package_assets.py "
                   "and pass the assets folder as the first argument.");
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
