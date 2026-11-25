/*
 * Shell callbacks: the core sets g_coreCallback to an ILT slot of the original
 * shell and calls Core_CallShell; the recompiler routes that here.
 * Names and meaning: re/symbols.tsv, docs/modules.md.
 */
#include "astro.h"

#include <stdlib.h>

static void cb_lock_primary(void)
{
    wr32(G_SURFACE_PTR, VA_PRIMARY);
    wr32(G_SURFACE_PITCH, SCREEN_W * SCREEN_BPP);
    wr32(G_SHELL_PARAM, 0);                 /* 0 = lock succeeded */
}

static void cb_wait_vblank(void)
{
    /* The menu draws straight into the primary surface and waits for the
     * vertical blank; that is when the frame becomes visible. */
    video_present_primary();
    plat_wait_frame();
}

/* Video_PresentFrame (0x405930): wait for the frame, then show the 320x200
 * frame. The original converted/stretched it per display mode; the frontend scales. */
static void cb_present_frame(void)
{
    plat_wait_frame();
    wr32(0x55c804, 0);
    video_present_game(rd32(G_FRAME_BUFFER));
}

/* App_PumpMessages (0x408150) with the WndProc's WM_ACTIVATEAPP handling:
 * losing focus pauses the CD and the game until the window is active again.
 * ASTRO_NO_FOCUS_PAUSE=1 disables this (automated runs). */
static void cb_pump_messages(void)
{
    plat_pump();
    if (!plat_focused() && !getenv("ASTRO_NO_FOCUS_PAUSE")) {
        bool music = rd32(G_CD_PLAYING) != 0;
        if (music) cd_pause();
        plat_wait_focus();
        if (music) cd_resume();
    }
    if (plat_quit_requested()) {
        /* The original ignored the window's close button (it ran in exclusive
         * fullscreen; only the menu quits). A windowed port must close: save
         * the configuration like the normal exit path and leave. */
        astro_exit();
    }
}

unsigned long sound_play_count[256];   /* plays per sound id (astro-bot's hit counting) */

/* Sound_Play (0x407980): exact integer arithmetic of the original. */
static void cb_sound_play(void)
{
    sound_play_count[rd32(G_SHELL_PARAM) & 0xff]++;
    uint32_t flags = rd32(G_SOUND_FLAGS);
    int pan = 0, volume = 0;
    if (flags & 2) pan = (int)((uint32_t)(rd32(G_CFG_PAN_RANGE) * rd32(G_SOUND_PANX)) / 0x280u) - 10000;
    if (flags & 4) volume = (int)((uint32_t)(rd32(G_CFG_VOL_RANGE) * rd32(G_SOUND_VOLUME)) / 1000u) - 10000;
    audio_play((int)rd32(G_SHELL_PARAM), flags, pan, volume);
}

/* CD_PlayTrack (0x407a50): the track is a word at g_cdTrack. */
static void cb_cd_play(void)
{
    if (rd32(G_CD_ENABLED)) wr32(G_CD_PLAYING, cd_play_track((int)(uint16_t)MEM_RD(G_CD_TRACK, 2)));
}

void host_shell_call(uint32_t ilt)
{
    switch (ilt) {
    case 0x40116d: cb_lock_primary(); break;                         /* Surface_LockPrimary   */
    case 0x4011d6: break;                                            /* Surface_UnlockPrimary */
    case 0x4011e0: cb_wait_vblank(); break;                          /* DD_WaitVBlank         */
    case 0x4011ef: cb_pump_messages(); break;                        /* App_PumpMessages      */
    case 0x401091: input_fill_keystate(); break;                     /* Input_PollKeyboard    */
    case 0x40108c:                                                   /* Input_PollJoystick    */
        input_poll_joystick((int)rd32(G_SHELL_PARAM), (int)rd32(G_JOY_SLOT));
        break;
    case 0x4011db:                                                   /* Sound_PrimeMenuSet    */
    case 0x401041: case 0x401046: case 0x401050:                     /* Sound_PrimeLevelNSet  */
    case 0x401055: case 0x40105a: case 0x40105f:
        break;                                                       /* warm-up only          */
    case 0x40122b: cb_sound_play(); break;                           /* Sound_Play            */
    case 0x40119f: audio_stop((int)rd32(G_SHELL_PARAM)); break;      /* Sound_Stop            */
    case 0x401145: cb_cd_play(); break;                              /* CD_PlayTrack          */
    case 0x401104: cd_stop(); break;                                 /* CD_Stop               */
    case 0x401203: cd_pause(); break;                                /* CD_Pause              */
    case 0x401230: cd_resume(); break;                               /* CD_Resume             */
    case 0x401235:                                                   /* CDVol_Set             */
    case 0x4010c8:                                                   /* CDVol_Apply           */
        audio_set_volumes(rd32(G_CFG_SFX_VOLUME), rd32(G_CFG_CD_VOLUME));
        break;
    case 0x40107d: break;                                            /* Palette_Set: read at present time */
    case 0x4010ff: cb_present_frame(); break;                        /* Video_PresentFrame    */
    default:
        astro_log("unknown shell callback %08x", ilt);
        abort();
    }
}
