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

/* The starfield (0x413887): with layer bit 0 set, the stars move, their
 * colours cycle and they are plotted in the given way (the masks: see
 * plot_stars). */
static void draw_stars(unsigned way, uint32_t mask400, uint32_t src320)
{
    if (!(rd16(G_LAYERS) & 1)) return;
    cycle_star_colours();
    move_stars();
    plot_stars(way, mask400, src320);
}

/* ---- composing the frame ---------------------------------------------------
 * Each layer is a colour plane and, for the layers drawn over others, a mask
 * plane further on (layer A: 0x29040 bytes, 400-pixel rows; layer B: 0x20d00
 * bytes, 320-pixel rows): where the mask is 0xff the layer is transparent. The
 * frame is composed dword by dword: lower & mask | colour. */
#define FRAME_BYTES   64000u
#define A_MASK        0x29040u
#define B_MASK        0x20d00u

/* The top-left of each layer's visible part (0x41d1e9, 0x41d226, 0x41d257):
 * its ring position plus the scroll within the current strip (a row per 60
 * units); layer A, 400 pixels wide, also moves with the horizontal scroll. */
static uint32_t layer_a_view(void)
{
    const uint32_t n = rd16(0x4b1f88u);
    return rd32(G_ARENA_BASE) + rd32(0x4b1f70u) + 0x1f40 + rd32(0x4b1f84u) - n / 60 * 400
         + (((uint32_t)rd16(G_SCROLL_X) + 1) >> 1);
}
static uint32_t layer_view(const layer *l)
{
    const uint32_t n = rd16(l->counter);
    return rd32(G_ARENA_BASE) + rd32(l->ring_base) + l->strip_bytes + rd32(l->ring_pos) - n / 60 * l->pitch;
}
#define layer_b_view() layer_view(&k_layer_b)
#define layer_c_view() layer_view(&k_layer_c)

/* One layer alone into the frame (0x41d2a5: 320-pixel rows; 0x41d292: layer
 * A, 320 of its 400 pixels per row), or nothing (0x41d288: cleared). */
static void copy_layer(uint32_t src) { copy_forward(rd32(G_FRAME), src, FRAME_BYTES); }
static void copy_layer_a(uint32_t src)
{
    uint32_t dst = rd32(G_FRAME);
    for (unsigned row = 0; row < 200; row++, src += 400, dst += 320) copy_forward(dst, src, 320);
}
static void clear_frame(void) { fill_dwords(rd32(G_FRAME), 0, FRAME_BYTES / 4); }

/* `lower` (320-pixel rows) under one layer: B (0x41d2ad) or A (0x41d60d). */
static void compose(uint32_t lower, uint32_t over, uint32_t over_pitch, uint32_t mask_offset)
{
    uint32_t dst = rd32(G_FRAME);
    for (unsigned row = 0; row < 200; row++, over += over_pitch - 320)
        for (unsigned x = 0; x < 320; x += 4, lower += 4, over += 4, dst += 4)
            wr32(dst, (rd32(lower) & rd32(over + mask_offset)) | rd32(over));
}
/* Layer C under B under A (0x41e284). */
static void compose3(uint32_t c, uint32_t b, uint32_t a)
{
    uint32_t dst = rd32(G_FRAME);
    for (unsigned row = 0; row < 200; row++, a += 80)
        for (unsigned x = 0; x < 320; x += 4, c += 4, b += 4, a += 4, dst += 4) {
            const uint32_t under = (rd32(c) & rd32(b + B_MASK)) | rd32(b);
            wr32(dst, (under & rd32(a + A_MASK)) | rd32(a));
        }
    wr32(0x46ff8cu, 0);                                        /* its row counter, left at 0 */
}
/* Layer A over what the frame holds (0x41dcb5), with layer bit 2: drawn after
 * the decorations, so it passes in front of them. */
static void layer_a_in_front(void)
{
    if (!(rd16(G_LAYERS) & 4)) return;
    uint32_t a = layer_a_view(), dst = rd32(G_FRAME);
    for (unsigned row = 0; row < 200; row++, a += 80)
        for (unsigned x = 0; x < 320; x += 4, a += 4, dst += 4)
            wr32(dst, (rd32(a + A_MASK) & rd32(dst)) | rd32(a));
}

/* ---- decorations -----------------------------------------------------------
 * Sprites drawn between the background and the front layer (0x413dc3): a list
 * of 0x4a9ad0 entries of 0x14 bytes (arena + 0x4a9ad8): kind (a descriptor
 * offset from 0x4a9ad4), dx (+2), dy (+4), frame (+6), frame step (+8),
 * frame period (+0xa), start position (+0xc), position (+0x10, x and y words in
 * 1/64 pixels). The first 0x4a9adc entries are active (the number moves one
 * step towards 0x4a9ade every 0x4a9ae0 frames): they restart at their start
 * position (x jittered by the random seed) once they leave the bottom; the
 * others finish their way and stop. Descriptor: frame count (+4), size (+6
 * height, +8 width), frame table (+0, dword), sprite data offset (+0xa). */
#define DECO_COUNT  0x4a9ad0u
#define DECO_KINDS  0x4a9ad2u
#define DECO_DESCS  0x4a9ad4u
#define DECO_LIST   0x4a9ad8u
#define DECO_ACTIVE 0x4a9adcu
#define DECO_TARGET 0x4a9adeu
#define DECO_PERIOD 0x4a9ae0u

static uint16_t div16(uint16_t a, uint16_t d)                  /* DIV r/m16 with DX = 0: the remainder */
{
    if (!d) rt_divide_error();
    return (uint16_t)(a % d);
}

static void step_decorations(void)                             /* 0x413e50 */
{
    if (div16(rd16(G_GAME_TIME), rd16(DECO_PERIOD)) == 0) {
        const int16_t to_go = (int16_t)(rd16(DECO_TARGET) - rd16(DECO_ACTIVE));
        if (to_go) wr16(DECO_ACTIVE, (uint16_t)(rd16(DECO_ACTIVE) + (to_go < 0 ? -1 : 1)));
    }
    uint32_t e = rd32(DECO_LIST) + rd32(G_ARENA_BASE);
    uint16_t i = 0;
    do {
        const uint32_t desc = rd16(e) + rd32(DECO_DESCS);
        bool move;
        if ((int16_t)i < (int16_t)rd16(DECO_ACTIVE)) {
            if ((int16_t)rd16(e + 0x12) >= 0x3200) {           /* gone: back to the start */
                wr32(e + 0x10, rd32(e + 0xc));
                wr8(e + 0x11, (uint8_t)(rd8(e + 0x11) ^ (rd16(G_RANDOM_SEED) & 0x3f)));
            }
            move = true;
        } else {
            move = (int16_t)rd16(e + 0x12) < 0x3200 &&
                   (int16_t)(uint16_t)((uint16_t)(rd16(desc + 8) << 6) + rd16(e + 0x12)) >= 0;
        }
        if (move) {
            wr16(e + 0x10, (uint16_t)(rd16(e + 0x10) + rd16(e + 2)));
            wr16(e + 0x12, (uint16_t)(rd16(e + 0x12) + rd16(e + 4)));
        }
        const uint16_t period = rd16(e + 0xa);
        if (period && div16(rd16(G_GAME_TIME), period) == 0) {  /* the next animation frame */
            const uint16_t frames = rd16(desc + 4);
            uint16_t f = (uint16_t)(rd16(e + 6) + rd16(e + 8));
            if ((int16_t)f < 0) f = (uint16_t)(f + frames);
            if ((int16_t)f >= (int16_t)frames) f = (uint16_t)(f - frames);
            wr16(e + 6, f);
        }
        e += 0x14;
        i++;
    } while (i != rd16(DECO_COUNT));
}

/* Core_DrawSprite (0x442454, register level): the sprite data at ESI, the
 * screen position in DX (x) and BX (y), the size in CX and BP. */
static void draw_sprite(uint32_t data, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    WR_REG(R_ESI, 4, data);
    WR_REG(R_EDX, 2, x); WR_REG(R_EBX, 2, y);
    WR_REG(R_ECX, 2, w); WR_REG(R_EBP, 2, h);
    core_call(f_442454, 0x413e37u);
}

static void draw_decorations(void)                             /* 0x413dc3 */
{
    step_decorations();
    wr16(0x46ff4au, rd16(DECO_KINDS));
    do {                                                       /* kind by kind, the highest first */
        uint32_t e = rd32(DECO_LIST) + rd32(G_ARENA_BASE);
        wr16(0x46ffa4u, rd16(DECO_COUNT));
        do {
            if (rd16(e) == rd16(0x46ff4au)) {
                const uint32_t desc = rd16(e) + rd32(DECO_DESCS);
                const uint32_t data = rd32(rd16(e + 6) + rd32(desc)) + rd32(desc + 0xa) + rd32(G_ARENA_BASE);
                const uint16_t x = (uint16_t)((int16_t)(uint16_t)(rd16(e + 0x10) + 0x20) >> 6);
                const uint16_t y = (uint16_t)((int16_t)(uint16_t)(rd16(e + 0x12) + 0x20) >> 6);
                draw_sprite(data, x, y, rd16(desc + 8), rd16(desc + 6));
            }
            e += 0x14;
            wr16(0x46ffa4u, (uint16_t)(rd16(0x46ffa4u) - 1));
        } while (rd16(0x46ffa4u));
        wr16(0x46ff4au, (uint16_t)(rd16(0x46ff4au) - 0xe));
    } while ((int16_t)rd16(0x46ff4au) >= 0);
}

/*
 * Background_DrawLayers (0x41cfaf): composes the frame from the layers the
 * set-up word enables (bit 4: layer C, bit 1: layer B, bit 2: layer A, bit 3:
 * decorations and a front copy of layer A after them; with layer C the stars
 * are plotted in a way that respects the layers above, without it they go on
 * empty pixels, last).
 */
static void draw_layers(void)
{
    const uint16_t l = rd16(G_LAYERS);
    const bool c = l & 0x10, deco = l & 8, b = l & 2, a = l & 4;
    if (c) {
        if (deco) {
            if (b) {
                const uint32_t bv = layer_b_view();
                compose(layer_c_view(), bv, 320, B_MASK);
                draw_stars(STARS_BLEND_MASK320, 0, bv + B_MASK);
            } else {
                copy_layer(layer_c_view());
                draw_stars(STARS_BLEND, 0, 0);
            }
            draw_decorations();
            layer_a_in_front();
        } else if (b && a) {
            const uint32_t av = layer_a_view(), bv = layer_b_view();
            compose3(layer_c_view(), bv, av);
            draw_stars(STARS_BLEND_MASK_AND, av + A_MASK, bv + B_MASK);
        } else if (b) {
            const uint32_t bv = layer_b_view();
            compose(layer_c_view(), bv, 320, B_MASK);
            draw_stars(STARS_BLEND_MASK320, 0, bv + B_MASK);
        } else if (a) {
            const uint32_t av = layer_a_view();
            compose(layer_c_view(), av, 400, A_MASK);
            draw_stars(STARS_BLEND_MASK400, av + A_MASK, 0);
        } else {
            copy_layer(layer_c_view());
            draw_stars(STARS_BLEND, 0, 0);
        }
        return;
    }
    if (deco) {
        if (b) copy_layer(layer_b_view()); else clear_frame();
        draw_decorations();
        layer_a_in_front();
    } else if (b && a) {
        const uint32_t av = layer_a_view();
        compose(layer_b_view(), av, 400, A_MASK);
    } else if (b) {
        copy_layer(layer_b_view());
    } else if (a) {
        copy_layer_a(layer_a_view());
    } else {
        clear_frame();
    }
    draw_stars(STARS_ON_EMPTY, 0, 0);
}
HAND_FN(41cfaf) { draw_layers(); x86_ret(); }

