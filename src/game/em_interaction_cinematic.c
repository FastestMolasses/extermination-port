#include "game/em_interaction_cinematic.h"
#include <string.h>

EmScriptCommandResult em_interaction_cinematic_command(EmInteractionFrame *frame,
    EmScript *script,unsigned sub,int immediate,int32_t stream,
    const EmInteractionCinematicInputs *input,EmInteractionCinematicEmit emit,void *context)
{
    if (!frame || !script || !input || !emit || sub<9 || sub>12)
        return EM_SCRIPT_UNSUPPORTED;
    switch ((uint8_t)script->phase) {
    case 0:
        if (frame->ready) return EM_SCRIPT_ADVANCE;
        frame->camera_top=1;
        frame->selector=2;
        frame->counter=0;
        script->skip_request=0;
        memset(frame->activity,0,sizeof frame->activity);
        frame->auxiliary=0;
        if (!emit(context,EM_CINEMATIC_FADE_OUT_FOUR,0) ||
            !emit(context,EM_CINEMATIC_REQUEST_STREAM,stream))
            return EM_SCRIPT_UNSUPPORTED;
        script->phase=(script->phase&~255)|1;
        if (!emit(context,EM_CINEMATIC_MUTE_CHANNEL,0) ||
            !emit(context,EM_CINEMATIC_MUTE_CHANNEL,1))
            return EM_SCRIPT_UNSUPPORTED;
        break;
    case 1:
        if (input->fade_phase==2) {
            if (!emit(context,EM_CINEMATIC_BARS_IMMEDIATE,0))
                return EM_SCRIPT_UNSUPPORTED;
            script->phase=(script->phase&~255)|2;
        }
        break;
    case 2:
        if (immediate || frame->player_ready) {
            if (sub>=11 && !emit(context,EM_CINEMATIC_ATTACH_PLAYER,0))
                return EM_SCRIPT_UNSUPPORTED;
            /* The immediate branch writes phase before the zoom call;
             * the player-ready branch writes it after that call. */
            if (immediate) script->phase=(script->phase&~255)|3;
            if (!emit(context,EM_CINEMATIC_SCOPE_ZERO,0))
                return EM_SCRIPT_UNSUPPORTED;
            if (!immediate) script->phase=(script->phase&~255)|3;
        }
        break;
    case 3:
        if (input->stream_ready!=1) break;
        if (!emit(context,EM_CINEMATIC_FADE_IN_SIXTEEN,0))
            return EM_SCRIPT_UNSUPPORTED;
        frame->ready=1;
        script->skip_phase=1;
        if (sub==10 || sub==12) script->skip_request=1;
        return EM_SCRIPT_ADVANCE;
    case 4:
        if (input->fade_phase) break;
        frame->ready=1;
        script->skip_phase=1;
        if (sub==10 || sub==12) script->skip_request=1;
        return EM_SCRIPT_ADVANCE;
    default:break;
    }
    return EM_SCRIPT_WAIT;
}
