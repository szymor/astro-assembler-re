#include "scale.h"

#include <stdlib.h>

void scale_aspect_rect(int ow, int oh, int *x, int *y, int *w, int *h)
{
    int rw = ow, rh = oh;
    if ((long)ow * 3 > (long)oh * 4) rw = oh * 4 / 3; else rh = ow * 3 / 4;
    *w = rw; *h = rh;
    *x = (ow - rw) / 2; *y = (oh - rh) / 2;
}

typedef struct { int i0, i1, w; } tap;   /* source indices and weight of i1 (0..256) */

static void build_taps(tap *t, int out, int in, scale_filter f)
{
    int k = out / in > 1 ? out / in : 1;                 /* integer prescale for "sharp" */
    for (int i = 0; i < out; i++) {
        if (f == FILTER_NEAREST) {
            int s = (int)(((long long)(2 * i + 1) * in) / (2LL * out));
            t[i] = (tap){s, s, 0};
            continue;
        }
        int span = f == FILTER_SHARP ? in * k : in;      /* sample in the prescaled image */
        double u = ((double)i + 0.5) * span / out - 0.5;
        if (u < 0) u = 0;
        int j0 = (int)u, j1 = j0 + 1 < span ? j0 + 1 : j0;
        int w = (int)((u - j0) * 256.0 + 0.5);
        if (f == FILTER_SHARP) { j0 /= k; j1 /= k; }    /* prescaled pixel -> source pixel */
        t[i] = (tap){j0, j1, j0 == j1 ? 0 : w};
    }
}

static uint32_t mix2(uint32_t a, uint32_t b, int w)    /* (a*(256-w) + b*w) / 256 per channel */
{
    uint32_t rb = ((a & 0xff00ff) * (uint32_t)(256 - w) + (b & 0xff00ff) * (uint32_t)w) >> 8;
    uint32_t g = ((a & 0x00ff00) * (uint32_t)(256 - w) + (b & 0x00ff00) * (uint32_t)w) >> 8;
    return (rb & 0xff00ff) | (g & 0x00ff00);
}

void scale_xrgb(const uint32_t *src, int sw, int sh, uint32_t *dst, int pitch,
                int dx, int dy, int dw, int dh, scale_filter f)
{
    if (dw <= 0 || dh <= 0) return;
    tap *tx = malloc(sizeof(tap) * (size_t)dw), *ty = malloc(sizeof(tap) * (size_t)dh);
    if (!tx || !ty) { free(tx); free(ty); return; }
    build_taps(tx, dw, sw, f);
    build_taps(ty, dh, sh, f);
    for (int y = 0; y < dh; y++) {
        const uint32_t *r0 = src + (size_t)ty[y].i0 * (size_t)sw, *r1 = src + (size_t)ty[y].i1 * (size_t)sw;
        uint32_t *out = dst + (size_t)(dy + y) * (size_t)pitch + dx;
        int wy = ty[y].w;
        for (int x = 0; x < dw; x++) {
            const tap *t = &tx[x];
            uint32_t top = t->w ? mix2(r0[t->i0], r0[t->i1], t->w) : r0[t->i0];
            if (wy) {
                uint32_t bot = t->w ? mix2(r1[t->i0], r1[t->i1], t->w) : r1[t->i0];
                top = mix2(top, bot, wy);
            }
            out[x] = top;
        }
    }
    free(tx);
    free(ty);
}
