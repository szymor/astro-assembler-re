/*
 * The background (stage C, docs/plan.md): plain C, no registers or flags.
 * Its entry points are listed as HAND_STATE in replaced.h, so the differential
 * build checks them by the game state they leave; the HAND_FN bodies below are
 * only the glue between the emulated calls and the C functions.
 *
 * The background is up to three scrolling layers of 20x20-pixel tiles. Each
 * layer keeps a ring buffer of rows in the arena; when its scroll counter
 * passes 0x4b0 units, a new strip of tiles comes into view: the ring position
 * moves back by one strip (wrapping around), the strip is drawn from the
 * layer's tile picture as a row of the layer's map picks it, and the map
 * moves on by the relative step stored after that row. Near the end of the
 * ring the strip is also drawn a second time, one ring further on, so the
 * drawing code can read across the wrap.
 */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>

#define TILE      20u        /* pixels, square */
#define TILE_SIZE (TILE * TILE)
#define STRIP     0x4b0      /* scroll units per strip */

/* One tile: 20 rows of 20 bytes from the tile picture (400 contiguous bytes)
 * to the layer buffer with the given row pitch. */
static void copy_tile(uint32_t dst, uint32_t src, uint32_t pitch)
{
    for (unsigned row = 0; row < TILE; row++, src += TILE, dst += pitch) copy_forward(dst, src, TILE);
}

/* A layer's state (dwords unless noted). */
typedef struct {
    uint32_t counter;        /* word: scroll units towards the next strip */
    uint32_t speed;          /* word: the layer's own speed (layers B and C) */
    uint32_t ring_pos;       /* byte position of the newest strip in the ring */
    uint32_t ring_base;      /* the ring's offset in the arena */
    uint32_t map;            /* the map row of the next strip (an absolute pointer) */
    uint32_t tiles;          /* the tile picture's offset in the arena */
    uint32_t tiles2;         /* layer A: the second plane's tile picture */
    uint32_t tiles2_limit;   /* layer A: the last valid tile offset in it */
    uint32_t pitch;          /* bytes per row of the ring */
    uint32_t strip_bytes;    /* one strip: 20 rows */
    uint32_t ring_bytes;     /* the whole ring */
    uint32_t wrap_pos;       /* the position whose strip is not drawn twice */
    uint32_t tiles_wide;     /* tiles per strip */
    uint32_t plane2;         /* bytes from the first plane to the second (0: none); the
                              * original adds them to where a tile's copy ends */
} layer;

/* Layer A: 400-pixel rows (the whole playfield), two planes. */
static const layer k_layer_a = {0, 0, 0x4b1f84, 0x4b1f70, 0x4b1f80, 0x4b1f74, 0x4b1f78, 0x4b1f7c,
                                400, 0x1f40, 0x157c0, 0x13880, 20, 0x29040};
/* Layers B and C: 320-pixel rows; B has a second plane when layer bit 4 is set. */
static const layer k_layer_b = {0x4b1fb8, 0x4b1fc2, 0x4b1fb4, 0x4b1fa0, 0x4b1fb0, 0x4b1fa4, 0x4b1fa8, 0x4b1fac,
                                320, 0x1900, 0x11300, 0xfa00, 16, 0x20d00};
static const layer k_layer_c = {0x4b1fd4, 0x4b1fde, 0x4b1fd0, 0x4b1fc4, 0x4b1fcc, 0x4b1fc8, 0, 0,
                                320, 0x1900, 0x11300, 0xfa00, 16, 0};

#define G_LAYERS  0x4b3ba0u  /* word: bit 4 = layer B's second plane, see Level_ScrollAndDraw */

/* One strip of a layer at `dst`, the tiles picked by the map words at `map`. */
static void draw_strip(const layer *l, uint32_t dst, uint32_t map)
{
    const uint32_t arena = rd32(G_ARENA_BASE);
    const bool plane2 = l->plane2 && (l != &k_layer_b || (rd16(G_LAYERS) & 0x10));
    for (unsigned i = 0; i < l->tiles_wide; i++, map += 2, dst += TILE) {
        const uint32_t tile = rd16(map) * TILE_SIZE;
        copy_tile(dst, arena + rd32(l->tiles) + tile, l->pitch);
        if (plane2) {                                        /* tile offsets clamped to the second picture */
            const uint32_t tile2 = tile > rd32(l->tiles2_limit) ? rd32(l->tiles2_limit) : tile;
            copy_tile(dst + l->plane2, tile2 + rd32(l->tiles2) + arena, l->pitch);
        }
    }
}

/* A new strip comes into view: the ring moves back by a strip, the strip is
 * drawn (twice near the ring's end) and the map steps on. */
static void new_strip(const layer *l)
{
    int32_t pos = (int32_t)(rd32(l->ring_pos) - l->strip_bytes);
    if (pos < 0) pos += (int32_t)l->ring_bytes;
    wr32(l->ring_pos, (uint32_t)pos);
    const uint32_t dst = (uint32_t)pos + rd32(l->ring_base) + rd32(G_ARENA_BASE);
    const uint32_t map = rd32(l->map);
    draw_strip(l, dst, map);
    if ((uint32_t)pos != l->wrap_pos)                        /* again, one ring further on */
        draw_strip(l, dst + l->ring_bytes, map);
    const uint32_t next = map + 2 * l->tiles_wide;
    wr32(l->map, next + (uint32_t)(int32_t)(int16_t)rd16(next));
}

/*
 * Layers B and C scroll at their own speed relative to the main scroll: this
 * frame's step from the speed list (0x4b1f96) times the layer's speed, divided
 * by the scroll speed (0x4b1f94), plus the layer's speed (16-bit arithmetic as
 * in the original; a scroll speed of 0 faults, as the x86 division does).
 */
static void scroll_layer(const layer *l)
{
    const int16_t speed = (int16_t)rd16(l->speed);
    const int16_t div = (int16_t)rd16(0x4b1f94u);
    if (div == 0) rt_divide_error();
    const int32_t q = (int32_t)speed * (int16_t)rd16(0x4b1f96u) / div;
    const uint16_t step = (uint16_t)(q + speed);
    const uint16_t counter = (uint16_t)(rd16(l->counter) + step);
    wr16(l->counter, counter);
    if ((int16_t)counter < STRIP) return;
    wr16(l->counter, (uint16_t)(counter - STRIP));
    new_strip(l);
}

/* Layer A's new strip (0x41ed18; Level_Scroll calls it when the main scroll
 * passes a strip and layer bit 2 is set). */
HAND_FN(41ed18) { new_strip(&k_layer_a); x86_ret(); }
/* Layer B's scroll (0x41ede7) and layer C's (0x41eefd). */
HAND_FN(41ede7) { scroll_layer(&k_layer_b); x86_ret(); }
HAND_FN(41eefd) { scroll_layer(&k_layer_c); x86_ret(); }

/* ---- the starfield ---------------------------------------------------------
 * Four sets of stars (10-byte records: x word at +0, y in 8.8 fixed point at
 * +4, so the whole pixel row is the byte at +5, a colour index at +9) are laid
 * out back to back in the arena. Each frame they move down at their set's
 * speed (wrapping at row 200), two sets cycle their colours, and they are
 * plotted into the frame buffer in one of five ways, chosen by the layer
 * set-up (Level_ScrollAndDraw's layer drawing passes the way in EAX). */
#define STAR_RECORD   10u
#define G_FRAME       0x55c7f4u      /* dword: the 320x200 frame buffer */
typedef struct { uint32_t base, count; } star_set;
/* In drawing order; the moves run through them the other way round. */
static const star_set k_star_sets[4] = {{0x4c2aa, 40}, {0x4bfee, 70}, {0x4bb3e, 120}, {0x4af86, 300}};

/* The star's offset in the frame (16-bit arithmetic: row * 320 + x wraps) and
 * in a 400-pitch plane (32-bit, from the dwords at +5 and +0 as the original
 * reads them). */
static uint32_t star_pos(uint32_t s) { return (uint16_t)((uint16_t)((int16_t)rd16(s + 5) * 0x140) + rd16(s)); }
static uint32_t star_pos400(uint32_t s) { return (uint32_t)((int32_t)rd32(s + 5) * 400) + rd32(s); }

/* Moves (0x413904): each set by its speed (words from 0x4a9a94, sets from the
 * one at the lowest address up), wrapping at 0xc800 (row 200). */
static void move_stars(void)
{
    uint32_t s = rd32(G_ARENA_BASE) + k_star_sets[3].base;
    for (int set = 3; set >= 0; set--) {
        const uint16_t speed = rd16(0x4a9a94u + 2 * (uint32_t)(3 - set));
        for (uint32_t i = 0; i < k_star_sets[set].count; i++, s += STAR_RECORD) {
            uint16_t y = (uint16_t)(rd16(s + 4) + speed);
            if (y >= 0xc800) y = (uint16_t)(y - 0xc800);
            wr16(s + 4, y);
        }
    }
}
/* Colour cycling (0x4138aa): every 0x4a9a9c-th frame the 70-star set steps its
 * colour index down within 0..7, every 0x4a9a9d-th the 40-star set within 0..15. */
static void cycle_star_colours(void)
{
    static const struct { unsigned set; uint32_t period; uint8_t mask; } k[2] = {{1, 0x4a9a9cu, 7}, {0, 0x4a9a9du, 0xf}};
    for (unsigned j = 0; j < 2; j++) {
        const uint8_t period = rd8(k[j].period);
        if (!period) rt_divide_error();
        if (rd8(G_GAME_TIME) % period) continue;
        uint32_t s = rd32(G_ARENA_BASE) + k_star_sets[k[j].set].base + 9;
        for (uint32_t i = 0; i < k_star_sets[k[j].set].count; i++, s += STAR_RECORD)
            wr8(s, (uint8_t)((rd8(s) - 1) & k[j].mask));
    }
}

/* The five ways (the handlers at 0x4a9a80): how a star is drawn on its pixel. */
enum { STARS_ON_EMPTY, STARS_BLEND, STARS_BLEND_MASK400, STARS_BLEND_MASK_AND, STARS_BLEND_MASK320 };
/* Per way and set: the colour table (sets drawn first) or the colour, and the
 * remap table's field (the frame constants patch the 64 KB blend table in). */
typedef struct { uint32_t table; uint8_t colour; uint32_t remap; } star_look;
static const star_look k_look[5][4] = {
    {{0x4a9aa6, 0, 0}, {0x4a9a9e, 0, 0}, {0, 0xb4, 0}, {0, 0xb0, 0}},
    {{0x4a9abe, 0, 0x413aac}, {0x4a9ab6, 0, 0x413aac}, {0, 0xaa, 0x413a8a}, {0, 0x95, 0x413a8a}},
    {{0x4a9abe, 0, 0x413b0f}, {0x4a9ab6, 0, 0x413b56}, {0, 0xaa, 0x413bb4}, {0, 0x95, 0x413bb4}},
    {{0x4a9abe, 0, 0x413c34}, {0x4a9ab6, 0, 0x413c80}, {0, 0xaa, 0x413ce3}, {0, 0x95, 0x413ce3}},
    {{0x4a9abe, 0, 0x413d2b}, {0x4a9ab6, 0, 0x413d66}, {0, 0xaa, 0x413db5}, {0, 0x95, 0x413db5}},
};

/*
 * The plotting: on an empty pixel only, in the star's colour; or blended with
 * the pixel through the blend table (pixel * 256 + colour), always, or where a
 * mask is set: a 400-pitch plane at `mask400` (EBX), that and a 320-pitch
 * source at `src320` (EBP) together, or a 320-pitch plane at EBP. The masked
 * ways patch their mask bases into their own loops first, as the original
 * does (the loops read them back from there).
 */
static void plot_stars(unsigned way, uint32_t ebx, uint32_t ebp)
{
    if (way == STARS_BLEND_MASK400) {
        wr32(0x413afdu, ebx); wr32(0x413b44u, ebx); wr32(0x413ba8u, ebx);
    } else if (way == STARS_BLEND_MASK_AND) {
        wr32(0x413cd8u, ebx); wr32(0x413c23u, ebx); wr32(0x413c6fu, ebx);
        wr32(0x413ccfu, ebp); wr32(0x413c1au, ebp); wr32(0x413c66u, ebp);
    }
    const uint32_t arena = rd32(G_ARENA_BASE), frame = rd32(G_FRAME);
    for (unsigned set = 0; set < 4; set++) {
        const star_look *look = &k_look[way][set];
        uint32_t s = arena + k_star_sets[set].base;
        for (uint32_t i = 0; i < k_star_sets[set].count; i++, s += STAR_RECORD) {
            const uint32_t at = frame + star_pos(s);
            bool draw;
            switch (way) {
            case STARS_BLEND_MASK400: draw = rd8(star_pos400(s) + ebx) != 0; break;
            case STARS_BLEND_MASK_AND: draw = (rd8(star_pos(s) + ebp) & rd8(star_pos400(s) + ebx)) != 0; break;
            case STARS_BLEND_MASK320: draw = rd8(star_pos(s) + ebp) != 0; break;
            case STARS_ON_EMPTY: draw = rd8(at) == 0; break;
            default: draw = true; break;
            }
            if (!draw) continue;
            const uint8_t colour = look->table ? rd8(look->table + rd8(s + 9)) : look->colour;
            if (way == STARS_ON_EMPTY) wr8(at, colour);
            else wr8(at, rd8(((uint32_t)rd8(at) << 8 | colour) + rd32(look->remap)));
        }
    }
}

/* Background_Stars (0x413887): with layer bit 0 set, the stars move, their
 * colours cycle and they are plotted the way EAX says (EBX and EBP: the
 * masks, see plot_stars). */
HAND_FN(413887)
{
    const uint32_t way = EAX, ebx = EBX, ebp = EBP;
    if (rd16(G_LAYERS) & 1) {
        cycle_star_colours();
        move_stars();
        if (way > STARS_BLEND_MASK320) {
            fprintf(stderr, "Background_Stars: plotting way %u\n", way);
            abort();
        }
        plot_stars(way, ebx, ebp);
    }
    x86_ret();
}
