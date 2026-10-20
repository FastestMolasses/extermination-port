/* The status pages' GS packet builders (see the header and
 * docs/STATUS_FIELD.md). Every store below is one store of the original, at
 * the same offset and width; the original's order is kept where two stores
 * overlap (the quadword zero before the VIF code inside it). */
#include "game/em_ui_packet_original.h"

#include <stddef.h>
#include <string.h>

#include "game/em_message_glyph_original.h"

static int fail(EmUip *u, const char *why)
{
    if (!u->fault) u->fault = why;
    return -1;
}

int em_uip_init(EmUip *u, const EmUipMemory *mem, uint32_t context, uint32_t gs_blocks)
{
    if (!u || !mem || !mem->bytes) return 0;
    memset(u, 0, sizeof *u);
    u->mem = *mem;
    u->context = context;
    u->gs_blocks = gs_blocks;
    return 1;
}

static void w8(uint8_t *p, uint32_t at, uint32_t v) { p[at] = (uint8_t)v; }
static void w16(uint8_t *p, uint32_t at, uint32_t v)
{
    p[at] = (uint8_t)v;
    p[at + 1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t *p, uint32_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[at + (uint32_t)i] = (uint8_t)(v >> (8 * i));
}
static void w64(uint8_t *p, uint32_t at, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[at + (uint32_t)i] = (uint8_t)(v >> (8 * i));
}
static void w128_zero(uint8_t *p, uint32_t at) { memset(p + at, 0, 16); }

/* Arithmetic right shift (the original's sra), sign-preserving. */
static uint32_t sra32(int32_t value, unsigned shift)
{
    const uint32_t v = (uint32_t)value;
    return v & 0x80000000u ? ~(~v >> shift) : v >> shift;
}

/* A 32-bit value as a 64-bit register holds it after the original's
 * sign extension (dsll32 / dsra32, or any 32-bit result). */
static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

static const uint64_t XYZ_Z = (uint64_t)0xFFFFFFu << 32;   /* the XYZ2 Z field */

/* The slot's cursor and `size` packet bytes at it. The cursor word lives
 * at context + 0x10 + 4 * slot (the original's address arithmetic, mod
 * 2^32), the packet at the cursor's value; the packet may not hold the
 * cursor word (the original would then read back its own stores). */
typedef struct {
    uint8_t *cell;
    uint8_t *p;
    uint32_t at;
} Slot;

static int open_slot(EmUip *u, int32_t slot, uint32_t size, Slot *s)
{
    if (!u || u->fault) return -1;
    const uint32_t cell = u->context + 0x10u + ((uint32_t)slot << 2);
    s->cell = u->mem.bytes(u->mem.ctx, cell, 4);
    if (!s->cell) return fail(u, "slot cursor outside memory");
    s->at = (uint32_t)s->cell[0] | (uint32_t)s->cell[1] << 8 | (uint32_t)s->cell[2] << 16 |
            (uint32_t)s->cell[3] << 24;
    if (s->at > UINT32_MAX - size) return fail(u, "packet wraps the address space");
    if (cell + 4u > s->at && cell < s->at + size) return fail(u, "packet overlaps its cursor");
    s->p = u->mem.bytes(u->mem.ctx, s->at, size);
    if (!s->p) return fail(u, "packet outside memory");
    return 0;
}

static void advance(Slot *s, uint32_t size) { w32(s->cell, 0, s->at + size); }

/* The 16-byte tag head every builder writes: byte +3 the id, the word +4,
 * the halfword +0 (the quadword count); bytes +2 and +8..+F are left. */
static void tag(uint8_t *p, uint32_t at, uint32_t id, uint32_t address, uint32_t qwc)
{
    w8(p, at + 3, id);
    w32(p, at + 4, address);
    w16(p, at, qwc);
}

/* 00207D00(slot, mode). */
int em_uip_00207D00(EmUip *u, int32_t slot, int32_t mode)
{
    Slot s;
    if (open_slot(u, slot, EM_UIP_00207D00_BYTES, &s) < 0) return -1;
    uint32_t off = 0x720u;
    if (mode == 3) off = 0x6A0u;
    else if (mode == 2) off = 0x820u;
    else if (mode == 1) off = 0x7A0u;
    tag(s.p, 0, 0x30u, u->gs_blocks + off, 8u);
    advance(&s, EM_UIP_00207D00_BYTES);
    return 0;
}

/* 00207D90(slot, x0, y0, x1, y1). */
int em_uip_00207D90(EmUip *u, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    Slot s;
    if (open_slot(u, slot, EM_UIP_00207D90_BYTES, &s) < 0) return -1;
    const uint64_t packed = (sx((uint32_t)y0) << 32) | (sx((uint32_t)x0) | (sx((uint32_t)x1) << 16)) |
                            (sx((uint32_t)y1) << 48);
    tag(s.p, 0, 0x10u, 0u, 3u);
    advance(&s, EM_UIP_00207D90_BYTES);
    w128_zero(s.p, 0x10);
    w32(s.p, 0x1C, 0x50000002u);
    w64(s.p, 0x20, 0x8001u | (uint64_t)0x10000000u << 32);
    w64(s.p, 0x28, 0xEu);
    w64(s.p, 0x30, packed);
    w64(s.p, 0x38, 0x40u);
    return 0;
}

/* 00207E40(slot, x, y, w, h, rgba, tex0). */
int em_uip_00207E40(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0)
{
    Slot s;
    if (open_slot(u, slot, EM_UIP_00207E40_BYTES, &s) < 0) return -1;
    const uint32_t tw = (uint32_t)(tex0 >> 26) & 0xFu, th = (uint32_t)(tex0 >> 30) & 0xFu;
    const uint32_t uv_far = ((1u << tw) << 4) | ((1u << th) << 20);
    const uint32_t xyz1 = (uint32_t)x | ((uint32_t)y + ((uint32_t)h << 3)) << 16;
    const uint32_t xyz2 = (uint32_t)y << 16 | ((uint32_t)x + ((uint32_t)w << 4));
    tag(s.p, 0, 0x10u, 0u, 7u);
    advance(&s, EM_UIP_00207E40_BYTES);
    w128_zero(s.p, 0x10);
    w32(s.p, 0x1C, 0x50000006u);
    w64(s.p, 0x20, 0xA400000000008001ull);
    w64(s.p, 0x28, 0x0000008413413680ull);
    w64(s.p, 0x30, 0x156u);           /* PRIM   */
    w64(s.p, 0x38, 5u);               /* TEX1_1 */
    w64(s.p, 0x40, tex0);             /* TEX0_1 */
    w64(s.p, 0x48, 0u);               /* UV     */
    w64(s.p, 0x50, rgba);             /* RGBAQ (zero-extended) */
    w64(s.p, 0x58, sx(xyz1) | XYZ_Z); /* XYZ2   */
    w64(s.p, 0x60, sx(uv_far));       /* UV     */
    w64(s.p, 0x68, rgba);             /* RGBAQ  */
    w64(s.p, 0x70, sx(xyz2) | XYZ_Z); /* XYZ2   */
    w64(s.p, 0x78, 0u);               /* TEX1_1 */
    return 0;
}

/* 00207F80(slot, x0, y0, x1, y1, rgba). */
int em_uip_00207F80(EmUip *u, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba)
{
    Slot s;
    if (open_slot(u, slot, EM_UIP_00207F80_BYTES, &s) < 0) return -1;
    const uint64_t d40 = sx((uint32_t)x0 | (uint32_t)y0 << 16), d48 = sx((uint32_t)x1 | (uint32_t)y1 << 16);
    tag(s.p, 0, 0x10u, 0u, 4u);
    advance(&s, EM_UIP_00207F80_BYTES);
    w128_zero(s.p, 0x10);
    w32(s.p, 0x1C, 0x50000003u);
    w64(s.p, 0x20, 0x8001u | (uint64_t)0x44000000u << 32);
    w64(s.p, 0x28, 0x4410u);
    w64(s.p, 0x30, 0x146u);
    w64(s.p, 0x38, rgba);
    w64(s.p, 0x40, d40 | XYZ_Z);
    w64(s.p, 0x48, d48 | XYZ_Z);
    return 0;
}

/* 00208040(slot, a, b, c, rgba): each vertex (int)(x | z << 16), x and z
 * through 001281C0 (z converted first, as the original calls them). */
int em_uip_00208040(EmUip *u, int32_t slot, const uint32_t a[2], const uint32_t b[2], const uint32_t c[2],
                    uint32_t rgba)
{
    if (!u || u->fault) return -1;
    if (!a || !b || !c) return fail(u, "00208040 vertex missing");
    if (!u->mem.to_int) return fail(u, "001281C0 worker missing");
    const uint32_t *v[3] = {a, b, c};
    uint64_t xyz[3];
    for (int i = 0; i < 3; i++) {
        int32_t z, x;
        if (!u->mem.to_int(u->mem.ctx, v[i][1], &z) || !u->mem.to_int(u->mem.ctx, v[i][0], &x))
            return fail(u, "001281C0 worker failed");
        xyz[i] = sx((uint32_t)x | (uint32_t)z << 16) | XYZ_Z;
    }
    Slot s;
    if (open_slot(u, slot, EM_UIP_00208040_BYTES, &s) < 0) return -1;
    tag(s.p, 0, 0x10u, 0u, 5u);
    advance(&s, EM_UIP_00208040_BYTES);
    w128_zero(s.p, 0x10);
    w32(s.p, 0x1C, 0x50000004u);
    w64(s.p, 0x20, 0x8001u | (uint64_t)0x54000000u << 32);
    w64(s.p, 0x28, 0x44410u);
    w64(s.p, 0x30, 0x143u);
    w64(s.p, 0x38, rgba);
    w64(s.p, 0x40, xyz[0]);
    w64(s.p, 0x48, xyz[1]);
    w64(s.p, 0x50, xyz[2]);
    w64(s.p, 0x58, 0u);
    return 0;
}

/* 001CCE80(pkt, dbp, dbw, dpsm, dsax, dsay, rrw, rrh, qwc): the 7-quadword
 * host-to-local transfer header (word asm in the decomp; its readable
 * comment and the instructions agree). */
static void transfer_header(uint8_t *p, uint32_t at, uint32_t dbp, uint32_t dbw, uint32_t dpsm, int32_t dsax,
                            int32_t dsay, uint32_t rrw, uint32_t rrh, uint32_t qwc)
{
    const uint64_t bitbltbuf = (uint64_t)dbp << 32 | (uint64_t)dbw << 48 | (uint64_t)dpsm << 56;
    const uint64_t trxpos = sx((uint32_t)dsax) << 32 | sx((uint32_t)dsay) << 48;
    const uint64_t trxreg = (uint64_t)rrw | (uint64_t)rrh << 32;
    w128_zero(p, at);
    w32(p, at + 0x08, 0x11000000u);
    w32(p, at + 0x0C, (qwc + 6u) | 0x50000000u);
    w64(p, at + 0x10, 4u | (uint64_t)0x10000000u << 32);
    w64(p, at + 0x18, 0xEu);
    w64(p, at + 0x20, bitbltbuf);
    w64(p, at + 0x28, 0x50u);
    w64(p, at + 0x30, trxpos);
    w64(p, at + 0x38, 0x51u);
    w64(p, at + 0x40, trxreg);
    w64(p, at + 0x48, 0x52u);
    w64(p, at + 0x50, 0u);
    w64(p, at + 0x58, 0x53u);
    w64(p, at + 0x60, qwc | (0x8000u | (uint64_t)0x08000000u << 32));
    w64(p, at + 0x68, 0u);
}

/* 001CC8A0(mode, x, y, offset). Each font byte gives two halfwords of four
 * 4-bit texels through D_0026E350 (its low nibble first). */
int em_uip_001CC8A0(EmUip *u, int32_t mode, int32_t x, int32_t y, int32_t offset)
{
    if (!u || u->fault) return -1;
    if (mode != 0 && mode != 1) return 0;
    const int tall = mode == 1;
    const uint8_t *font = tall ? u->font_tall : u->font_small;
    const uint32_t font_size = tall ? u->font_tall_size : u->font_small_size;
    const uint32_t reads = tall ? 30u : 0x20u;
    if (!u->nibbles) return fail(u, "D_0026E350 missing");
    if (!font) return fail(u, "font missing");
    if (offset < 0 || (uint32_t)offset > font_size || font_size - (uint32_t)offset < reads)
        return fail(u, "glyph outside the font");
    const uint32_t bytes = tall ? EM_UIP_001CC8A0_TALL_BYTES : EM_UIP_001CC8A0_SMALL_BYTES;
    Slot s;
    if (open_slot(u, 1, bytes, &s) < 0) return -1;
    const uint8_t *f = font + offset;
    const uint16_t *n = u->nibbles;
    tag(s.p, 0, 0x10u, 0u, tall ? 0x27u : 0xFu);
    advance(&s, bytes);
    if (tall) {
        transfer_header(s.p, 0x10, 0x1B00u, 8u, 0x14u, x, y, 0x20u, 0x20u, 0x20u);
        for (uint32_t i = 0, d = 0x80; i < 10; i++, d += 0x20, f += 3) {
            w16(s.p, d + 0x00, n[f[0] & 0xF]);
            w16(s.p, d + 0x02, n[f[0] >> 4]);
            w16(s.p, d + 0x04, n[f[1] & 0xF]);
            w16(s.p, d + 0x10, n[f[1] >> 4]);
            w16(s.p, d + 0x12, n[f[2] & 0xF]);
            w16(s.p, d + 0x14, n[f[2] >> 4]);
        }
    } else {
        transfer_header(s.p, 0x10, 0x1B00u, 8u, 0x14u, x, y, 0x10u, 0x10u, 8u);
        for (uint32_t i = 0, d = 0x80; i < 0x20; i++, d += 4) {
            w16(s.p, d + 0, n[f[i] & 0xF]);
            w16(s.p, d + 2, n[f[i] >> 4]);
        }
    }
    return 0;
}

/* 001CBC20(slot, x, y, w, h, u, v, style). */
int em_uip_001CBC20(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, int32_t uu, int32_t vv,
                    const EmMessageTextStyle *style)
{
    Slot s;
    if (open_slot(u, slot, EM_UIP_001CBC20_BYTES, &s) < 0) return -1;
    uint32_t rgba = 0x80808080u, alpha = 0;
    if (style) {
        rgba = (uint32_t)style->glyph << 24 | (uint32_t)style->color;
        alpha = style->pad[1];
    }
    const uint64_t v40 = sx((uint32_t)x << 4 | (uint32_t)y << 20) | XYZ_Z;
    const uint64_t v48 = sx((uint32_t)uu << 4 | (uint32_t)vv << 20);
    const uint64_t v58 = sx(((uint32_t)x + (uint32_t)w) << 4 | ((uint32_t)y + sra32(h, 1)) << 20) | XYZ_Z;
    tag(s.p, 0x00, 0x30u, EM_UIP_D_002511C0, 3u);
    tag(s.p, 0x10, 0x30u, EM_UIP_D_00250FC0, 8u);
    /* the inline packet at cursor + 0x20 */
    uint8_t *q = s.p + 0x20;
    tag(q, 0, 0x10u, 0u, 5u);
    advance(&s, EM_UIP_001CBC20_BYTES);
    w128_zero(q, 0x10);
    w32(q, 0x1C, 0x50000004u);
    w64(q, 0x20, 0x8001u | (uint64_t)0x64000000u << 32);
    w64(q, 0x28, 0x413413u);
    /* the style's +7 byte, clamped to 0xF, ORed into both XYZ2 words at
     * bit 16 (the original's read-modify-write after the stores) */
    if (alpha >= 0x10u) alpha = 0xFu;
    const uint64_t z = (uint64_t)alpha << 16;
    w64(q, 0x30, 0u);
    w64(q, 0x38, sx(rgba));
    w64(q, 0x40, v40 | z);
    w64(q, 0x48, v48);
    w64(q, 0x50, sx(rgba));
    w64(q, 0x58, v58 | z);
    return 0;
}

/* 001CC3B0(slot, x, y, u_end, v_end, width, height, style): the packet of
 * the five passes whose register values em_message_glyph_cc3b0 packs. */
int em_uip_001CC3B0(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t u_end, int32_t v_end, int32_t width,
                    int32_t height, const EmMessageTextStyle *style)
{
    EmMessageGlyphFlush f;
    em_message_glyph_cc3b0(slot, x, y, u_end, v_end, width, height, style, &f);
    const int strip = f.kind == EM_MESSAGE_GLYPH_SKEWED;
    const uint32_t bytes = strip ? EM_UIP_001CC3B0_STRIP_BYTES : EM_UIP_001CC3B0_SPRITE_BYTES;
    const uint32_t stride = strip ? 0x60u : 0x40u;
    Slot s;
    if (open_slot(u, slot, bytes, &s) < 0) return -1;
    tag(s.p, 0x00, 0x30u, EM_UIP_D_002511C0, 3u);
    tag(s.p, 0x10, 0x30u, strip ? EM_UIP_D_00251140 : EM_UIP_D_002510C0, 8u);
    tag(s.p, 0x20, 0x10u, 0u, strip ? 0x1Fu : 0x15u);
    advance(&s, bytes);
    w128_zero(s.p, 0x30);
    w32(s.p, 0x3C, strip ? 0x5000001Eu : 0x50000014u);
    const uint64_t giftag = 0x8001u | (uint64_t)(strip ? 0x94000000u : 0x54000000u) << 32;
    const uint64_t regs = strip ? (0x34343431u | (uint64_t)4 << 32) : 0x43431u;
    for (uint32_t i = 0, at = 0x40; i < EM_MESSAGE_GLYPH_PASSES; i++, at += stride) {
        const EmMessageGlyphPass *p = &f.pass[i];
        w64(s.p, at + 0x00, giftag);
        w64(s.p, at + 0x08, regs);
        w64(s.p, at + 0x10, p->rgbaq);
        for (int32_t v = 0; v < f.vertex_count; v++) {
            w64(s.p, at + 0x18 + 0x10u * (uint32_t)v, p->uv[v]);
            w64(s.p, at + 0x20 + 0x10u * (uint32_t)v, p->xyz2[v]);
        }
    }
    return 0;
}

/* The packet range one cursor will take: the cursor word and `size` bytes
 * at its value must be memory, and the range may hold neither cursor word
 * (`other`: the second cursor of the run, or the same one). */
static int check_range(EmUip *u, int32_t slot, uint32_t size, uint32_t other)
{
    const uint32_t cell = u->context + 0x10u + ((uint32_t)slot << 2);
    const uint8_t *c = u->mem.bytes(u->mem.ctx, cell, 4);
    if (!c) return fail(u, "slot cursor outside memory");
    const uint32_t at = (uint32_t)c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
    if (at > UINT32_MAX - size) return fail(u, "packet wraps the address space");
    if ((cell + 4u > at && cell < at + size) || (other + 4u > at && other < at + size))
        return fail(u, "packet overlaps its cursor");
    if (size && !u->mem.bytes(u->mem.ctx, at, size)) return fail(u, "packet outside memory");
    return 0;
}

/* 001CBA50's glyph mapping (the byte before the remap). */
static uint32_t small_glyph(uint32_t c)
{
    if (c < 0x20u) return 0;
    return c == 0x24u ? 0x87u : c - 0x20u;
}

/* 001CBA50(slot, x, y, advance, height, text, style). */
int em_uip_001CBA50(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t advance, int32_t height,
                    const uint8_t *text, uint32_t avail, const EmMessageTextStyle *style)
{
    if (!u || u->fault) return -1;
    if (!text) return fail(u, "001CBA50 text missing");
    uint32_t n = 0;
    while (n < avail && text[n]) n++;
    if (n >= avail) return fail(u, "001CBA50 text without terminator");
    /* Everything the run will touch, before the first store. */
    if (!u->nibbles) return fail(u, "D_0026E350 missing");
    if (!u->font_small) return fail(u, "font missing");
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t offset = small_glyph(text[i]) << 5;
        if (offset > u->font_small_size || u->font_small_size - offset < 0x20u)
            return fail(u, "glyph outside the font");
    }
    const uint32_t flushes = n / EM_UIP_001CBA50_CELLS + 1u;
    if (n > (UINT32_MAX - EM_UIP_001CBC20_BYTES * flushes) / EM_UIP_001CC8A0_SMALL_BYTES)
        return fail(u, "001CBA50 text too long");
    const uint32_t uploads = n * EM_UIP_001CC8A0_SMALL_BYTES, sprites = flushes * EM_UIP_001CBC20_BYTES;
    const uint32_t cell1 = u->context + 0x14u, cell = u->context + 0x10u + ((uint32_t)slot << 2);
    if (cell == cell1) {
        if (check_range(u, 1, uploads + sprites, cell1) < 0) return -1;
    } else if (check_range(u, 1, uploads, cell) < 0 || check_range(u, slot, sprites, cell1) < 0) {
        return -1;
    }
    /* 001CCB00(slot, 0, 0, 0x200, 0x10) is empty. */
    int32_t col = 0, w = 0, h = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (em_uip_001CC8A0(u, 0, col, 0, (int32_t)(small_glyph(text[i]) << 5)) < 0) return -1;
        col += 0x10;
        w = (int32_t)((uint32_t)w + (uint32_t)advance);
        if (col >= 0x200) {
            h = (int32_t)((uint32_t)h + (uint32_t)height);
            if (em_uip_001CBC20(u, slot, x, y, w, h, col, 0x10, style) < 0) return -1;
            x = (int32_t)((uint32_t)x + (uint32_t)w);
            col = w = h = 0;
        }
    }
    h = (int32_t)((uint32_t)h + (uint32_t)height);
    return em_uip_001CBC20(u, slot, x, y, w, h, col, 0x10, style);
}
