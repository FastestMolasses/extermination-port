/* Area-load veil particles: the two routines 0021B550 calls on every veil
 * tick while the 001ADF50 load veil is up (states 1 and 2 sub 0), and the
 * draw-packet builders they reach. Docs: docs/LOAD_VEIL_PARTICLES.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112,
 * Extermination/src plus the splat .s where the C is not byte-matched):
 *   0021B1B0  the veil draw (asm-word; translated from its instructions):
 *             veil +0x14 = 0x07234567, a 512-segment line strip across the
 *             screen (one 001D63B0 packet per segment, the Y offset a
 *             value noise over the +0x14 LCG, the colour a sweep at the
 *             +0x04 phase scaled by +0x08), then two 001DFA40 lens passes
 *             behind 001D1F80(0, 0, 7) and 001D1F80(0, 0, 2)
 *   0021B500  +0x04 phase step: += 0.007, wrap at 1.0 (byte-matched C)
 *   001D1F80, 001D1FF0, 001D2040, 001D1F20  REF DMA tags into the static
 *             GS blocks at *D_00275674 (byte-matched C)
 *   001D63B0  line packet (NEARMISS C; the .s was followed)
 *   001DFA40  lens pass (NEARMISS C; the .s was followed): 001D6B60 frame
 *             copy, 001D6BA0 TEX0, 001D1FF0(3), 001D2040(0), 001D7080 RGBAQ,
 *             a 16x16 distortion table, 15 textured strips, 001D1F20,
 *             001D1FF0(1)
 *   001D6B60  001D6930 then 001D1F20 (byte-matched C)
 *   001D6930  frame-copy packet (NEARMISS C; the .s was followed: it passes
 *             its a0..a3 on to 001D6E60, which the C drops)
 *   001D6E60  draw-environment packet (NEARMISS C; .s followed)
 *   001006D8  SDK draw-environment fill (word asm; decoded from the .s),
 *             including its read-modify-write of three packet dwords
 *   00100610  SDK Z-buffer size (byte-matched ee-gcc C), 00100268 (returns
 *             &D_00241010)
 *   001D6BA0  TEX0 packet (NEARMISS C; .s followed)
 *   001D7080  RGBAQ packet (word asm)
 *   00102948  quadword copy (inlined as a 16-byte copy)
 *
 * Workers (reused translations, bound by the caller): 0011DF78 fabsf and
 * 001281C0 float_to_int.
 *
 * Verified by tools/test_load_veil_particles_reference.py, which executes the
 * original instructions over captured AREA11 RAM and compares every packet
 * byte (written, read-modify-written and untouched), every cursor, the veil
 * block, the 001DFA40 table and every worker call; tests/
 * load_veil_particles_test.c pins the fail-stop contract.
 *
 * Arithmetic: every EE COP1 instruction goes through game/em_ee_float.h
 * (docs/EE_FLOAT_MODEL.md section 6) on raw binary32 bits; the module
 * performs no host float operation. There is no VU0 code in these routines.
 *
 * Packet memory: the original writes its packets at the channel cursor
 * *(D_00275670 + 0x10 + 4 * chan) and advances it. The port gives the
 * module a byte window standing for original addresses [packet_address,
 * packet_address + packet_size); cursors are original addresses. Bytes the
 * original leaves unwritten (DMA tag byte +2 and +8..+0xF, 001D6930 +0x88..
 * +0x8F and +0xA8..+0xAF) keep their value, and the three dwords 001006D8
 * reads back (+0x40/+0x50/+0x60 of its block) are modified in place, exactly
 * as the original does. A cursor that is not qword aligned, or a packet that
 * would not fit in the window, faults before anything is written.
 *
 * Only original bytes these routines read or write are modelled; each view
 * names its original address. A reached NULL view or worker, a negative
 * worker result, a channel index outside the view, or a packet outside the
 * window latches a fault (fail-stop); every later call then returns -1.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_LOAD_VEIL_PARTICLES_H
#define EM_LOAD_VEIL_PARTICLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Packet bytes one call emits (all fixed by the original code). */
#define EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES 0x162B0u /* 3 REF + 512 lines + 2 x 001DFA40 */
#define EM_LOAD_VEIL_PARTICLES_001DFA40_BYTES 0x4140u
/* 001DFA40's 16 x 16 quadword table (its stack at sp+0xD0). */
#define EM_LOAD_VEIL_PARTICLES_TABLE_BYTES 0x1000u

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h). */
enum {
    EM_LVP_FAULT_NONE = 0,
    EM_LVP_FAULT_NULL_WORKER = 1,   /* a reached worker or data view is NULL */
    EM_LVP_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_LVP_FAULT_BAD_INDEX = 4      /* channel index, cursor alignment or window overrun */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_LVP_FAULT_* */
} EmLoadVeilParticlesFault;

/* The veil block *D_00275888 bytes these routines read or write. Views,
 * not copies: the module reads through them every time the original loads
 * the field (0021B1B0 reloads +0x04/+0x08/+0x14/+0x18 every segment). */
typedef struct {
    uint32_t *phase;        /* +0x04 (float bits): 0021B1B0 reads, 0021B500 advances */
    const float *level0;    /* +0x08: 0021B1B0 reads (the line brightness) */
    uint32_t *seed;         /* +0x14: 0021B1B0 stores 0x07234567, then the LCG */
    const uint32_t *base_y; /* +0x18: the line's GS Y (0x8000 from 0021B180) */
} EmLoadVeilParticlesBlock;

/* Canonical storage views. Each pointer is required only when a routine
 * reaches it; the module keeps no copies. */
typedef struct {
    uint32_t *cursor;            /* D_00275670 + 0x10 + 4 * chan: channel cursors */
    uint32_t cursor_count;       /* native bound of `cursor` */
    const uint32_t *ctx_9C;      /* *(D_00275670 + 0x9C): 001D1F20 block index, 001D6930 test */
    const uint32_t *d00275674;   /* static GS block base the REF tags point into */
    const uint32_t *d0027568C;   /* the capture texture's GS memory address word */
    const uint8_t *d0026E880;    /* 16 bytes: the 001D6930 source quadword */
    const uint8_t *d00241010;    /* first 8 bytes of the SDK GS parameter block (00100610) */
    uint8_t *packet;             /* the packet memory window */
    uint32_t packet_address;     /* original address of packet[0] */
    uint32_t packet_size;
    /* EM_LOAD_VEIL_PARTICLES_TABLE_BYTES: 001DFA40's table (its stack frame
     * at sp+0xD0). Lanes 0..2 of each 16-byte entry are written every call;
     * lane 3 never is, yet the strips copy whole entries, so the original
     * ships the stale stack word there (the GS ignores that word of a PACKED
     * ST). The module leaves lane 3 as the buffer holds it. */
    uint8_t *table;
} EmLoadVeilParticlesWorld;

/* Workers. Each returns >= 0 on success; a negative result faults. Float
 * values cross as raw binary32 bits. */
typedef struct {
    void *ctx;
    int (*w_0011DF78)(void *ctx, uint32_t x, uint32_t *result);  /* fabsf */
    int (*w_001281C0)(void *ctx, uint32_t x, int32_t *result);   /* float_to_int */
} EmLoadVeilParticlesWorkers;

typedef struct {
    EmLoadVeilParticlesWorld world;
    EmLoadVeilParticlesWorkers workers;
    EmLoadVeilParticlesFault fault; /* latched; cleared only by the caller */
} EmLoadVeilParticles;

/* ---- The two veil routines. 0 on success, -1 on a fault. ---- */

/* 0021B1B0(veil): every view, worker and the whole packet run
 * (EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES at channel 0) are checked before
 * anything is written. */
int em_load_veil_particles_0021B1B0(EmLoadVeilParticles *s, const EmLoadVeilParticlesBlock *veil);
/* 0021B500(veil): -1 when veil or veil->phase is NULL (nothing written). */
int em_load_veil_particles_0021B500(const EmLoadVeilParticlesBlock *veil);

/* ---- The packet builders. Register arguments are the original's (a0 is
 * the channel). *result (may be NULL) receives the original return value;
 * addresses are original addresses. 0 on success, -1 on a fault. ---- */
int em_load_veil_particles_001D1F80(EmLoadVeilParticles *s, int32_t chan, int32_t a1, int32_t a2);
int em_load_veil_particles_001D1FF0(EmLoadVeilParticles *s, int32_t chan, int32_t a1);
int em_load_veil_particles_001D2040(EmLoadVeilParticles *s, int32_t chan, int32_t a1);
int em_load_veil_particles_001D1F20(EmLoadVeilParticles *s, int32_t chan);
/* 001D63B0(chan, a1, a2, a3, t0): a1/a3 read 3 words, a2/t0 read 4. */
int em_load_veil_particles_001D63B0(EmLoadVeilParticles *s, int32_t chan, const uint32_t a1[3],
                                    const uint32_t a2[4], const uint32_t a3[3], const uint32_t t0[4],
                                    uint32_t *result);
/* 001D7080(chan, a1, f12): the low word of a1 and the f12 bits. */
int em_load_veil_particles_001D7080(EmLoadVeilParticles *s, int32_t chan, uint32_t a1, uint32_t f12);
int em_load_veil_particles_001D6BA0(EmLoadVeilParticles *s, int32_t chan, int32_t a1, int32_t a2,
                                    int32_t a3, int32_t t0, int32_t t1, uint32_t *result);
/* 00100610(psm, w, h): reads the D_00241010 dword. */
int em_load_veil_particles_00100610(EmLoadVeilParticles *s, int32_t psm, int32_t w, int32_t h,
                                    int32_t *result);
/* 001006D8(env, psm, w, h, t0, t1): fills 0x80 bytes at `env` (an original
 * address inside the packet window, 8-aligned); returns 8. */
int em_load_veil_particles_001006D8(EmLoadVeilParticles *s, uint32_t env, int32_t psm, int32_t w,
                                    int32_t h, int32_t t0, int32_t t1, int32_t *result);
int em_load_veil_particles_001D6E60(EmLoadVeilParticles *s, int32_t chan, int32_t a1, int32_t a2,
                                    int32_t a3, uint32_t *result);
/* 001D6930 / 001D6B60(chan, a1, a2, a3, t0 = src): `src` is the quadword the
 * original loads from its t0 (001DFA40 passes &D_0026E880). */
int em_load_veil_particles_001D6930(EmLoadVeilParticles *s, int32_t chan, int32_t a1, int32_t a2,
                                    int32_t a3, const uint8_t src[16], uint32_t *result);
int em_load_veil_particles_001D6B60(EmLoadVeilParticles *s, int32_t chan, int32_t a1, int32_t a2,
                                    int32_t a3, const uint8_t src[16], uint32_t *result);
/* 001DFA40(chan, a1, a2, f12): only the low words of a1/a2 reach the packets. */
int em_load_veil_particles_001DFA40(EmLoadVeilParticles *s, int32_t chan, uint32_t a1, uint32_t a2,
                                    uint32_t f12, uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_LOAD_VEIL_PARTICLES_H */
