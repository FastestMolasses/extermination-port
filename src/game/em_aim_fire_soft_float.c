#include "game/em_aim_fire_soft_float.h"
#include "game/em_sdk_soft_float.h"

int em_aim_fire_soft_float_call(EmAimFireTargetCall *f)
{
    if (!f) return -1;
    switch (f->function) {
    case 0x128350:
        if (f->nf < 1) return -1;
        f->v0 = em_sdk_soft_float_00128350(f->f[0]);
        return 0;
    case 0x1000C0:
        if (f->na < 2) return -1;
        f->v0 = em_sdk_soft_float_001274B0(f->a[0], f->a[1]) < 0;
        return 0;
    default:
        return 1;
    }
}
