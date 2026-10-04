/* em_weather_packets.h - the weather's channel-3 list: 001E67C0's per-tile
 * draw requests 001CFAE0 / 001CFFE0 and 001E55F0's close of the list
 * (docs/SNOW_PARTICLES.md "The channel-3 list", docs/CHAIN_PAGE.md).
 *
 * Every snow tile 001E67C0 emits (em_snow.c) becomes one draw request:
 *
 *   001CFAE0(state, 0, 0x700036A0, f12 phase, f13 seed, f14 1.0, f15 1e-6)
 *            (byte-matched C) fills the 0x58-byte draw state: +0x00..+0x3F
 *            the tile matrix, +0x40 = 001CD370(a1) = D_00275670 + (a1 << 6)
 *            + 0x2240 (the clip projection's address), +0x44 = f12,
 *            +0x48 = f14, +0x4C = f13, +0x50 = f15, +0x54 = 0.
 *   001CFFE0(3, 3, D_00255170, state) (NEARMISS C; the .s followed) writes
 *            five DMA packets at the channel cursor *(D_00275670 + 0x10 +
 *            4 * slot): a REF of the blend preset 001CB9B0(kind) (8 qwords;
 *            kind = the descriptor's +0x8C word), a CALL of the program
 *            packet the (kind, variant) pair selects (kind 2, variant 3:
 *            D_00233800, the snow program), and three CNTs whose data are
 *            VIF codes and rows: STCYCL 1/1 and 15 rows to VU1 0x6E (P from
 *            0x70003A40, the clip projection at state +0x40, K from
 *            0x70003AC0, the context's +0xA0 fog, (0, 0, state +0x54, 0) and
 *            the D_00251260 row the pair selects), the descriptor's 9 rows
 *            to 0x50, and (+0x44, +0x48, +0x50, +0x4C) with the matrix to
 *            0x59 followed by MSCAL 0 and FLUSH. Each tag writes only its
 *            ID byte (+3), address word (+4) and QWC halfword (+0); its
 *            other bytes keep what the arena held.
 *   001E55F0 (the weather actor, state 1, after its 001E67C0) closes the
 *            list with a RET tag at the cursor (+3 = 0x60, +4 = 0, +0 = 0)
 *            and, when the cursor it read before the tiles was not 0,
 *            001D2DE0(0, that cursor): context +0x2520, which the frame
 *            close's 001E0D70 CALLs into the chain page (slot 0xFFB).
 *
 * The routines address original memory through the caller's EmWeatherMem
 * (live: the render context's storage, em_rcl_bytes / em_rcl_bytes_mut).
 * Pure C, no host float arithmetic (the float arguments are bit patterns).
 * Every entry returns 0, or -1 with *fault = the original address of the
 * datum it could not reach (or of the routine that has no defined result:
 * 001CFFE0 with a kind outside 1..4 or a variant outside 0..6 uses registers
 * the original never set). */
#ifndef EM_WEATHER_PACKETS_H
#define EM_WEATHER_PACKETS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_WEATHER_PACKETS_TILE_BYTES 0x260u   /* one 001CFFE0 request */

typedef struct {
    void *ctx;
    /* `size` bytes of original memory at `address`, or NULL. */
    const uint8_t *(*read)(void *ctx, uint32_t address, uint32_t size);
    /* The same, writable; NULL where the caller's storage is read-only. */
    uint8_t *(*write)(void *ctx, uint32_t address, uint32_t size);
    uint32_t d275670;      /* the render context D_00275670 points at */
    uint32_t d275674;      /* the GS block base D_00275674 points at  */
} EmWeatherMem;

/* The 0x58-byte draw state 001CFAE0 fills (001E67C0's sp block). */
typedef struct {
    uint8_t q[64];                 /* +0x00: the source's four quadwords */
    uint32_t m40;                  /* +0x40: 001CD370(a1)                */
    uint32_t w44, w48, w4C, w50;   /* +0x44..+0x50, bit patterns          */
    uint32_t w54;                  /* +0x54: 0                           */
} EmWeatherDrawState;

/* 001CFAE0(dst, a1, src, f12, f13, f14, f15). */
void em_weather_packets_001CFAE0(EmWeatherDrawState *dst, int32_t a1, const uint8_t src[64],
                                 uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15,
                                 uint32_t d275670);
/* Original-address variant for shared callers. The authoritative provider
 * sees each original load/store in order, including same-value stores.
 * No destination bytes are read; a refused access retains earlier stores. */
typedef uint8_t *(*EmWeatherDrawView)(void *, uint32_t, uint32_t, int);
int em_weather_packets_001CFAE0_view(void *ctx, EmWeatherDrawView view,
                                    uint32_t dst, int32_t index, uint32_t src,
                                    const uint32_t f[4], uint32_t *fault);

/* 001CFFE0(slot, variant, obj, src): `obj` is the 0x90 bytes of the
 * descriptor (D_00255170 as 001E67C0 left it). 0, or -1. */
int em_weather_packets_001CFFE0(const EmWeatherMem *m, int32_t slot, uint32_t variant,
                                const uint8_t obj[0x90], const EmWeatherDrawState *src,
                                uint32_t *fault);

/* One tile of 001E67C0's loop (em_snow.c EmSnowTile's fields as bytes:
 * the descriptor D_00255170 as the loop left it, VU59 (phase, 1.0,
 * 0.000001, seed), the tile matrix 0x700036A0): 001CFAE0(state, 0,
 * matrix, f12 = phase, f13 = seed, f14 = 1.0, f15 = 0.000001), then
 * 001CFFE0(3, 3, descriptor, state). 0, or -1. */
int em_weather_packets_tile(const EmWeatherMem *m, const uint8_t descriptor[0x90], const uint32_t vu59[4],
                            const uint8_t matrix[64], uint32_t *fault);

/* 001E55F0's tail after its 001E67C0: the RET tag at channel 3's cursor;
 * `start` is the cursor it read before the tiles. *pending = the value
 * handed to 001D2DE0(0, ...) (0: none). 0, or -1. */
int em_weather_packets_close(const EmWeatherMem *m, uint32_t start, uint32_t *pending, uint32_t *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_WEATHER_PACKETS_H */
