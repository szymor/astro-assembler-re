/* Astro Assembler port - entry point. */
#include "astro.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *assets = argc > 1 ? argv[1] : "assets";

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    mem = calloc(RT_MEM_SIZE, 1);
    if (!mem) { SDL_Log("out of memory"); return 1; }

    if (!video_init()) return 1;
    if (!shell_init(assets)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Astro Assembler",
                                 "Could not load the game data. Build it with tools/package_assets.py.", NULL);
        return 1;
    }
#ifdef RT_WATCH
    /* Debug builds: ASTRO_WATCH=<byte-hex> watches writes of that byte into the
     * 320x200 frame buffer (break on rt_watch in a debugger). */
    if (SDL_getenv("ASTRO_WATCH")) {
        rt_watch_lo = rd32(G_ARENA_BASE);
        rt_watch_hi = rt_watch_lo + GAME_W * GAME_H;
        /* ASTRO_WATCH_RANGE=<lo-offset>:<hi-offset> relative to the level load base */
        const char *r = SDL_getenv("ASTRO_WATCH_RANGE");
        if (r) {
            char *end;
            uint32_t lo = (uint32_t)SDL_strtoul(r, &end, 16), hi = lo + 0x10000;
            if (*end == ':') hi = (uint32_t)SDL_strtoul(end + 1, NULL, 16);
            rt_watch_lo = rd32(G_LEVEL_LOAD_BASE) + lo;
            rt_watch_hi = rd32(G_LEVEL_LOAD_BASE) + hi;
        }
        rt_watch_byte = (int)SDL_strtol(SDL_getenv("ASTRO_WATCH"), NULL, 16);
    }
#endif
    shell_run();
    audio_shutdown();
    video_shutdown();
    SDL_Quit();
    return 0;
}
