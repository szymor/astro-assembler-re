/*
 * Shell callbacks: the core sets g_coreCallback to an ILT slot of the original
 * shell and calls Core_CallShell; the recompiler routes that here.
 * Names and meaning: re/symbols.tsv, docs/modules.md.
 */
#include "astro.h"

#include <SDL3/SDL.h>
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
    video_wait_frame();
}

/* Video_PresentFrame (0x405930): wait for the frame, then show the 320x200
 * frame. The original converted/stretched it per display mode; SDL scales. */
static void cb_present_frame(void)
{
    video_wait_frame();
    wr32(0x55c804, 0);
    video_present_game(rd32(G_FRAME_BUFFER));
}

static void cb_pump_messages(void)
{
    input_pump();
    if (input_quit_requested) {
        SDL_Log("quit requested");
        exit(0);            /* runs atexit handlers (debug reports) */            /* TODO: save the configuration like App_Shutdown */
    }
}

static void unimplemented(uint32_t ilt, const char *name)
{
    static uint32_t seen[64];
    for (int i = 0; i < 64; i++) {
        if (seen[i] == ilt) return;
        if (!seen[i]) { seen[i] = ilt; break; }
    }
    SDL_Log("shell callback %s (%08x) not implemented yet", name, ilt);
}

void host_shell_call(uint32_t ilt)
{
    switch (ilt) {
    case 0x40116d: cb_lock_primary(); break;                         /* Surface_LockPrimary   */
    case 0x4011d6: break;                                            /* Surface_UnlockPrimary */
    case 0x4011e0: cb_wait_vblank(); break;                          /* DD_WaitVBlank         */
    case 0x4011ef: cb_pump_messages(); break;                        /* App_PumpMessages      */
    case 0x401091: input_fill_keystate(); break;                     /* Input_PollKeyboard    */
    case 0x40108c: break;                                            /* Input_PollJoystick: no joystick yet */
    case 0x4011db:                                                   /* Sound_PrimeMenuSet    */
    case 0x401041: case 0x401046: case 0x401050:                     /* Sound_PrimeLevelNSet  */
    case 0x401055: case 0x40105a: case 0x40105f:
        break;                                                       /* warm-up only          */
    case 0x40122b: unimplemented(ilt, "Sound_Play"); break;
    case 0x40119f: unimplemented(ilt, "Sound_Stop"); break;
    case 0x401145: unimplemented(ilt, "CD_PlayTrack"); break;
    case 0x401104: unimplemented(ilt, "CD_Stop"); break;
    case 0x401203: unimplemented(ilt, "CD_Pause"); break;
    case 0x401230: unimplemented(ilt, "CD_Resume"); break;
    case 0x401235: unimplemented(ilt, "CDVol_Set"); break;
    case 0x4010c8: unimplemented(ilt, "CDVol_Apply"); break;
    case 0x40107d: break;                                            /* Palette_Set: read at present time */
    case 0x4010ff: cb_present_frame(); break;                        /* Video_PresentFrame    */
    default:
        SDL_Log("unknown shell callback %08x", ilt);
        abort();
    }
}
