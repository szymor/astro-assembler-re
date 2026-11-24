#include "scale.h"

#include <stdlib.h>

void scale_place(int ow, int oh, int num, int den, bool display_4_3, int *x, int *y, int *w, int *h)
{
    /* the output's shape: its pixel count, or 4:3 for a display mode */
    long long on = display_4_3 ? 4 : ow, od = display_4_3 ? 3 : oh;
    int rw = ow, rh = oh;
    if (on * den > od * num) rw = (int)((long long)ow * num * od / (on * den));   /* output wider */
    else rh = (int)((long long)oh * on * den / (od * num));
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

/* Box-filter taps: output i covers source [i*in/out, (i+1)*in/out). Weights
 * are 16.16 fixed point and sum to exactly 65536 per output pixel. */
typedef struct { int first, count; uint32_t w[8]; } area_tap;

static void build_area_taps(area_tap *t, int out, int in)
{
    for (int i = 0; i < out; i++) {
        long long a = (long long)i * in, b = (long long)(i + 1) * in;   /* in units of 1/out */
        int first = (int)(a / out), last = (int)((b - 1) / out);
        if (last - first >= 8) last = first + 7;                        /* >8:1 never needed */
        uint32_t sum = 0;
        t[i].first = first;
        t[i].count = last - first + 1;
        for (int j = first; j <= last; j++) {
            long long lo = (long long)j * out > a ? (long long)j * out : a;
            long long hi = (long long)(j + 1) * out < b ? (long long)(j + 1) * out : b;
            uint32_t w = (uint32_t)(((hi - lo) * 65536 + (b - a) / 2) / (b - a));
            t[i].w[j - first] = w;
            sum += w;
        }
        t[i].w[t[i].count - 1] += 65536u - sum;   /* rounding remainder */
    }
}

static uint32_t area_pixel(const uint32_t *p, size_t stride, const area_tap *t)
{
    uint32_t r = 0x8000, g = 0x8000, b = 0x8000;
    for (int k = 0; k < t->count; k++) {
        uint32_t v = p[(size_t)(t->first + k) * stride], w = t->w[k];
        r += (v >> 16 & 0xff) * w;
        g += (v >> 8 & 0xff) * w;
        b += (v & 0xff) * w;
    }
    return (r >> 16) << 16 | (g >> 16) << 8 | (b >> 16);
}

void scale_area(const uint32_t *src, int sw, int sh, uint32_t *dst, int dw, int dh)
{
    area_tap *tx = malloc(sizeof(area_tap) * (size_t)dw), *ty = malloc(sizeof(area_tap) * (size_t)dh);
    uint32_t *tmp = malloc(sizeof(uint32_t) * (size_t)dw * (size_t)sh);
    if (!tx || !ty || !tmp) { free(tx); free(ty); free(tmp); return; }
    build_area_taps(tx, dw, sw);
    build_area_taps(ty, dh, sh);
    for (int y = 0; y < sh; y++)                                     /* horizontal pass */
        for (int x = 0; x < dw; x++)
            tmp[(size_t)y * (size_t)dw + (size_t)x] = area_pixel(src + (size_t)y * (size_t)sw, 1, &tx[x]);
    for (int y = 0; y < dh; y++)                                     /* vertical pass */
        for (int x = 0; x < dw; x++)
            dst[(size_t)y * (size_t)dw + (size_t)x] = area_pixel(tmp + x, (size_t)dw, &ty[y]);
    free(tx); free(ty); free(tmp);
}
