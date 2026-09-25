#include "game/em_player_foot_stop.h"
#include "game/em_camera_rotation.h"
#include "game/em_effect_color.h"
#include "game/em_effect_original.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"

#include <string.h>

int em_player_foot_stop_begin(EmPlayerFootStop *stop, unsigned tier,
    float clip_remaining, const float foot17[3], const float foot18[3],
    const float position[3], const float euler[3])
{
    /* 0017B910 has no lower bound on the +0x3C clock: a clock below 1 takes
     * the cur < lim arm (residual cur - 1 < 0); walk then clamps the halved
     * residual to 1 and jog uses 10. Non-finite inputs are a native refusal. */
    if (!stop || !foot17 || !foot18 || !position || !euler ||
        (tier != 1 && tier != 2) || !isfinite(clip_remaining))
        return 0;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(foot17[axis]) || !isfinite(foot18[axis]) || !isfinite(position[axis]))
            return 0;

    /*D0024875C's healthy rows select the next planted foot from the
     * current remaining clock. Only walk halves that residual interval. */
    float limit = tier == 1 ? 58.0f : 24.0f;
    const float *foot = clip_remaining < limit ? foot18 : foot17;
    float duration = 10.0f;
    if (tier == 1) {
        float residual = em_effect_float32((double)clip_remaining -
                                           (clip_remaining < limit ? 1.0f : limit));
        duration = residual / 2.0f;
        if (duration < 1) duration = 1;
    }
    float dx = em_effect_float32((double)foot[0] - position[0]);
    float dz = em_effect_float32((double)foot[2] - position[2]);
    float square = em_effect_float32((double)em_effect_float32((double)dx * dx) +
                                     em_effect_float32((double)dz * dz));
    /*0011E748 calls the original software square root, which rounds its
     * result nearest. The surrounding EE products/addition truncate. */
    float distance = sqrtf(square);
    float matrix[16], direction[4];
    if (!em_camera_rotation_offset(euler, distance, matrix, direction)) return 0;
    *stop = (EmPlayerFootStop){direction[0] / duration, direction[2] / duration,
                              duration, tier, 1};
    return 1;
}

int em_player_foot_stop_tick(EmPlayerFootStop *stop, unsigned animation_flags,
    float position[3], float *animation_rate)
{
    if (!stop || !position || !animation_rate || !stop->active ||
        (stop->tier != 1 && stop->tier != 2))
        return -1;
    if (stop->remaining < 1) {
        if (stop->tier == 1 || (animation_flags & 0x1000)) {
            stop->active = 0;
            return 0;
        }
    } else {
        position[0] = em_effect_float32((double)position[0] + stop->step_x);
        position[2] = em_effect_float32((double)position[2] + stop->step_z);
        stop->remaining = em_effect_float32((double)stop->remaining - 1);
        if (stop->tier == 1) *animation_rate = 2;
    }
    return 1;
}

/* ---- 0017B910 over the raw record ------------------------------------------ */

#define D_0024875C UINT32_C(0x0024875C)   /* the stop-clip limits, 2 words per +235 row */

static uint32_t fs_rec32(const EmPlayerLiveActor *a, unsigned at)
{
    const uint8_t *r = a->bytes + at;
    return (uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
}

static void fs_set32(EmPlayerLiveActor *a, unsigned at, uint32_t v)
{
    uint8_t *r = a->bytes + at;
    r[0] = (uint8_t)v; r[1] = (uint8_t)(v >> 8); r[2] = (uint8_t)(v >> 16); r[3] = (uint8_t)(v >> 24);
}

int em_player_foot_stop_0017B910(const EmPlayerFootStopWorkers *w, const EmPlayerFootStopScratch *s,
                                 const uint32_t *d275B40, EmPlayerLiveActor *a)
{
    if (!w || !s || !d275B40 || !a || !w->eval_skeleton || !w->select || !w->clip_frames ||
        !w->request || !w->sqrt || !w->read || !s->s3A20 || !s->s3A24 || !s->s36A0 || !s->s38A0 ||
        !s->s38B0)
        return -1;
    uint8_t *r = a->bytes;
    if (w->eval_skeleton(w->context, a) < 0) return -1;
    int16_t clip;
    if (r[0x236] != 0) {
        /* The row default against the current clip. */
        if (w->select(w->context, a, 0, r[0x235], 0, &clip) < 0) return -1;
        const int16_t current = (int16_t)(r[0x20C] | r[0x20D] << 8);
        if (clip == current) {
            if (!(fs_rec32(a, 0x200) & 0x8000u)) {
                r[0x1F0] = 0;
                r[0x25C] = 0;
            }
            return 0;
        }
        return w->request(w->context, a, clip, 0, 14.0f) < 0 ? -1 : 0;
    }
    if (w->select(w->context, a, 1, r[0x235], r[0x25C], &clip) < 0) return -1;
    int32_t frames;
    if (w->clip_frames(w->context, fs_rec32(a, 0x40), clip, &frames) < 0) return -1;
    *s->s3A20 = em_ee_cvt_s_w_bits((uint32_t)frames);
    /* The limit D_0024875C[+235][+25C] and the clock +3C. */
    const uint32_t row = D_0024875C + 8u * r[0x235] + 4u * r[0x25C];
    uint32_t limit;
    if (w->read(w->context, row, &limit) < 0) return -1;
    const uint32_t clock = fs_rec32(a, 0x3C);
    /* *(D_00275B40 + 0x48) is node 18, + 0x44 node 17. */
    uint32_t node;
    if (em_ee_c_lt_bits(clock, limit)) {
        s->s3A24[0] = em_ee_sub_bits(clock, EM_EE_ONE);
        *s->s3A20 = em_ee_sub_bits(*s->s3A20, EM_EE_ONE);
        if (w->read(w->context, *d275B40 + 0x48, &node) < 0 ||
            w->read(w->context, node + 0xC0, &s->s3A24[1]) < 0 ||
            w->read(w->context, *d275B40 + 0x48, &node) < 0 ||
            w->read(w->context, node + 0xC8, &s->s3A24[2]) < 0)
            return -1;
    } else {
        s->s3A24[0] = em_ee_sub_bits(clock, limit);
        uint32_t again;   /* the row word is re-read (the recomputed address) */
        if (w->read(w->context, D_0024875C + 8u * r[0x235] + 4u * r[0x25C], &again) < 0) return -1;
        *s->s3A20 = em_ee_sub_bits(*s->s3A20, again);
        if (w->read(w->context, *d275B40 + 0x44, &node) < 0 ||
            w->read(w->context, node + 0xC0, &s->s3A24[1]) < 0 ||
            w->read(w->context, *d275B40 + 0x44, &node) < 0 ||
            w->read(w->context, node + 0xC8, &s->s3A24[2]) < 0)
            return -1;
    }
    if (r[0x25C] == 1) {
        s->s3A24[0] = em_ee_div_bits(s->s3A24[0], UINT32_C(0x40000000));   /* / 2.0 */
        if (em_ee_c_lt_bits(s->s3A24[0], EM_EE_ONE)) s->s3A24[0] = EM_EE_ONE;
        fs_set32(a, 0x268, s->s3A24[0]);
    } else {
        fs_set32(a, 0x268, UINT32_C(0x41200000));   /* 10.0 */
        if (w->select(w->context, a, 6, r[0x235], r[0x25C], &clip) < 0) return -1;
        if (w->request(w->context, a, clip, 0, em_ee_float(fs_rec32(a, 0x268))) < 0) return -1;
    }
    /* dx into 0x70003A20, dz into 0x70003A24; the planar distance. */
    *s->s3A20 = em_ee_sub_bits(s->s3A24[1], fs_rec32(a, 0xB0));
    s->s3A24[0] = em_ee_sub_bits(s->s3A24[2], fs_rec32(a, 0xB8));
    const uint32_t square = em_ee_madd_bits(em_ee_mula_bits(*s->s3A20, *s->s3A20), s->s3A24[0],
                                            s->s3A24[0]);
    uint32_t distance;
    if (w->sqrt(w->context, square, &distance) < 0) return -1;
    s->s3A24[1] = distance;
    float m[16], angles[3];
    memcpy(m, s->s36A0, sizeof m);
    if (em_owner_services_identity_001029C0(m) != EM_EE_FLOAT_OK) { memcpy(s->s36A0, m, sizeof m); return -1; }
    memcpy(angles, a->bytes + 0xC0, sizeof angles);
    int st = em_owner_services_euler_00102C58(m, m, angles);
    memcpy(s->s36A0, m, sizeof m);
    if (st != EM_EE_FLOAT_OK) return -1;
    s->s38A0[0] = 0;
    s->s38A0[1] = 0;
    s->s38A0[2] = s->s3A24[1];
    s->s38A0[3] = 0;
    float v[4], out[4];
    memcpy(v, s->s38A0, sizeof v);
    em_effect_original_001026A0(out, m, v);
    memcpy(s->s38B0, out, sizeof out);
    fs_set32(a, 0x260, em_ee_div_bits(s->s38B0[0], fs_rec32(a, 0x268)));
    fs_set32(a, 0x264, em_ee_div_bits(s->s38B0[2], fs_rec32(a, 0x268)));
    r[0x1F0] = 5;
    return 0;
}
