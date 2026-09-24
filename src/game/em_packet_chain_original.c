/* em_packet_chain_original.c - see em_packet_chain_original.h and
 * docs/PACKET_CHAIN.md. Each routine is translated from the original code
 * (the decomp C where it is byte-matched, the .s for 001CB9B0's default
 * and for the NEARMISS 0021B9A0). */
#include "game/em_packet_chain_original.h"

#include "game/em_ee_float.h"
#include "game/em_status_ui_leftovers.h"

#include <string.h>

typedef uint32_t u32;

#define LOW28 UINT32_C(0x0FFFFFFF)

/* ---------------------------------------------------------------- memory */

void em_packet_chain_init(EmPacketChain *pc, const EmPacketChainRegion *regions,
                          unsigned region_count, uint32_t d275670, uint32_t d275674)
{
    if (!pc) return;
    memset(pc, 0, sizeof *pc);
    pc->regions = regions;
    pc->region_count = regions ? region_count : 0;
    pc->d275670 = d275670;
    pc->d275674 = d275674;
}

void em_packet_chain_clear_fault(EmPacketChain *pc)
{
    if (!pc) return;
    pc->fault = EM_PACKET_CHAIN_FAULT_NONE;
    pc->fault_function = 0;
    pc->fault_address = 0;
}

static uint8_t *span(const EmPacketChain *pc, uint64_t address, uint64_t size)
{
    for (unsigned i = 0; i < pc->region_count; ++i) {
        const EmPacketChainRegion *r = &pc->regions[i];
        if (!r->bytes) continue;
        if (address >= r->base && address + size <= (uint64_t)r->base + r->size)
            return r->bytes + (size_t)(address - r->base);
    }
    return NULL;
}

uint8_t *em_packet_chain_at(const EmPacketChain *pc, uint32_t address, uint32_t size)
{
    return pc ? span(pc, address, size) : NULL;
}

static int fault(EmPacketChain *pc, int32_t code, u32 function, u32 address)
{
    pc->fault = code;
    pc->fault_function = function;
    pc->fault_address = address;
    return -1;
}

static u32 rd32(const uint8_t *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static void wr32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------ the builders */

/* The id clamp shared by 001CB5F0 / 001CB6B0 / 001CB760: 0xFFF000 passes;
 * anything else is floored at 0 and capped at 0xFFB000 (the cap compares
 * unsigned). The slot index is then id >> 12 (arithmetic; the value is
 * never negative here). */
static int32_t slot_index(int32_t id)
{
    if (id != EM_PACKET_CHAIN_KEY_PASS) {
        if (id < 0) id = 0;
        if ((u32)id > (u32)EM_PACKET_CHAIN_KEY_MAX) id = EM_PACKET_CHAIN_KEY_MAX;
    }
    return id >> 12;
}

/* One append, in the original order of memory operations:
 *   base = cursor; block = base + 0x100;
 *   block+0x00 = w0; block+0x04 = w4; block+0x10 = w10;
 *   prev = *slot; prev != 0 ? block+0x14 = prev & 0x0FFFFFFF
 *                           : *(slot + 0x4000) = block;
 *   *slot = block; cursor = cursor (re-read) + advance.
 * w4_self: the +0x04 word is the block's own +0x10 address (001CB5F0);
 * otherwise it is w4 (the address argument's low word) masked to 28 bits
 * here, the only place the mask is applied.
 * payload: bytes 001CB5F0 hands out after the block (0 for the others).
 * Every address is checked before the first write. */
static int append(EmPacketChain *pc, u32 function, u32 table, int32_t id, u32 w0, u32 w4,
                  int w4_self, u32 w10, u32 advance, uint64_t payload, u32 *block_out,
                  uint8_t **payload_out)
{
    if (pc->fault) return -1;
    const u32 ctx = pc->d275670;
    uint8_t *cursor = span(pc, (uint64_t)ctx + EM_PACKET_CHAIN_CURSOR, 4);
    if (!cursor)
        return fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, ctx + EM_PACKET_CHAIN_CURSOR);
    const u32 base = rd32(cursor);
    const u32 block = base + EM_PACKET_CHAIN_BLOCK_OFFSET;
    const u32 slot_address = table + (u32)slot_index(id) * 4u;
    uint8_t *b = span(pc, block, 0x18);
    if (!b) return fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, block);
    uint8_t *slot = span(pc, slot_address, 4);
    if (!slot) return fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, slot_address);
    uint8_t *head = span(pc, (uint64_t)slot_address + EM_PACKET_CHAIN_HEADS, 4);
    if (!head)
        return fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, slot_address + EM_PACKET_CHAIN_HEADS);
    uint8_t *packet = NULL;
    if (payload_out) {
        packet = span(pc, (uint64_t)block + 0x20u, payload);
        if (!packet) return fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, block + 0x20u);
    }

    wr32(b + 0x00, w0);
    wr32(b + 0x04, w4_self ? ((block + 0x10u) & LOW28) : (w4 & LOW28));
    wr32(b + 0x10, w10);
    const u32 prev = rd32(slot);
    if (prev != 0)
        wr32(b + 0x14, prev & LOW28);
    else
        wr32(head, block);
    wr32(slot, block);
    wr32(cursor, rd32(cursor) + advance);

    if (block_out) *block_out = block;
    if (payload_out) *payload_out = packet;
    return 0;
}

int em_packet_chain_001CB5F0(EmPacketChain *pc, uint32_t table, int32_t id, int32_t count,
                             uint32_t *packet, uint8_t **bytes)
{
    if (!pc) return -1;
    if (pc->fault) return -1;
    if (!packet) return fault(pc, EM_PACKET_CHAIN_FAULT_NULL, 0x001CB5F0u, 0);
    /* +0x10 = count | 0x20000000; the cursor advances (count + 2) * 16. */
    const u32 w10 = (u32)count | UINT32_C(0x20000000);
    const u32 advance = ((u32)count + 2u) * 0x10u;
    const uint64_t payload = count > 0 ? (uint64_t)(u32)count * 16u : 0u;
    u32 block = 0;
    uint8_t *p = NULL;
    if (append(pc, 0x001CB5F0u, table, id, UINT32_C(0x20000000), 0, 1, w10, advance, payload,
               &block, &p) < 0)
        return -1;
    *packet = block + 0x20u;
    if (bytes) *bytes = p;
    return 0;
}

int em_packet_chain_001CB6B0(EmPacketChain *pc, uint32_t table, int32_t id, int32_t kind,
                             uint64_t address)
{
    if (!pc) return -1;
    return append(pc, 0x001CB6B0u, table, id, (u32)kind | UINT32_C(0x30000000),
                  (u32)address, 0, UINT32_C(0x20000000), 0x20u, 0, NULL, NULL);
}

int em_packet_chain_001CB760(EmPacketChain *pc, uint32_t table, int32_t id, uint64_t address)
{
    if (!pc) return -1;
    return append(pc, 0x001CB760u, table, id, UINT32_C(0x50000000), (u32)address, 0,
                  UINT32_C(0x20000000), 0x20u, 0, NULL, NULL);
}

int32_t em_packet_chain_001CB9B0(uint32_t d275674, int32_t mode)
{
    /* Tested in the order 4, 3, 2, 1, 0; any other mode returns 0 (the
     * zeroing of the result register sits in the fall-through branch's
     * delay slot; the decomp C leaves this path's value unset). */
    switch (mode) {
    case 4: return (int32_t)(d275674 + 0x8A0u);
    case 3: return (int32_t)(d275674 + 0x820u);
    case 2: return (int32_t)(d275674 + 0x7A0u);
    case 1: return (int32_t)(d275674 + 0x720u);
    case 0: return (int32_t)(d275674 + 0x6A0u);
    default: return 0;
    }
}

int em_packet_chain_001CB900(EmPacketChain *pc, uint32_t table, int32_t id, int32_t mode)
{
    if (!pc) return -1;
    if (pc->fault) return -1;
    /* The 32-bit result register is passed on sign-extended; 001CB6B0
     * keeps its low 28 bits. */
    const int32_t state = em_packet_chain_001CB9B0(pc->d275674, mode);
    return em_packet_chain_001CB6B0(pc, table, id, 8, (uint64_t)(int64_t)state);
}

/* -------------------------------------------------------------------- fog */

#define F_255   UINT32_C(0x437F0000) /* 255.0 */
#define F_2048  UINT32_C(0x45000000) /* 2048.0 */

static uint8_t *context(EmPacketChain *pc, u32 function)
{
    uint8_t *c = span(pc, pc->d275670, EM_PACKET_CHAIN_CONTEXT_SPAN);
    if (!c) fault(pc, EM_PACKET_CHAIN_FAULT_UNMAPPED, function, pc->d275670);
    return c;
}

/* 0021B920 body on mapped context bytes: k = 255 / (far - near) (SUB.S
 * then DIV.S), +0xA0 = 255.0, +0xA4 = 2048.0, +0xA8 = far * k (MUL.S),
 * +0xAC = -k (NEG.S), stored in that order. */
static void fog_coefficients(uint8_t *c, u32 near_bits, u32 far_bits)
{
    const u32 span_bits = em_ee_sub_bits(far_bits, near_bits);
    const u32 k = em_ee_div_bits(F_255, span_bits);
    wr32(c + 0xA0, F_255);
    wr32(c + 0xA4, F_2048);
    wr32(c + 0xA8, em_ee_mul_bits(far_bits, k));
    wr32(c + 0xAC, em_ee_neg_bits(k));
}

int em_packet_chain_0021B920(EmPacketChain *pc, uint32_t near_bits, uint32_t far_bits)
{
    if (!pc || pc->fault) return -1;
    uint8_t *c = context(pc, 0x0021B920u);
    if (!c) return -1;
    fog_coefficients(c, near_bits, far_bits);
    return 0;
}

/* 00121870 block_copy(dst, src, 32) inside the context, as the C runtime
 * copy: 0021B900's source (+0xA0..+0xBF) and destination (+0xC0..+0xDF)
 * never overlap, so a forward copy is exactly the original's result. */
static int latch_copy(void *unused, uint8_t *block, uint32_t dst, uint32_t src, int32_t count)
{
    (void)unused;
    if (!block || count < 0) return -1;
    for (int32_t i = 0; i < count; ++i)
        block[dst + (u32)i] = block[src + (u32)i];
    return 0;
}

int em_packet_chain_0021B9A0(EmPacketChain *pc, int32_t mode, uint32_t scale_bits,
                             uint32_t bias_bits)
{
    if (!pc || pc->fault) return -1;
    uint8_t *c = context(pc, 0x0021B9A0u);
    if (!c) return -1;

    /* The six-entry jump table: 0 -> the +0xF8 pair, 1 -> the +0xD8 pair,
     * 2 and 4 -> near scaled, 3 and 5 -> far scaled. An unsigned mode of 6
     * or more (a negative mode too) takes the +0xF8 pair. */
    u32 near_bits, far_bits;
    switch (mode) {
    case 1:
        far_bits = rd32(c + 0xDC);
        near_bits = rd32(c + 0xD8);
        break;
    case 2:
    case 4:
        /* near = bias + near * scale (MUL.S, then ADD.S with bias first). */
        near_bits = em_ee_add_bits(bias_bits, em_ee_mul_bits(rd32(c + 0xB8), scale_bits));
        far_bits = rd32(c + 0xBC);
        break;
    case 3:
    case 5:
        near_bits = rd32(c + 0xB8);
        far_bits = em_ee_add_bits(bias_bits, em_ee_mul_bits(rd32(c + 0xBC), scale_bits));
        break;
    default: /* 0 and every other value */
        far_bits = rd32(c + 0xFC);
        near_bits = rd32(c + 0xF8);
        break;
    }
    wr32(c + 0xB8, near_bits);
    wr32(c + 0xBC, far_bits);
    /* 0021B920 receives the two values in registers (not re-read). */
    fog_coefficients(c, near_bits, far_bits);

    /* 0021B900 for modes 4, 5 (mode - 4 < 2 unsigned) and 0, 1 (mode < 2
     * unsigned). */
    if ((u32)mode - 4u < 2u || (u32)mode < 2u) {
        EmSulWorkers w;
        memset(&w, 0, sizeof w);
        w.block_copy = latch_copy;
        if (em_sul_0021B900(&w, c, EM_PACKET_CHAIN_CONTEXT_SPAN) < 0)
            return fault(pc, EM_PACKET_CHAIN_FAULT_WORKER, 0x0021B900u, pc->d275670);
    }
    return 0;
}

int em_packet_chain_fog(EmPacketChain *pc, uint32_t out[4])
{
    if (!pc || pc->fault) return -1;
    if (!out) return fault(pc, EM_PACKET_CHAIN_FAULT_NULL, 0x0021B9A0u, 0);
    uint8_t *c = context(pc, 0x0021B9A0u);
    if (!c) return -1;
    for (int i = 0; i < 4; ++i) out[i] = rd32(c + EM_PACKET_CHAIN_FOG + 4u * (u32)i);
    return 0;
}

/* -------------------------------------------------------- worker adapters */

int em_packet_chain_w_001CB5F0(void *ctx, uint32_t table, int32_t id, int32_t count,
                               uint8_t **bytes)
{
    EmPacketChain *pc = ctx;
    if (!pc) return -1;
    if (!bytes) return pc->fault ? -1 : fault(pc, EM_PACKET_CHAIN_FAULT_NULL, 0x001CB5F0u, 0);
    u32 packet = 0;
    return em_packet_chain_001CB5F0(pc, table, id, count, &packet, bytes);
}

int em_packet_chain_w_001CB6B0(void *ctx, uint32_t table, int32_t id, int32_t kind,
                               uint32_t address)
{
    return em_packet_chain_001CB6B0(ctx, table, id, kind, address);
}

int em_packet_chain_w_001CB760(void *ctx, uint32_t table, int32_t id, uint32_t address)
{
    return em_packet_chain_001CB760(ctx, table, id, address);
}

int em_packet_chain_w_001CB760_4(void *ctx, uint32_t table, int32_t id, uint32_t address,
                                 uint32_t unread)
{
    (void)unread;
    return em_packet_chain_001CB760(ctx, table, id, address);
}

int em_packet_chain_w_001CB900(void *ctx, uint32_t table, int32_t id, int32_t mode)
{
    return em_packet_chain_001CB900(ctx, table, id, mode);
}

int em_packet_chain_w_0021B9A0(void *ctx, int32_t mode, uint32_t f12, uint32_t f13)
{
    return em_packet_chain_0021B9A0(ctx, mode, f12, f13);
}

int em_packet_chain_w_0021B9A0_float(void *ctx, int32_t mode, float f12, float f13)
{
    return em_packet_chain_0021B9A0(ctx, mode, em_ee_bits(f12), em_ee_bits(f13));
}

int em_packet_chain_w_0021B9A0_heads(void *ctx, uint32_t f12, uint32_t f13, int32_t mode)
{
    return em_packet_chain_0021B9A0(ctx, mode, f12, f13);
}
