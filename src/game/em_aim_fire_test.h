#ifndef EM_AIM_FIRE_TEST_H
#define EM_AIM_FIRE_TEST_H
/* Optional input-only extension to newgame-control. */
void em_aim_fire_test_begin(void);
void em_aim_fire_test_before_frame(void);
/* 0 running, 1 complete, -1 failed. */
int em_aim_fire_test_after_frame(void);
#endif
