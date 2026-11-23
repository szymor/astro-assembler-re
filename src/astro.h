/*
 * Astro Assembler port - platform layer shared declarations.
 *
 * The game logic is the recompiled assembly core (build/recomp/gen), which
 * runs on an emulated 32-bit address space (recomp_rt.h: `mem`, `cpu`). This
 * layer replaces the original C++ shell: it sets up that memory the way the
 * shell did, issues core commands, and implements the shell callbacks the core
 * calls (see docs/modules.md).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "recomp_rt.h"

/* ---- emulated address space layout (chosen by the port) ------------------ */
#define VA_TEXT        0x00401000u   /* ASTRO.exe .text image                 */
#define VA_DATA        0x0046e000u   /* ASTRO.exe .data image                 */
#define VA_STACK_TOP   0x00300000u   /* x86 stack for core calls              */
#define VA_ALLOC       0x01000000u   /* the shell's VirtualAlloc block         */
#define VA_PRIMARY     0x03000000u   /* 640x480x32 "primary surface"          */

#define SCREEN_W 640
#define SCREEN_H 480
#define SCREEN_BPP 4                 /* menu mode: 32 bpp XRGB                */
#define GAME_W 320                   /* gameplay frame                        */
#define GAME_H 200

/* ---- globals of the original program (see re/symbols.tsv) ---------------- */
#define G_CORE_COMMAND     0x556ae4u
#define G_CORE_CALLBACK    0x46ffb0u
#define G_QUIT_GAME        0x556ae8u
#define G_SHELL_PARAM      0x556aecu
#define G_SOUND_FLAGS      0x556af0u
#define G_SOUND_PANX       0x556af4u
#define G_SOUND_VOLUME     0x556af8u
#define G_CD_TRACK         0x556b2cu
#define G_ALLOC_BASE       0x556ac4u
#define G_ARENA_BASE       0x556ac8u
#define G_LOAD_PTR         0x556aa4u
#define G_LEVEL_LOAD_BASE  0x556accu
#define G_LEVEL_DATA_END   0x556ad0u   /* load pointer after LEVELn.DAT */
#define G_SURFACE_PTR      0x55c7fcu
#define G_SURFACE_PITCH    0x55c800u
#define G_SCREEN_BPP       0x55c374u
#define G_KEY_STATE        0x55c9b0u
#define G_APP_ACTIVE       0x556aacu
#define G_APP_QUIT_MSG     0x556ab4u
#define G_SOUND_ENABLED    0x55c814u
#define G_CD_ENABLED       0x55c81cu
#define G_CFG_RUNCOUNT     0x55cbe0u
#define G_CFG_PLAYERS      0x46f440u
#define G_CFG_CTRL_P1      0x46f444u
#define G_CFG_CTRL_P2      0x46f448u
#define G_CFG_MODE_INDEX   0x46ec04u
#define G_JOYSTICK_COUNT   0x55cbdcu
#define G_MODE_TABLE       0x55a7c8u   /* stride 0x48 */
#define G_MODE_COUNT       0x556b4cu
#define G_MENU_BPP_BYTES   0x55b5d8u
#define G_MENU_PITCH       0x55b5dcu
#define G_MENU_IMG_SIZE    0x55b5e0u
#define G_MENU_RBYTE       0x55b5e4u
#define G_MENU_GBYTE       0x55b5e8u
#define G_MENU_BBYTE       0x55b5ecu
#define G_MENU_BITCOUNT    0x556970u
#define G_MENU_PIXFMT      0x556964u   /* DDPIXELFORMAT dwords 2..9 */
#define G_PIXSHIFT         0x55c378u   /* R shift,width, G shift,width, B shift,width */
#define G_IMG_LOGO1        0x55b5f0u   /* .. LOGO3 at +8 */
#define G_IMG_PORTADA      0x55b5fcu   /* PORTADA, FONDO1, FONDO2, NAVES, LETRAS, backup */
#define G_MODE_LABEL_TEXT  0x55b620u   /* per mode: pointer to text record */
#define G_MODE_LABEL_RECT  0x55b6f0u   /* per mode: pointer to rect record */
#define G_MODE_LABEL_BUF   0x55b7b8u
#define G_MODE_RECT_BUF    0x55c050u
#define G_FRAME_BUFFER     0x55c7f4u   /* 320x200 8-bit frame */
#define G_PALETTE_ENTRIES  0x55c3f0u   /* 256 PALETTEENTRY    */
#define G_MODE_FLAG40      0x55c39cu   /* 1: core keeps PALETTEENTRY palette */
#define G_WAIT_VBLANK      0x556828u
#define G_CFG_DIFFICULTY   0x556abcu

static inline uint32_t rd32(uint32_t va) { return (uint32_t)MEM_RD(va, 4); }
static inline void wr32(uint32_t va, uint32_t v) { MEM_WR(va, 4, v); }

/* ---- shell.c: the original WinMain flow -------------------------------- */
bool shell_init(const char *assets_dir);
void shell_run(void);
void core_command(uint32_t cmd);

/* ---- assets.c ------------------------------------------------------------ */
void *assets_read(const char *rel, size_t *size);          /* malloc'd, NULL on error */
bool assets_load_to_mem(const char *rel, uint32_t va, uint32_t *size_out);
bool assets_load_image(const char *rel, uint32_t va);      /* 640x480 -> XRGB8888 */
void assets_set_dir(const char *dir);

/* ---- video.c ------------------------------------------------------------- */
bool video_init(void);
void video_shutdown(void);
void video_present_primary(void);    /* show the 640x480 primary surface */
void video_wait_frame(void);         /* frame pacing (60 Hz)             */
void video_present_game(uint32_t frame_va);   /* show the 320x200 frame */
void video_toggle_fullscreen(void);
unsigned long video_frame_count(void);   /* frames presented so far */

/* ---- input.c ------------------------------------------------------------- */
void input_pump(void);               /* process SDL events               */
void input_fill_keystate(void);      /* DirectInput-style key state      */
extern bool input_quit_requested;
