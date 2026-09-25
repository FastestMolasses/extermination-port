#include "game/em_player_motor.h"

#include <math.h>

#include "game/em_ee_float.h"

/* The tier tables of the mirror adapter (em_player_motor_tick) and of the
 * legacy re-entry metadata below: D_00248870 (speed), D_00248880 (rise) and
 * D_00248890 (fall). The live path reads the exported ELF span instead. */
static const float speed[4] = {0, .1f, .3f, .8f};
static const float rise[4] = {.05f, .05f, .0625f, 0};
static const float fall[4] = {0, .05f, .025f, .022727273404598236f};

/* ---- 0017BC40 over the raw record --------------------------------------- */

#define REC_SPEED  0x38u    /* +38: the running scalar */
#define REC_RATE   0x204u   /* +204: the display rate */
#define REC_BLEND  0x208u   /* +208: the tier cross-fade */
#define REC_MODE   0x1F0u
#define REC_SUB    0x1F1u
#define REC_TIER   0x25Cu
#define REC_TARGET 0x240u   /* +240: the stick's target speed */
#define REC_GAIT   0x23Fu
#define REC_LANES  0x314u

#define T_LOWER 0x0024886Cu /* D_0024886C[i] */
#define T_SPEED 0x00248870u /* D_00248870[i] */
#define T_UPPER 0x00248874u /* D_00248874[i] */
#define T_RISE  0x00248880u /* D_00248880[i] */
#define T_FALL  0x00248890u /* D_00248890[i] */

static uint32_t rec32(const uint8_t *r, unsigned at)
{
    return (uint32_t)r[at] | (uint32_t)r[at + 1] << 8 | (uint32_t)r[at + 2] << 16 |
           (uint32_t)r[at + 3] << 24;
}

static void set32(uint8_t *r, unsigned at, uint32_t v)
{
    r[at] = (uint8_t)v; r[at + 1] = (uint8_t)(v >> 8);
    r[at + 2] = (uint8_t)(v >> 16); r[at + 3] = (uint8_t)(v >> 24);
}

/* 1 - (hi - x) / (hi - lo), the order of the original's operations. */
static uint32_t cross_fraction(uint32_t hi, uint32_t x, uint32_t lo)
{
    return em_ee_sub_bits(EM_EE_ONE, em_ee_div_bits(em_ee_sub_bits(hi, x), em_ee_sub_bits(hi, lo)));
}

/* The +240 == 0 stop: +1F0 = 3 below tier 3, else +1F0 = 2 and +208 = 0. */
static void stop_record(uint8_t *r)
{
    if (r[REC_TIER] < 3) {
        r[REC_MODE] = 3;
    } else {
        r[REC_MODE] = 2;
        set32(r, REC_BLEND, 0);
    }
}

int em_player_motor_0017BC40(uint8_t *r, EmPlayerMotorRead read, void *context, uint32_t *spad3A20)
{
    if (!r || !read || !spad3A20) return -1;
    const uint32_t zero = 0;
    switch (r[REC_MODE]) {
    case 1: {
        const unsigned i = r[REC_TIER];
        if (r[REC_SUB] == 1) {
            uint32_t rise, lo, hi;
            if (read(context, T_RISE + 4u * i, &rise) < 0 ||
                read(context, T_SPEED + 4u * i, &lo) < 0 ||
                read(context, T_UPPER + 4u * i, &hi) < 0)
                return -1;
            const uint32_t x = em_ee_add_bits(rec32(r, REC_SPEED), rise);
            set32(r, REC_SPEED, x);
            if (em_ee_c_le_bits(hi, x)) {
                set32(r, REC_SPEED, hi);
                r[REC_TIER] = (uint8_t)(r[REC_TIER] + 1);
                r[REC_SUB] = 0;
            } else {
                *spad3A20 = cross_fraction(hi, x, lo);
                set32(r, REC_BLEND, *spad3A20);
                set32(r, REC_RATE, em_ee_add_bits(EM_EE_ONE, *spad3A20));
            }
        } else if (r[REC_SUB] == 2) {
            if (em_ee_c_eq_bits(rec32(r, REC_TARGET), zero)) {
                stop_record(r);
            } else {
                uint32_t fall, lo, hi;
                if (read(context, T_FALL + 4u * i, &fall) < 0 ||
                    read(context, T_SPEED + 4u * i, &lo) < 0 ||
                    read(context, T_LOWER + 4u * i, &hi) < 0)
                    return -1;
                const uint32_t x = em_ee_sub_bits(rec32(r, REC_SPEED), fall);
                set32(r, REC_SPEED, x);
                if (em_ee_c_le_bits(x, hi)) {
                    set32(r, REC_SPEED, hi);
                    r[REC_TIER] = (uint8_t)(r[REC_TIER] - 1);
                    r[REC_SUB] = 0;
                } else {
                    *spad3A20 = cross_fraction(hi, x, lo);
                    set32(r, REC_BLEND, *spad3A20);
                    set32(r, REC_RATE, em_ee_add_bits(EM_EE_ONE, *spad3A20));
                }
            }
        } else {
            const uint32_t target = rec32(r, REC_TARGET), speed = rec32(r, REC_SPEED);
            if (em_ee_c_eq_bits(target, zero)) {
                stop_record(r);
            } else if (em_ee_c_lt_bits(speed, target)) {
                r[REC_SUB] = 1;
                set32(r, REC_BLEND, 0);
            } else if (em_ee_c_lt_bits(target, speed)) {
                if (r[REC_TIER] > 1) {
                    r[REC_SUB] = 2;
                } else {
                    r[REC_MODE] = 3;
                }
                set32(r, REC_BLEND, 0);
            } else if (r[REC_TIER] == 2) {
                set32(r, REC_RATE, UINT32_C(0x3F400000));   /* 0.75 */
            }
        }
        break;
    }
    case 2: {
        /* The step 0.03125, doubled (exactly) when a lane bit is set. */
        uint32_t step = UINT32_C(0x3D000000);
        if (r[REC_LANES] & 0x1F) step = em_ee_mul_bits(step, UINT32_C(0x40000000));
        const unsigned i = r[REC_TIER];
        uint32_t lo, hi;
        if (read(context, T_SPEED + 4u * i, &lo) < 0 || read(context, T_LOWER + 4u * i, &hi) < 0)
            return -1;
        const uint32_t x = em_ee_sub_bits(rec32(r, REC_SPEED), step);
        set32(r, REC_SPEED, x);
        if (em_ee_c_le_bits(x, hi)) {
            set32(r, REC_SPEED, hi);
            if (r[REC_GAIT] != 0) {
                r[REC_MODE] = 1;
                r[REC_TIER] = (uint8_t)(r[REC_TIER] - 1);
                set32(r, REC_BLEND, 0);
                const uint32_t speed = rec32(r, REC_SPEED), target = rec32(r, REC_TARGET);
                r[REC_SUB] = em_ee_c_lt_bits(speed, target) ? 1 : em_ee_c_lt_bits(target, speed) ? 2 : 0;
            } else {
                set32(r, REC_SPEED, 0);
                r[REC_MODE] = 3;
                r[REC_SUB] = 0;
            }
        }
        *spad3A20 = cross_fraction(hi, rec32(r, REC_SPEED), lo);
        set32(r, REC_RATE, em_ee_add_bits(EM_EE_ONE, em_ee_mul_bits(UINT32_C(0x3F000000), *spad3A20)));
        break;
    }
    case 6: {
        set32(r, REC_SPEED, em_ee_sub_bits(rec32(r, REC_SPEED), UINT32_C(0x3D4CCCCD)));   /* 0.05 */
        if (em_ee_c_lt_bits(rec32(r, REC_SPEED), zero)) set32(r, REC_SPEED, 0);
        break;
    }
    default:
        break;   /* 0, 3, 4, 5, 7 and the rest: nothing */
    }
    return 0;
}

/* The mirror adapter's tables at their EE addresses: D_00248870[0..3] (so
 * D_00248874[0..2]), D_00248880[0..3] and D_00248890[0..3]. D_0024886C[0]
 * and D_00248874[3] lie outside them. */
static int mirror_table(void *context, uint32_t address, uint32_t *word)
{
    (void)context;
    const float *table = NULL;
    uint32_t base = 0;
    if (address >= T_SPEED && address < T_SPEED + 16) { table = speed; base = T_SPEED; }
    else if (address >= T_RISE && address < T_RISE + 16) { table = rise; base = T_RISE; }
    else if (address >= T_FALL && address < T_FALL + 16) { table = fall; base = T_FALL; }
    if (!table || (address - base) % 4) return -1;
    *word = em_ee_bits(table[(address - base) / 4]);
    return 0;
}

void em_player_motor_tick(EmPlayerMotor *m)
{
    if (!m || m->tier > 3) return;
    uint8_t r[0x320] = {0};
    set32(r, REC_SPEED, em_ee_bits(m->speed));
    set32(r, REC_TARGET, em_ee_bits(m->target));
    set32(r, REC_RATE, em_ee_bits(m->rate));
    set32(r, REC_BLEND, em_ee_bits(m->blend));
    r[REC_MODE] = m->mode;
    r[REC_SUB] = m->substate;
    r[REC_TIER] = m->tier;
    r[REC_GAIT] = m->gait;
    r[REC_LANES] = m->obstruction;
    uint32_t spad3A20 = 0;
    if (em_player_motor_0017BC40(r, mirror_table, NULL, &spad3A20) < 0) return;
    m->speed = em_ee_float(rec32(r, REC_SPEED));
    m->rate = em_ee_float(rec32(r, REC_RATE));
    m->blend = em_ee_float(rec32(r, REC_BLEND));
    m->mode = r[REC_MODE];
    m->substate = r[REC_SUB];
    m->tier = r[REC_TIER];
}

void em_player_stop_begin(EmPlayerStop *s, unsigned frames)
{
    if (!s || frames < 6) return;
    *s = (EmPlayerStop){1, 6, frames - 6, frames - 1};
}

void em_player_stop_tick(EmPlayerStop *s)
{
    if (!s) return;
    switch (s->phase) {
    case 1:
        if (--s->blend_left == 0) s->phase = 2;
        break;
    case 2:
        if (s->frame >= s->last_frame) s->phase = 3;
        else ++s->frame;
        break;
    case 3:
        s->phase = 4;
        s->blend_left = 12;
        break;
    case 4:
        if (--s->blend_left == 0) s->phase = 0;
        break;
    default:
        break;
    }
}

int em_player_reentry_begin(EmPlayerReentry *r, EmPlayerMotor *m,
                           unsigned gait, unsigned frames)
{
    unsigned remaining = gait == 3 ? 18 : 46;
    if (!r || !m || gait < 2 || gait > 3 || frames < remaining) return 0;
    m->tier = gait - 1;
    m->speed = speed[m->tier];
    m->mode = 1;
    *r = (EmPlayerReentry){1, 4, frames - remaining};
    return 1;
}

void em_player_reentry_tick(EmPlayerReentry *r, EmPlayerMotor *m)
{
    if (!r || !m || r->phase != 1) return;
    if (--r->blend_left == 0) {
        r->phase = 2;
        /* 0017C540 restores walk phase0 and scalar substate0. Both
         * valid interruption tiers are nonzero. */
        m->mode = 1;
        m->substate = 0;
    }
}
