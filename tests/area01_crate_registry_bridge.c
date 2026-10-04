/* Production binding under test, without adding game-only probe entrypoints. */
#include "game/em_area11_boxes.c"

static EmSceneState registry_scene;
static Box registry_box;

void cr_registry_bind(EmArea11BoxesRegistryView view,void *ctx)
{ em_area11_boxes_bind_registry(view,ctx); }
const EmCrateRegistry *cr_registry_view(void)
{ return S.registry.resolve ? &S.registry : NULL; }
uint32_t cr_registry_fault(void)
{ return em_area11_boxes_registry_fault(); }
void cr_registry_reset(void)
{ em_area11_boxes_reset(); }
void cr_registry_progress(uint8_t area,const uint8_t *bytes)
{
    memset(&registry_scene,0,sizeof registry_scene);
    registry_scene.d810700=area;
    memcpy(em_scene_progress_spawn_view(&registry_scene),bytes,sizeof(EmActorRosterProgress));
    registry_box.scene=&registry_scene;
}
int cr_registry_taken(uint8_t puid)
{ return h_taken(&registry_box,puid); }
int cr_registry_tick(EmCrateOriginal *crate,EmCrateInput *input,const EmCrateOriginalHooks *hooks)
{
    input->registry=cr_registry_view();
    return em_crate_original_tick(crate,input,hooks);
}
const uint8_t *cr_registry_group(uint8_t area,int16_t link)
{ return S.registry.resolve ? S.registry.resolve(S.registry.ctx,area,link) : NULL; }

/* Reset binds the existing service table but this test never invokes its
 * library-model boundary. A reached call must abort rather than fake it. */
int em_area11_roger_001C6120(uint32_t bank,uint32_t id,uint32_t *handle)
{ (void)bank;(void)id;(void)handle;abort(); }
const uint8_t *em_area11_roger_resource(uint32_t address,uint32_t size)
{ (void)address;(void)size;abort(); }
