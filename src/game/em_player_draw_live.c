/* em_player_draw_live.c - see em_player_draw_live.h and docs/OWNER_DRAW.md
 * section 10. Nothing here computes: 001CAA00 is em_owner_draw_live's. */
#include "game/em_player_draw_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_owner_draw_live.h"
#include "game/em_player.h"
#include "game/em_scene_bindings.h"

#define METHOD_001CAA00 UINT32_C(0x001CAA00)
#define NODE_BYTES 0xD0u      /* a node record (001AF780's slot) */

static struct {
    EmWorldModels bank;       /* the one model, a table-less bank */
    uint8_t *file;
    int loaded, tried;
    EmOwnerBone bone[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerServicesOwner view;
} P;

static int report(uint32_t address, const char *what)
{
    fprintf(stderr, "em_player_draw_live: %s (%08X)\n", what, (unsigned)address);
    return -1;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const EmWorldModels *em_player_draw_live_bank(void)
{
    if (P.loaded) return &P.bank;
    if (P.tried) return NULL;
    P.tried = 1;
    FILE *f = fopen(EM_PLAYER_DRAW_LIVE_MODEL, "rb");
    if (!f) {
        report(0x0028A57Cu, "no " EM_PLAYER_DRAW_LIVE_MODEL " (run tools/export_player_model.py)");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size > 0 ? malloc((size_t)size) : NULL;
    const int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (!ok || em_object_model_parse(&P.bank, data, (size_t)size) < 0) {
        free(data);
        report(0x0028A57Cu, EM_PLAYER_DRAW_LIVE_MODEL " is not a valid model export");
        return NULL;
    }
    P.file = data;
    P.loaded = 1;
    return &P.bank;
}

void em_player_draw_live_unload(void)
{
    free(P.file);
    memset(&P, 0, sizeof P);
}

int em_player_draw_live_001C6150(uint32_t handle, uint8_t *count)
{
    const EmWorldModels *bank = em_player_draw_live_bank();
    if (!bank || !count) return -1;
    const EmWorldModel *m = em_world_models_at(bank, handle);
    if (!m) return report(0x001C6150u, "the player's model handle is not the exported model");
    EmOwnerServices s;
    memset(&s, 0, sizeof s);
    return em_owner_services_001C6150(&s, &m->model, count) < 0 ? report(0x001C6150u, "001C6150 faulted") : 0;
}

int em_player_draw_live_001CAA00(void)
{
    const EmWorldModels *bank = em_player_draw_live_bank();
    const EmPlayerLiveActor *p = player_states_actor();
    if (!bank) return -1;
    if (!p) return report(EM_PLAYER_DRAW_LIVE_RECORD, "no player record");
    const uint8_t *r = p->bytes;
    if (rd32(r + 0x4C) != METHOD_001CAA00)
        return report(rd32(r + 0x4C), "the player's +0x4C is not 001CAA00 (another draw method is not bound)");
    const EmWorldModel *m = em_world_models_at(bank, rd32(r + 0x44));
    if (!m) return report(rd32(r + 0x44), "the player's +0x44 is not the exported model (0015C1F0's bind)");
    const uint8_t count = r[0x0C];
    if (count != m->model.bone_count || count > EM_OWNER_SERVICES_MAX_BONES)
        return report(EM_PLAYER_DRAW_LIVE_RECORD + 0x0C, "the player's node count is not its model's");

    EmOwnerServicesOwner *v = &P.view;
    memset(v, 0, sizeof *v);
    v->drawn = r[0x01];
    v->cls = r[0x02];
    v->kind = r[0x03];
    v->bone_count = count;
    v->model_id = r[0x0D];
    v->model = &m->model;                       /* +0x44 */
    memcpy(&v->attachment, r + 0x90, 4);
    memcpy(&v->collapsed_bone, r + 0x94, 2);
    v->pose_bone = r[0x98];
    memcpy(v->pos, r + 0xB0, sizeof v->pos);
    /* D_00275B40 = player + 0x110 (001CB590(player)): the node records'
     * +0x90..+0xCF world matrices. */
    for (unsigned k = 0; k < count; ++k) {
        const uint32_t node = rd32(r + 0x110 + 4u * k);
        const uint8_t *n = player_pose_record_bytes(node, NODE_BYTES);
        if (!n) return report(node, "a player node record is not mapped");
        memcpy(P.bone[k].world, n + 0x90, sizeof P.bone[k].world);
        v->bone[k] = &P.bone[k];
    }
    uint32_t rgb[4];
    memcpy(rgb, r + 0x80, sizeof rgb);          /* +0x80..+0x8F */
    return em_owner_draw_live_001CAA00(bank, v, rgb, EM_PLAYER_DRAW_LIVE_RECORD);
}

int em_player_draw_live_node_world(unsigned node, float out16[16])
{
    const EmPlayerLiveActor *p = player_states_actor();
    if (!out16 || !p || node >= p->bytes[0x0C] || node >= EM_OWNER_SERVICES_MAX_BONES ||
        !em_scene_bindings_player_record_drawn())
        return 0;
    const uint8_t *n = player_pose_record_bytes(rd32(p->bytes + 0x110 + 4u * node) + 0x90u, 0x40);
    if (!n) return 0;
    memcpy(out16, n, 0x40);
    return 1;
}
