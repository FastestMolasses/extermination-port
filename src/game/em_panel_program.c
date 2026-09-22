#include "game/em_panel_program.h"
#include "game/em_effect_color.h"
#include <string.h>

static void put32(unsigned char *p,uint32_t value)
{for (unsigned i=0;i<4;i++) p[i]=(unsigned char)(value>>(i*8));}

static unsigned char *resolve(void *context,uint32_t address)
{
    EmPanelProgram *p=context;
    return em_script_image_read(&p->image,address,64);
}

static EmScriptCommandResult execute(void *context,EmScript *script,
                                     unsigned char *record)
{
    EmPanelProgram *p=context;
    EmPanelProgramHooks *h=&p->hooks;void *host=h->context;
    uint32_t op=em_script_u32(record,0)&0xFFF,sub=em_script_u32(record,8);
    switch (op) {
    case 2: { /*001B9BA0: first-call seed, decrement, later completion. */
        float timer=em_script_f32(record,0x10);
        if (!script->phase) {
            put32(record+0x10,em_script_u32(record,0xC));script->phase=1;
        } else if (timer<=0) return EM_SCRIPT_ADVANCE;
        else {
            timer=em_effect_float32((double)timer-1.0);
            uint32_t bits;memcpy(&bits,&timer,4);put32(record+0x10,bits);
        }
        return EM_SCRIPT_WAIT;
    }
    case 7:
        if ((sub!=2 && sub!=4) || !h->frame) return EM_SCRIPT_UNSUPPORTED;
        return h->frame(host,script,record);
    case 9: { /*001B99F0 dispatches the original record callback. */
        uint32_t callback=em_script_u32(record,4);
        if (callback==0x157F60) {
            if (!h->battery_open) return EM_SCRIPT_UNSUPPORTED;
            uint8_t request=em_panel_battery_request(p->owner);
            if (!h->battery_open(host,p->owner,request)) return EM_SCRIPT_UNSUPPORTED;
        } else if (callback==0x1575B0) {
            if (!h->sound || !h->sound(host,0x3EF)) return EM_SCRIPT_UNSUPPORTED;
        } else if (callback==0x1580C0) {
            if (!h->power || !h->sound || !h->power(host,0x80) ||
                !h->sound(host,0x3EE)) return EM_SCRIPT_UNSUPPORTED;
        } else return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    }
    case 10: /*001B9A00 sub0: direct current-bank clip, rate1,blend+C. */
        if (sub!=0 || !h->player_animation ||
            !h->player_animation(host,(uint16_t)em_script_u32(record,0x14),
                                 1.0f,em_script_f32(record,0xC)))
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    case 12: /*001B7D60 sub0: issue then wait on the real message worker. */
        if (sub!=0 || !h->message_start || !h->message_done) return EM_SCRIPT_UNSUPPORTED;
        if (!script->phase) {
            if (!h->message_start(host,em_script_u32(record,0x14),
                                   em_script_u32(record,0x18))) return EM_SCRIPT_UNSUPPORTED;
            script->phase=1;
        }
        {
            int done=h->message_done(host);
            return done<0 ? EM_SCRIPT_UNSUPPORTED :
                   done>0 ? EM_SCRIPT_ADVANCE : EM_SCRIPT_WAIT;
        }
    case 13:
        if (sub!=3 || !h->camera_retarget || !h->camera_retarget(host))
            return EM_SCRIPT_UNSUPPORTED;
        return EM_SCRIPT_ADVANCE;
    default:return EM_SCRIPT_UNSUPPORTED;
    }
}

int em_panel_program_load(EmPanelProgram *p,const char *path,EmPanel *owner,
                           const EmPanelProgramHooks *hooks)
{
    if (!p || !owner || !hooks) return -1;
    memset(p,0,sizeof *p);p->owner=owner;p->hooks=*hooks;
    if (!em_script_image_load(&p->image,path) || p->image.base!=0x246F20 ||
        p->image.length!=0xF00) {em_panel_program_free(p);return -1;}
    return 0;
}

void em_panel_program_free(EmPanelProgram *p)
{
    if (!p) return;
    em_script_image_free(&p->image);memset(p,0,sizeof *p);
}

int em_panel_program_start(EmPanelProgram *p,EmPanelScript entry,uint32_t token)
{
    if (!p || !p->image.bytes || p->failed) return -1;
    uint32_t patch=0;
    switch (entry) {
    case EM_PANEL_NO_BATTERY:patch=0x246FB4;break;
    case EM_PANEL_OPEN_BATTERY:patch=0x247834;break;
    case EM_PANEL_POWER_SCRIPT:case EM_PANEL_POWER_AFTER_MENU:case EM_PANEL_CANCEL_SCRIPT:break;
    default:return -1;
    }
    if (patch) put32(em_script_image_read(&p->image,patch,4),token);
    em_script_start(&p->script,(uint32_t)entry);
    return 0;
}

int em_panel_program_tick(EmPanelProgram *p)
{
    if (!p || !p->image.bytes || p->failed) return -1;
    EmScriptResult result=em_script_tick(&p->script,resolve,execute,p);
    if (result==EM_SCRIPT_FAULT || result==EM_SCRIPT_ABORTED) {
        p->failed=1;return -1;
    }
    return result==EM_SCRIPT_FINISHED ? 1 : 0;
}
