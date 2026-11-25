/*
 * astro-bot: plays the game by search and writes the inputs it chose as an
 * ASTRO_KEYS script, which astro-headless (or any frontend) replays exactly.
 * A test tool for producing whole-game recordings; Linux/POSIX only (fork).
 *
 * It runs like astro-headless (same core, same deterministic state) and, every
 * ASTRO_BOT_SEG frames of gameplay, decides what to hold next:
 *
 *   - rollouts: it forks copies of itself, each of which plays a random plan of
 *     ASTRO_BOT_HORIZON segments ahead and reports how it went (death, level,
 *     lives, score); the first segment of the best plan is kept
 *   - save states: a paused fork is left behind every ASTRO_BOT_CP_EVERY
 *     decisions; when every rollout dies, the search resumes from an earlier
 *     one (further back after repeated failures) with a new random seed
 *   - a death is accepted only when backtracking keeps failing and a ship is
 *     left, and then the older save states are dropped, so later failures
 *     cannot undo it; with no ship left it backtracks further or gives up
 *
 * Menu input comes from ASTRO_KEYS or ASTRO_KEYS_FILE as usual (the bot presses
 * nothing outside gameplay). The output script contains those keys too; pass
 * it back with ASTRO_KEYS_FILE (it soon exceeds the 128 KiB a single
 * environment string may have).
 *
 *   ASTRO_BOT_OUT=<file>     the key script (written at each level and at the end)
 *   ASTRO_BOT_LEVEL=<n>      stop after level n (default 6: play to the ending)
 *   ASTRO_BOT_FRAMES=<n>     stop at this frame (default: no limit)
 *   ASTRO_BOT_FROM=<n>       press nothing before this frame: continue from an
 *                            earlier output script given as ASTRO_KEYS
 *   ASTRO_BOT_SEG, ASTRO_BOT_HORIZON, ASTRO_BOT_ROLLOUTS, ASTRO_BOT_SEED,
 *   ASTRO_BOT_CP_EVERY, ASTRO_BOT_JOBS   search parameters (see defaults below)
 *
 * Game state it reads (see re/symbols.tsv): lives of player 1 (byte 0x47004f),
 * the "player in game" flag (0x46ffe4, cleared by the last death), the score
 * (0x470050), the level the shell is running, and how often the impact sounds
 * were played (hits, also on bosses, which give no score until they die).
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../astro.h"

#define VA_LIVES   0x47004fu
#define VA_INGAME  0x46ffe4u
#define VA_SCORE   0x470050u
#define VA_BOSS    0x4a9774u   /* final boss health (level 6 dome; only RAIDEN's mega bomb hurts it) */
#define VA_BOSS_SHIELD 0x4b1f94u
#define VA_PARTS   0x47008cu   /* special-ship parts, 3 bits per ship; RAIDEN = bits 24-26 */
#define VA_COMPLETE 0x470090u  /* sets completed (kept after assembling) */
#define VA_ENERGY  0x47004eu
#define VA_SHIP    0x4700f2u   /* 0 main ship, 9 RAIDEN */
#define VA_WINDOW  0x470089u   /* ship window open */
#define VA_RESPAWN 0x4700a4u   /* respawn invulnerability: fire 2 is ignored */

#define MAX_HORIZON 32
#define MAX_CP      64
#define MAX_JOBS    64

/* An action: bits 0-3 left/right/up/down, bit 4 fire (held), bit 5 fire 2,
 * bit 6 fire tapped (pressed 3 frames in every 8: every press fires the
 * missiles, which the manual recommends against ground targets), bit 7
 * "assemble": fire 2 pressed at offsets 0-2 and 14-16 from the start of the
 * action (opens the ship window with the first complete ship selected, then
 * assembles it). Keys of keyboard set 1, player 1's default. */
#define A_TAP 0x40
#define A_ASSEMBLE 0x80
#define TAP_PERIOD 8
#define TAP_DOWN 3
static const uint8_t k_keys[6] = {0x18, 0x19, 0x10, 0x1e, 0x39, 0x1c};

static int g_seg = 20, g_horizon = 6, g_rollouts = 24, g_cp_every = 2, g_jobs = 8;
static int g_target_level = 6;
static unsigned long g_frame_limit, g_from;
static const char *g_out;
static uint64_t g_rng = 0x9e3779b97f4a7c15ull;

static enum { MAIN, ROLLOUT } g_mode = MAIN;
static uint8_t g_action;                       /* held now */
static unsigned long g_action_start;           /* frame the current action began */
static unsigned long g_next_decision;

/* rollout child */
static uint8_t g_plan[MAX_HORIZON];
static unsigned long g_plan_start;
static int g_result_fd = -1;
static int g_start_lives, g_start_ingame, g_start_level, g_min_lives;
static unsigned long g_start_impacts;


/* Hits: plays of Impacto01-04 (sound ids 28-31). Hits on bosses give no score
 * until the boss dies, so the search needs them to aim at all. */
#define IMPACT_WEIGHT 150.0
#define RAIDEN_PART_WEIGHT 3e8      /* less than a ship (1e9), more than any score */
#define OTHER_PART_WEIGHT 2e8
#define ENERGY_WEIGHT 2000.0        /* RAIDEN needs 24 */
static unsigned long impacts(void)
{
    return sound_play_count[28] + sound_play_count[29] + sound_play_count[30] + sound_play_count[31];
}

/* main line */
typedef struct { unsigned long frame; uint8_t action; } change;
static change *g_log;
static size_t g_log_len, g_log_cap;
typedef struct { pid_t pid; int fd; unsigned long frame; } checkpoint;
static checkpoint g_cp[MAX_CP];
static int g_ncp, g_decisions;
static uint8_t g_warm[MAX_HORIZON];
static int g_escalation;                       /* failures in a row at this spot */
static unsigned long g_fail_frame;
static int g_main_lives = -1, g_allow_loss;
static unsigned long g_stat_rollouts, g_stat_backtracks, g_stat_deaths;
static int g_super_fd = -1;                    /* to the supervisor: the exit status */
static time_t g_t0;

static uint64_t rnd(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}

static int lives(void) { return mem[VA_LIVES]; }
static int ingame(void) { return (int)rd32(VA_INGAME); }
static uint32_t score(void) { return rd32(VA_SCORE); }

static int popcount(uint32_t x) { int n = 0; while (x) { n += x & 1; x >>= 1; } return n; }

/* RAIDEN (special ship 9) is the only way to hurt the final boss: its mega
 * bomb. Progress towards it: parts collected (3 once completed; assembling
 * uses the parts up but the completed set stays recorded). Any other ship's
 * part blocks it: capsules offer the next part of a started set first, and a
 * new set wipes the others. */
static int raiden_parts(void)
{
    if ((rd32(VA_COMPLETE) >> 24 & 7) == 7) return 3;
    return popcount(rd32(VA_PARTS) >> 24 & 7);
}
static int other_parts(void) { return popcount(rd32(VA_PARTS) & 0x00ffffffu); }

/* Assembling RAIDEN can work now: the dome is alive and can be hit. */
static bool raiden_ready(void)
{
    return shell_current_level == 6 && (int32_t)rd32(VA_BOSS) > 0 && MEM_RD(VA_BOSS_SHIELD, 2) == 0 &&
           (rd32(VA_PARTS) >> 24 & 7) == 7 && mem[VA_ENERGY] >= 24 && mem[VA_SHIP] == 0 &&
           mem[VA_WINDOW] == 0 && MEM_RD(VA_RESPAWN, 2) == 0;
}

static uint8_t random_action(uint8_t prev)
{
    if (rnd() % 100 < 45) return prev;
    static const uint8_t dirs[9] = {0, 1, 2, 4, 8, 1 | 4, 2 | 4, 1 | 8, 2 | 8};
    uint8_t a = dirs[rnd() % 9];
    unsigned r = (unsigned)(rnd() % 100);
    if (r < 50) a |= 0x10;
    else if (r < 92) a |= A_TAP;
    return a;
}

/* ---- the key script ------------------------------------------------------ */

static void log_action(unsigned long frame, uint8_t a)
{
    if (g_log_len && g_log[g_log_len - 1].action == a) return;
    if (g_log_len && g_log[g_log_len - 1].frame == frame) { g_log[g_log_len - 1].action = a; return; }
    if (g_log_len == g_log_cap) {
        g_log_cap = g_log_cap ? g_log_cap * 2 : 1024;
        change *n = realloc(g_log, g_log_cap * sizeof *n);
        if (!n) { astro_log("bot: out of memory"); _exit(1); }
        g_log = n;
    }
    g_log[g_log_len++] = (change){frame, a};
}

static void set_action(unsigned long frame, uint8_t a)
{
    if (a != g_action) g_action_start = frame;
    g_action = a;
    log_action(frame, a);
}

/* Fire 2 presses of A_ASSEMBLE, relative to the action's start. */
static bool assemble_down(unsigned long offset) { return offset < 3 || (offset >= 14 && offset < 17); }

static void write_script(unsigned long end_frame)
{
    if (!g_out) return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", g_out);
    FILE *f = fopen(tmp, "w");
    if (!f) { astro_log("bot: cannot write %s", tmp); return; }
    const char *menu = input_key_script();
    bool first = true;
    if (menu && *menu) {
        size_t n = strlen(menu);
        while (n && (menu[n - 1] == '\n' || menu[n - 1] == '\r' || menu[n - 1] == ',')) n--;
        fwrite(menu, 1, n, f);
        first = n == 0;
    }
    for (int k = 0; k < 6; k++) {
        unsigned long down = 0;
        bool held = false;
#define EMIT(from, to) do { fprintf(f, "%s%lu:%02x:%lu", first ? "" : ",", (unsigned long)(from), k_keys[k], \
                                    (unsigned long)((to) - (from))); first = false; } while (0)
        for (size_t i = 0; i <= g_log_len; i++) {
            unsigned long fr = i < g_log_len ? g_log[i].frame : end_frame;
            bool on = i < g_log_len && (g_log[i].action >> k & 1);
            if (on && !held) { down = fr; held = true; }
            else if (!on && held) { EMIT(down, fr); held = false; }
            /* fire tapped during this change interval: one entry per press */
            if (k == 4 && i < g_log_len && (g_log[i].action & A_TAP) && !(g_log[i].action & 0x10)) {
                unsigned long to = i + 1 < g_log_len ? g_log[i + 1].frame : end_frame;
                for (unsigned long t = fr - fr % TAP_PERIOD; t < to; t += TAP_PERIOD) {
                    unsigned long a = t < fr ? fr : t, b = t + TAP_DOWN < to ? t + TAP_DOWN : to;
                    if (a < b) EMIT(a, b);
                }
            }
            /* assemble: fire 2 presses relative to the interval's start */
            if (k == 5 && i < g_log_len && (g_log[i].action & A_ASSEMBLE)) {
                unsigned long to = i + 1 < g_log_len ? g_log[i + 1].frame : end_frame;
                static const unsigned long win[2][2] = {{0, 3}, {14, 17}};
                for (int w = 0; w < 2; w++) {
                    unsigned long a = fr + win[w][0], b = fr + win[w][1] < to ? fr + win[w][1] : to;
                    if (a < b) EMIT(a, b);
                }
            }
        }
#undef EMIT
    }
    fputc('\n', f);
    fclose(f);
    rename(tmp, g_out);
}

static void kill_checkpoints(int from)
{
    for (int i = from; i < g_ncp; i++) {
        kill(g_cp[i].pid, SIGKILL);
        close(g_cp[i].fd);
        waitpid(g_cp[i].pid, NULL, WNOHANG);
    }
    if (from < g_ncp) g_ncp = from;
}

static void finish(const char *why, int status)
{
    unsigned long f = video_frame_count();
    write_script(f);
    fprintf(stderr, "bot: %s at frame %lu, level %d, score %u, lives %d; %lu rollouts, %lu backtracks, %lu deaths, %lds\n",
            why, f, shell_current_level, score(), lives(), g_stat_rollouts, g_stat_backtracks, g_stat_deaths,
            (long)(time(NULL) - g_t0));
    printf("bot frames=%lu\n", f);
    fflush(NULL);
    kill_checkpoints(0);
    unsigned char st = (unsigned char)status;
    if (g_super_fd >= 0 && write(g_super_fd, &st, 1) != 1) {}
    _exit(status);
}

/* ---- rollouts ------------------------------------------------------------- */

static double fitness(bool dead, unsigned long survived)
{
    if (dead) return -1e15 + (double)survived * 1e6 + score();
    return (double)shell_current_level * 1e12 + (double)lives() * 1e9 + score() +
           IMPACT_WEIGHT * (double)(impacts() - g_start_impacts) +
           RAIDEN_PART_WEIGHT * raiden_parts() - OTHER_PART_WEIGHT * other_parts() +
           ENERGY_WEIGHT * mem[VA_ENERGY];
}

static void report(bool dead)
{
    double fit = fitness(dead, video_frame_count() - g_plan_start);
    if (write(g_result_fd, &fit, sizeof fit) != sizeof fit) {}
    _exit(0);
}

static void rollout_frame(unsigned long f)
{
    bool dead = lives() < g_min_lives || (g_start_ingame && !ingame()) ||
                (shell_current_level == 0 && g_start_level != 0);   /* back in the menus: game over */
    if (dead) report(true);
    if (shell_current_level > g_start_level) report(false);
    unsigned long t = f - g_plan_start;
    if (t >= (unsigned long)(g_horizon * g_seg)) report(false);
    if (t % (unsigned long)g_seg == 0) {
        uint8_t a = g_plan[t / (unsigned long)g_seg];
        if (a != g_action) g_action_start = f;      /* as set_action does in the main line */
        g_action = a;
    }
}

/* Fork the rollouts for the decision at frame f. Returns true in a rollout
 * child (which then just keeps playing), false in the main line, with the best
 * plan in `best` and its fitness in *best_fit. */
static bool run_rollouts(unsigned long f, uint8_t best[MAX_HORIZON], double *best_fit)
{
    int n = g_rollouts * (1 + g_escalation);
    if (n > 256) n = 256;
    static uint8_t plans[256][MAX_HORIZON];
    const bool ready = raiden_ready();
    for (int i = 0; i < n; i++) {
        uint8_t prev = g_action;
        for (int s = 0; s < g_horizon; s++) {
            if (i == 0) plans[i][s] = s + 1 < g_horizon ? g_warm[s + 1] : random_action(g_warm[s]);
            else if (i == 1) plans[i][s] = g_action;
            else plans[i][s] = prev = random_action(prev);
            /* assembling RAIDEN: first segment of a quarter of the plans */
            if (ready && s == 0 && i >= 2 && i % 4 == 0) plans[i][s] = prev = (uint8_t)((plans[i][s] & 0x0f) | A_ASSEMBLE | 0x10);
        }
    }
    struct { pid_t pid; int fd, idx; } run[MAX_JOBS];
    int running = 0, next = 0, besti = -1;
    *best_fit = -1e30;
    while (next < n || running) {
        while (next < n && running < g_jobs) {
            int fds[2];
            if (pipe(fds) != 0) { astro_log("bot: pipe: %s", strerror(errno)); finish("error", 1); }
            fflush(NULL);
            pid_t pid = fork();
            if (pid < 0) { astro_log("bot: fork: %s", strerror(errno)); finish("error", 1); }
            if (pid == 0) {
                close(fds[0]);
                int null = open("/dev/null", O_WRONLY);   /* rollouts stay quiet */
                if (null >= 0) { dup2(null, 1); dup2(null, 2); close(null); }
                g_mode = ROLLOUT;
                memcpy(g_plan, plans[next], sizeof g_plan);
                g_plan_start = f;
                g_result_fd = fds[1];
                g_start_lives = lives();
                g_min_lives = g_start_lives - (g_allow_loss ? 1 : 0);
                g_start_ingame = ingame();
                g_start_level = shell_current_level;
                g_start_impacts = impacts();
                if (g_plan[0] != g_action) g_action_start = f;
                g_action = g_plan[0];
                return true;
            }
            close(fds[1]);
            run[running].pid = pid; run[running].fd = fds[0]; run[running].idx = next;
            running++; next++;
            g_stat_rollouts++;
        }
        int status;
        pid_t done = waitpid(-1, &status, 0);
        if (done < 0) { if (errno == EINTR) continue; astro_log("bot: waitpid: %s", strerror(errno)); finish("error", 1); }
        for (int r = 0; r < running; r++) {
            if (run[r].pid != done) continue;
            double fit = -1e30;
            if (read(run[r].fd, &fit, sizeof fit) != sizeof fit) fit = -1e30;
            close(run[r].fd);
            if (fit > *best_fit) { *best_fit = fit; besti = run[r].idx; }
            run[r] = run[--running];
            break;
        }
    }
    memcpy(best, plans[besti < 0 ? 1 : besti], MAX_HORIZON);
    return false;
}

/* ---- save states and backtracking ---------------------------------------- */

typedef struct { uint64_t seed; int escalation; unsigned long fail_frame, rollouts, backtracks; } resume_msg;

/* Forget save states that are gone. A resumed save state inherits the list as
 * it was when it was made; the main line may have dropped some of those since
 * (their pipe then has no reader: POLLERR). */
static void prune_checkpoints(void)
{
    int n = 0;
    for (int i = 0; i < g_ncp; i++) {
        struct pollfd p = {g_cp[i].fd, POLLOUT, 0};
        if (poll(&p, 1, 0) >= 0 && (p.revents & (POLLERR | POLLHUP | POLLNVAL))) { close(g_cp[i].fd); continue; }
        g_cp[n++] = g_cp[i];
    }
    g_ncp = n;
}

/* Leave a paused copy at this decision. Returns true in that copy once it is
 * resumed (it then redoes this decision as the main line). */
static bool make_checkpoint(unsigned long f)
{
    if (g_ncp == MAX_CP) {                       /* drop the oldest */
        kill(g_cp[0].pid, SIGKILL);
        close(g_cp[0].fd);
        waitpid(g_cp[0].pid, NULL, WNOHANG);
        memmove(g_cp, g_cp + 1, (MAX_CP - 1) * sizeof g_cp[0]);
        g_ncp--;
    }
    int fds[2];
    if (pipe(fds) != 0) return false;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return false; }
    if (pid == 0) {
        close(fds[1]);
        resume_msg m;
        ssize_t got;
        do got = read(fds[0], &m, sizeof m); while (got < 0 && errno == EINTR);
        if (got != sizeof m) _exit(0);           /* abandoned */
        close(fds[0]);
        g_rng = m.seed;
        g_escalation = m.escalation;
        g_fail_frame = m.fail_frame;
        g_stat_rollouts = m.rollouts;
        g_stat_backtracks = m.backtracks;
        prune_checkpoints();
        return true;
    }
    close(fds[0]);
    g_cp[g_ncp++] = (checkpoint){pid, fds[1], f};
    return false;
}

/* Every rollout died: resume an earlier save state and leave. Returns only
 * when there is none to go back to. */
static void backtrack(unsigned long f)
{
    prune_checkpoints();
    if (g_ncp == 0) return;
    int back = 1 << (g_escalation < 6 ? g_escalation : 6);
    if (back > g_ncp) back = g_ncp;
    g_stat_backtracks++;
    resume_msg m = {rnd() | 1, g_escalation + 1, f > g_fail_frame ? f : g_fail_frame, g_stat_rollouts, g_stat_backtracks};
    /* the chosen save state, else the next younger ones */
    for (int k = g_ncp - back; k < g_ncp; k++) {
        if (write(g_cp[k].fd, &m, sizeof m) != sizeof m) continue;   /* gone (SIGPIPE is ignored) */
        fprintf(stderr, "bot: frame %lu: no safe plan, back to frame %lu (escalation %d)\n", f, g_cp[k].frame, m.escalation);
        fflush(NULL);
        close(g_cp[k].fd);
        g_cp[k].fd = -1;
        kill_checkpoints(k + 1);
        _exit(0);                                    /* the resumed copy carries on */
    }
    g_ncp = 0;                                       /* none left */
}

/* ---- the main line ---------------------------------------------------------- */

static void progress(unsigned long f)
{
    static unsigned long last;
    if (f - last < 1200) return;
    last = f;
    write_script(f);
    fprintf(stderr, "bot: frame %lu level %d score %u lives %d energy %d raiden %d other %d boss %d hits %lu | %lu rollouts, %lu backtracks, %lu deaths, %lds\n",
            f, shell_current_level, score(), lives(), mem[VA_ENERGY], raiden_parts(), other_parts(), (int)rd32(VA_BOSS),
            impacts(), g_stat_rollouts, g_stat_backtracks, g_stat_deaths, (long)(time(NULL) - g_t0));
}

static void main_frame(unsigned long f)
{
    /* the committed line's own deaths and extra ships */
    if (shell_current_level >= 1 && shell_current_level <= 6) {
        int l = lives();
        if (g_main_lives >= 0 && l < g_main_lives) { g_stat_deaths++; g_allow_loss = 0; }
        g_main_lives = l;
    }
    if (g_frame_limit && f >= g_frame_limit) finish("frame limit", 0);
    if (shell_current_level > g_target_level) finish("target level done", 0);
    if (f < g_next_decision) return;

    if (f < g_from || !video_is_game() || shell_current_level < 1 || shell_current_level > 6) {
        set_action(f, 0);
        g_next_decision = f + 1;
        static int saved_level;
        if (shell_current_level != saved_level) { saved_level = shell_current_level; write_script(f); }
        return;
    }
    progress(f);
    if (f > g_fail_frame + (unsigned long)(4 * g_horizon * g_seg)) g_escalation = 0;

    if (g_decisions++ % g_cp_every == 0) make_checkpoint(f);   /* a resumed copy continues here too */

    uint8_t best[MAX_HORIZON];
    double fit;
    if (run_rollouts(f, best, &fit)) return;      /* rollout child: keep playing */

    if (fit < -1e14) {                            /* every plan dies */
        bool last_ship = lives() == 0;
        if (g_escalation < 7 || (last_ship && g_escalation < 12)) backtrack(f);
        if (last_ship) finish("no way to survive", 2);
        fprintf(stderr, "bot: frame %lu: accepting a death\n", f);
        kill_checkpoints(0);                      /* a commit point: never back up past it */
        g_allow_loss = 1;
        g_escalation = 0;
        g_fail_frame = f;
    }
    memcpy(g_warm, best, sizeof g_warm);
    set_action(f, best[0]);
    g_next_decision = f + (unsigned long)g_seg;
}

/* ---- supervisor ----------------------------------------------------------------- */

/* The search hands the main line from process to process (a backtrack resumes
 * a save state and the old main line exits), so the process that was started
 * stays behind as a supervisor: it waits for the final status and exits with
 * it, and kills the search when it is interrupted. The search runs in a
 * process group of its own. */
static pid_t g_search_pgid;

static void supervisor_signal(int sig)
{
    if (g_search_pgid > 0) kill(-g_search_pgid, SIGKILL);
    _exit(128 + sig);
}

static void start_supervisor(void)
{
    int fds[2];
    if (pipe(fds) != 0) return;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return; }
    if (pid == 0) {                               /* the search */
        setpgid(0, 0);
        close(fds[0]);
        g_super_fd = fds[1];
        return;
    }
    close(fds[1]);
    g_search_pgid = pid;
    setpgid(pid, pid);                            /* also here: no race with the child */
    signal(SIGINT, supervisor_signal);
    signal(SIGTERM, supervisor_signal);
    signal(SIGHUP, supervisor_signal);
    unsigned char st;
    ssize_t got;
    do got = read(fds[0], &st, 1); while (got < 0 && errno == EINTR);
    if (got != 1) {
        fprintf(stderr, "bot: the search ended without a result\n");
        kill(-pid, SIGKILL);
        _exit(3);
    }
    _exit(st);
}

/* ---- frontend interface (as astro-headless) --------------------------------- */

const char *plat_device_name(void) { return "HEADLESS"; }

static int env_int(const char *name, int def, int lo, int hi)
{
    const char *v = getenv(name);
    int x = v ? atoi(v) : def;
    return x < lo ? lo : x > hi ? hi : x;
}

bool plat_init(const char *title)
{
    g_seg = env_int("ASTRO_BOT_SEG", 20, 1, 600);
    g_horizon = env_int("ASTRO_BOT_HORIZON", 6, 1, MAX_HORIZON);
    g_rollouts = env_int("ASTRO_BOT_ROLLOUTS", 24, 2, 256);
    g_cp_every = env_int("ASTRO_BOT_CP_EVERY", 2, 1, 1000);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    g_jobs = env_int("ASTRO_BOT_JOBS", ncpu > 0 ? (int)ncpu : 4, 1, MAX_JOBS);
    g_target_level = env_int("ASTRO_BOT_LEVEL", 6, 1, 6);
    g_frame_limit = (unsigned long)env_int("ASTRO_BOT_FRAMES", 0, 0, 0x7fffffff);
    g_from = (unsigned long)env_int("ASTRO_BOT_FROM", 0, 0, 0x7fffffff);
    g_out = getenv("ASTRO_BOT_OUT");
    const char *seed = getenv("ASTRO_BOT_SEED");
    if (seed) g_rng = strtoull(seed, NULL, 0) | 1;
    if (getenv("ASTRO_RECORD") || getenv("ASTRO_REPLAY")) {
        astro_log("astro-bot: ASTRO_RECORD/ASTRO_REPLAY are not supported; replay the output script instead");
        return false;
    }
    g_t0 = time(NULL);
    signal(SIGPIPE, SIG_IGN);                     /* a dropped save state must not kill the writer */
    start_supervisor();
    return true;
}

void plat_shutdown(void)
{
    if (g_mode == ROLLOUT) report(true);           /* the game ended inside a rollout */
    finish("game quit", 0);
}
void plat_error(const char *message) { astro_log("%s", message); }

void plat_present(const uint32_t *xrgb, int w, int h)
{
    unsigned long f = video_frame_count();
    if (g_mode == ROLLOUT) rollout_frame(f);
    else main_frame(f);
}

void plat_wait_frame(void) {}
void plat_pump(void) {}
bool plat_quit_requested(void) { return false; }
bool plat_focused(void) { return true; }
void plat_wait_focus(void) {}

static bool tap_down(unsigned long frame) { return frame % TAP_PERIOD < TAP_DOWN; }

void plat_keys(uint8_t dik[256])
{
    for (int k = 0; k < 6; k++)
        if (g_action >> k & 1) dik[k_keys[k]] = 0x80;
    if ((g_action & A_TAP) && tap_down(video_frame_count())) dik[k_keys[4]] = 0x80;
    if ((g_action & A_ASSEMBLE) && assemble_down(video_frame_count() - g_action_start)) dik[k_keys[5]] = 0x80;
}

int plat_joystick_count(void) { return 0; }
bool plat_joystick(int index, int *ax, int *ay, int *dx, int *dy, bool *b0, bool *b1) { return false; }

/* Audio is opened like in astro-headless (the core sees the same state) but
 * never mixed. */
bool plat_audio_open(int rate, plat_mix_fn mix) { return true; }
void plat_audio_close(void) {}
void plat_audio_lock(void) {}
void plat_audio_unlock(void) {}

int main(int argc, char **argv)
{
    astro_no_user_files = true;
    return astro_main(argc, argv);
}
