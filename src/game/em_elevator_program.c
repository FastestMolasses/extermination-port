#include "game/em_elevator_program.h"
#include <string.h>

static void vector(float out[3], const unsigned char *record, unsigned offset)
{
    for (unsigned i = 0; i < 3; ++i) out[i] = em_script_f32(record, offset+i*4);
}

static unsigned char *resolve(void *context, uint32_t address)
{
    EmElevatorProgram *program = context;
    return em_script_image_read(&program->image, address, 64);
}

static EmScriptCommandResult execute(void *context, EmScript *script,
                                      unsigned char *record)
{
    EmElevatorProgram *program = context;
    EmElevatorProgramHooks *hooks = &program->hooks;
    void *host = hooks->context;
    uint32_t opcode = em_script_u32(record, 0) & 0xfff;
    uint32_t sub = em_script_u32(record, 8);
    switch (opcode) {
    case 0: { /*001B8FC0 sub0: copy, publish, yield, then finish.*/
        if (sub != 0 || em_script_f32(record, 0xc) != 0 ||
            !hooks->camera_set || !hooks->camera_publish)
            return EM_SCRIPT_UNSUPPORTED;
        if (script->phase == 0) {
            float eye[3], target[3];
            vector(eye, record, 0x20); vector(target, record, 0x30);
            if (!hooks->camera_set(host, eye, target)) return EM_SCRIPT_UNSUPPORTED;
            memset(record+0x10, 0, 4);
            script->phase = 1;
            if (!hooks->camera_publish(host)) return EM_SCRIPT_UNSUPPORTED;
            return EM_SCRIPT_WAIT;
        }
        if (script->phase != 1 || !hooks->camera_publish(host))
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    }
    case 1: { /*001B94F0 sub1 immediately aligns all player positions.*/
        if (sub != 1 || !hooks->align_player) return EM_SCRIPT_UNSUPPORTED;
        float position[3]; vector(position, record, 0x30);
        return hooks->align_player(host, position) ? EM_SCRIPT_ADVANCE : EM_SCRIPT_UNSUPPORTED;
    }
    case 4:
        if (sub != 8 || !hooks->face_player ||
            !hooks->face_player(host, em_script_f32(record, 0x24)))
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    case 7:
        if ((sub != 2 && sub != 4) || !hooks->frame) return EM_SCRIPT_UNSUPPORTED;
        return hooks->frame(host, script, record);
    case 9:
        if (em_script_u32(record, 4) != 0x828050 || !hooks->move)
            return EM_SCRIPT_UNSUPPORTED;
        return hooks->move(host, script);
    case 10:
        if (sub == 0) {
            if (!hooks->animation_start ||
                !hooks->animation_start(host, (uint16_t)em_script_u32(record, 0x14),
                                        1.0f, em_script_f32(record, 0xc)))
                return EM_SCRIPT_UNSUPPORTED;
            return EM_SCRIPT_ADVANCE;
        }
        if (sub == 3 && hooks->animation_done) {
            int done = hooks->animation_done(host);
            return done < 0 ? EM_SCRIPT_UNSUPPORTED : done ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
        }
        return EM_SCRIPT_UNSUPPORTED;
    case 12:
        if (sub != 0 || !hooks->message_start || !hooks->message_done)
            return EM_SCRIPT_UNSUPPORTED;
        if (script->phase == 0) {
            if (!hooks->message_start(host, em_script_u32(record, 0x14),
                                      em_script_u32(record, 0x18)))
                return EM_SCRIPT_UNSUPPORTED;
            script->phase = 1;
        }
        {
            int done = hooks->message_done(host);
            return done < 0 ? EM_SCRIPT_UNSUPPORTED : done ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
        }
    case 13:
        if (sub != 5 || !hooks->camera_chase || !hooks->camera_chase(host))
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    default: return EM_SCRIPT_UNSUPPORTED;
    }
}

int em_elevator_program_load(EmElevatorProgram *program, const char *path,
                              EmElevator *owner, const EmElevatorProgramHooks *hooks)
{
    if (!program || !owner || !hooks) return -1;
    memset(program, 0, sizeof *program);
    program->owner = owner; program->hooks = *hooks;
    if (!em_script_image_load(&program->image, path) ||
        program->image.base != EM_ELEVATOR_POWERED_SCRIPT || program->image.length != 960) {
        em_elevator_program_free(program);
        return -1;
    }
    return 0;
}

void em_elevator_program_free(EmElevatorProgram *program)
{
    if (!program) return;
    em_script_image_free(&program->image);
    memset(program, 0, sizeof *program);
}

int em_elevator_program_start(EmElevatorProgram *program, uint32_t entry)
{
    if (!program || !program->image.bytes || program->failed ||
        (entry != EM_ELEVATOR_POWERED_SCRIPT && entry != EM_ELEVATOR_REFUSAL_SCRIPT))
        return -1;
    const uint32_t fields[3] = {0x82a7c4, 0x82a844, 0x82a944};
    for (unsigned i = 0; i < 3; ++i) {
        unsigned char *field = em_script_image_read(&program->image, fields[i], 4);
        uint32_t bits;
        memcpy(&bits, &program->owner->script_heights[i], 4);
        for (unsigned byte = 0; byte < 4; ++byte) field[byte] = (unsigned char)(bits >> (byte*8));
    }
    em_script_start(&program->script, entry);
    return 0;
}

int em_elevator_program_tick(EmElevatorProgram *program)
{
    if (!program || !program->image.bytes || program->failed) return -1;
    EmScriptResult result = em_script_tick(&program->script, resolve, execute, program);
    if (result == EM_SCRIPT_FAULT || result == EM_SCRIPT_ABORTED) {
        program->failed = 1;
        return -1;
    }
    return result == EM_SCRIPT_FINISHED;
}
