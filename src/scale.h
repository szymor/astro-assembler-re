/* Scaling helpers shared by the frontends. */
#pragma once
#include <stdint.h>

#include "astro.h"

/* Largest 4:3 rectangle centred in an output of out_w x out_h pixels. */
void scale_aspect_rect(int out_w, int out_h, int *x, int *y, int *w, int *h);

/*
 * Software scaler (used by frontends without a GPU renderer, e.g. SDL1):
 * scales src (sw x sh XRGB) into the rectangle (dx, dy, dw, dh) of dst, whose
 * line length is dst_pitch pixels, with the same three filters as the GPU
 * frontends: sharp (integer nearest + linear remainder), nearest, smooth.
 */
void scale_xrgb(const uint32_t *src, int sw, int sh, uint32_t *dst, int dst_pitch,
                int dx, int dy, int dw, int dh, scale_filter filter);
