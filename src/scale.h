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

/*
 * Area (box) resampling of src (sw x sh XRGB) to dst (dw x dh, packed): every
 * output pixel is the average of the source area it covers. Used for the
 * reduced output resolutions: 2x2 averages for 640x480 -> 320x240, coverage
 * weights for 480 -> 200 lines, and a "sharp" 200 -> 240 line stretch (rows
 * copied, blended only where an output row straddles two source rows).
 */
void scale_area(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh);
