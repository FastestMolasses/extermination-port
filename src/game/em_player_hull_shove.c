/* em_player_hull_shove.c - 00176180 (see the header). */
#include "game/em_player_hull_shove.h"

#include <string.h>

#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_sdk_vu0.h"

int em_player_00176180(EmPlayerProbeActor *a, const float delta[3], const float target[3],
                       const EmPlayerHullShoveWorkers *w)
{
    if (!a || !delta || !target || !w || !w->sweep || !w->sqrt) return -1;
    if (a->mode == 0x3C) return 0;
    uint32_t d[4] = {0, 0, 0, 0}, pos[4] = {0, 0, 0, 0}, p[4] = {0, 0, 0, 0}, dot;
    memcpy(d, delta, 12);                                   /* D_00281B50; w unused */
    memcpy(pos, a->position, 12);                           /* 00102948(+0xB0); w unused */
    if (em_sdk_vu0_00102738(&dot, d, d) != EM_EE_FLOAT_OK) return -1;
    (void)w->sqrt(w->context, em_ee_float(dot));            /* 0011E748, result unused */
    float in[4], n[4];
    memcpy(in, d, sizeof in);
    em_effect_original_00102760(n, in);
    if (em_vu_vec_bits(EM_VU_ADD, 0xF, EM_VU_NO_BC, pos, d, 0, NULL, p) != EM_EE_FLOAT_OK)   /* 001028B8 */
        return -1;
    float px = em_ee_float(p[0]), pz = em_ee_float(p[2]);
    const float v[3] = {
        em_ee_add(px, em_ee_mul(4.5f, n[0])),
        target[1],
        em_ee_add(pz, em_ee_mul(4.5f, n[2])),
    };
    EmPlayerProbeHit hit;
    memset(&hit, 0, sizeof hit);
    const int kind = w->sweep(w->context, target, v, 6, &hit);   /* 0019AFE0 */
    if (kind < 0) return -1;
    if (kind != 0) {
        px = em_ee_add(px, hit.delta[0]);                   /* 0x700031C0 */
        pz = em_ee_add(pz, hit.delta[2]);                   /* 0x700031C8 */
    }
    a->position[0] = px;
    a->position[2] = pz;
    if (a->major == 1 && a->state == 0x1D) {
        if (!w->carry31F0) return -1;
        *w->carry31F0 = 1;
    }
    return 0;
}
