/* Scaling helpers shared by the frontends. */
#pragma once
#include <stdint.h>

#include "astro.h"

/*
 * Where a picture of shape num:den goes in an output of out_w x out_h pixels:
 * the largest such rectangle, centred. With display_4_3 the output is a
 * display mode the screen shows as 4:3 whatever its pixel count (exclusive
 * fullscreen), so the shape is measured against 4:3 instead of the pixels.
 */
void scale_place(int out_w, int out_h, int num, int den, bool display_4_3, int *x, int *y, int *w, int *h);

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
