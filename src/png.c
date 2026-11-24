/*
 * Minimal PNG reader for the packaged images (tools/package_assets.py writes
 * 8-bit RGB, non-interlaced). Also accepts 8-bit RGBA and every PNG filter
 * type, so images re-saved by other tools still load. Uses zlib.
 */
#include "astro.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint32_t *png_decode_xrgb(const void *data, size_t size, int *width, int *height)
{
    const uint8_t *p = data, *end = p + size;
    if (size < 8 || memcmp(p, "\x89PNG\r\n\x1a\n", 8)) return NULL;
    p += 8;
    uint32_t w = 0, h = 0;
    int channels = 0;
    uint8_t *idat = NULL;
    size_t idat_len = 0;
    while (p + 12 <= end) {
        uint32_t len = be32(p);
        const uint8_t *type = p + 4, *body = p + 8;
        if (body + len + 4 > end) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            w = be32(body); h = be32(body + 4);
            int depth = body[8], color = body[9], interlace = body[12];
            if (depth != 8 || interlace || (color != 2 && color != 6)) { free(idat); return NULL; }
            channels = color == 2 ? 3 : 4;
        } else if (!memcmp(type, "IDAT", 4)) {
            uint8_t *grown = realloc(idat, idat_len + len);
            if (!grown) { free(idat); return NULL; }
            idat = grown;
            memcpy(idat + idat_len, body, len);
            idat_len += len;
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        p = body + len + 4;
    }
    if (!w || !h || !channels || !idat || w > 8192 || h > 8192) { free(idat); return NULL; }

    size_t stride = (size_t)w * (size_t)channels;
    uLongf raw_len = (uLongf)((stride + 1) * h);
    uint8_t *raw = malloc(raw_len);
    uint32_t *out = malloc((size_t)w * h * 4);
    if (!raw || !out || uncompress(raw, &raw_len, idat, (uLong)idat_len) != Z_OK || raw_len != (stride + 1) * h) {
        free(raw); free(out); free(idat);
        return NULL;
    }
    free(idat);
    /* undo the per-line filters in place */
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *line = raw + y * (stride + 1), *cur = line + 1;
        const uint8_t *prev = y ? raw + (y - 1) * (stride + 1) + 1 : NULL;
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)channels ? cur[i - (size_t)channels] : 0;
            int b = prev ? prev[i] : 0;
            int c = prev && i >= (size_t)channels ? prev[i - (size_t)channels] : 0;
            switch (line[0]) {
            case 1: cur[i] = (uint8_t)(cur[i] + a); break;
            case 2: cur[i] = (uint8_t)(cur[i] + b); break;
            case 3: cur[i] = (uint8_t)(cur[i] + ((a + b) >> 1)); break;
            case 4: cur[i] = (uint8_t)(cur[i] + paeth(a, b, c)); break;
            default: break;
            }
        }
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *px = cur + (size_t)x * (size_t)channels;
            out[(size_t)y * w + x] = (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
        }
    }
    free(raw);
    *width = (int)w;
    *height = (int)h;
    return out;
}
