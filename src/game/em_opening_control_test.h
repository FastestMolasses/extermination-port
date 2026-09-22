/* Optional end-to-end instrumentation; inactive in normal gameplay. */
#ifndef EM_OPENING_CONTROL_TEST_H
#define EM_OPENING_CONTROL_TEST_H
void em_opening_control_test_begin(void);
void em_opening_control_test_before_frame(void);
void em_opening_control_test_after_frame(void);
int em_opening_control_test_active(void);
int em_opening_control_test_failed(void);
#endif
