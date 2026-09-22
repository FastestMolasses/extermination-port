#include "game/em_script.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

uint32_t em_script_u32(const unsigned char *record, unsigned offset)
{
    const unsigned char *p = record + offset;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

float em_script_f32(const unsigned char *record, unsigned offset)
{
    uint32_t bits = em_script_u32(record,offset);
    float value;
    memcpy(&value,&bits,sizeof value);
    return value;
}

void em_script_start(EmScript *script, uint32_t address)
{
    script->active = 1;
    script->phase = 0;
    script->pc = address;
    script->skip_phase = 0;
    script->skip_request = 0;
}

/* 001BA284..001BA370 and 001BA3EC..001BA4E4. The original enters
 * this scan only while signed skip_phase < 2; an older readable
 * decompilation inverted that branch. It examines STOP on the current
 * record, advances (following JUMP), and stops before executing op18.
 * Only a STOP record encountered after advancing is executed here.
 * The advance path retains its OLD record pointer for the first scan
 * check, even though script->pc has already advanced (001BA410->4A8). */
static EmScriptResult skip_to_end(EmScript *script, unsigned char *record,
                                 EmScriptResolve resolve,
                                 EmScriptExecute execute, void *context)
{
    script->skip_phase=2;
    for (unsigned scanned=0;scanned<256;++scanned) {
        uint32_t flags=em_script_u32(record,0);
        if (flags&UINT32_C(0x80000000)) {
            script->active=-1;
            script->phase=0;
            return EM_SCRIPT_ABORTED;
        }
        if (flags&UINT32_C(0x40000000)) script->pc=em_script_u32(record,4);
        else script->pc+=EM_SCRIPT_RECORD_SIZE;
        record=resolve(context,script->pc);
        if (!record) return EM_SCRIPT_FAULT;
        flags=em_script_u32(record,0);
        if ((flags&0xFFF)==0x18) {
            script->phase=0;
            return EM_SCRIPT_YIELDED;
        }
        if (flags&UINT32_C(0x80000000)) {
            script->phase=0;
            unsigned attempts;
            for (attempts=0;attempts<256;++attempts) {
                EmScriptCommandResult result=execute(context,script,record);
                if (result==EM_SCRIPT_UNSUPPORTED) return EM_SCRIPT_FAULT;
                if (result!=EM_SCRIPT_WAIT) break;
            }
            if (attempts==256) return EM_SCRIPT_FAULT;
        }
    }
    return EM_SCRIPT_FAULT;
}

EmScriptResult em_script_tick(EmScript *script, EmScriptResolve resolve,
                             EmScriptExecute execute, void *context)
{
    if (script->active <= 0) return EM_SCRIPT_FINISHED;
    /* Valid programs yield or finish in a few instructions. A bad
     * exported jump must fail instead of hanging the native main loop.
     * This execution bound is a host validation policy, not a game timer. */
    for (unsigned commands = 0; commands < 256; ++commands) {
        unsigned char *record = resolve(context,script->pc);
        if (!record) return EM_SCRIPT_FAULT;
        EmScriptCommandResult result = execute(context,script,record);
        if (result == EM_SCRIPT_ABORT) {
            script->active = -1;
            script->phase = 0;
            return EM_SCRIPT_ABORTED;
        }
        if (result == EM_SCRIPT_WAIT) {
            if (script->skip_request==2 && script->skip_phase<2)
                return skip_to_end(script,record,resolve,execute,context);
            return EM_SCRIPT_YIELDED;
        }
        if (result != EM_SCRIPT_ADVANCE && result != EM_SCRIPT_CONTINUE)
            return EM_SCRIPT_FAULT;
        uint32_t flags = em_script_u32(record,0);
        if (flags & UINT32_C(0x80000000)) {
            script->active = -1;
            script->phase = 0;
            return EM_SCRIPT_FINISHED;
        }
        if (flags & UINT32_C(0x40000000)) {
            script->pc = em_script_u32(record,4);
        } else {
            script->pc += EM_SCRIPT_RECORD_SIZE;
            if (flags & UINT32_C(0x20000000)) result = EM_SCRIPT_CONTINUE;
        }
        script->phase = 0;
        if (script->skip_request==2 && script->skip_phase<2)
            return skip_to_end(script,record,resolve,execute,context);
        if (result == EM_SCRIPT_ADVANCE) return EM_SCRIPT_YIELDED;
    }
    return EM_SCRIPT_FAULT;
}

int em_script_image_load(EmScriptImage *image, const char *path)
{
    unsigned char header[20];
    EmScriptImage loaded={0};
    FILE *file=fopen(path,"rb");
    if (!file) return 0;
    if (fread(header,1,sizeof header,file)!=sizeof header ||
        memcmp(header,"EMSC",4) || em_script_u32(header,4)!=1) {
        fclose(file); return 0;
    }
    loaded.base=em_script_u32(header,8);
    loaded.entry=em_script_u32(header,12);
    loaded.length=em_script_u32(header,16);
    if (loaded.length<EM_SCRIPT_RECORD_SIZE || loaded.length>0x1000000 ||
        loaded.entry<loaded.base || loaded.entry-loaded.base>loaded.length-64 ||
        loaded.base>UINT32_MAX-loaded.length) {
        fclose(file); return 0;
    }
    loaded.bytes=malloc(loaded.length);
    if (!loaded.bytes) {fclose(file); return 0;}
    if (fread(loaded.bytes,1,loaded.length,file)!=loaded.length || fgetc(file)!=EOF) {
        free(loaded.bytes); fclose(file); return 0;
    }
    fclose(file);
    *image=loaded;
    return 1;
}

void em_script_image_free(EmScriptImage *image)
{
    free(image->bytes);
    memset(image,0,sizeof *image);
}

unsigned char *em_script_image_read(EmScriptImage *image, uint32_t address,
                                    uint32_t length)
{
    if (!image->bytes || address<image->base || length>image->length ||
        address-image->base>image->length-length) return NULL;
    return image->bytes+(address-image->base);
}
