/* Optional end-to-end instrumentation; inactive in normal gameplay. */
#ifndef EM_OPENING_CONTROL_TEST_H
#define EM_OPENING_CONTROL_TEST_H
void em_opening_control_test_begin(void);
void em_opening_control_test_before_frame(void);
void em_opening_control_test_after_frame(void);
/* From the slot-0 game task while a scene fault is latched (the fail-stop
 * return of em_scene_task_001ACEC0): the hooks above no longer run, so this
 * ends the run with a FAIL line and a nonzero exit. */
void em_opening_control_test_scene_stopped(void);
int em_opening_control_test_active(void);
int em_opening_control_test_failed(void);
#endif
