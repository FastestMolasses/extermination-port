#include "game/em_pickup_program.h"
#include <string.h>

static unsigned char *resolve(void *context, uint32_t address)
{
    EmPickupProgram *p = context;
    return em_script_image_read(&p->image, address, 64);
}

static EmScriptCommandResult waiting(int result)
{
    return result == 0 ? EM_SCRIPT_WAIT : result == 1 ? EM_SCRIPT_ADVANCE :
           EM_SCRIPT_UNSUPPORTED;
}

static EmScriptCommandResult execute(void *context, EmScript *script,
                                     unsigned char *record)
{
    EmPickupProgram *p = context;
    EmPickupProgramHooks *h = &p->hooks;
    unsigned op = em_script_u32(record, 0) & 0xFFF;
    unsigned sub = em_script_u32(record, 8);
    switch (op) {
    case 7:
        if (sub != 13 && sub != 4) break;
        return h->frame(h->context, script, record);
    case 14:
        if (sub != 1) break;
        return waiting(h->turn(h->context, em_script_f32(record, 0x24)));
    case 10:
        if (sub == 3) return waiting(h->animation_done(h->context));
        if (!sub) return h->animation(h->context, (uint16_t)em_script_u32(record, 0x14),
                                     1.0f, em_script_f32(record, 0xC)) == 1 ?
                                     EM_SCRIPT_ADVANCE : EM_SCRIPT_UNSUPPORTED;
        break;
    case 0:
        if (sub != 8) break;
        return waiting(h->camera(h->context, script));
    case 9:
        if (em_script_u32(record, 4) != 0x1B6EA0) break;
        return h->take(h->context) == 1 ? EM_SCRIPT_ADVANCE : EM_SCRIPT_UNSUPPORTED;
    }
    return EM_SCRIPT_UNSUPPORTED;
}

int em_pickup_program_load(EmPickupProgram *p, const char *path, uint32_t callback,
                            const EmPickupProgramHooks *h)
{
    if (!p || !path || !h || !h->frame || !h->turn || !h->camera ||
        !h->animation || !h->animation_done || !h->take ||
        (callback != 0x15AFA0 && callback != 0x219550)) return 0;
    memset(p, 0, sizeof *p);
    uint32_t base = callback == 0x219550 ? 0x266620 : 0x2482C0;
    if (!em_script_image_load(&p->image, path) || p->image.base != base ||
        p->image.entry != base || p->image.length != 0x280) goto fail;
    /* Validate the complete original programs before any world mutation. */
    static const uint32_t ops[10] = {7,14,10,0,10,9,0x80000007,7,9,0x80000007};
    static const uint32_t subs[10] = {13,1,0,8,3,0,4,13,0,4};
    for (unsigned i=0; i<10; ++i) {
        const unsigned char *record = p->image.bytes + i*64;
        if (em_script_u32(record, 0) != ops[i] || em_script_u32(record, 8) != subs[i])
            goto fail;
        if ((i == 5 || i == 8) && em_script_u32(record, 4) != 0x1B6EA0) goto fail;
    }
    if (em_script_u32(p->image.bytes+64,0x24) != 0x3E32B8C3 ||
        em_script_u32(p->image.bytes+128,0xC) != 0x3F800000 ||
        em_script_u32(p->image.bytes+128,0x14) != 0x42 ||
        em_script_u32(p->image.bytes,0x14) ||
        em_script_u32(p->image.bytes+7*64,0x14)) goto fail;
    p->hooks = *h;
    return 1;
fail:
    em_pickup_program_free(p);
    return 0;
}

void em_pickup_program_free(EmPickupProgram *p)
{
    if (!p) return;
    em_script_image_free(&p->image);
    memset(p, 0, sizeof *p);
}

int em_pickup_program_start(EmPickupProgram *p, uint32_t entry, uint16_t clip)
{
    if (!p || !p->image.bytes || p->failed ||
        (entry != p->image.base && entry != p->image.base+0x1C0) ||
        (entry == p->image.base ? clip < 0x40 || clip > 0x42 : clip != 0)) return 0;
    if (clip) {
        unsigned char *field = p->image.bytes + 0x94;
        for (unsigned i=0; i<4; ++i) field[i] = (unsigned char)((uint32_t)clip >> (i*8));
    }
    em_script_start(&p->script, entry);
    return 1;
}

int em_pickup_program_tick(EmPickupProgram *p)
{
    if (!p || !p->image.bytes || p->failed) return -1;
    EmScriptResult result = em_script_tick(&p->script, resolve, execute, p);
    if (result == EM_SCRIPT_FAULT || result == EM_SCRIPT_ABORTED) {
        p->failed = 1;
        return -1;
    }
    return result == EM_SCRIPT_FINISHED;
}
