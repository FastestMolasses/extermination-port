/* AREA07 sub 2's turret probe 0x825470, the AREA03 0x824D30 C with its own tint. */
#include "em_area07_probe_internal.h"
#define GIFTAG_828030 UINT64_C(0x20045BA5154222DC)
#define K_M2 0xC0000000u      /* -2.0f */
#define K_0_8 0x3F4CCCCDu     /* 0.8f */
#define K_10000 0x461C4000u   /* 10000.0f */
#define D_00275B40 0x00275B40u /* pointer: the running actor's bone block */
#define D_00810360 0x00810360u
#define S_700031B0 0x700031B0u
#define S_700031D0 0x700031D0u
#define S_700031D4 0x700031D4u
#define S_700031D8 0x700031D8u
#define S_700038A0 0x700038A0u
#define S_700038A4 0x700038A4u
#define S_700038A8 0x700038A8u
#define S_700038AC 0x700038ACu
#define S_700038B0 0x700038B0u
#define S_700038B4 0x700038B4u
#define S_700038B8 0x700038B8u
#define S_700038BC 0x700038BCu
#define S_700038C0 0x700038C0u
#define S_700038C4 0x700038C4u
#define S_700038C8 0x700038C8u
#define S_700038CC 0x700038CCu
#define S_700038D0 0x700038D0u
#define S_70003A20 0x70003A20u
#define F_ZERO 0x00000000u
#define F_ONE 0x3F800000u
#define F_2 0x40000000u
#define F_3 0x40400000u
#define F_60 0x42700000u
#define F_100 0x42C80000u
#define A07_TINT 0x00828030u

static int32_t probe(A07A *o, uint32_t self, uint32_t matrix, uint32_t sp)
{
    uint32_t dir = sp - 0x20, tint = sp - 0x10;
    int32_t r = 0;
    int hit;

    a07a_w32(o, S_700038A0, F_60);
    a07a_w32(o, dir + 0x0, F_3);
    a07a_w32(o, dir + 0x4, K_M2);
    a07a_w32(o, dir + 0x8, F_ZERO);
    a07a_w32(o, dir + 0xC, F_ONE);
    a07a_w32(o, S_700038A8, F_ZERO);
    a07a_w32(o, S_700038A4, F_ZERO);
    a07a_w32(o, S_700038AC, F_ZERO);
    a07a_c_001026A0(o, S_700038A0, matrix, S_700038A0);
    a07a_c_001026A0(o, dir, matrix, dir);
    a07a_c_001028B8(o, S_700038A0, S_700038A0, dir);
    a07a_w32(o, S_700038AC, F_ONE);
    a07a_c_0019AA80(o, dir, S_700038A0, 0x20, &r);
    if (r != 0) {
        a07a_c_00102948(o, S_700038A0, S_700031B0);
        hit = 2;
    } else {
        hit = 0;
    }
    uint32_t save_d8 = a07a_u32(o, S_700031D8);
    uint32_t save_d4 = a07a_u32(o, S_700031D4);
    uint32_t save_d0 = a07a_u32(o, S_700031D0);
    r = 0;
    a07a_c_0019A570(o, dir, S_700038A0, 7, 0x20, &r);
    if (r != 0) {
        uint32_t d = 0;
        a07a_c_001028D0(o, S_700038A0, S_700031B0, D_00810360);
        a07a_w32(o, S_700038AC, F_ZERO);
        a07a_c_00102738(o, S_700038A0, S_700038A0, &d);
        a07a_w32(o, S_70003A20, d);
        if (!A07A_LE(d, K_10000)) {
            hit = 1;
        } else if (a07a_u32(o, S_700031D8) != 1) {
            hit = 2;
        } else {
            uint32_t kind = a07a_u8(o, a07a_u32(o, S_700031D4) + 3);
            hit = (kind >= 0x10 && kind < 0x14) ? 1 : 2;
        }
    } else {
        a07a_w32(o, S_700031D8, save_d8);
        a07a_w32(o, S_700031D4, save_d4);
        a07a_w32(o, S_700031D0, save_d0);
    }

    if (hit == 2) {
        if (a07a_u32(o, self + 0x204) == 0 && a07a_u32(o, S_700031D8) == 1) {
            uint32_t other = a07a_u32(o, S_700031D4);
            a07a_w32(o, self + 0x204, other);
        }
        if (a07a_u8(o, self + 4) == 4) {
            a07a_c_001031E0(o, S_700038A0, S_700031B0);
            a07a_w32(o, S_700038AC, F_ONE);
            r = 0;
            a07a_c_00122BB8(o, &r);
            int32_t x = a07a_sra((uint32_t)r, 16);
            x = a07a_sra(((uint32_t)x << 16) - (uint32_t)x, 15);
            a07a_w32(o, S_700038B0, (uint32_t)((a07a_sra((uint32_t)x, 15) & 0x1F) + 0x40));
            a07a_w32(o, S_700038B4, 0);
            a07a_w32(o, S_700038B8, 0);
            a07a_w32(o, S_700038BC, 0x80);
            uint32_t bc = a07a_u32(o, S_700038BC);
            uint32_t b8 = a07a_u32(o, S_700038B8);
            uint32_t b4 = a07a_u32(o, S_700038B4);
            uint32_t b0 = a07a_u32(o, S_700038B0);
            uint32_t rgba = b0 | (b4 << 8 | (bc << 24 | b8 << 16));
            r = 0;
            a07a_c_001CD520(o, 0, 2, S_700038A0, GIFTAG_828030, rgba, F_3, F_3, F_2, &r);
            a07a_w32(o, S_700038C0, K_0_8);
            a07a_w32(o, S_700038C8, F_ZERO);
            a07a_w32(o, S_700038C4, F_ZERO);
            a07a_c_001E2BA0(o, dir, S_700038A0, S_700038C0, F_100);
        } else if (a07a_s32(o, self + 0x200) >= 0xD) {
            uint8_t q[16];
            a07a_q(o, A07_TINT, q);
            a07a_wq(o, tint, q);
            a07a_c_001031E0(o, S_700038A0, S_700031B0);
            uint32_t table = a07a_u32(o, D_00275B40);
            a07a_w32(o, S_700038AC, F_ONE);
            uint32_t record = a07a_u32(o, table + 0xC);
            a07a_c_001026A0(o, S_700038C0, record + 0x90, tint);
            a07a_w32(o, S_700038CC, F_ONE);
            a07a_w32(o, S_700038D0 + 8, K_0_8);
            a07a_w32(o, S_700038D0 + 4, K_0_8);
            a07a_w32(o, S_700038D0, K_0_8);
            a07a_c_001E2BA0(o, S_700038A0, S_700038C0, S_700038D0, F_100);
        }
    } else if (hit == 1) {
        a07a_c_001031E0(o, S_700038B0, S_700031B0);
        a07a_w32(o, S_700038BC, F_ONE);
        a07a_w32(o, S_700038C0, K_0_8);
        a07a_w32(o, S_700038C8, F_ZERO);
        a07a_w32(o, S_700038C4, F_ZERO);
        a07a_c_001E2BA0(o, dir, S_700038B0, S_700038C0, F_100);
    }
    if (hit != 2) return 0;
    uint32_t mine = a07a_u32(o, self + 0x204);
    uint32_t other = a07a_u32(o, S_700031D4);
    return mine == other ? 2 : 1;
}

int em_area07_probe_00825470(const EmArea07ProbeHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                             int32_t *result, EmArea07ProbeFault *fault)
{
    A07A o;
    if (!result || a07a_begin(&o, h, fault)) return -1;
    int32_t r = probe(&o, self, matrix, sp);
    if (a07a_failed(&o)) return -1;
    *result = r;
    return 0;
}
