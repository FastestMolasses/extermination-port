#ifndef EM_AIM_FIRE_SOFT_FLOAT_H
#define EM_AIM_FIRE_SOFT_FLOAT_H
#include "game/em_aim_fire_target.h"

/* The aim/fire binding's two soft-float leaves, reached from the knife's
 * facing test (00189EC0 type 0x0E: 001000C0(00128350(t), pi/2)):
 *   00128350 (decomp eegcc C, __extendsfdf2): f12's float word -> the
 *            64-bit double word in v0;
 *   001000C0 (decomp C): v0 = 1 when 001274B0 of a0, a1 (two 64-bit
 *            double words, passed through untouched) is negative, else 0.
 * Both use em_sdk_soft_float, their one translation. Returns 0 with v0 set,
 * -1 when the call lacks its argument (f12, or a0 and a1), and 1 when the
 * function is neither (the caller dispatches it). Checked against the
 * original instructions by tools/test_aim_fire_soft_float_reference.py. */
int em_aim_fire_soft_float_call(EmAimFireTargetCall *f);
#endif
