/* The production resource adapter over the user's original module. This
 * fixture proves aliasing/selection, not native arrival gameplay. */
#include "game/em_area01_script_live.h"
#include "game/em_area11_boxes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static EmAreaScriptHostArea area;
static EmStatusSceneLoader loader;
static uint8_t *module, *replacement, *delivered;
static size_t module_size;
static EmArea01Call last;
static unsigned calls;
static int reject, missing;

int em_area11_script_host_area(const EmAreaScriptHostArea *a) { area=*a;return 0; }
uint8_t *em_area11_script_host_timeline_bytes(uint32_t a,uint32_t n)
{ (void)a;(void)n;return NULL; }
EmStatusSceneLoader *em_module_loader_state(EmModuleLoader *l) { (void)l;return &loader; }
const uint8_t *em_module_loader_memory(const EmModuleLoader *l,uint32_t a,uint32_t n)
{
    (void)l;
    return !missing && n && a>=0x823500u && a-0x823500u<module_size && n<=module_size-(a-0x823500u)
        ? delivered+(a-0x823500u) : NULL;
}
uint8_t *em_module_loader_memory_mutable(EmModuleLoader *l,uint32_t a,uint32_t n)
{ return (uint8_t *)(void *)em_module_loader_memory(l,a,n); }
const uint8_t *em_module_loader_memory_rest(const EmModuleLoader *l,uint32_t a,uint32_t *n)
{ (void)l;(void)a;(void)n;return NULL; }
EmOwnerServicesOwner *em_area11_boxes_owner_view(EmActor *a) { (void)a;return NULL; }
static int worker(void *ctx,EmArea01Call *c)
{ assert(ctx==&last);last=*c;++calls;c->v0=7;return reject ? -1 : 0; }
static void w32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fseek(f,0,SEEK_END)==0);long length=ftell(f);assert(length==0x9F80);
    module_size=(size_t)length;module=malloc(module_size);replacement=malloc(module_size);
    assert(module && replacement && fseek(f,0,SEEK_SET)==0);
    assert(fread(module,1,module_size,f)==module_size && fclose(f)==0);
    assert(!memcmp(module,"MWo3",4) && em_script_u32(module,4)==1);
    memcpy(replacement,module,module_size);delivered=module;
    EmArea01Script s={0};EmArea01RuntimeHost host={.ctx=&last,.worker=worker};
    EmModuleLoader *ld=(EmModuleLoader *)(void *)&loader;
    assert(em_area01_script_bind_area(&s,&host,ld,NULL,NULL,2)<0 && !s.bound);
    assert(em_area01_script_bind_area(&s,&host,ld,NULL,NULL,0)==0);
    assert(area.overlay_id==1 && s.area==0);
    assert(em_area01_script_bind_area(&s,&host,ld,NULL,NULL,1)<0);
    EmScriptImage *im=area.image(area.ctx,0x828D60u);
    assert(im && im==&s.overlay && im->base==0x826F80u && im->entry==0x8284E0u && im->length==0x6500u);
    assert(im->bytes==module+0x3A80u);
    uint8_t *p=em_script_image_read(im,0x828FA0u,64);assert(p==module+(0x828FA0u-0x823500u));
    assert((em_script_u32(p,0)&0xFFFu)==9 && em_script_u32(p,4)==0x824E40u);
    w32(p+0x10,0x12345678u);
    assert(em_script_u32(area.resource(area.ctx,0x828FB0u,4),0)==0x12345678u);
    assert(area.image(area.ctx,0x24DE40u)==&s.boot[1]);
    assert(!area.image(area.ctx,0x8284DFu) && !area.image(area.ctx,0x82BB11u));
    assert(area.image(area.ctx,0x82BB10u));
    assert(!area.image(area.ctx,0xFFFFFFFFu) && !area.image(area.ctx,0x82D070u));
    delivered=replacement;
    assert(area.image(area.ctx,0x828D60u)->bytes==replacement+0x3A80u);
    assert(em_script_u32(area.resource(area.ctx,0x828FB0u,4),0)!=0x12345678u);
    w32(replacement+4,2);assert(!area.image(area.ctx,0x829860u));w32(replacement+4,1);
    w32(replacement+8,0x823540u);assert(!area.image(area.ctx,0x828D60u));w32(replacement+8,0x823500u);
    replacement[0]^=1;assert(!area.image(area.ctx,0x828D60u));replacement[0]^=1;
    missing=1;assert(!area.image(area.ctx,0x828D60u));missing=0;
    int32_t result=0;
    const uint32_t accepted[]={0x824E40u,0x8253E0u,0x1BAC00u,0x1B7670u,0x1B7700u};
    for(unsigned i=0;i<sizeof accepted/sizeof accepted[0];++i) {
        assert(area.record(area.ctx,accepted[i],0x7A8250u,0x7A8440u,0x828FA0u,&result)==0);
        assert(result==7 && last.function==accepted[i] && last.na==3 && last.a[0]==0x7A8250u &&
               last.a[1]==0x7A8440u && last.a[2]==0x828FA0u);
    }
    unsigned before=calls;
    const uint32_t denied[]={0x825130u,0x825240u,0x825910u,0x826950u,0x8260B0u,0xFFFFFFFFu};
    for(unsigned i=0;i<sizeof denied/sizeof denied[0];++i)
        assert(area.record(area.ctx,denied[i],0,0,0x828FA0u,&result)<0 && calls==before);
    assert(area.record(area.ctx,0x824E40u,0,0,0x82BB50u,&result)<0 && calls==before);
    assert(area.record(area.ctx,0x824E40u,0,0,0x828FA0u,NULL)<0 && calls==before);
    reject=1;result=123;
    assert(area.record(area.ctx,0x824E40u,0,0,0x828FA0u,&result)<0 && result==123);
    em_area01_script_free(&s);assert(!s.bound && !s.overlay.bytes && !s.boot[0].bytes);
    free(module);free(replacement);
    puts("PASS: AREA00 original script window, borrowed mutable data, replacement delivery, module identity, callback separation and failures");
    return 0;
}
