#include "game/em_pickup_owner.h"
#include "game/em_ee_float.h"
#include "game/em_effect_color.h"
#include <math.h>

static int event(const EmPickupOwnerHooks *hooks, EmPickupOwnerEvent kind,
                 uint32_t argument)
{
    return hooks->event(hooks->context, kind, argument);
}

static int publish(const EmPickupOwnerHooks *hooks)
{
    int visible = event(hooks, EM_PICKUP_OWNER_PUBLISH, 0);
    if (visible < 0 || visible > 1) return -1;
    if (visible && event(hooks, EM_PICKUP_OWNER_DRAW, 0) != 1) return -1;
    return 1;
}

static int pickup_step(EmPickupOwner *p, float item_y, float player_y,
                         uint8_t player_action, uint8_t no_grab,
                         uint8_t scripted_frame, const EmPickupOwnerHooks *h, int class4)
{
    if (!class4 || p->lifecycle == 1) {
        if (!p->phase && (p->armed & 4)) {
            if (class4) p->class_flags = 0x87;
            p->phase = 1;
            uint16_t clip = 0;
            uint32_t entry;
            if (player_action == 0x2D || no_grab) {
                entry = class4 ? 0x2667E0 : 0x248480;
            } else {
                /* 0015AE20's two add.s (em_ee_float.h, the measured EE model). */
                float low = em_ee_add(player_y, 6.0f);
                float high = em_ee_add(low, class4 ? 6.0f : 7.0f);
                clip = item_y < low ? 0x42 : item_y < high ? 0x41 : 0x40;
                entry = class4 ? 0x266620 : 0x2482C0;
            }
            if (h->script_start(h->context, entry, clip) != 1) return -1;
        } else if (p->phase == 1) {
            int done = h->script_tick(h->context);
            if (done < 0 || done > 1) return -1;
            if (done) {
                if (class4) {
                    if (event(h, EM_PICKUP_OWNER_TAKE_SOUND, 0x194) != 1 ||
                        event(h, EM_PICKUP_OWNER_PERSIST, p->uid) != 1) return -1;
                    p->lifecycle = 3;
                    if (p->has_child) {
                        p->child_status = 3;
                        if (event(h, EM_PICKUP_OWNER_STOP_CHILD, 3) != 1) return -1;
                    }
                    p->class_flags = 4;
                } else ++p->lifecycle;
            }
        }
    }
    if (!class4 && !scripted_frame && event(h, EM_PICKUP_OWNER_AURA, 0) != 1)
        return -1;
    return publish(h);
}

int em_pickup_owner_tick(EmPickupOwner *p, float item_y, float player_y,
                         uint8_t player_action, uint8_t no_grab,
                         uint8_t scripted_frame, const EmPickupOwnerHooks *h)
{
    if (!p || !h || !h->script_start || !h->script_tick || !h->event ||
        !isfinite(item_y) || !isfinite(player_y) ||
        (p->callback != 0x15AFA0 && p->callback != 0x219550)) return -1;
    if (p->freed) return 0;
    if (!p->lifecycle) return -1; /* Original model initialization required. */
    int class4 = p->callback == 0x219550;
    if ((!class4 && p->lifecycle != 1) || (class4 && p->lifecycle > 2)) {
        if (!class4 && event(h, EM_PICKUP_OWNER_PERSIST, p->uid) != 1) return -1;
        if (event(h, EM_PICKUP_OWNER_FREE, 0) != 1) return -1;
        p->freed = 1;
        return 0;
    }
    return pickup_step(p,item_y,player_y,player_action,no_grab,scripted_frame,h,class4);
}

int em_pickup_owner_0015AE20(EmPickupOwner *p, float item_y, float player_y,
                             uint8_t player_action, uint8_t no_grab,
                             uint8_t scripted_frame, const EmPickupOwnerHooks *h)
{
    if (!p || !h || !h->script_start || !h->script_tick || !h->event ||
        !isfinite(item_y) || !isfinite(player_y)) return -1;
    return pickup_step(p,item_y,player_y,player_action,no_grab,scripted_frame,h,0);
}

int em_pickup_owner_0015AC00(EmPickupState0 *r, const EmPickupState0Hooks *h)
{
    if (!r || !h || !h->bind_001B0FD0 || !h->bind_001B1020 || !h->place_001C6380 || !h->aura_001F1110)
        return -1;
    float v;
    switch (r->model) {                       /* +0x0D */
    case 0x5B: v = 1.5f; break;               /* 0x3FC00000 */
    case 0x6D: case 0x6C: case 0x59: case 0x57: case 0x56: case 0x55: case 0x4F: case 0x4E:
    case 0x4D: case 0x45: case 0x42: case 0x41: case 0x40: v = 2.0f; break;   /* 0x40000000 */
    default: v = 1.0f; break;                 /* 0x3F800000 */
    }
    r->scale[2] = v;                          /* +0x68 */
    r->scale[1] = v;                          /* +0x64 */
    r->scale[0] = v;                          /* +0x60 */
    int32_t refused = 0;
    if ((r->subtype & 0xF) == 1) {
        r->color[0] = r->color[1] = r->color[2] = 4.0f;   /* +0x80..+0x88 = 0x40800000 */
        if (h->bind_001B0FD0(h->context, &refused) < 0) return -1;
    } else if (h->bind_001B1020(h->context, r->model, -1, 0, &refused) < 0) {
        return -1;
    }
    if (refused != 0) return 1;
    if (h->place_001C6380(h->context) < 0) return -1;
    r->status = 1;                            /* +0x00 */
    r->mode = 3;                              /* +0x08 */
    int16_t variant;
    switch (r->subtype & 0xF) {
    case 1: variant = 1; break;
    case 2: variant = 4; break;
    case 0: variant = r->model == 0x34 ? 5 : 0; break;
    default: variant = 0; break;
    }
    return h->aura_001F1110(h->context, variant) < 0 ? -1 : 0;
}

int em_pickup_owner_00219550_state0(EmPickupState0 *r, uint8_t item3, const EmPickupState0Hooks *h)
{
    if (!r || !h || !h->bind_001B0FD0 || !h->bind_001B1020 || !h->place_001C6380 || !h->cell_001A2370)
        return -1;
    int32_t result = 0;
    if (r->subtype == 0 && r->flags2 == 0x28) {
        if (h->bind_001B0FD0(h->context, &result) < 0) return -1;
        if (result != 0) return 1;
    } else if (h->bind_001B1020(h->context, r->model, -1, 0, &result) < 0) {
        return -1;
    }
    /* +0x03 == 0 (the original tests it again after the bind; the decomp's
     * NEARMISS C inverts this test), +0x2E == 3 and D_00810C64[+0x2E] held:
     * +0x2E = 0x14. */
    if (r->subtype == 0 && r->flags2 == 3 && item3 != 0) r->flags2 = 0x14;
    if (h->place_001C6380(h->context) < 0) return -1;
    r->status = 1;                            /* +0x00 */
    r->mode = 3;                              /* +0x08 */
    return h->cell_001A2370(h->context) < 0 ? -1 : 0;
}

int em_pickup_owner_take(const EmPickupOwner *p, uint8_t maps[256],
    uint8_t keys[256], EmPickupStatusRequest *request,
    int (*add_item)(void *, uint16_t, int), void *context)
{
    if (!p || !maps || !keys || !request || p->item_type > 255) return -1;
    uint16_t type = p->item_type;
    if (!p->subtype) {
        if (!add_item || add_item(context, type, 1) != 1) return -1;
        request->kind = 1;
        request->index = (uint8_t)type;
    } else if (p->subtype == 1) {
        ++maps[type];
        request->kind = 2;
        request->index = (uint8_t)type;
    } else {
        ++keys[type];
        if (type >= 0x20) {
            request->kind = 3;
            request->index = (uint8_t)type;
        }
    }
    return 1;
}
