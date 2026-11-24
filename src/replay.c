/*
 * Input recording and playback.
 *
 *   ASTRO_RECORD=<file>   write every input the core reads to <file>
 *   ASTRO_REPLAY=<file>   feed the core the inputs from <file> instead of the
 *                         keyboard and joysticks
 *
 * The core is deterministic: given the same starting state and the same answers
 * to its input polls (Input_PollKeyboard, Input_PollJoystick) it produces the
 * same frames and sounds. A recording therefore holds the starting state (the
 * configuration file contents, the number of joysticks, debug start options)
 * and the answer to each poll, numbered in the order the core asked. A session
 * recorded in any frontend replays bit-exactly in another, e.g. a game played
 * with the SDL3 frontend replays in astro-headless for a regression test.
 *
 * The file is text, one record per line ('#' starts a comment):
 *
 *   ASTRO-REPLAY 1
 *   joysticks <n>                 joysticks present at start
 *   level <n>                     first level (ASTRO_LEVEL); default 1
 *   ending 1                      ASTRO_ENDING was set
 *   config <hex>                  configuration as loaded (config.c fields);
 *                                 left out when it is the game's built-in
 *                                 default, so recordings carry no game data
 *   k <poll> <frame> [<dik>...]   keyboard state from this poll on (pressed
 *                                 DIK codes in hex); only written on change
 *   j <poll> <frame> <index> <slot> <bits>
 *                                 joystick `index` from this poll on: '-' if
 *                                 absent, else hex bits 0 right, 1 left, 2 up,
 *                                 3 down, 4 button 0, 5 button 1; on change
 *   end <polls> <frame> [quit]    where the recording stopped; "quit" if the
 *                                 game ended by itself (menu quit, ending)
 *
 * <poll> counts keyboard and joystick polls together; <frame> is the number of
 * frames presented at that poll and is only used to detect a playback that has
 * gone out of step with the recording (reported once on stderr). When the
 * recording runs out, input comes from the frontend again.
 */
#include "astro.h"

#include <stdlib.h>
#include <string.h>

#define REPLAY_MAGIC "ASTRO-REPLAY 1"

typedef struct {
    char kind;                 /* 'k' or 'j' */
    unsigned long poll, frame;
    int index, slot;           /* joystick */
    int bits;                  /* joystick: -1 absent */
    uint8_t keys[256];         /* keyboard: 0x80 = pressed */
} replay_event;

static FILE *g_rec;
static unsigned long g_poll;                    /* polls so far (both modes) */

/* recording state */
static uint8_t g_rec_keys[256];
static int g_rec_joy[2] = {-2, -2};             /* -2: nothing written yet */

/* playback state */
static bool g_playing;
static replay_event *g_events;
static size_t g_event_count, g_next;
static unsigned long g_end_poll, g_end_frame;
static bool g_end_quit, g_rec_quit;
static uint8_t g_play_keys[256];
static int g_play_joy[2] = {-1, -1};
static int g_joysticks, g_level = 1;
static bool g_ending, g_has_config, g_desync_reported;
static uint8_t *g_config;
static size_t g_config_size;

bool replay_playing(void) { return g_playing && g_poll <= g_end_poll; }
unsigned long replay_end_frame(void) { return g_playing ? g_end_frame : 0; }
bool replay_ends_with_quit(void) { return g_playing && g_end_quit; }

/* The game ended by itself (shell_run returned). */
void replay_quit(void) { g_rec_quit = true; }

/* ---- recording ----------------------------------------------------------- */

static void rec_finish(void)
{
    if (!g_rec) return;
    fprintf(g_rec, "end %lu %lu%s\n", g_poll, video_frame_count(), g_rec_quit ? " quit" : "");
    fclose(g_rec);
    g_rec = NULL;
}

static void rec_config(const uint8_t *defaults, size_t default_size)
{
    size_t size;
    const uint8_t *cfg = config_image(&size);
    if (size == default_size && !memcmp(cfg, defaults, size)) return;
    fputs("config ", g_rec);
    for (size_t i = 0; i < size; i++) fprintf(g_rec, "%02x", cfg[i]);
    fputc('\n', g_rec);
}

/* ---- playback ------------------------------------------------------------ */

static bool parse_hex_bytes(const char *s, uint8_t **out, size_t *size)
{
    size_t n = strspn(s, "0123456789abcdefABCDEF");
    if (n % 2) return false;
    *out = malloc(n / 2 + 1);
    if (!*out) return false;
    for (size_t i = 0; i < n / 2; i++) {
        char b[3] = {s[2 * i], s[2 * i + 1], 0};
        (*out)[i] = (uint8_t)strtoul(b, NULL, 16);
    }
    *size = n / 2;
    return true;
}

static bool load(const char *path)
{
    FILE *f = astro_fopen(path, "r");
    if (!f) { astro_log("replay: cannot open %s", path); return false; }
    static char line[4096];
    size_t cap = 0;
    bool ok = fgets(line, sizeof line, f) && !strncmp(line, REPLAY_MAGIC, strlen(REPLAY_MAGIC));
    if (!ok) astro_log("replay: %s is not a recording", path);
    g_end_poll = (unsigned long)-1;
    for (int lineno = 2; ok && fgets(line, sizeof line, f); lineno++) {
        char *p = line, *end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) continue;
        if (!strncmp(p, "joysticks ", 10)) {
            g_joysticks = atoi(p + 10);
        } else if (!strncmp(p, "level ", 6)) {
            g_level = atoi(p + 6);
        } else if (!strncmp(p, "ending ", 7)) {
            g_ending = atoi(p + 7) != 0;
        } else if (!strncmp(p, "config ", 7)) {
            free(g_config);
            ok = parse_hex_bytes(p + 7, &g_config, &g_config_size);
            g_has_config = ok;
        } else if (!strncmp(p, "end ", 4)) {
            g_end_poll = strtoul(p + 4, &end, 10);
            g_end_frame = strtoul(end, &end, 10);
            g_end_quit = strstr(end, "quit") != NULL;
        } else if ((*p == 'k' || *p == 'j') && p[1] == ' ') {
            if (g_event_count == cap) {
                cap = cap ? cap * 2 : 1024;
                replay_event *n = realloc(g_events, cap * sizeof *n);
                if (!n) { ok = false; break; }
                g_events = n;
            }
            replay_event *e = &g_events[g_event_count];
            memset(e, 0, sizeof *e);
            e->kind = *p;
            e->poll = strtoul(p + 2, &end, 10);
            e->frame = strtoul(end, &end, 10);
            if (e->kind == 'k') {
                for (;;) {
                    while (*end == ' ') end++;
                    char *next;
                    unsigned long d = strtoul(end, &next, 16);
                    if (next == end) break;
                    if (d < 256) e->keys[d] = 0x80;
                    end = next;
                }
            } else {
                e->index = (int)strtol(end, &end, 10);
                e->slot = (int)strtol(end, &end, 10);
                while (*end == ' ') end++;
                e->bits = *end == '-' ? -1 : (int)strtol(end, NULL, 16);
                if (e->index < 0 || e->index > 1) ok = false;
            }
            if (g_event_count && e->poll <= g_events[g_event_count - 1].poll) ok = false;
            if (!ok) astro_log("replay: %s line %d: bad record", path, lineno);
            g_event_count++;
        } else {
            astro_log("replay: %s line %d: unknown record ignored", path, lineno);
        }
    }
    fclose(f);
    return ok;
}

static void desync(const char *what, const replay_event *e)
{
    if (g_desync_reported) return;
    g_desync_reported = true;
    astro_log("replay out of step at poll %lu: %s (recorded at frame %lu, now frame %lu)",
              g_poll, what, e->frame, video_frame_count());
}

/* The event for the current poll, if the recording has one. */
static const replay_event *current_event(char kind)
{
    if (g_next >= g_event_count || g_events[g_next].poll != g_poll) return NULL;
    const replay_event *e = &g_events[g_next++];
    if (e->kind != kind) desync(kind == 'k' ? "keyboard poll, joystick record" : "joystick poll, keyboard record", e);
    else if (e->frame != video_frame_count()) desync("frame differs", e);
    return e->kind == kind ? e : NULL;
}

/* ---- interface ------------------------------------------------------------- */

void replay_init(void)
{
    const char *play = getenv("ASTRO_REPLAY"), *rec = getenv("ASTRO_RECORD");
    if (play) {
        if (!load(play)) { astro_log("replay: not playing %s", play); exit(1); }
        g_playing = true;
    }
    if (rec) {
        g_rec = astro_fopen(rec, "w");
        if (!g_rec) { astro_log("cannot write recording %s", rec); exit(1); }
        atexit(rec_finish);
    }
}

/* Starting state. Called by shell_init right after Config_Load, with the
 * configuration fields as they were before it (the built-in defaults). */
void replay_start(int joysticks, const uint8_t *defaults, size_t default_size)
{
    if (g_playing && g_has_config) config_set_image(g_config, g_config_size);
    if (!g_rec) return;
    fprintf(g_rec, REPLAY_MAGIC "\n");
    fprintf(g_rec, "joysticks %d\n", joysticks);
    if (replay_start_level() != 1) fprintf(g_rec, "level %d\n", replay_start_level());
    if (replay_ending()) fprintf(g_rec, "ending 1\n");
    rec_config(defaults, default_size);
}

int replay_joystick_count(int live)
{
    return g_playing ? g_joysticks : live;
}

int replay_start_level(void)
{
    if (g_playing) return g_level < 1 ? 1 : g_level > 6 ? 6 : g_level;
    const char *dbg = getenv("ASTRO_LEVEL");
    int first = dbg ? atoi(dbg) : 1;
    return first < 1 ? 1 : first > 6 ? 6 : first;
}

bool replay_ending(void)
{
    return g_playing ? g_ending : getenv("ASTRO_ENDING") != NULL;
}

/* Keyboard poll: `state` holds the live state; during playback it is replaced. */
void replay_keys(uint8_t state[256])
{
    if (replay_playing()) {
        const replay_event *e = current_event('k');
        if (e) memcpy(g_play_keys, e->keys, 256);
        memcpy(state, g_play_keys, 256);
    }
    if (g_rec && memcmp(state, g_rec_keys, 256)) {
        fprintf(g_rec, "k %lu %lu", g_poll, video_frame_count());
        for (int i = 0; i < 256; i++)
            if (state[i] & 0x80) fprintf(g_rec, " %02x", i);
        fputc('\n', g_rec);
        memcpy(g_rec_keys, state, 256);
    }
    g_poll++;
}

/* Joystick poll: `bits` is the live state (-1 absent); returns the state to use. */
int replay_joystick(int index, int slot, int bits)
{
    if (replay_playing()) {
        const replay_event *e = current_event('j');
        if (e) {
            if (e->index != index || e->slot != slot) desync("joystick index or slot differs", e);
            g_play_joy[index] = e->bits;
        }
        bits = g_play_joy[index];
    }
    if (g_rec && bits != g_rec_joy[index]) {
        if (bits < 0) fprintf(g_rec, "j %lu %lu %d %d -\n", g_poll, video_frame_count(), index, slot);
        else fprintf(g_rec, "j %lu %lu %d %d %x\n", g_poll, video_frame_count(), index, slot, bits);
        g_rec_joy[index] = bits;
    }
    g_poll++;
    return bits;
}
