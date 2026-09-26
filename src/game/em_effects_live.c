/* em_effects_live.c - the effect originals bound live (see em_effects_live.h,
 * docs/EFFECT_MANAGER.md section 8).
 *
 * Nothing here computes: every worker below stands for the original callee
 * it names and calls that callee's one translation over the shared storage
 * (the render context of em_render_context_live, the actor pool, the scene
 * state, the player record and Roger's). */
#include "game/em_effects_live.h"

#include "game/em_area11_roger.h"
#include "game/em_collision_world.h"
#include "game/em_effect_kinds.h"
#include "game/em_effect_manager.h"
#include "game/em_effect_original.h"
#include "game/em_frame.h"
#include "game/em_game_internal.h"
#include "game/em_head_sprite_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_packet_chain_original.h"
#include "game/em_player.h"
#include "game/em_player_equipment_sprite.h"
#include "game/em_point_light.h"
#include "game/em_pose_host_workers.h"
#include "game/em_random.h"
#include "game/em_render_context_live.h"
#include "game/em_scene_workers.h"
#include "game/em_sdk_math_original.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;

#define CTX 0x00811CC0u              /* D_00275670's value (EM_RCL_CONTEXT) */
#define ELF_SIZE 1532624u            /* SCUS_971.12 */
#define ELF_AT(a) ((size_t)(a) - 0x00100000u + 0x300u)
#define PLAYER 0x008102B0u           /* D_008102B0 */
#define PLAYER_NODES 21u             /* the player's +0x0C */
/* The stack quadword 001F5C20 builds its marker position in (sp + 0x20 in
 * the original) is not modelled: its address only travels 001F4D40 ->
 * 001CD520, whose `point` is read through this module. This handle names it;
 * it is not an EE address. */
#define MARKER_POSITION_HANDLE 0xFFFFFFF0u

/* ------------------------------------------------------------ storage */

enum { KIND_NONE, KIND_DRIVER, KIND_HEAD, KIND_OTHER };

typedef struct {
    uint32_t generation;
    int kind;
    EmEffectOriginalNode e;     /* 001EF9D0's bytes and 001EA240's */
    EmHeadSpriteOriginal h;     /* 001F0120's and 001E2560's */
} Slot;

static struct {
    int loaded, load_tried;
    uint8_t *elf;                             /* the sparse image the loaders read */
    EmEffectOriginalTables etables;
    EmEffectManagerTables mtables;
    EmEffectKindsTables ktables;
    EmHeadSpriteOriginalTables htables;
    uint8_t sources[0x480];                   /* D_002565E0..D_00256A60 */

    EmActorPool *pool;
    EmSceneState *scene;
    EmEffectsLiveBind bind;
    int attached;
    uint32_t fault;

    Slot slot[EM_ACTOR_POOL_CAPACITY];

    /* em_effect_original */
    EmEffectOriginal e;
    EmEffectOriginalGlobals eglobals;
    EmEffectOriginalView eview;
    EmEffectOriginalWorkers eworkers;
    EmEffectOriginalDecals decals;            /* D_0028F700 + 0x4DBEC0, D_0081F950 */
    /* em_effect_kinds */
    EmEffectKinds k;
    EmEffectKindsGlobals kglobals;
    EmEffectKindsWorkers kworkers;
    EmEffectKindsParticles particles;         /* D_007709C0 */
    EmEffectKindsXfState xs;
    EmEffectKindsXf xf;                       /* D_0081F8F0 */
    /* em_effect_manager */
    EmEffectManager m;
    EmEffectManagerGlobals mglobals;
    EmEffectManagerView mview;
    EmEffectManagerWorkers mworkers;
    EmEffectManagerEntity entities[EM_EFFECT_MANAGER_ENTITIES];
    /* em_head_sprite_original */
    EmHeadSpriteOriginalWorkers hworkers;
    EmHeadSpriteOriginalWorld hworld;
    EmHeadSpriteOriginalFault hfault;
    /* em_player_equipment_sprite (001CD520) */
    EmPlayerEquipmentSprite sprite;
    EmPlayerEquipmentSpriteWorld sworld;
    EmPlayerEquipmentSpriteWorkers sworkers;
    EmPlayerEquipmentFault sfault;
    uint32_t spad3600_sprite[8];
    /* 001F5C20's marker position, handed through 001F4D40 to 001CD520 */
    uint32_t marker[4];
    int marker_set;
    /* the current head sprite's owner (its 001026A0 matrix read) */
    uint32_t head_owner;
    /* records 001AFA90 handed out during the current entry, bound to their
     * behaviour once 001EF9D0 (and 001F0120) have written them */
    Slot *pending[8];
    int npending;

    EmEffectsLiveCounters counters;
    /* The last barrel's 001F0720 packets (the capture log's digests). */
    int recording;
    const uint8_t *pk[24];
    uint32_t pk_size[24];
    int npk;
    uint32_t digest[EM_EFFECTS_LIVE_LANE_DIGESTS];
    const uint8_t *sp[16];
    int nsp;
    uint32_t sprite_digest[16];
    int nsprite_digest;
} S;

/* ------------------------------------------------------------ faults */

static int fail(u32 address, const char *what)
{
    if (!S.fault) {
        S.fault = address ? address : 0x001EA240u;
        fprintf(stderr, "effects: %s at %08X (effect %08X/%d, kinds %08X/%d, manager %08X/%d, head %08X/%d, "
                "sprite %08X/%d)\n", what, (unsigned)S.fault,
                (unsigned)S.e.fault.address, (int)S.e.fault.code,
                (unsigned)S.k.fault.address, (int)S.k.fault.code,
                (unsigned)S.m.fault.address, (int)S.m.fault.code,
                (unsigned)S.hfault.address, (int)S.hfault.code,
                (unsigned)S.sfault.address, (int)S.sfault.code);
    }
    return -1;
}

/* A translation's latched fault becomes this module's. */
static int check(int rc, u32 entry)
{
    if (rc >= 0 && !S.e.fault.code && !S.k.fault.code && !S.m.fault.code && !S.hfault.code &&
        !S.sfault.code)
        return rc;
    u32 at = S.e.fault.code ? S.e.fault.address : S.k.fault.code ? S.k.fault.address
           : S.m.fault.code ? S.m.fault.address : S.hfault.code ? S.hfault.address
           : S.sfault.code ? S.sfault.address : entry;
    return fail(at, "fault");
}

/* The counted gap of the two packet-only handlers (w_handler): `address`
 * is reported the first time only. */
static int effects_gap(uint32_t address, uint32_t detail)
{
    static uint32_t seen[2];
    static int nseen;
    ++S.counters.gaps;
    for (int i = 0; i < nseen; ++i)
        if (seen[i] == address) return 0;
    if (nseen < 2) seen[nseen++] = address;
    fprintf(stderr, "effects: handler %08X (subtype %02X) is not translated; counted gap, its packets "
            "are missing\n", (unsigned)address, (unsigned)detail);
    return 0;
}

uint32_t em_effects_live_fault(void) { return S.fault; }
int em_effects_live_attached(void) { return S.attached; }

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 fbits(float f) { u32 b; memcpy(&b, &f, 4); return b; }
static float bfloat(u32 b) { float f; memcpy(&f, &b, 4); return f; }

/* ------------------------------------------------------------ the export */

int em_effects_live_load(void)
{
    if (S.loaded) return 0;
    if (S.load_tried) return -1;
    S.load_tried = 1;
    FILE *f = fopen(EM_EFFECTS_LIVE_TABLES_PATH, "rb");
    if (!f) {
        fprintf(stderr, "effects: %s is missing (run tools/export_effect_tables.py)\n",
                EM_EFFECTS_LIVE_TABLES_PATH);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *file = size > 0x10 ? malloc((size_t)size) : NULL;
    int ok = file && fread(file, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    uint8_t *elf = ok ? calloc(1, ELF_SIZE) : NULL;
    ok = elf && memcmp(file, "EMET", 4) == 0 && rd32(file + 4) == 1;
    const u32 count = ok ? rd32(file + 8) : 0;
    ok = ok && count > 0 && 0x10u + 0x10u * count <= (u32)size;
    for (u32 i = 0; ok && i < count; ++i) {
        const uint8_t *e = file + 0x10 + 0x10 * i;
        const u32 address = rd32(e), bytes = rd32(e + 4), at = rd32(e + 8);
        ok = address >= 0x00100000u && ELF_AT(address) + bytes <= ELF_SIZE && at <= (u32)size &&
             bytes <= (u32)size - at;
        if (ok) memcpy(elf + ELF_AT(address), file + at, bytes);
    }
    free(file);
    if (ok) {
        /* The table loaders of the translations, over the placed windows. */
        memcpy(elf, "\x7F" "ELF", 4);
        ok = em_effect_original_load_tables(elf, ELF_SIZE, &S.etables) == 0 &&
             em_effect_manager_load_tables(elf, ELF_SIZE, &S.mtables) == 0 &&
             em_effect_kinds_load_tables(elf, ELF_SIZE, &S.ktables) == 0 &&
             em_head_sprite_original_load_tables(elf, ELF_SIZE, &S.htables) == 0;
        if (ok) memcpy(S.sources, elf + ELF_AT(0x002565E0u), sizeof S.sources);
    }
    if (!ok) {
        free(elf);
        fprintf(stderr, "effects: %s is not an effect-table export\n", EM_EFFECTS_LIVE_TABLES_PATH);
        return -1;
    }
    S.elf = elf;
    S.loaded = 1;
    return 0;
}

/* Bytes of the placed ELF windows (the equipment binder's tables read the
 * same image). */
const uint8_t *em_effects_live_elf(uint32_t address, uint32_t size)
{
    if (!S.loaded || address < 0x00100000u || ELF_AT(address) + size > ELF_SIZE) return NULL;
    return S.elf + ELF_AT(address);
}

/* ------------------------------------------------------------ views */

/* The views the routines read, from the render context as it stands (the
 * frame head wrote +0x2240.. and the scratchpad copies; the fog programmer
 * rewrites +0xA0, so every 0021B9A0 worker refreshes the fog). */
static int view_refresh(void)
{
    const uint8_t *c0 = em_rcl_bytes(CTX + 0x2240u, 0x40), *c2 = em_rcl_bytes(CTX + 0x22C0u, 0x40);
    const uint8_t *fog = em_rcl_bytes(CTX + 0xA0u, 0x10);
    const uint8_t *k = em_rcl_bytes(0x70003AC0u, 0x40), *p = em_rcl_bytes(0x70003A40u, 0x40);
    if (!c0 || !c2 || !fog || !k || !p) return fail(0x00275670u, "the render context is not loaded");
    memcpy(S.eview.clip, c0, 0x40);
    memcpy(S.eview.fog, fog, 0x10);
    memcpy(S.eview.camera, k, 0x40);
    memcpy(S.mview.clip0, c0, 0x40);
    memcpy(S.mview.clip2, c2, 0x40);
    memcpy(S.mview.fog, fog, 0x10);
    memcpy(S.mview.camera, k, 0x40);
    memcpy(S.mview.screen, p, 0x40);
    memcpy(S.xs.spad3AC0, k, 0x40);
    return 0;
}

static void fog_refresh(void)
{
    const uint8_t *fog = em_rcl_bytes(CTX + 0xA0u, 0x10);
    if (!fog) return;
    memcpy(S.eview.fog, fog, 0x10);
    memcpy(S.mview.fog, fog, 0x10);
}

/* The scene bytes the routines read, as they stand at the call. */
static void globals_refresh(void)
{
    const EmPlayerLiveActor *p = player_states_actor_mut();
    S.eglobals.d8101E4 = S.scene->d8101E4;
    S.eglobals.d810700 = S.scene->d810700;
    S.eglobals.spad3B68 = S.scene->spad3B68;
    S.eglobals.d8102E8 = p ? bfloat(rd32(p->bytes + 0x38)) : 0.0f;   /* D_008102E8: the player's +0x38 */
    S.kglobals.d810700 = S.scene->d810700;
    S.kglobals.d810701 = S.scene->d810701;
    S.kglobals.spad3B68 = S.scene->spad3B68;
    S.mglobals.d810700 = S.scene->d810700;
    S.mglobals.d810701 = S.scene->d810701;
    S.mglobals.d810702 = S.scene->d810702;
}

/* ------------------------------------------------------------ slots */

static int slot_index(const EmActor *a)
{
    if (!S.pool || !a || a < S.pool->records || a >= S.pool->records + EM_ACTOR_POOL_CAPACITY) return -1;
    return (int)(a - S.pool->records);
}

static Slot *slot_of_node(const EmEffectOriginalNode *n)
{
    if (!n) return NULL;
    Slot *s = (Slot *)((char *)n - offsetof(Slot, e));
    return s >= S.slot && s < S.slot + EM_ACTOR_POOL_CAPACITY ? s : NULL;
}

static EmActor *actor_of(const Slot *s)
{
    return S.pool ? &S.pool->records[s - S.slot] : NULL;
}

/* The pool's own fields of a node, from the translation's bytes. */
static void sync_driver(Slot *s)
{
    EmActor *a = actor_of(s);
    a->model = s->e.b03;
    a->u04[0] = s->e.state;
    a->bones = s->e.b09;
    a->u0A[2] = s->e.b0C;
    a->param = s->e.subtype;
    a->callback = s->e.callback;
    memcpy(a->pos, s->e.pos, sizeof a->pos);
    memcpy(a->rot, s->e.rot, sizeof a->rot);
}

static void sync_head(Slot *s)
{
    EmActor *a = actor_of(s);
    a->u04[0] = s->h.lifecycle;
    a->u04[1] = s->h.sub;
    a->bones = s->h.b09;
    a->u0A[2] = s->h.b0C;
    a->param = s->h.key;
    memcpy(a->pos, s->h.pos, sizeof a->pos);
}

/* ------------------------------------------------------------ workers */

static int w_rand(void *ctx, int32_t *value)
{
    (void)ctx;
    *value = (int32_t)em_random_next();
    return 0;
}

/* 001AFA90(class): a pool record. The translation's node view starts from
 * the record's bytes as 001AFA90 leaves them (+0x04 / +0x09 / +0x0C as the
 * last 001AFC10 or the pool reset cleared them). */
static int w_001AFA90(void *ctx, uint8_t cls, EmEffectOriginalNode **node)
{
    (void)ctx;
    *node = NULL;
    EmActor *a = em_actor_pool_alloc_001AFA90(S.pool, S.scene, cls);
    if (!a) return 0;
    Slot *s = &S.slot[slot_index(a)];
    if (S.npending == (int)(sizeof S.pending / sizeof S.pending[0])) return -1;
    memset(s, 0, sizeof *s);
    s->generation = a->generation;
    s->kind = KIND_NONE;
    S.pending[S.npending++] = s;
    s->e.b03 = a->model;
    s->e.state = a->u04[0];
    s->e.b09 = a->bones;
    s->e.b0C = a->u0A[2];
    s->e.subtype = a->param;
    s->e.callback = a->callback;
    memcpy(s->e.pos, a->pos, sizeof s->e.pos);
    memcpy(s->e.rot, a->rot, sizeof s->e.rot);
    *node = &s->e;
    return 0;
}

/* 001D7FA0(pos, colour, type, fa, fb): the point-light register; its
 * callers here drop the result (a full pool loses the light). */
static int w_001D7FA0(void *ctx, const float pos[4], const float color[4], int32_t type, float fa,
                      float fb)
{
    (void)ctx;
    (void)em_point_light_register(&g.point_lights, pos, color, type, fa, fb);
    return 0;
}

/* 0021B9A0(mode, scale, bias): the fog programmer on the render context;
 * the views' fog copies follow it. */
static int fog_program(int32_t mode, u32 f12, u32 f13)
{
    if (em_rcl_0021B9A0(mode, f12, f13) < 0) return -1;
    fog_refresh();
    return 0;
}
static int w_0021B9A0_float(void *ctx, int32_t mode, float f12, float f13)
{
    (void)ctx;
    return fog_program(mode, fbits(f12), fbits(f13));
}
static int w_0021B9A0_bits(void *ctx, int32_t mode, u32 f12, u32 f13)
{
    (void)ctx;
    return fog_program(mode, f12, f13);
}

/* The two untranslated handlers checked to be packet-only: the skid's
 * 001EAD70 (0x80000033, subtype 1; byte-matched decomp C) and 001EC270
 * (0x80000012, subtype 0xB; its split listing). Each stores only the work
 * block's +0x1F4 (D_00275C34 + 4, twice: the LCG), which 001EA240 rewrites
 * from +0x1F0 before every handler call and reads nowhere else, and calls
 * only 001CFB50 (it rewrites D_0081F8F0 +0x00..+0x57 in full; in the port
 * its only reader is the 001CFBE0 each translated handler calls right after
 * it) and 001CFBE0 (the packets). Skipping one changes only the packets. */
static int packet_only_handler(u32 handler)
{
    return handler == 0x001EAD70u || handler == 0x001EC270u;
}

/* D_00255434[subtype]: the handler, em_effect_kinds' translation. The two
 * packet-only handlers are the counted gap (effects_gap: their packets are
 * missing, nothing else differs). Every other handler em_effect_kinds does
 * not translate faults: none is checked, and some do more than draw
 * (001EF510, subtype 6, spawns 001EFD90(0x80000036) from inside the
 * handler). The port reaches none of them in AREA11 (EFFECT_MANAGER.md
 * 8.2). */
static int w_handler(void *ctx, u32 handler, EmEffectOriginalNode *node, int32_t depth,
                     EmEffectOriginalWork *work)
{
    (void)ctx;
    if (em_effect_kinds_translates(handler))
        return em_effect_kinds_handler(&S.k, handler, node->matrix, depth, work);
    if (packet_only_handler(handler)) return effects_gap(handler, node->subtype);
    return fail(handler, "untranslated effect handler (not packet-only)");
}

static int free_actor(EmActor *a)
{
    return em_actor_pool_free_001AFC10(S.pool, S.scene, a) < 0 ? -1 : 0;
}

/* 001AFC10(node). */
static int w_001AFC10(void *ctx, EmEffectOriginalNode *node)
{
    (void)ctx;
    Slot *s = slot_of_node(node);
    if (!s) return -1;
    EmActor *a = actor_of(s);
    if (a->generation != s->generation) return -1;
    sync_driver(s);
    if (free_actor(a) < 0) return -1;
    s->kind = KIND_NONE;
    return 0;
}

/* 001CFB50(D_0081F8F0, a1, src, ...): em_effect_kinds' translation over the
 * context's camera copy 0x70003AC0. */
static int w_001CFB50(void *ctx, u32 dst, u32 a1, const float src[16], u32 f12, u32 f13, u32 f14,
                      u32 f15, u32 f16)
{
    (void)ctx;
    if (dst != EM_EFFECT_KINDS_XF) return -1;
    u32 words[16];
    memcpy(words, src, sizeof words);
    return em_effect_kinds_001CFB50(&S.k, &S.xs, &S.xf, (int32_t)a1, words, f12, f13, f14, f15, f16);
}

/* 001CFBE0(id, kind, source, xf, copy): em_head_sprite_original's
 * translation, the cursor re-read at the call. */
static int chain_001CFBE0(int32_t id, u32 kind, const EmHeadSpriteOriginalSource *st,
                          const EmHeadSpriteOriginalXf *xf, int32_t copy)
{
    const uint8_t *cursor = em_rcl_bytes(CTX + 0x18u, 4);
    const uint8_t *e80 = em_frame_d810E80();
    if (!cursor || !e80) return -1;
    S.hworld.cursor = rd32(cursor);
    S.hworld.d810E80 = (int16_t)(uint16_t)(e80[0] | e80[1] << 8);
    int r = em_head_sprite_original_001CFBE0(id, kind, st, xf, copy, &S.hworld, &S.hworkers, &S.hfault);
    if (r > 0) ++S.counters.chains;
    else if (r == 0) ++S.counters.chains_skipped;
    return r < 0 ? -1 : 0;
}

static int w_001CFBE0(void *ctx, int32_t id, int32_t kind, u32 source, u32 xf, int32_t copy)
{
    (void)ctx;
    if (xf != EM_EFFECT_KINDS_XF || source < 0x002565E0u || source - 0x002565E0u > sizeof S.sources - 0x90u)
        return -1;
    EmHeadSpriteOriginalXf x;
    memset(&x, 0, sizeof x);
    memcpy(x.q, S.xf.word, 64);
    x.m40 = S.xf.word[0x40 / 4];
    x.m40_bytes = em_rcl_bytes(x.m40, 0x40);
    x.w44 = S.xf.word[0x44 / 4];
    x.w48 = S.xf.word[0x48 / 4];
    x.w4C = S.xf.word[0x4C / 4];
    x.w50 = S.xf.word[0x50 / 4];
    x.w54 = S.xf.word[0x54 / 4];
    const EmHeadSpriteOriginalSource st = {source, S.sources + (source - 0x002565E0u)};
    return chain_001CFBE0(id, (u32)kind, &st, &x, copy);
}

static int w_0011DF78(void *ctx, u32 x, u32 *f0)
{
    (void)ctx;
    *f0 = fbits(em_sdk_math_original_0011DF78(bfloat(x)));
    return 0;
}

static int w_001281C0(void *ctx, u32 x, int32_t *v0)
{
    (void)ctx;
    *v0 = em_effect_original_float_to_int(bfloat(x));
    return 0;
}

/* 001F4D40(pos, colour, size, half) from 001F5940: em_effect_manager's
 * translation; the position travels through MARKER_POSITION_HANDLE. */
static int w_001F4D40(void *ctx, const float pos[4], const int32_t col[4], u32 f12, u32 f13)
{
    (void)ctx;
    memcpy(S.marker, pos, sizeof S.marker);
    S.marker_set = 1;
    u32 colour[4];
    for (int i = 0; i < 4; ++i) colour[i] = (u32)col[i];
    int r = em_effect_manager_001F4D40(&S.m, MARKER_POSITION_HANDLE, colour, f12, f13);
    S.marker_set = 0;
    return r;
}

/* 001CB5F0 for the manager: the chain builder; during the barrel the
 * packets 001F0720 opens are noted for the capture log. */
static int w_manager_001CB5F0(void *ctx, u32 chain, int32_t id, int32_t count, uint8_t **out)
{
    int r = em_packet_chain_w_001CB5F0(ctx, chain, id, count, out);
    if (r >= 0 && S.recording && S.npk < 24) {
        S.pk[S.npk] = *out;
        S.pk_size[S.npk] = (u32)count * 16u;
        ++S.npk;
    }
    return r;
}

/* 001CB5F0 for 001CD520 (the glow markers): noted during the barrel. */
static int w_sprite_001CB5F0(void *ctx, u32 chain, int32_t z, int32_t count, uint8_t **packet)
{
    int r = em_packet_chain_w_001CB5F0(ctx, chain, z, count, packet);
    if (r >= 0 && S.recording && S.nsp < 16) S.sp[S.nsp++] = *packet;
    return r;
}

/* 001F5C20 and 001F5CA0 as the barrel's workers (em_effect_kinds). */
static int w_001F5C20(void *ctx)
{
    (void)ctx;
    return em_effect_kinds_001F5C20(&S.k);
}
static int w_001F5CA0(void *ctx, u32 *list)
{
    (void)ctx;
    *list = em_effect_kinds_001F5CA0(S.scene->d810700, S.scene->d810701);
    return 0;
}

/* 001CD520(bucket, mode, position, tex0, rgb, w, h, zbias):
 * em_player_equipment_sprite's translation. */
static int w_001CD520(void *ctx, int32_t a0, int32_t a1, u32 position, uint64_t tex0, u32 rgb, u32 f12,
                      u32 f13, u32 f14)
{
    (void)ctx;
    if (position != MARKER_POSITION_HANDLE || !S.marker_set) return -1;
    int32_t z = 0;
    if (em_player_equipment_001CD520(&S.sprite, a0, a1, S.marker, tex0, f12, f13, f14, rgb, &z) < 0)
        return -1;
    ++S.counters.sprites;
    return 0;
}

static int w_0011E2A8(void *ctx, u32 x, u32 *result)
{
    (void)ctx;
    EmSdkMathContext *sdk = em_collision_world_sdk();
    float r = 0.0f;
    if (!sdk || em_sdk_math_original_w_0011E2A8(sdk, bfloat(x), &r) < 0 || sdk->fault) return -1;
    *result = fbits(r);
    return 0;
}

/* 001CD370(a0): D_00275670 + (a0 << 6) + 0x2240. */
static int w_sprite_001CD370(void *ctx, int32_t a0, u32 m[16])
{
    (void)ctx;
    const uint8_t *p = em_rcl_bytes(CTX + ((u32)a0 << 6) + 0x2240u, 0x40);
    if (!p) return -1;
    memcpy(m, p, 0x40);
    return 0;
}
static int w_head_001CD370(void *ctx, int32_t a0, u32 *address, const uint8_t **bytes)
{
    (void)ctx;
    *address = CTX + ((u32)a0 << 6) + 0x2240u;
    *bytes = em_rcl_bytes(*address, 0x40);
    return *bytes ? 0 : -1;
}

/* The head sprite's owner bone matrix: the player's node records are the
 * record pose's (the player's +0x110 words), Roger's are the slot arena's. */
static const uint8_t *owner_bytes(u32 owner, u32 address, u32 size)
{
    if (owner == PLAYER) {
        const EmPoseHost *h = player_pose_record_host();
        for (unsigned i = 0; h && i < h->region_count; ++i) {
            const EmPoseRegion *r = &h->region[i];
            if (r->bytes && address >= r->address && size <= r->size && address - r->address <= r->size - size)
                return r->bytes + (address - r->address);
        }
        return NULL;
    }
    return em_area11_roger_slot_bytes(address, size);
}

static int w_head_001026A0(void *ctx, float out[4], u32 matrix, const float v[4])
{
    (void)ctx;
    const uint8_t *m = owner_bytes(S.head_owner, matrix, 0x40);
    if (!m) return -1;
    float mf[16];
    memcpy(mf, m, sizeof mf);
    em_effect_original_001026A0(out, mf, v);
    return 0;
}
static int w_head_001029C0(void *ctx, float m[16])
{
    (void)ctx;
    return em_owner_services_identity_001029C0(m) == 0 ? 0 : -1;
}
static int w_head_00102C58(void *ctx, float m[16], const float v[3])
{
    (void)ctx;
    return em_owner_services_euler_00102C58(m, m, v) == 0 ? 0 : -1;
}
static int w_head_00102918(void *ctx, float out[16], const float in[16], const float v[4])
{
    (void)ctx;
    return em_owner_services_translate_00102918(out, in, v) == 0 ? 0 : -1;
}
/* 001CCF70(pos): em_effect_original's translation (its view and its
 * 0x70003600 / D_00275C04 are the one copy). */
static int w_head_001CCF70(void *ctx, const float pos[4], int32_t *handle)
{
    (void)ctx;
    return em_effect_original_001CCF70(&S.e, pos, handle);
}

/* 001EF9D0(0x80000010, 0, 1.0) from 001F0120: the one allocator. */
static int w_head_001EF9D0(void *ctx, u32 handle, u32 a1, float weight, EmHeadSpriteOriginal **record)
{
    (void)ctx;
    *record = NULL;
    if (a1 != 0) return -1;
    EmEffectOriginalNode *n = NULL;
    if (em_effect_original_001EF9D0(&S.e, handle, NULL, weight, &n) < 0) return -1;
    if (!n) return 0;
    Slot *s = slot_of_node(n);
    if (!s) return -1;
    EmActor *a = actor_of(s);
    memset(&s->h, 0, sizeof s->h);
    s->h.self = em_actor_pool_address(S.pool, a);
    s->h.lifecycle = n->state;
    s->h.b09 = n->b09;
    s->h.b0C = n->b0C;
    s->h.key = n->subtype;
    memcpy(s->h.pos, n->pos, sizeof s->h.pos);   /* +0xB0 as 001AFA90 left it */
    *record = &s->h;
    return 0;
}

static int w_head_001AFC10(void *ctx, u32 self)
{
    (void)ctx;
    for (unsigned i = 0; i < EM_ACTOR_POOL_CAPACITY; ++i) {
        Slot *s = &S.slot[i];
        if (s->kind != KIND_HEAD || s->h.self != self) continue;
        EmActor *a = actor_of(s);
        if (a->generation != s->generation) return -1;
        sync_head(s);
        if (free_actor(a) < 0) return -1;
        s->kind = KIND_NONE;
        return 0;
    }
    return -1;
}

static void wire(EmPacketChain *pc)
{
    EmEffectOriginalWorkers *ew = &S.eworkers;
    memset(ew, 0, sizeof *ew);
    ew->w_001AFA90 = w_001AFA90;
    ew->w_00122BB8 = w_rand;
    ew->w_001D7FA0 = w_001D7FA0;
    /* 001FBF50 / 001FB9F0: no first-level effect record carries a sound
     * (+0x24 == -1): NULL, a fault if reached. */
    ew->w_0021B9A0 = w_0021B9A0_float;
    ew->w_handler = w_handler;
    ew->w_001AFC10 = w_001AFC10;
    S.e = (EmEffectOriginal){&S.etables, &S.eglobals, &S.decals, &S.eview, ew, {0, 0}};

    EmEffectKindsWorkers *kw = &S.kworkers;
    memset(kw, 0, sizeof *kw);
    /* 001D7FA0 / 001D80B0 (the room point-light lists): not reached from
     * the barrel in AREA11; the offline point-light adoption stays their
     * stand-in (critic 7.2). */
    kw->w_001F4D40 = w_001F4D40;
    kw->w_0011DF78 = w_0011DF78;
    kw->w_001281C0 = w_001281C0;
    kw->w_0021B9A0 = w_0021B9A0_bits;
    kw->w_00122BB8 = w_rand;
    kw->w_001CFB50 = w_001CFB50;
    kw->w_001CFBE0 = w_001CFBE0;
    S.k = (EmEffectKinds){&S.ktables, &S.kglobals, &S.decals, &S.particles, kw, {0, 0}};
    S.xs.d275670 = CTX;

    /* The manager's worker table carries the packet chain as its context,
     * so the chain adapters bind directly; every other worker ignores it. */
    EmEffectManagerWorkers *mw = &S.mworkers;
    memset(mw, 0, sizeof *mw);
    mw->ctx = pc;
    mw->w_001F5C20 = w_001F5C20;
    mw->w_001F5CA0 = w_001F5CA0;
    mw->w_00122BB8 = w_rand;
    mw->w_001CB5F0 = w_manager_001CB5F0;
    mw->w_001CB760 = em_packet_chain_w_001CB760;
    mw->w_001CB900 = em_packet_chain_w_001CB900;
    mw->w_0021B9A0 = w_0021B9A0_bits;
    mw->w_0011E2A8 = w_0011E2A8;
    mw->w_001CD520 = w_001CD520;
    /* 001F6210's list loop, the selectors' point-light paths and the sweep's
     * live entities are not reached in AREA11: NULL. */
    S.mglobals.d275670 = CTX;
    S.m = (EmEffectManager){&S.mtables, &S.mglobals, &S.decals, &S.mview, S.entities, mw, {0, 0}};

    EmHeadSpriteOriginalWorkers *hw = &S.hworkers;
    memset(hw, 0, sizeof *hw);
    hw->ctx = pc;
    hw->w_00122BB8 = w_rand;
    hw->w_001EF9D0 = w_head_001EF9D0;
    hw->w_001AFC10 = w_head_001AFC10;
    hw->w_001026A0 = w_head_001026A0;
    hw->w_001029C0 = w_head_001029C0;
    hw->w_00102C58 = w_head_00102C58;
    hw->w_00102918 = w_head_00102918;
    hw->w_001CCF70 = w_head_001CCF70;
    hw->w_001CD370 = w_head_001CD370;
    hw->w_001CB5F0 = em_packet_chain_w_001CB5F0;
    hw->w_001CB6B0 = em_packet_chain_w_001CB6B0;
    hw->w_001CB760 = em_packet_chain_w_001CB760_4;
    hw->w_001CB900 = em_packet_chain_w_001CB900;
    memset(&S.hworld, 0, sizeof S.hworld);
    S.hworld.scratch_3A40 = em_rcl_bytes(0x70003A40u, 0x40);
    S.hworld.scratch_3AC0 = em_rcl_bytes(0x70003AC0u, 0x40);
    S.hworld.ctx_A0 = em_rcl_bytes(CTX + 0xA0u, 0x10);
    S.hworld.tables = &S.htables;
    memset(&S.hfault, 0, sizeof S.hfault);

    EmPlayerEquipmentSpriteWorkers *sw = &S.sworkers;
    memset(sw, 0, sizeof *sw);
    sw->ctx = pc;
    sw->w_001CD370 = w_sprite_001CD370;
    sw->w_001CB5F0 = w_sprite_001CB5F0;
    sw->w_001CB6B0 = em_packet_chain_w_001CB6B0;
    sw->w_001CB900 = em_packet_chain_w_001CB900;
    S.sworld.fog = (const u32 *)(const void *)em_rcl_bytes(CTX + 0xA0u, 0x10);
    S.sworld.s3A40 = (const u32 *)(const void *)em_rcl_bytes(0x70003A40u, 0x40);
    S.sworld.s3AC0 = (const u32 *)(const void *)em_rcl_bytes(0x70003AC0u, 0x40);
    S.sworld.s3600 = S.spad3600_sprite;
    memset(&S.sfault, 0, sizeof S.sfault);
    S.sprite = (EmPlayerEquipmentSprite){sw, &S.sworld, &S.sfault};
}

/* ------------------------------------------------------------ attach */

int em_effects_live_attach(EmActorPool *pool, EmSceneState *scene, EmEffectsLiveBind bind)
{
    em_effects_live_detach();
    if (!pool || !scene || !bind || em_effects_live_load() < 0) return -1;
    EmPacketChain *pc = em_rcl_packet_chain();
    if (!pc || !em_rcl_bytes(CTX + 0xA0u, 0x10)) {
        fprintf(stderr, "effects: the render context is not live\n");
        return -1;
    }
    S.pool = pool;
    S.scene = scene;
    S.bind = bind;
    S.fault = 0;
    memset(S.slot, 0, sizeof S.slot);
    memset(&S.eglobals, 0, sizeof S.eglobals);
    memset(&S.kglobals, 0, sizeof S.kglobals);
    memset(&S.mglobals, 0, sizeof S.mglobals);
    memset(&S.counters, 0, sizeof S.counters);
    wire(pc);
    S.attached = 1;
    return 0;
}

void em_effects_live_detach(void)
{
    S.attached = 0;
    S.pool = NULL;
    S.scene = NULL;
    S.bind = NULL;
}

#define READY() do { if (!S.attached || S.fault) return -1; } while (0)

/* ------------------------------------------------------------ resets */

int em_effects_live_001F0310(void)
{
    READY();
    globals_refresh();
    int r = em_effect_kinds_001F0310(&S.k);
    /* D_00275C44 is one word: 001F3FA0 writes it here, 001F40C0 counts it
     * down in the barrel. */
    S.mglobals.d275C44 = S.kglobals.d275C44;
    return check(r, 0x001F0310u);
}

/* ------------------------------------------------------------ spawns */

/* The records 001AFA90 handed out during this entry: their pool fields from
 * the translation's bytes, then their behaviour by their +0x10 (001EF9D0
 * wrote it after the alloc; a head sprite's +0x0D / +0x24 come from
 * 001F0120 after that). */
static int bind_pending(void)
{
    int rc = 0;
    for (int i = 0; i < S.npending; ++i) {
        Slot *s = S.pending[i];
        EmActor *a = actor_of(s);
        if (!a->allocated || a->generation != s->generation) continue;   /* freed again */
        s->kind = s->e.callback == EM_EFFECTS_LIVE_DRIVER ? KIND_DRIVER
                : s->e.callback == EM_EFFECTS_LIVE_HEAD_SPRITE ? KIND_HEAD : KIND_OTHER;
        sync_driver(s);
        if (s->kind == KIND_HEAD) sync_head(s);
        if (rc == 0 && S.bind(a) < 0) rc = fail(s->e.callback, "no pool binding for the effect node");
    }
    S.npending = 0;
    return rc;
}

static int finish(int rc, u32 entry)
{
    int b = bind_pending();
    return check(rc, entry) < 0 || b < 0 ? -1 : 0;
}

int em_effects_live_001EFD90(uint32_t id, const float pos[4], const float rot[4])
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    EmEffectOriginalNode *n = NULL;
    return finish(em_effect_original_001EFD90(&S.e, id, pos, rot, &n), 0x001EFD90u);
}

int em_effects_live_001EFD20(uint32_t id, const float pos[4])
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    EmEffectOriginalNode *n = NULL;
    return finish(em_effect_original_001EFD20(&S.e, id, pos, &n), 0x001EFD20u);
}

int em_effects_live_001F0460(int32_t n, const float m[16])
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    /* Preset 0 spawns 001EFD20(0x8000000E) first; bind_pending binds it. */
    return finish(em_effect_original_001F0460(&S.e, n, m), 0x001F0460u);
}

int em_effects_live_001F0120(uint32_t owner14, int32_t key)
{
    READY();
    globals_refresh();
    EmHeadSpriteOriginal *h = NULL;
    return finish(em_head_sprite_original_spawn_001F0120(owner14, key, &S.hworkers, &h, &S.hfault),
                  0x001F0120u);
}

/* ------------------------------------------------------------ the node ticks */

/* The head sprite's owner view: the player record, or Roger's. */
static int head_owner(u32 address, EmHeadSpriteOriginalOwner *o)
{
    const uint8_t *r = NULL;
    if (address == PLAYER) {
        const EmPlayerLiveActor *p = player_states_actor_mut();
        r = p ? p->bytes : NULL;
        o->slot_count = PLAYER_NODES;
    } else {
        r = em_area11_roger_record_bytes(address, EM_ACTOR_RECORD_SIZE);
        o->slot_count = r ? r[0x0C] : 0;
    }
    if (!r) return -1;
    o->address = address;
    o->b01 = r[0x01];
    o->b02 = r[0x02];
    o->b04 = r[0x04];
    o->f220 = bfloat(rd32(r + 0x220));
    o->slots = (const u32 *)(const void *)(r + 0x110);
    o->rot = (const float *)(const void *)(r + 0xC0);
    return 0;
}

int em_effects_live_tick(EmActor *actor)
{
    READY();
    int i = slot_index(actor);
    if (i < 0) return fail(0x001AFD70u, "an effect tick outside the pool");
    Slot *s = &S.slot[i];
    if (s->generation != actor->generation || s->kind == KIND_NONE)
        return fail(actor->callback, "an effect node this binder did not allocate");
    if (view_refresh() < 0) return -1;
    globals_refresh();
    if (s->kind == KIND_DRIVER) {
        s->e.state = actor->u04[0];
        int r = em_effect_original_001EA240(&S.e, &s->e);
        if (r == 1) sync_driver(s);
        if (finish(r, EM_EFFECTS_LIVE_DRIVER) < 0) return -1;
        return r;
    }
    if (s->kind == KIND_HEAD) {
        EmHeadSpriteOriginalOwner owner;
        memset(&owner, 0, sizeof owner);
        const int have_owner = head_owner(s->h.owner, &owner) == 0;
        const uint8_t *cursor = em_rcl_bytes(CTX + 0x18u, 4);
        const uint8_t *e80 = em_frame_d810E80();
        if (!cursor || !e80) return fail(0x00275670u, "the render context is not loaded");
        S.hworld.cursor = rd32(cursor);
        S.hworld.d810E80 = (int16_t)(uint16_t)(e80[0] | e80[1] << 8);
        S.hworld.d8106C8 = (int32_t)em_scene_req_u32(S.scene, EM_SCENE_REQ_C8);
        S.head_owner = s->h.owner;
        s->h.lifecycle = actor->u04[0];
        int r = em_head_sprite_original_tick(&s->h, have_owner ? &owner : NULL, &S.hworld, &S.hworkers,
                                             &S.hfault);
        if (r == 1) sync_head(s);
        if (finish(r, EM_EFFECTS_LIVE_HEAD_SPRITE) < 0) return -1;
        return r;
    }
    return fail(actor->callback, "an effect node without a translated behaviour");
}

/* ------------------------------------------------------------ the barrel */

/* CRC-32 (the IEEE polynomial, as zlib's crc32), for the capture log. */
static u32 crc32_bytes(u32 crc, const uint8_t *p, u32 n)
{
    crc = ~crc;
    for (u32 i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Per lane: packet 1, packet 2 with each slot's +0x40 parameter quadword
 * zeroed, those 32 parameter quadwords, packet 3, packet 4. */
static void barrel_digest(void)
{
    memset(S.digest, 0, sizeof S.digest);
    if (S.npk != 24) return;
    for (int lane = 0; lane < 6; ++lane) {
        const uint8_t *const *p = S.pk + 4 * lane;
        const u32 *n = S.pk_size + 4 * lane;
        u32 *d = S.digest + 5 * lane;
        d[0] = crc32_bytes(0, p[0], n[0]);
        static uint8_t lane_bytes[0xC10], params[0x200];
        if (n[1] != sizeof lane_bytes) return;
        memcpy(lane_bytes, p[1], sizeof lane_bytes);
        for (int i = 0; i < 32; ++i) {
            memcpy(params + 0x10 * i, lane_bytes + 0x10 + 0x60 * i + 0x40, 0x10);
            memset(lane_bytes + 0x10 + 0x60 * i + 0x40, 0, 0x10);
        }
        d[1] = crc32_bytes(0, lane_bytes, sizeof lane_bytes);
        d[2] = crc32_bytes(0, params, sizeof params);
        d[3] = crc32_bytes(0, p[2], n[2]);
        d[4] = crc32_bytes(0, p[3], n[3]);
    }
}

/* 001CD520's primitives of the barrel (6 quadwords each): the words the
 * route rows can compare, i.e. without the rand()-pulsed colour +0x10..+0x1F
 * and the words 001CD520 leaves as it finds them (+0x08..+0x0F, +0x2C,
 * +0x4C), sorted. */
static void sprite_digest(void)
{
    S.nsprite_digest = 0;
    for (int i = 0; i < S.nsp; ++i) {
        uint8_t q[0x60];
        memcpy(q, S.sp[i], sizeof q);
        memset(q + 0x08, 0, 0x08);
        memset(q + 0x10, 0, 0x10);
        memset(q + 0x2C, 0, 4);
        memset(q + 0x4C, 0, 4);
        const u32 d = crc32_bytes(0, q, sizeof q);
        int k = S.nsprite_digest++;
        while (k > 0 && S.sprite_digest[k - 1] > d) { S.sprite_digest[k] = S.sprite_digest[k - 1]; --k; }
        S.sprite_digest[k] = d;
    }
}

int em_effects_live_sprite_digests(uint32_t out[16])
{
    memcpy(out, S.sprite_digest, sizeof S.sprite_digest);
    return S.nsprite_digest;
}

void em_effects_live_lane_digests(uint32_t out[EM_EFFECTS_LIVE_LANE_DIGESTS])
{
    memcpy(out, S.digest, sizeof S.digest);
}

int em_effects_live_001F0360(void)
{
    READY();
    const int key = (S.scene->d810700 << 8) | S.scene->d810701;
    /* 001F6BB0 reads the latch bytes D_0081075D / 778 / 77B / 79E for keys
     * 0 and 0x1301 only; the port keeps no canonical copy of them. */
    if (key == 0 || key == 0x1301) return fail(0x001F6BB0u, "the selector's latch bytes are not canonical");
    if (view_refresh() < 0) return -1;
    globals_refresh();
    /* The particle records' +0x80 / +0x82 halfwords (001F3FA0's, the one
     * copy) as the sweep's view; no writer changes them on the route. */
    for (int i = 0; i < EM_EFFECT_MANAGER_ENTITIES; ++i) {
        const uint8_t *r = S.particles.record[i];
        S.entities[i].live = (int16_t)(uint16_t)(r[0x80] | r[0x81] << 8);
        S.entities[i].kind = (int16_t)(uint16_t)(r[0x82] | r[0x83] << 8);
    }
    S.mglobals.d275C44 = S.kglobals.d275C44;
    S.recording = 1;
    S.npk = 0;
    S.nsp = 0;
    int r = em_effect_manager_001F0360(&S.m);
    S.recording = 0;
    barrel_digest();
    sprite_digest();
    S.kglobals.d275C44 = S.mglobals.d275C44;
    for (int i = 0; i < EM_EFFECT_MANAGER_ENTITIES; ++i) {
        uint8_t *p = S.particles.record[i];
        p[0x80] = (uint8_t)S.entities[i].live;
        p[0x81] = (uint8_t)((uint16_t)S.entities[i].live >> 8);
        p[0x82] = (uint8_t)S.entities[i].kind;
        p[0x83] = (uint8_t)((uint16_t)S.entities[i].kind >> 8);
    }
    if (check(r, 0x001F0360u) < 0) return -1;
    S.counters.lanes += 6;
    ++S.counters.frames;
    return 0;
}

int em_effects_live_aura_draw(const float owner_d0[16], uint32_t record, uint32_t angle, uint32_t timer)
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    u32 d0[16];
    memcpy(d0, owner_d0, sizeof d0);
    return check(em_effect_manager_aura_draw(&S.m, d0, record, angle, timer), 0x001F1180u);
}

/* ------------------------------------------------------------ capture log */

int em_effects_live_nodes(EmEffectsLiveNode *out, int max)
{
    if (!S.attached || !S.pool || !out) return 0;
    int n = 0;
    for (const EmActor *a = S.pool->head; a && n < max; a = a->next) {
        int i = slot_index(a);
        if (i < 0) break;
        const Slot *s = &S.slot[i];
        if (s->generation != a->generation || (s->kind != KIND_DRIVER && s->kind != KIND_HEAD)) continue;
        EmEffectsLiveNode *o = &out[n++];
        memset(o, 0, sizeof *o);
        o->address = em_actor_pool_address(S.pool, a);
        o->callback = a->callback;
        o->state = a->u04[0];
        o->key = a->param;
        if (s->kind == KIND_DRIVER) {
            for (int k = 0; k < 3; ++k) {
                o->w[k] = fbits(s->e.pos[k]);
                o->w[3 + k] = fbits(s->e.matrix[12 + k]);
            }
            o->w[6] = fbits(s->e.work.step);
            o->w[7] = fbits(s->e.work.limit);
            o->w[8] = fbits(s->e.work.accumulator);
        } else {
            o->sub = s->h.sub;
            o->w[0] = s->h.owner;
            o->w[1] = (u32)s->h.bone;
            for (int k = 0; k < 3; ++k) o->w[2 + k] = fbits(s->h.local[k]);
            o->w[5] = (u32)s->h.timer;
            o->w[6] = fbits(s->h.ramp);
            o->w[7] = fbits(s->h.scalar);
        }
    }
    return n;
}

void em_effects_live_counters(EmEffectsLiveCounters *out)
{
    if (out) *out = S.counters;
}
