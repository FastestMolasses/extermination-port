/* Original001AE040 status entry/state3/state5. The status-page driver is
 * an explicit worker boundary; this core does not replace it with a timer. */
#ifndef EM_STATUS_FRAME_H
#define EM_STATUS_FRAME_H
#include <stdint.h>

typedef struct {
    uint8_t phase, step, task_flag11;                /* task+B/C/11 */
    uint8_t control_mode, recovery_lock, audio_busy; /*8106C4/EF,282157 */
} EmStatusFrame;

typedef enum {
    EM_STATUS_RESET_UI,      /*0020E060() */
    EM_STATUS_RESET_SOUNDS,  /*001FBC50() */
    EM_STATUS_STOP_STREAMS,  /*001FABB0(), including clear282157 */
    EM_STATUS_CHANNEL_ZERO,  /*00119828(0,3FFF,3FFF) */
    EM_STATUS_CHANNEL_ONE,   /*00119828(1,3FFF,3FFF) */
    EM_STATUS_BEGIN_FRAME,   /*001D1C50() */
    EM_STATUS_DRAW_CONTEXT,  /*001D2830(3,1) */
    EM_STATUS_MODE_ZERO,     /*001E0CC0(0) */
    EM_STATUS_BLACK_HOLD,    /*001AEDB0(0) */
    EM_STATUS_END_FRAME,     /*001D1EA0(0) */
    EM_STATUS_RESET_FRAME,   /*001D1EF0() */
    EM_STATUS_CAMERA_COMMIT, /*0018C0D0(camera,1) */
    EM_STATUS_RESUME_MUSIC,  /*001FAE70(1) */
    EM_STATUS_FLASH          /*001AEE40(32), original state4 fade kind */
} EmStatusFrameEvent;

typedef int (*EmStatusFrameEmit)(void *, EmStatusFrameEvent);
/* -1 worker failure,0 waiting,1 actual0020CDC0 completion. */
typedef int (*EmStatusPageTick)(void *);

/* Call only for the original AE7E0 classifier result2 in ordinary phase1.
 * The successful call consumes the frame; ordinary world tasks do not run. */
int em_status_frame_enter(EmStatusFrame *state, EmStatusFrameEmit emit, void *context);
/*0 still in status,1 returned to ordinary phase1,-1 failed/missing worker.
 * Side effects must return1; on error the host must stop the affected task.
 * STOP_STREAMS has actually accepted its reset before audio_busy is cleared. */
int em_status_frame_tick(EmStatusFrame *state, EmStatusFrameEmit emit, EmStatusPageTick page_tick,
                         void *context);
int em_status_frame_active(const EmStatusFrame *state);

#endif
