/* Gamepad backend placeholder. The contract (em_gamepad.h) exists on every
 * platform so game-side code can call it unconditionally; only macOS has a
 * real implementation today (GameController.framework). Linux would use
 * evdev, Windows XInput — both platform-native, no third-party libraries. */
#include "em_gamepad.h"

void em_gamepad_poll(void) { }
int  em_gamepad_present(void) { return 0; }
void em_gamepad_rumble(float big, float small, int frames)
{ (void)big; (void)small; (void)frames; }
void em_gamepad_rumble_state(float *big, float *small, int *frames)
{ if (big) *big = 0.0f; if (small) *small = 0.0f; if (frames) *frames = 0; }
