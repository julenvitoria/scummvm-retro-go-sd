/* On-screen keyboard (osk.c): drawn in the bands above and below the 320x200 picture. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* a key typed: ScummVM key code, ASCII value and modifiers (OSK_SHIFT: uppercase / Shift held) */
enum { OSK_SHIFT = 1, OSK_CTRL = 2 };
typedef void (*osk_emit_fn)(int keycode, int ascii, int mods, void *arg);

int  osk_is_open(void);
void osk_open(void);
void osk_close(void);
/* buttons just pressed, and which bit means what; returns 1 when consumed (redraw) */
int  osk_input(uint32_t pressed, uint32_t up, uint32_t down, uint32_t left, uint32_t right, uint32_t type,
               uint32_t close, osk_emit_fn emit, void *arg);
/* draws over a 320x240 LUT8 frame, with the palette entries nearest to its colours */
void osk_draw(uint8_t *fb, const uint32_t *clut);

#ifdef __cplusplus
}
#endif
