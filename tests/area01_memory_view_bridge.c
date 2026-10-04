/* Test-only wrapper around the actual shared load/store helpers. */
#include "game/em_area01_render_mem.h"
#include "game/em_area01_ui_internal.h"
#include "game/em_area00_fx_internal.h"

int mv_helper(int family, int op, EmArea01RenderMemory view, void *ctx,
              uint8_t *fallback, uint32_t *fault_out)
{
    EmArea01RenderView region = {0x1000, 32, fallback};
    EmArea01RenderWorld world = {.views=&region, .view_count=1, .view=view, .view_ctx=ctx};
    EmArea01RenderCore core = {.world=world, .function=0xABCDEF};
    EmArea01Ui ui = {.core=core};
    EmArea00Fx fx = {.core=core};
    uint32_t q[4] = {1, 2, 3, 4}, v = 0;
    if (!family) {
        switch (op) {
        case 0: (void)em_a01r_ld8(&core, 0x1000, &v); break;
        case 1: (void)em_a01r_ld32(&core, 0x1000, &v); break;
        case 2: (void)em_a01r_st8(&core, 0x1000, 0x12); break;
        case 3: (void)em_a01r_st16(&core, 0x1000, 0x1234); break;
        case 4: (void)em_a01r_st32(&core, 0x1000, 0x12345678); break;
        case 5: (void)em_a01r_st64(&core, 0x1000, UINT64_C(0x1234567812345678)); break;
        case 6: (void)em_a01r_ldq(&core, 0x100F, q); break;
        case 7: (void)em_a01r_stq(&core, 0x100F, q); break;
        case 8: (void)em_a01r_mem(&core, 0xFFFFFFFF, 2, 1); break;
        case 9: (void)em_a01r_mem(&core, 0x1000, 0, 0); break;
        }
    } else if (family == 1) {
        switch (op) {
        case 0: v=ui_lbu(&ui, 0x1000); break;
        case 1: v=ui_lw(&ui, 0x1000); break;
        case 2: ui_sb(&ui, 0x1000, 0x12); break;
        case 3: ui_sh(&ui, 0x1000, 0x1234); break;
        case 4: ui_sw(&ui, 0x1000, 0x12345678); break;
        case 5: ui_sd(&ui, 0x1000, UINT64_C(0x1234567812345678)); break;
        case 6: ui_ldq(&ui, 0x100F, q); break;
        case 7: ui_stq(&ui, 0x100F, q); break;
        case 8: v=ui_lhu(&ui, 0x1000); break;
        }
        core=ui.core;
    } else {
        switch (op) {
        case 0: v=fx_lbu(&fx, 0x1000); break;
        case 1: v=fx_lw(&fx, 0x1000); break;
        case 2: fx_sb(&fx, 0x1000, 0x12); break;
        case 3: fx_sh(&fx, 0x1000, 0x1234); break;
        case 4: fx_sw(&fx, 0x1000, 0x12345678); break;
        case 5: fx_sd(&fx, 0x1000, UINT64_C(0x1234567812345678)); break;
        case 6: fx_ldq(&fx, 0x100F, q); break;
        case 7: fx_stq(&fx, 0x100F, q); break;
        case 8: v=fx_lhu(&fx, 0x1000); break;
        }
        core=fx.core;
    }
    fault_out[0]=core.fault.code; fault_out[1]=core.fault.address; fault_out[2]=core.fault.detail;
    fault_out[3]=v;
    return core.fault.code ? -1 : 0;
}

#include "game/em_area01_overlay_internal.h"
static uint8_t *overlay_fallback(void *ctx,uint32_t a,uint32_t n)
{ (void)a;(void)n;return ctx; }
int mv_overlay(int op,EmArea01RenderMemory view,void *ctx,uint8_t *fallback,uint32_t *out)
{
    /* ctx passes through the canonical callback. The legacy callback is
     * deliberately usable so a refused view cannot silently fall through. */
    EmArea01OvlHooks h={.ctx=ctx,.bytes=overlay_fallback,.view=view};
    EmArea01OvlFault f={0};A01Ovl o;
    a01_open(&o,&h,&f);
    switch(op) {
    case 0:out[2]=a01_u8(&o,0x1000);break;
    case 1:out[2]=a01_u16(&o,0x1000);break;
    case 2:out[2]=a01_u32(&o,0x1000);break;
    case 3:a01_w8(&o,0x1000,0x55);break; /* unchanged stores remain writes */
    case 4:a01_w16(&o,0x1000,0x5555);break;
    case 5:a01_w32(&o,0x1000,0x55555555);break;
    case 6:a01_w32(&o,0x1000,0x12345678);out[2]=a01_u32(&o,0x1000);break;
    case 7:a01_w32(&o,0xFFFFFFFE,1);break;
    case 8:(void)a01_at(&o,0x1000,0,0);break;
    }
    /* Once latched, no provider or fallback can be consulted. */
    if(f.code)a01_w32(&o,0x1000,0xABCD);
    out[0]=f.code;out[1]=f.address;(void)fallback;
    return f.code?-1:0;
}
