#include "game/em_status_frame.h"

int em_status_frame_active(const EmStatusFrame *state)
{return state && (state->phase==3 || state->phase==5);}

int em_status_frame_enter(EmStatusFrame *state,EmStatusFrameEmit emit,void *context)
{
    if (!state || state->phase!=1 || !emit) return -1;
    if (emit(context,EM_STATUS_RESET_UI)!=1) return -1;
    state->control_mode=1;
    state->phase=3;
    state->task_flag11=0;
    state->step=0;
    if (emit(context,EM_STATUS_RESET_SOUNDS)!=1 ||
        emit(context,EM_STATUS_STOP_STREAMS)!=1) return -1;
    state->audio_busy=0;
    if (emit(context,EM_STATUS_CHANNEL_ZERO)!=1 ||
        emit(context,EM_STATUS_CHANNEL_ONE)!=1) return -1;
    return 0;
}

int em_status_frame_tick(EmStatusFrame *state,EmStatusFrameEmit emit,
                        EmStatusPageTick page_tick,void *context)
{
    if (!state || !emit) return -1;
    if (state->phase==3) {
        if (state->step==0) {
            if (!state->audio_busy) ++state->step;
        } else if (state->step==1) {
            if (!page_tick || emit(context,EM_STATUS_BEGIN_FRAME)!=1 ||
                emit(context,EM_STATUS_DRAW_CONTEXT)!=1) return -1;
            int result=page_tick(context);
            if (result<0 || result>1) return -1;
            if (result) {
                if (emit(context,EM_STATUS_MODE_ZERO)!=1) return -1;
                state->phase=5;
                state->recovery_lock=0x46;
                if (emit(context,EM_STATUS_BLACK_HOLD)!=1) return -1;
            }
            if (emit(context,EM_STATUS_END_FRAME)!=1) return -1;
        }
        return 0;
    }
    if (state->phase==5) {
        if (emit(context,EM_STATUS_BLACK_HOLD)!=1 ||
            emit(context,EM_STATUS_RESET_FRAME)!=1 ||
            emit(context,EM_STATUS_CAMERA_COMMIT)!=1) return -1;
        state->control_mode=0;
        state->phase=1;
        state->step=0;
        if (emit(context,EM_STATUS_RESUME_MUSIC)!=1 ||
            emit(context,EM_STATUS_FLASH)!=1) return -1;
        return 1;
    }
    return -1;
}
