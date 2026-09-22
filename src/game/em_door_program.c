#include "game/em_door_program.h"
#include "game/em_effect_color.h"

#include <string.h>

static void store(unsigned char *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (unsigned char)(value >> (i*8));
}

static unsigned char *resolve(void *context, uint32_t address)
{
    EmDoorProgram *program = context;
    return em_script_image_read(&program->image, address, 64);
}

EmScriptCommandResult em_door_program_command(EmDoorProgram *p, EmScript *script,
                                               unsigned char record[64])
{
    if (!p || !p->owner || !script || !record || p->failed) return EM_SCRIPT_UNSUPPORTED;
    EmDoorProgramHooks *hooks = &p->hooks;
    unsigned opcode = em_script_u32(record, 0) & 0xfff;
    unsigned sub = em_script_u32(record, 8);
    switch (opcode) {
    case 2: { /*001B9BA0: seed, decrement, then complete on a later callback.*/
        float timer = em_script_f32(record, 0x10);
        if (!(uint8_t)script->phase) {
            memcpy(record + 0x10, record + 0x0c, 4);
            script->phase = (script->phase & ~255) | 1;
        } else if (timer <= 0) {
            return EM_SCRIPT_ADVANCE;
        } else {
            timer = em_effect_float32((double)timer - 1);
            memcpy(record + 0x10, &timer, 4);
        }
        return EM_SCRIPT_WAIT;
    }
    case 7:
        if ((sub != 0 && sub != 4) || !hooks->frame) return EM_SCRIPT_UNSUPPORTED;
        return hooks->frame(hooks->context, script, record);
    case 10:
        if (sub != 0 || !hooks->player_animation ||
            hooks->player_animation(hooks->context, (uint16_t)em_script_u32(record, 0x14),
                                     1, em_script_f32(record, 0xc)) != 1)
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    case 11: /*001B8020/sub6: sound precedes clip initialization.*/
        if (sub != 6 || !hooks->sound || !hooks->object_animation)
            return EM_SCRIPT_UNSUPPORTED;
        if (hooks->sound(hooks->context, em_script_u32(record, 0x18), 300) != 1 ||
            hooks->object_animation(hooks->context, (uint16_t)em_script_u32(record, 0x14),
                                     em_script_f32(record, 0xc), 0) != 1)
            return EM_SCRIPT_UNSUPPORTED;
        p->owner->animation_flags = 0;
        return EM_SCRIPT_ADVANCE;
    case 13:
        if (sub != 5 || !hooks->camera_chase || hooks->camera_chase(hooks->context) != 1)
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    default:
        return EM_SCRIPT_UNSUPPORTED;
    }
}

static EmScriptCommandResult execute(void *context, EmScript *script, unsigned char *record)
{
    return em_door_program_command(context, script, record);
}

int em_door_program_load(EmDoorProgram *p, const char *path,
    EmDoorOriginal *owner, const EmDoorProgramHooks *hooks)
{
    if (!p || !path || !owner || !hooks) return 0;
    memset(p, 0, sizeof *p);
    if (!em_script_image_load(&p->image, path) || p->image.base != 0x24dbc0 ||
        p->image.entry != 0x24de40 || p->image.length != 0x3c0) {
        em_door_program_free(p);
        return 0;
    }
    p->owner = owner;
    p->hooks = *hooks;
    return 1;
}

void em_door_program_free(EmDoorProgram *p)
{
    if (!p) return;
    em_script_image_free(&p->image);
    memset(p, 0, sizeof *p);
}

int em_door_program_patch(EmDoorProgram *p, const EmDoorTransitPlan *plan)
{
    if (!p || !p->image.bytes || p->failed || !plan || plan->locked ||
        plan->script_entry != 0x24de40 || plan->side > 1 ||
        plan->player_clip != (plan->side ? 0x43 : 0x45) ||
        plan->door_clip != (plan->side ? 0 : 2) ||
        plan->wait_ticks != (plan->side ? 70 : 90))
        return 0;
    uint32_t wait;
    memcpy(&wait, &plan->wait_ticks, 4);
    store(p->image.bytes + 0x24dc14 - p->image.base, plan->player_clip);
    store(p->image.bytes + 0x24dc54 - p->image.base, plan->door_clip);
    store(p->image.bytes + 0x24dc58 - p->image.base, plan->sound);
    store(p->image.bytes + 0x24dc8c - p->image.base, wait);
    return 1;
}

int em_door_program_start(EmDoorProgram *p, uint32_t entry)
{
    if (!p || !p->image.bytes || p->failed || (entry != 0x24de40 && entry != 0x24dbc0))
        return 0;
    em_script_start(&p->script, entry);
    p->owner->animation_active = 0;
    return 1;
}

int em_door_program_tick(EmDoorProgram *p)
{
    if (!p || !p->image.bytes || p->failed) return -1;
    /* BC0E0 reads exactly the same actor+1FC byte as the sequencer's skip
     * phase. Frame preparation sets1; completion/skip can change it again. */
    p->script.skip_phase = p->owner->animation_active;
    EmScriptResult result = em_script_tick(&p->script, resolve, execute, p);
    p->owner->animation_active = p->script.skip_phase;
    if (result == EM_SCRIPT_FAULT || result == EM_SCRIPT_ABORTED) {
        p->failed = 1;
        return -1;
    }
    return result == EM_SCRIPT_FINISHED;
}
