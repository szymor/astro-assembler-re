/*
 * Frontend interface. The game (recompiled core + common code in src/) needs
 * only these functions from a frontend; each frontend in src/frontend/
 * implements them and provides main(), which calls astro_main().
 *
 * Pictures are handed over as XRGB8888 (0x00RRGGBB) pixels: the 640x480 menu
 * surface or the 320x200 gameplay frame (already palette-converted), or with a
 * reduced output resolution (settings.resolution) pictures of that size. The
 * frontend shows them in the largest 4:3 area of its output, using
 * settings.filter (see settings.c and scale.h for a software scaler).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* ---- lifetime ------------------------------------------------------------ */
bool plat_init(const char *title);          /* window etc.; settings are loaded */
void plat_shutdown(void);
void plat_error(const char *message);       /* fatal error to show the user     */
const char *plat_device_name(void);         /* shown in Options > Display, like the
                                               original's DirectDraw device name */

/* ---- video ----------------------------------------------------------------- */
void plat_present(const uint32_t *xrgb, int width, int height);
void plat_wait_frame(void);                 /* pace to 60 Hz (one core tick)    */

/* ---- events ---------------------------------------------------------------- */
void plat_pump(void);                       /* process pending events           */
bool plat_quit_requested(void);             /* window closed                    */
bool plat_focused(void);                    /* window is the active one         */
void plat_wait_focus(void);                 /* block until focused or quit      */

/* ---- input ------------------------------------------------------------------- */
/* DirectInput key state: dik[code] = 0x80 while pressed (DIK = PC set-1 scancode,
 * extended keys | 0x80). input.c provides input_dik_from_usb() for frontends
 * whose scancodes are USB HID usages (SDL2, SDL3). */
void plat_keys(uint8_t dik[256]);
int plat_joystick_count(void);              /* joysticks present at start (max 2) */
/* Raw state of joystick `index`: stick axes in -32768..32767, d-pad as -1/0/1,
 * buttons 0 (fire) and 1 (fire 2). False if there is no such joystick. */
bool plat_joystick(int index, int *axis_x, int *axis_y, int *dpad_x, int *dpad_y, bool *b0, bool *b1);

/* ---- audio ------------------------------------------------------------------- */
/* Open the output at `rate` Hz stereo; the frontend calls mix(out, frames) to
 * get float samples (interleaved L/R, already limited to -1..1). */
typedef void (*plat_mix_fn)(float *out, int frames);
bool plat_audio_open(int rate, plat_mix_fn mix);
void plat_audio_close(void);
void plat_audio_lock(void);                 /* exclude the mixer while changing state */
void plat_audio_unlock(void);
