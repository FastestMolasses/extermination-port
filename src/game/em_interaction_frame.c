#include "game/em_interaction_frame.h"
#include <string.h>

EmScriptCommandResult em_interaction_frame_command(EmInteractionFrame *state,
    EmScript *script, unsigned subcommand, int immediate,
    EmInteractionFrameEmit emit, void *context)
{
    if (!state || !script || !emit) return EM_SCRIPT_UNSUPPORTED;
    if (subcommand==2) {
        switch ((uint8_t)script->phase) {
        case 0:
            if (state->ready) return EM_SCRIPT_ADVANCE;
            state->camera_top=1;
            state->selector=2;
            if (!emit(context,EM_INTERACTION_BARS_ENTER))
                return EM_SCRIPT_UNSUPPORTED;
            script->phase=1;
            state->counter=0;
            script->skip_request=0;
            memset(state->activity,0,sizeof state->activity);
            state->auxiliary=0;
            break;
        case 1:
            if (immediate || state->player_ready) {
                state->ready=1;
                script->skip_phase=1;
                if (!emit(context,EM_INTERACTION_SCOPE_ZOOM_ZERO))
                    return EM_SCRIPT_UNSUPPORTED;
                return EM_SCRIPT_ADVANCE;
            }
            break;
        default:break;
        }
        return EM_SCRIPT_WAIT;
    }
    if (subcommand!=4) return EM_SCRIPT_UNSUPPORTED;
    if (!state->selector) return EM_SCRIPT_ADVANCE;
    state->auxiliary=0;
    state->camera_top=0;
    state->recovery_lock=0x50;
    if (state->camera_mode==3) {
        state->camera_mode=0;
        state->camera_state=0;
        state->camera_swing=0;
    }
    state->camera_phase=0;
    if (!emit(context,EM_INTERACTION_BARS_LEAVE)) return EM_SCRIPT_UNSUPPORTED;
    state->message_phase=2;
    if (state->player_ready==2) {
        if (!emit(context,EM_INTERACTION_RELEASE_SKELETON))
            return EM_SCRIPT_UNSUPPORTED;
        state->player_ready=1;
    }
    state->zoom=480;
    if (!emit(context,EM_INTERACTION_ZOOM_DEFAULT)) return EM_SCRIPT_UNSUPPORTED;
    state->up[0]=0;state->up[1]=-1;state->up[2]=0;state->up[3]=1;
    int aborted=script->skip_request==2 && script->skip_phase==2;
    if (aborted && (!emit(context,EM_INTERACTION_RESUME_MUSIC) ||
                    !emit(context,EM_INTERACTION_FADE_IN)))
        return EM_SCRIPT_UNSUPPORTED;
    script->skip_phase=0;
    state->selector=0;
    state->ready=0;
    script->skip_request=0;
    return aborted ? EM_SCRIPT_ABORT : EM_SCRIPT_ADVANCE;
}
