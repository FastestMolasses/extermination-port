/* em_camera_commit_original.c - 0018C0D0, 00102798 and 00193660 (see
 * em_camera_commit_original.h, docs/CAMERA_LIVE.md section 2).
 *
 * Read from the original instructions (build/asm of the decomp); 0018C0D0
 * and 00193660 are byte-matched C in the decomp, 00102798 an asm-word file.
 * Every address in a comment is the original instruction translated there. */
#include "game/em_camera_commit_original.h"

#include "game/em_camera_leftovers_internal.h"

#include <stddef.h>
#include <string.h>

#define CC_0_001 UINT32_C(0x3A83126F)   /* 0.001 as its float bits */
#define CC_5_5   UINT32_C(0x40B00000)

static int cc_fault(EmCameraCommitWorld *w, uint32_t address)
{
    if (w && w->fault == 0) w->fault = address;
    return -1;
}

#define CC_CALL(w, address, call) do { if ((call) < 0) return cc_fault((w), (address)); } while (0)

void em_camera_commit_00102798(uint32_t out[16], const uint32_t m[16])
{
    /* Reads all 16 words, then stores the transpose: row r of out is
     * column r of m. Every word is read before any store, so out may be m. */
    uint32_t t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t[4 * r + c] = m[4 * c + r];
    memcpy(out, t, sizeof t);
}

int em_camera_commit_0018C0D0(EmCameraCommitWorld *w, int mode)
{
    if (!w) return -1;
    const EmCameraCommitWorkers *k = w->workers;
    if (!w->cam || !w->player || !w->eye || !w->target || !w->up || !w->fwd || !w->view ||
        !w->view_t || !w->d690 || !w->d694 || !w->d698 || !w->d69C || !w->d6A0 || !w->scratch || !k)
        return cc_fault(w, 0x0018C0D0);
    if (!k->sqrt) return cc_fault(w, 0x0011E748);
    if (!k->lookat) return cc_fault(w, 0x00102CD0);
    if (!k->atan2) return cc_fault(w, 0x0011E620);
    if (!k->heading) return cc_fault(w, 0x001B1240);

    EmCameraFollowRecord *c = w->cam;
    uint32_t *a0 = em_camleft_spad(w->scratch, 0x700038A0);          /* 0x700038A0..AC */
    uint32_t *b0 = em_camleft_spad(w->scratch, 0x700038B0);          /* 0x700038B0..BC */
    uint32_t *c0 = em_camleft_spad(w->scratch, 0x700038C0);          /* 0x700038C0..CC */

    w->target[3] = CL_ONE;                                            /* 0018C0E8: D_008105EC */
    w->eye[3] = CL_ONE;                                               /* 0018C0F0: D_008105DC */
    if (cl_v_sub(a0, w->target, w->eye) < 0) return cc_fault(w, 0x001028D0); /* 0018C110 */
    a0[3] = CL_ONE;                                                   /* 0018C120 */

    /* The actual pair's horizontal distance: sqrt(x*x + z*z), where the sum is
     * one multiply-add onto the rounded x*x (the square root's argument). */
    uint32_t d;
    CC_CALL(w, 0x0011E748, k->sqrt(k->context, cl_madd(cl_mul(a0[0], a0[0]), a0[2], a0[2]), &d)); /* 0018C138 */
    *w->d69C = d;                                                     /* 0018C15C */
    if (cl_lt(d, CC_0_001)) {                                         /* 0018C150 */
        *w->d69C = CC_0_001;                                          /* 0018C164 */
        a0[2] = CC_0_001;                                             /* 0018C16C */
        w->eye[2] = cl_add(w->eye[2], CC_0_001);                      /* 0018C178..80 */
    }

    *w->d698 = cl_sub(w->eye[1], cl_pw(w->player, 0xA4));            /* 0018C1A4 / 0018C1B0 */

    /* The desired pair's horizontal distance. */
    if (cl_v_sub(b0, c->bytes + 0x20, c->bytes + 0x10) < 0) return cc_fault(w, 0x001028D0); /* 0018C1AC */
    CC_CALL(w, 0x0011E748, k->sqrt(k->context, cl_madd(cl_mul(b0[0], b0[0]), b0[2], b0[2]), &d)); /* 0018C1C8 */
    *w->d690 = d;                                                     /* 0018C1EC */
    if (cl_lt(d, CC_0_001)) {                                         /* 0018C1E0 */
        *w->d690 = CC_0_001;                                          /* 0018C1F4 */
        b0[2] = CC_0_001;                                             /* 0018C1FC */
        cl_cset(c, 0x18, cl_add(cl_cw(c, 0x18), CC_0_001));           /* 0018C200..08 */
    }

    *w->d694 = cl_fabs(b0[1]);                                        /* 0018C210 / 0018C230 */
    if (cl_v_normalize(a0, a0) < 0) return cc_fault(w, 0x00102760);   /* 0018C22C */

    /* The view position 0x700038C0. */
    uint32_t push = 0;                                                /* 0: the eye itself */
    if (mode == 0) {                                                  /* 0018C234 */
        if (cl_cb(c, 4) != 3) {                                       /* 0018C244 */
            uint8_t action = cl_cb(c, 6);                             /* 0018C268 */
            if (action == 2 || action == 1) push = CL_4;              /* 0018C270 / 0018C27C */
        }
    } else {
        push = cl_cb(c, 6) == 0xA ? CL_M1 : CL_4;                     /* 0018C31C */
    }
    if (push) {
        /* Per lane eye + k*v, rounded after the multiply and after the add. */
        uint32_t x = cl_add(w->eye[0], cl_mul(push, a0[0]));
        uint32_t y = cl_add(w->eye[1], cl_mul(push, a0[1]));
        uint32_t z = cl_add(w->eye[2], cl_mul(push, a0[2]));
        c0[0] = x;                                                    /* 0018C2DC / 0018C374 / 0018C3E0 */
        c0[1] = y;
        c0[2] = z;
    } else {
        cl_v_copy3(c0, w->eye);                                       /* 0018C258 / 0018C304: 001031E0 */
    }

    CC_CALL(w, 0x00102CD0, k->lookat(k->context, w->view, c0, a0, w->up));   /* 0018C414 */
    cl_v_copy(c->bytes + 0xB0, a0);                                   /* 0018C424: 00102948 */
    em_camera_commit_00102798(w->view_t, w->view);                   /* 0018C438 */
    uint32_t heading;
    CC_CALL(w, 0x0011E620, k->atan2(k->context, cl_neg(a0[2]), a0[0], &heading)); /* 0018C450 */
    *w->d6A0 = heading;                                               /* 0018C470 */
    cl_v_copy(w->fwd, a0);                                            /* 0018C46C: 00102948 */
    uint32_t obj[3];
    memcpy(obj, w->eye, sizeof obj);
    CC_CALL(w, 0x001B1240, k->heading(k->context, obj, w->target[0], w->target[2], &heading)); /* 0018C488 */
    cl_cset(c, 0x9C, heading);                                        /* 0018C490 */
    return 0;
}

int em_camera_commit_00193660(const EmCameraCommitWorkers *workers, const uint32_t eye[4],
                              const uint32_t target[4], uint32_t s38A0[4], uint32_t *s3A20,
                              int32_t *result)
{
    if (!workers || !workers->sqrt || !eye || !target || !s38A0 || !s3A20 || !result) return -1;
    if (cl_v_sub(s38A0, target, eye) < 0) return -1;                  /* 0019367C: 001028D0 */
    uint32_t dot, r;
    if (cl_v_dot(s38A0, s38A0, &dot) < 0) return -1;                  /* 00193690: 00102738 */
    if (workers->sqrt(workers->context, dot, &r) < 0) return -1;      /* 00193698: 0011E748 */
    *s3A20 = r;                                                       /* 001936B8 */
    *result = cl_lt(r, CC_5_5) ? 1 : 0;                               /* 001936AC..C4 */
    return 0;
}
