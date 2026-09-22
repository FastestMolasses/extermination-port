#include "game/em_pickup_owner.h"
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
    if (p->lifecycle == 1) {
        if (!p->phase && (p->armed & 4)) {
            if (class4) p->class_flags = 0x87;
            p->phase = 1;
            uint16_t clip = 0;
            uint32_t entry;
            if (player_action == 0x2D || no_grab) {
                entry = class4 ? 0x2667E0 : 0x248480;
            } else {
                float low = em_effect_float32((double)player_y + 6.0);
                float high = em_effect_float32((double)low + (class4 ? 6.0 : 7.0));
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
