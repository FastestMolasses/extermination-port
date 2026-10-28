/* tests/ui_packet_reference_bridge.c - the C side of
 * tools/test_ui_packet_reference.py (docs/STATUS_FIELD.md section 5):
 * one em_ui_packet_original builder over caller regions addressed by
 * original address, with its arguments in a flat array.
 *
 * Nothing here is disc-derived: the captures, the fonts and the ELF data
 * are read at run time from the user's own files. */
#include "game/em_item_sdk_math.h"
#include "game/em_ui_font_raw.h"
#include "game/em_ui_packet_original.h"

#include <stdio.h>
#include <string.h>

typedef int (*UipbToInt)(void *ctx, uint32_t bits, int32_t *result);

typedef struct {
    const uint32_t *address, *size;
    uint8_t *const *bytes;
    uint32_t count;
    UipbToInt to_int;
} Regions;

static uint8_t *region_bytes(void *ctx, uint32_t address, uint32_t size)
{
    const Regions *r = ctx;
    for (uint32_t i = 0; i < r->count; i++)
        if (address >= r->address[i] && size <= r->size[i] && address - r->address[i] <= r->size[i] - size)
            return r->bytes[i] + (address - r->address[i]);
    return NULL;
}

static int region_to_int(void *ctx, uint32_t bits, int32_t *result)
{
    const Regions *r = ctx;
    return r->to_int ? r->to_int(NULL, bits, result) : 0;
}

enum { B_7D00, B_7D90, B_7E40, B_7F80, B_8040, B_CC8A0, B_CBC20, B_CC3B0, B_CBA50 };

/* a: the builder's arguments in the original's order; a style is five
 * entries (present, colour, glyph, flag, the +7 byte). */
int uipb_call(const uint32_t *address, const uint32_t *size, uint8_t *const *bytes, uint32_t count,
              uint32_t context, uint32_t gs_blocks, const uint16_t *nibbles, const uint8_t *tall,
              uint32_t tall_size, const uint8_t *small, uint32_t small_size, UipbToInt to_int, int which,
              const int64_t *a, char *fault, uint32_t fault_cap)
{
    Regions r = {address, size, bytes, count, to_int};
    EmUipMemory mem = {&r, region_bytes, region_to_int};
    EmUip u;
    if (!em_uip_init(&u, &mem, context, gs_blocks)) return -2;
    u.nibbles = nibbles;
    u.font_tall = tall;
    u.font_tall_size = tall_size;
    u.font_small = small;
    u.font_small_size = small_size;
    EmMessageTextStyle style;
    memset(&style, 0, sizeof style);
    const EmMessageTextStyle *sp = NULL;
    if ((which == B_CBC20 || which == B_CC3B0) && a[7]) {
        style.color = (int32_t)a[8];
        style.glyph = (uint8_t)a[9];
        style.flag = (uint8_t)a[10];
        style.pad[1] = (uint8_t)a[11];
        sp = &style;
    }
    if (which == B_CBA50 && a[6]) { /* 001CBA50: six arguments, then the style */
        style.color = (int32_t)a[7];
        style.glyph = (uint8_t)a[8];
        style.flag = (uint8_t)a[9];
        style.pad[1] = (uint8_t)a[10];
        sp = &style;
    }
    int rc;
    switch (which) {
    case B_7D00: rc = em_uip_00207D00(&u, (int32_t)a[0], (int32_t)a[1]); break;
    case B_7D90:
        rc = em_uip_00207D90(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4]);
        break;
    case B_7E40:
        rc = em_uip_00207E40(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                             (uint32_t)a[5], (uint64_t)a[6]);
        break;
    case B_7F80:
        rc = em_uip_00207F80(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                             (uint32_t)a[5]);
        break;
    case B_8040: {
        const uint32_t va[2] = {(uint32_t)a[1], (uint32_t)a[2]}, vb[2] = {(uint32_t)a[3], (uint32_t)a[4]},
                       vc[2] = {(uint32_t)a[5], (uint32_t)a[6]};
        rc = em_uip_00208040(&u, (int32_t)a[0], va, vb, vc, (uint32_t)a[7]);
        break;
    }
    case B_CC8A0: rc = em_uip_001CC8A0(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3]); break;
    case B_CBC20:
        rc = em_uip_001CBC20(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                             (int32_t)a[5], (int32_t)a[6], sp);
        break;
    case B_CC3B0:
        rc = em_uip_001CC3B0(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4],
                             (int32_t)a[5], (int32_t)a[6], sp);
        break;
    case B_CBA50: {
        /* the text: the region holding a[5], to that region's end */
        const uint32_t at = (uint32_t)a[5];
        const uint8_t *text = NULL;
        uint32_t avail = 0;
        for (uint32_t i = 0; i < count; i++)
            if (at >= address[i] && at - address[i] < size[i]) {
                text = bytes[i] + (at - address[i]);
                avail = size[i] - (at - address[i]);
            }
        rc = em_uip_001CBA50(&u, (int32_t)a[0], (int32_t)a[1], (int32_t)a[2], (int32_t)a[3], (int32_t)a[4], text,
                             avail, sp);
        break;
    }
    default: return -2;
    }
    if (fault && fault_cap) snprintf(fault, fault_cap, "%s", u.fault ? u.fault : "");
    return rc;
}

/* The latch: a call that faults (no cursor word in memory), then a valid
 * one on the same EmUip with the cursor present. 1 when the second call
 * refused (-1) with the first fault kept; the area is the caller's to check
 * for writes. */
int uipb_latch(uint8_t *area, uint32_t area_address, uint32_t context, uint8_t *cell)
{
    uint32_t address[2] = {area_address, context + 0x14u}, size[2] = {0x400u, 4u};
    uint8_t *bytes[2] = {area, cell};
    Regions r = {address, size, bytes, 1, NULL};
    EmUipMemory mem = {&r, region_bytes, region_to_int};
    EmUip u;
    if (!em_uip_init(&u, &mem, context, 0x814220u)) return -2;
    if (em_uip_00207D00(&u, 1, 0) != -1 || !u.fault) return 0;
    const char *first = u.fault;
    r.count = 2;
    return em_uip_00207D00(&u, 1, 0) == -1 && u.fault == first;
}

/* The fonts rebuilt from a font export (em_ui_font_raw) and D_0026E350's
 * formula: 0, or -1 with the reason in `why`. */
int uipb_fonts(const char *path, uint8_t *tall, uint8_t *small, uint16_t *nibbles, char *why, uint32_t why_cap)
{
    static EmUfrFonts f;
    const char *reason = NULL;
    const int rc = em_ufr_load(path, &f, &reason);
    if (why && why_cap) snprintf(why, why_cap, "%s", reason ? reason : "");
    if (rc == 0) {
        memcpy(tall, f.tall, sizeof f.tall);
        memcpy(small, f.small, sizeof f.small);
    }
    em_ufr_nibbles(nibbles);
    return rc;
}

/* 001D66A0(slot, a1, a2, a3, f12) with the four worker results `trig` (the
 * caller's: the original instructions' hooks return the same bits). */
int uipb_fan(const uint32_t *address, const uint32_t *size, uint8_t *const *bytes, uint32_t count,
             uint32_t context, UipbToInt to_int, int32_t slot, const uint32_t *a1, const uint32_t *a2, const uint32_t *a3,
             const uint32_t *trig, char *fault, uint32_t fault_cap)
{
    Regions r = {address, size, bytes, count, to_int};
    EmUipMemory mem = {&r, region_bytes, region_to_int};
    EmUip u;
    if (!em_uip_init(&u, &mem, context, 0x814220u)) return -2;
    const int rc = em_uip_001D66A0(&u, slot, a1, a2, a3, trig);
    if (fault && fault_cap) snprintf(fault, fault_cap, "%s", u.fault ? u.fault : "");
    return rc;
}

/* The same with the live workers' results (em_item_sdk_sine / cosine, the
 * translated 0011E2A8 / 0011DE90) for the angle f12 and for pi / 32. */
int uipb_fan_sdk(const uint32_t *address, const uint32_t *size, uint8_t *const *bytes, uint32_t count,
                 uint32_t context, UipbToInt to_int, int32_t slot, const uint32_t *a1, const uint32_t *a2,
                 const uint32_t *a3, uint32_t angle_bits, char *fault, uint32_t fault_cap)
{
    float angle, step = 0.09817477f, v[4];
    memcpy(&angle, &angle_bits, 4);
    v[0] = em_item_sdk_sine(angle);
    v[1] = em_item_sdk_cosine(angle);
    v[2] = em_item_sdk_sine(step);
    v[3] = em_item_sdk_cosine(step);
    uint32_t trig[4];
    memcpy(trig, v, sizeof trig);
    return uipb_fan(address, size, bytes, count, context, to_int, slot, a1, a2, a3, trig, fault, fault_cap);
}
