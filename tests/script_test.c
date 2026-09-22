#include "game/em_script.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char records[4][EM_SCRIPT_RECORD_SIZE];
    EmScriptCommandResult result[4];
    unsigned calls[4];
} Fixture;

static void put32(unsigned char *p, uint32_t value)
{
    for (unsigned i=0;i<4;++i) p[i]=(unsigned char)(value>>(8*i));
}
static unsigned char *resolve(void *context,uint32_t address)
{
    Fixture *fixture=context;
    if (address<0x100 || address>=0x200 || (address&63)) return NULL;
    return fixture->records[(address-0x100)/64];
}
static EmScriptCommandResult execute(void *context,EmScript *script,
                                    unsigned char record[64])
{
    Fixture *fixture=context;
    unsigned index=(script->pc-0x100)/64;
    assert(record==fixture->records[index]);
    fixture->calls[index]++;
    script->phase++;
    return fixture->result[index];
}

int main(void)
{
    Fixture f={0}; EmScript script;
    em_script_start(&script,0x100);
    assert(script.active==1 && script.phase==0 && script.skip_phase==0);
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_YIELDED);
    assert(script.pc==0x100 && script.phase==1 && f.calls[0]==1);
    f.result[0]=EM_SCRIPT_ADVANCE;
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_YIELDED);
    assert(script.pc==0x140 && script.phase==0 && f.calls[1]==0);

    /* A chain of same-frame commands reaches the next blocking handler. */
    em_script_start(&script,0x100); memset(f.calls,0,sizeof f.calls);
    put32(f.records[0],0x2000000A); f.result[0]=EM_SCRIPT_ADVANCE;
    f.result[1]=EM_SCRIPT_CONTINUE; f.result[2]=EM_SCRIPT_WAIT;
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_YIELDED);
    assert(script.pc==0x180 && script.phase==1);
    assert(f.calls[0]==1 && f.calls[1]==1 && f.calls[2]==1);

    /* The STOP flag is tested only after its handler completes. */
    put32(f.records[2],0x80000007);
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_YIELDED);
    f.result[2]=EM_SCRIPT_ADVANCE;
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_FINISHED);
    assert(script.active==-1 && script.phase==0 && script.pc==0x180);

    /* JUMP has precedence over bit29: an ADVANCE jump still yields. */
    em_script_start(&script,0x100); memset(f.calls,0,sizeof f.calls);
    put32(f.records[0],0x60000009); put32(f.records[0]+4,0x1C0);
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_YIELDED);
    assert(script.pc==0x1C0 && !f.calls[3]);
    f.result[3]=EM_SCRIPT_ABORT;
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_ABORTED);
    assert(script.active==-1 && script.phase==0);

    em_script_start(&script,0x1C0); f.result[3]=EM_SCRIPT_UNSUPPORTED;
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_FAULT);
    assert(script.active==1); /* failure never silently completes */
    em_script_start(&script,0x200);
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_FAULT);
    em_script_start(&script,0x100); f.result[0]=EM_SCRIPT_CONTINUE;
    put32(f.records[0]+4,0x100); /* malformed endless jump */
    assert(em_script_tick(&script,resolve,execute,&f)==EM_SCRIPT_FAULT);
    puts("script_test: PASS");
    return 0;
}
