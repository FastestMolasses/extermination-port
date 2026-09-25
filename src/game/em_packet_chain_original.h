/* em_packet_chain_original.h - the GS packet-chain builders and the fog /
 * depth-range programmer (census lane "packet-chain"; docs/PACKET_CHAIN.md).
 * Prefix em_packet_chain_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112):
 *   001CB5F0  open a packet of `count` quadwords in a depth-bucket chain
 *             table (byte-matched C)
 *   001CB6B0  append a reference block: `kind` quadwords at `address`
 *             (byte-matched C)
 *   001CB760  append a call block to `address` (byte-matched C)
 *   001CB900  append the blend-state reference block of `mode`:
 *             001CB6B0(table, id, 8, 001CB9B0(mode)) (byte-matched C)
 *   001CB9B0  the blend-state block address of `mode` (0..4), else 0
 *             (followed from the .s: the C leaves the default unset)
 *   0021B9A0  the fog / depth-range programmer (NEARMISS C; followed
 *             from the .s and its jump table)
 *   0021B920  the fog coefficients at render context +0xA0 (EE COP1
 *             arithmetic through em_ee_float.h)
 * 0021B900 (the +0xA0 -> +0xC0 latch) is not translated again: it is
 * em_sul_0021B900 (em_status_ui_leftovers), called with a block_copy
 * worker that is the C runtime copy of 32 bytes.
 *
 * The chain table. A table (D_007635C0 on the route) is 0x1000 slot words
 * followed by 0x1000 head words (+0x4000). The id is a depth key: 0xFFF000
 * passes through, anything else is clamped to 0..0xFFB000; the slot is
 * id >> 12. Every block is 0x20 bytes placed 0x100 bytes past the render
 * context's +0x18 cursor: two DMA tags, the first at +0x00 (its kind), the
 * second at +0x10 (a "next" tag whose address word +0x14 is the block
 * previously appended to the same slot, low 28 bits). The slot word keeps
 * the newest block; the head word gets the first block of an empty slot.
 * The slot is therefore a list from newest to oldest, which 001CB800 (a
 * renderer boundary, not translated here) splices into the frame chain.
 *
 * Memory model. The routines address original EE memory: the render
 * context (D_00275670 points at it), the packet buffer its +0x18 cursor
 * walks, and the chain table. The caller supplies that memory as a list of
 * mutable regions addressed by original address; D_00275670 and D_00275674
 * are plain values (the routines never write them). Every access is
 * checked against the regions BEFORE the first write: an unmapped byte
 * faults (-1) with nothing written. 001CB5F0 also requires the packet it
 * hands out (count * 16 bytes) to be mapped, because its caller fills it.
 *
 * Fail-stop: a NULL chain, a NULL output pointer or an unmapped address
 * latches the fault; every later call returns -1 until
 * em_packet_chain_clear_fault.
 *
 * Oracle: tools/test_packet_chain_reference.py executes the original
 * instructions (COP1 through tools/ee_float_model.py) over the captured
 * route RAM. After every call it compares the render context and every
 * byte the original wrote; after each sequence, all of EE RAM. It runs the
 * consumer patterns through every worker adapter below, and replays the
 * frame chains captured in the route snapshots. */
#ifndef EM_PACKET_CHAIN_ORIGINAL_H
#define EM_PACKET_CHAIN_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- original addresses ------------------------------------------------ */
#define EM_PACKET_CHAIN_TABLE        0x007635C0u /* D_007635C0: the route's chain table */
#define EM_PACKET_CHAIN_TABLE_BYTES  0x8000u     /* 0x1000 slots + 0x1000 heads */
#define EM_PACKET_CHAIN_HEADS        0x4000u     /* head words follow the slot words */
#define EM_PACKET_CHAIN_KEY_PASS     0x00FFF000  /* id passed through unclamped */
#define EM_PACKET_CHAIN_KEY_MAX      0x00FFB000  /* clamp ceiling of every other id */
#define EM_PACKET_CHAIN_BLOCK_OFFSET 0x100u      /* a block sits at cursor + 0x100 */
#define EM_PACKET_CHAIN_CURSOR       0x18u       /* render context +0x18: packet cursor */
#define EM_PACKET_CHAIN_FOG          0xA0u       /* render context +0xA0: fog quadword */
#define EM_PACKET_CHAIN_FOG_LATCH    0xC0u       /* +0xC0: 0021B900's copy of +0xA0..+0xBF */
#define EM_PACKET_CHAIN_NEAR         0xB8u       /* current near / far pair */
#define EM_PACKET_CHAIN_PRESET1      0xD8u       /* mode 1 pair */
#define EM_PACKET_CHAIN_PRESET0      0xF8u       /* mode 0 / default pair */
#define EM_PACKET_CHAIN_CONTEXT_SPAN 0x100u      /* bytes of the context the fog code maps */

/* ---- faults ------------------------------------------------------------ */
#define EM_PACKET_CHAIN_FAULT_NONE     0
#define EM_PACKET_CHAIN_FAULT_NULL     1 /* NULL chain / output / worker */
#define EM_PACKET_CHAIN_FAULT_UNMAPPED 2 /* an address outside every region */
#define EM_PACKET_CHAIN_FAULT_WORKER   3 /* em_sul_0021B900 failed */

typedef struct EmPacketChainRegion {
    uint32_t base;   /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;  /* mutable */
} EmPacketChainRegion;

typedef struct EmPacketChain {
    const EmPacketChainRegion *regions;
    unsigned region_count;
    uint32_t d275670;        /* D_00275670: original address of the render context */
    uint32_t d275674;        /* D_00275674: base of 001CB9B0's blend-state blocks */
    int32_t fault;           /* EM_PACKET_CHAIN_FAULT_*, latched */
    uint32_t fault_function; /* original address of the faulting routine */
    uint32_t fault_address;  /* the unmapped original address, if any */
} EmPacketChain;

void em_packet_chain_init(EmPacketChain *pc, const EmPacketChainRegion *regions,
                          unsigned region_count, uint32_t d275670, uint32_t d275674);
void em_packet_chain_clear_fault(EmPacketChain *pc);

/* Host bytes of [address, address + size) when one region holds them all,
 * else NULL (no fault is latched). */
uint8_t *em_packet_chain_at(const EmPacketChain *pc, uint32_t address, uint32_t size);

/* ---- the builders ------------------------------------------------------ */

/* 001CB5F0(table, id, count). *packet = the original address of the
 * packet (block + 0x20); *bytes (optional) = its host bytes, count * 16 of
 * them writable. Returns 0, or -1 on a fault (nothing written). */
int em_packet_chain_001CB5F0(EmPacketChain *pc, uint32_t table, int32_t id, int32_t count,
                             uint32_t *packet, uint8_t **bytes);

/* 001CB6B0(table, id, kind, address): +0x00 = kind | 0x30000000, +0x04 =
 * address & 0x0FFFFFFF. The original takes a 64-bit address register. */
int em_packet_chain_001CB6B0(EmPacketChain *pc, uint32_t table, int32_t id, int32_t kind,
                             uint64_t address);

/* 001CB760(table, id, address): +0x00 = 0x50000000, +0x04 = address &
 * 0x0FFFFFFF. (Callers that pass a fourth register: it is not read.) */
int em_packet_chain_001CB760(EmPacketChain *pc, uint32_t table, int32_t id, uint64_t address);

/* 001CB9B0(mode): D_00275674 + 0x6A0 / 0x720 / 0x7A0 / 0x820 / 0x8A0 for
 * modes 0..4, else 0. Pure. */
int32_t em_packet_chain_001CB9B0(uint32_t d275674, int32_t mode);

/* 001CB900(table, id, mode) = 001CB6B0(table, id, 8, 001CB9B0(mode)). */
int em_packet_chain_001CB900(EmPacketChain *pc, uint32_t table, int32_t id, int32_t mode);

/* ---- fog --------------------------------------------------------------- */

/* 0021B920(near, far), raw float bits: context +0xA0 = (255.0, 2048.0,
 * far * k, -k) with k = 255.0 / (far - near), EE COP1 arithmetic. */
int em_packet_chain_0021B920(EmPacketChain *pc, uint32_t near_bits, uint32_t far_bits);

/* 0021B9A0(mode, scale, bias), raw float bits.
 *   1:      (near, far) = the +0xD8 pair
 *   2, 4:   near = bias + near * scale
 *   3, 5:   far  = bias + far * scale
 *   0, any other value (negative too): the +0xF8 pair
 * then +0xB8/+0xBC = (near, far), 0021B920(near, far), and for modes 0, 1,
 * 4 and 5 only, 0021B900 (+0xC0..+0xDF = +0xA0..+0xBF). */
int em_packet_chain_0021B9A0(EmPacketChain *pc, int32_t mode, uint32_t scale_bits,
                             uint32_t bias_bits);

/* The context +0xA0 quadword (raw bits), for callers that keep a copy of
 * the fog (em_effect_manager's view->fog) and must refresh it after
 * 0021B9A0. */
int em_packet_chain_fog(EmPacketChain *pc, uint32_t out[4]);

/* 0021B970(near, far), raw float bits (byte-matched C): context +0xB8 =
 * near, +0xBC = far, then 0021B920(near, far) on the register values and
 * 0021B900. */
int em_packet_chain_0021B970(EmPacketChain *pc, uint32_t near_bits, uint32_t far_bits);

/* 0021BA80(a0, a1, a2) (asm words): 0021BA70(a0 | a1 << 8 | a2 << 16),
 * each argument sign-extended to 64 bits before its shift. 0021BA70 (the
 * doubleword +0xB0, then 0021B900) is em_sul_0021BA70. */
int em_packet_chain_0021BA80(EmPacketChain *pc, int32_t a0, int32_t a1, int32_t a2);

/* 001D8FD0() (byte-matched C): the area fog. rec = 001D7B30() (an entry of
 * the room table D_00251C50, which must be mapped); when 001B0070() & 0x80:
 * 0021B970(0, 110) and 0021BA80(0, 0, 0); else 0021B970(rec +4, rec +8) and
 * 0021BA80(rec +0xC, rec +0x10, rec +0x14); then 0021B8E0 (em_sul_0021B8E0:
 * +0xE0..+0xFF = +0xA0..+0xBF). The two callees outside this module are
 * workers; a NULL or failing worker latches EM_PACKET_CHAIN_FAULT_WORKER. */
typedef struct {
    void *ctx;
    /* 001D7B30(): *record = the original address of the room entry. */
    int (*w_001D7B30)(void *ctx, uint32_t *record);
    /* 001B0070(): *flags = the area flag word. */
    int (*w_001B0070)(void *ctx, uint32_t *flags);
} EmPacketChainAreaFogWorkers;
int em_packet_chain_001D8FD0(EmPacketChain *pc, const EmPacketChainAreaFogWorkers *w);

/* ---- the frame chain start and splice ------------------------------------
 * Both read the buffer index D_00810E80 (a signed halfword, which must be
 * mapped) and address the arena D_0028F700: base = D_0028F700 + index *
 * 0x70000 + 0x1F3EC0 + (a1 << 6). */
#define EM_PACKET_CHAIN_ARENA 0x0028F700u  /* D_0028F700 */
#define EM_PACKET_CHAIN_D_00810E80 0x00810E80u

/* 001CB8A0(a0, a1, a2, a3) (byte-matched C; a0 is not read): base +0x00 =
 * 0x20000000, base +0x04 = (base + 0x20) & 0x0FFFFFFF, then the word at the
 * original address a2 = base & 0x0FFFFFFF and the word at a3 = (base +
 * 0x20) & 0x0FFFFFFF, in that order. */
int em_packet_chain_001CB8A0(EmPacketChain *pc, int32_t a1, uint32_t a2, uint32_t a3);

/* 001CB800(table, a1, a2, a3) (byte-matched C): base +0x00 = 0x20000000
 * and the cursor starts at base; for each of the 0x1000 slot words w of the
 * table, in order, when w != 0: cursor +0x04 = w & 0x0FFFFFFF, the cursor
 * becomes the slot's head word + 0x10, and the slot word is cleared (head
 * words are never cleared). Then cursor +0x04 = (base + 0x20) & 0x0FFFFFFF,
 * *a2 = base & 0x0FFFFFFF and *a3 = (base + 0x20) & 0x0FFFFFFF. Every
 * address the walk touches is checked before the first store. */
int em_packet_chain_001CB800(EmPacketChain *pc, uint32_t table, int32_t a1, uint32_t a2,
                             uint32_t a3);

/* ---- worker adapters ----------------------------------------------------
 * Drop-in worker functions for the consumers' worker tables; ctx must be
 * the EmPacketChain. Each returns 0 or -1 (the latched fault). Binding
 * notes per consumer: docs/PACKET_CHAIN.md section 5. */

/* em_head_sprite_original, em_effect_manager, em_player_equipment_sprite:
 * w_001CB5F0(ctx, table, id, count, &bytes) */
int em_packet_chain_w_001CB5F0(void *ctx, uint32_t table, int32_t id, int32_t count,
                               uint8_t **bytes);
/* em_head_sprite_original, em_player_equipment_sprite:
 * w_001CB6B0(ctx, table, id, kind, address) */
int em_packet_chain_w_001CB6B0(void *ctx, uint32_t table, int32_t id, int32_t kind,
                               uint32_t address);
/* em_effect_manager, em_anim_runtime_rest, em_render_context:
 * w_001CB760(ctx, table, id, address) */
int em_packet_chain_w_001CB760(void *ctx, uint32_t table, int32_t id, uint32_t address);
/* em_head_sprite_original: w_001CB760(ctx, table, id, address, unread) */
int em_packet_chain_w_001CB760_4(void *ctx, uint32_t table, int32_t id, uint32_t address,
                                 uint32_t unread);
/* em_head_sprite_original, em_effect_manager, em_player_equipment_sprite:
 * w_001CB900(ctx, table, id, mode) */
int em_packet_chain_w_001CB900(void *ctx, uint32_t table, int32_t id, int32_t mode);
/* em_effect_manager, em_effect_kinds: w_0021B9A0(ctx, mode, f12 bits, f13 bits) */
int em_packet_chain_w_0021B9A0(void *ctx, int32_t mode, uint32_t f12, uint32_t f13);
/* em_effect_original, em_area_script: w_0021B9A0(ctx, mode, f12, f13) as
 * host floats (bit-copied, never converted) */
int em_packet_chain_w_0021B9A0_float(void *ctx, int32_t mode, float f12, float f13);
/* em_frame_render_heads: w_0021B9A0(ctx, f12 bits, f13 bits, a0) */
int em_packet_chain_w_0021B9A0_heads(void *ctx, uint32_t f12, uint32_t f13, int32_t mode);

#ifdef __cplusplus
}
#endif

#endif /* EM_PACKET_CHAIN_ORIGINAL_H */
