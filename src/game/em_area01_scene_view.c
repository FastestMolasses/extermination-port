#include "game/em_area01_scene_view.h"
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Original scene byte views require a little-endian host"
#endif
_Static_assert(offsetof(EmSceneState,d810702)==offsetof(EmSceneState,d810700)+2,"area byte layout");
_Static_assert(offsetof(EmSceneState,spad3B93)==offsetof(EmSceneState,spad3B8C)+7,"scratch byte layout");
uint8_t *em_area01_scene_view(EmSceneState *s, uint32_t a, uint32_t n)
{
    if (!s || !n || (uint64_t)a+n > UINT64_C(0x100000000)) return NULL;
#define FIELD(base, member, count) do { \
    if (a >= (base) && n <= (count) && a-(base) <= (count)-n) \
        return (uint8_t *)&s->member + (a-(base)); \
} while (0)
    FIELD(0x00810040u,d810040,0xD4u);
    FIELD(EM_SCENE_REQ_BASE,req,EM_SCENE_REQ_SIZE);
    FIELD(0x00810700u,d810700,3u);
    FIELD(0x00810730u,d810730,0x20u);
    FIELD(0x00810750u,d810750,4u);
    FIELD(0x00810E50u,d810E50,1u);
    FIELD(0x00810E70u,d810E70,2u);
    FIELD(0x00810E74u,d810E74,2u);
    FIELD(0x00275BD8u,d275BD8,1u);
    FIELD(0x00275BDCu,d275BDC,1u);
    FIELD(0x00275BE0u,d275BE0,1u);
    FIELD(0x700031F4u,spad31F4,4u);
    FIELD(0x70003258u,spad3258,4u);
    FIELD(0x70003B40u,spad3B40,32u);
    FIELD(0x70003B68u,spad3B68,4u);
    FIELD(0x70003B84u,spad3B84,2u);
    FIELD(0x70003B8Au,spad3B8A,2u);
    FIELD(0x70003B8Cu,spad3B8C,8u);
#undef FIELD
    return em_scene_progress_at(s,a,n);
}
