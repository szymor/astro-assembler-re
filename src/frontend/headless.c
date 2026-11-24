/*
 * Headless frontend: no window, no audio device, no frame pacing. Runs the game
 * as fast as possible and prints hashes of everything it would have shown and
 * played, for automated regression tests and cross-platform comparisons.
 *
 *   ASTRO_FRAMES=<n>       stop after n presented frames (default 3600 = 1 min)
 *   ASTRO_HASH_EVERY=<n>   also print the running hashes every n frames
 *   ASTRO_KEYS=...         scripted input (see input.c)
 *   ASTRO_REPLAY=<file>    recorded input (see replay.c); without ASTRO_FRAMES
 *                          the run stops where the recording ends, or, if
 *                          the recorded session quit, where the game quits
 *                          (at most 10 s after the recorded end)
 *
 * The game never reads or writes the user's settings here, so runs depend only
 * on the assets and the input script. Audio is mixed for exactly 1/60 s per
 * frame and hashed after quantising to 16 bits.
 */
#include <stdlib.h>
#include <string.h>

#include "../astro.h"

static uint64_t g_video_hash, g_audio_hash;
static unsigned long g_limit = 3600, g_every;
static plat_mix_fn g_mix;

const char *plat_device_name(void) { return "HEADLESS"; }

static void report(const char *what)
{
    printf("%s frames=%lu video=%016llx audio=%016llx\n", what, video_frame_count(),
           (unsigned long long)g_video_hash, (unsigned long long)g_audio_hash);
    fflush(stdout);
}

bool plat_init(const char *title)
{
    const char *n = getenv("ASTRO_FRAMES"), *e = getenv("ASTRO_HASH_EVERY");
    if (n) g_limit = strtoul(n, NULL, 10);
    else if (replay_end_frame()) g_limit = replay_end_frame() + (replay_ends_with_quit() ? 600 : 0);
    if (e) g_every = strtoul(e, NULL, 10);
    return true;
}

/* The game quit by itself (menu) before the frame limit. */
void plat_shutdown(void) { report("headless"); }
void plat_error(const char *message) { astro_log("%s", message); }

void plat_present(const uint32_t *xrgb, int w, int h)
{
    uint32_t dims[2] = {(uint32_t)w, (uint32_t)h};
    g_video_hash = astro_hash(g_video_hash, dims, sizeof dims);
    g_video_hash = astro_hash(g_video_hash, xrgb, (size_t)w * (size_t)h * 4);
    unsigned long frames = video_frame_count();
    if (g_every && frames % g_every == 0) report("at");
    if (frames >= g_limit) {
        report("headless");
        exit(0);
    }
}

void plat_wait_frame(void)
{
    if (!g_mix) return;
    static float buf[2 * (MIX_RATE / 60)];
    int16_t q[2 * (MIX_RATE / 60)];
    g_mix(buf, MIX_RATE / 60);
    for (int i = 0; i < 2 * (MIX_RATE / 60); i++) q[i] = (int16_t)(buf[i] * 32767.0f);
    g_audio_hash = astro_hash(g_audio_hash, q, sizeof q);
}

void plat_pump(void) {}
bool plat_quit_requested(void) { return false; }
bool plat_focused(void) { return true; }
void plat_wait_focus(void) {}

void plat_keys(uint8_t dik[256]) { (void)dik; }
int plat_joystick_count(void) { return 0; }
bool plat_joystick(int index, int *ax, int *ay, int *dx, int *dy, bool *b0, bool *b1) { return false; }

bool plat_audio_open(int rate, plat_mix_fn mix) { g_mix = mix; return true; }
void plat_audio_close(void) { g_mix = NULL; }
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

int main(int argc, char **argv)
{
    astro_no_user_files = true;
    return astro_main(argc, argv);
}
