/* em_collision_world.c - the scene's one original collision world (see the
 * header). Storage and binding only: every original routine it runs is the
 * verified translation named at the call. */
#include "game/em_collision_world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_coll_list_passes.h"
#include "game/em_coll_list_passes_walkers.h"
#include "game/em_sdk_math_original.h"
#include "game/em_sdk_soft_float.h"

/* The list bases (the original pushes slot i at base - 4(i + 1)):
 * docs/ACTOR_COLLISION.md section 1, em_actor_collision.h. */
enum {
    BASE_CLASS1 = 0x0028B020u,
    BASE_CLASS_D = 0x0028AFF0u,
    BASE_CLASS2 = 0x0028AF30u,
    BASE_CLASS4 = 0x0028AE30u,
    BASE_CLASS7 = 0x0028AD30u,
    BASE_FLAG80 = 0x0028AB30u
};

static struct {
    EmActorCellTable cells;          /* *0x70003250, 0x7000324C */
    EmActorClassLists lists;         /* D_00275B54..D_00275BB8 */
    EmActorCollisionWorld acw;
    EmCollProbeGrid grid;
    EmCollProbeWorld probe;
    EmCollProbeState state;          /* the one scratchpad */
    EmCollSegmentFaceScratch face;   /* 0x70003600.. */
    EmSdkMathTables tables;
    int32_t d26C5D0;
    EmSdkMathContext math;
    EmCollSegment seg;
    EmCollListGlobals globals;
    EmCollListData data;
    EmCollListPasses passes;
    /* The floor service's workers (em_collision_world_bind_player). */
    EmCollProbeWorkers probe_workers;
    EmCollProbePlayer probe_player;
    EmActorCollisionPlayer ground_player;
    EmCollColumnMath column_math;
    EmActorCollisionPlayerColumn column_player;
    /* 0019AD00 / 0019AFE0 (em_coll_move_original) and the hull locks. */
    EmCollHullWorld hulls;
    EmCollMoveWorld move;
    EmCollMoveScratch move_scratch;
    EmCollMovePlayer move_player;
    int loaded;
    int dumped_first;
} w;

/* The soft-float workers of the SDK context (docs/SDK_SOFT_FLOAT.md section
 * 4): 00128350, 0011DB90, 0011FD78 and 00127758, which the domain-error
 * tails of atan2f 0011E620 and sqrtf 0011E748 call. Their data is boot-ELF
 * .data: D_0024295C (0011FD78 reads it on every call) and the errno word at
 * the address it holds (0x00242670 in the ELF and every capture), which the
 * tails store 0x21 into. The original initialises both only in the ELF
 * image, so this storage is loaded once from the user's export and is not
 * reset by an area build (em_collision_world_unload leaves it). */
static struct {
    uint32_t d24295C;
    int32_t errno_word;
    EmSdkSoftFloatContext context;
    int loaded;
} s_soft;

static int load_soft_float(void)
{
    if (s_soft.loaded)
        return 0;
    if (em_sdk_soft_float_load_export(EM_COLLISION_WORLD_SOFT_FLOAT_PATH, &s_soft.d24295C,
                                      &s_soft.errno_word) != 0)
        return -1;
    s_soft.context = (EmSdkSoftFloatContext){ &s_soft.d24295C, s_soft.d24295C, &s_soft.errno_word, 0 };
    s_soft.loaded = 1;
    return 0;
}

/* D_0024A740 is not exported: the view holds no byte, so 001A8660's
 * knock-back table read faults (0x1A87C0). It is reached only after the
 * entry's +0x34 behaviour, which is itself a fail-stop binding below. */
static const uint8_t k_no_d24A740[1];

/* The owners that publish records the passes and the hull locks read
 * (census L22: Roger 008237E0, class 0x0A, em_area11_roger). */
static EmCollisionWorldOwners s_owners;

/* The list arrays as original words: slot i of a list at base - 4(i + 1)
 * (em_actor_class_lists_*), the entry's original record address, built at
 * every close-out from the live lists. */
static const uint32_t k_list_base[EM_ACTOR_LIST_COUNT] = {
    [EM_ACTOR_LIST_CLASS1] = BASE_CLASS1, [EM_ACTOR_LIST_CLASS_D] = BASE_CLASS_D,
    [EM_ACTOR_LIST_CLASS2] = BASE_CLASS2, [EM_ACTOR_LIST_CLASS4] = BASE_CLASS4,
    [EM_ACTOR_LIST_CLASS7] = BASE_CLASS7, [EM_ACTOR_LIST_FLAG80] = BASE_FLAG80};
static uint8_t s_list_words[EM_ACTOR_LIST_COUNT][EM_ACTOR_LIST_MAX * 4];
/* The slots whose record no owner names an original address for: a pass
 * that reads one faults (its word is not supplied). */
static uint8_t s_list_unbound[EM_ACTOR_LIST_COUNT][EM_ACTOR_LIST_MAX];

static void list_words_build(void)
{
    for (int k = 0; k < EM_ACTOR_LIST_COUNT; ++k) {
        const EmActorClassList *l = &w.lists.list[k];
        for (int i = 0; i < l->live; ++i) {
            uint32_t a = s_owners.address_of ? s_owners.address_of(s_owners.context, l->slot[i]) : 0;
            /* slot i at base - 4(i + 1): word index EM_ACTOR_LIST_MAX - 1 - i */
            memcpy(&s_list_words[k][4u * (EM_ACTOR_LIST_MAX - 1 - (unsigned)i)], &a, 4);
            s_list_unbound[k][EM_ACTOR_LIST_MAX - 1 - i] = a == 0;
        }
    }
}

/* EmCollListMemory.bytes: the list arrays (live slots only), then the
 * records an owner supplies in the original layout (Roger's record and the
 * resources its +0x58 names, census L22). Any other range: NULL, and the
 * pass that reads it faults (docs/COLL_LIST_PASSES.md section 4 item 4:
 * the pool keeps native EmActor records, so only the owners that publish
 * onto the lists the passes walk supply their bytes). */
static uint8_t *owner_bytes(void *context, uint32_t address, uint32_t size)
{
    (void)context;
    for (int k = 0; k < EM_ACTOR_LIST_COUNT; ++k) {
        const uint32_t base = k_list_base[k];
        const EmActorClassList *l = &w.lists.list[k];
        const uint32_t low = base - 4u * (uint32_t)l->live;
        if (address >= low && address < base) {
            if (size > base - address) return NULL;
            const uint32_t at = 4u * EM_ACTOR_LIST_MAX - (base - address);
            for (uint32_t b = at / 4u; size && b <= (at + size - 1u) / 4u; ++b)
                if (s_list_unbound[k][b]) return NULL;
            return &s_list_words[k][at];
        }
    }
    return s_owners.record_bytes ? s_owners.record_bytes(s_owners.context, address, size) : NULL;
}

void em_collision_world_bind_owners(const EmCollisionWorldOwners *owners)
{
    if (owners) s_owners = *owners;
    else memset(&s_owners, 0, sizeof s_owners);
    w.hulls.context = s_owners.context;
    w.hulls.chain = s_owners.chain;
}

static void bind_passes(void)
{
    memset(&w.passes, 0, sizeof w.passes);
    w.data.d24A740 = k_no_d24A740;
    w.data.d24A740_size = 0;
    w.passes.memory.bytes = owner_bytes;
    w.passes.globals = &w.globals;
    w.passes.data = &w.data;
    w.passes.math = &w.math;
    EmCollListWorkers *k = &w.passes.workers;
    /* None of these callees has a translation; none ran on the census route
     * (docs/COLL_LIST_PASSES.md section 2). Reaching one faults with its
     * call site's address. */
    k->w_001A8840 = em_coll_list_passes_unported;
    k->w_001A8970 = em_coll_list_passes_unported;
    k->w_001A8CE0 = em_coll_list_passes_unported;
    k->w_001A8E80 = em_coll_list_passes_unported;
    k->w_001A8F40 = em_coll_list_passes_unported;
    k->w_001A9360 = em_coll_list_passes_unported;
    k->w_001A96F0 = em_coll_list_passes_unported;
    k->w_001A9480 = em_coll_list_passes_unported;
    k->w_001A99E0 = em_coll_list_passes_unported;
    k->w_001A9C40 = em_coll_list_passes_unported;
    k->w_001A9E00 = em_coll_list_passes_unported;
    k->w_001AA000 = em_coll_list_passes_unported_001AA000;
    k->w_0021BD10 = em_coll_list_passes_unported_0021BD10;
    /* The +0x34 behaviour of a class-0xD type-1 entry: AREA11's is the
     * overlay routine 0x00823580, which has no binding here. */
    k->behaviour = em_coll_list_passes_unported_behaviour;
    k->normalize = em_coll_list_passes_normalize;
}

void em_collision_world_unload(void)
{
    em_actor_cells_free(&w.cells);
    em_coll_probe_grid_free(&w.grid);
    memset(&w, 0, sizeof w);
}

int em_collision_world_load(const EmCollision *emcl, const char *emcl_path, const char *cells_path,
                            const char *sdk_path)
{
    em_collision_world_unload();
    if (!emcl || !emcl->blob || !emcl_path || !cells_path || !sdk_path) {
        fprintf(stderr, "collision world: no loaded EMCL\n");
        return -1;
    }
    if (em_actor_cells_load(&w.cells, cells_path) != 0) {
        fprintf(stderr, "collision world: the cell directory %s is missing or malformed "
                        "(python3 tools/test_actor_collision_reference.py --export; STARTUP.md)\n",
                cells_path);
        em_collision_world_unload();
        return -1;
    }
    if (em_coll_probe_grid_load(&w.grid, emcl, emcl_path) != 0) {
        fprintf(stderr, "collision world: %s has no node class / rank section (EMCL flags 7: "
                        "export_collision.py --node-class; STARTUP.md)\n", emcl_path);
        em_collision_world_unload();
        return -1;
    }
    if (em_sdk_math_original_load_export(sdk_path, &w.tables, &w.d26C5D0) != 0) {
        fprintf(stderr, "collision world: the SDK math tables %s are missing or invalid "
                        "(tools/export_sdk_math_tables.py)\n", sdk_path);
        em_collision_world_unload();
        return -1;
    }
    if (load_soft_float() != 0) {
        fprintf(stderr, "collision world: the SDK soft-float data %s is missing or invalid "
                        "(tools/export_sdk_math_tables.py)\n", EM_COLLISION_WORLD_SOFT_FLOAT_PATH);
        em_collision_world_unload();
        return -1;
    }
    w.math.tables = &w.tables;
    w.math.world.d26C5D0 = &w.d26C5D0;
    em_sdk_soft_float_bind(&w.math.workers, &s_soft.context);
    /* AREA11's directory has no static cell (word 0 has no bit 31), so no
     * D_0024D7C0 kind view is needed; reaching one faults. */
    w.acw = (EmActorCollisionWorld){ &w.cells, &w.lists, emcl, NULL, 0, &w.grid };
    w.probe = (EmCollProbeWorld){ &w.acw, &w.grid };
    /* The hull locks 001A6440 / 001A6AD0 / 001A7280 (mask bit 0,
     * em_coll_grid_hull) walk the published class-2 list, which no AREA11
     * owner the port runs publishes: no chain reader and no 001A7280 player
     * are bound, so a lock that would need one faults. */
    w.hulls = (EmCollHullWorld){ s_owners.context, s_owners.chain, NULL };
    w.seg = (EmCollSegment){ &w.probe, &w.math, &w.hulls, &w.state, &w.face };
    w.move = (EmCollMoveWorld){ &w.acw, &w.grid, &w.hulls, &w.math };
    bind_passes();
    w.loaded = 1;
    return 0;
}

int em_collision_world_loaded(void)
{
    return w.loaded;
}

void em_collision_world_lists_reset_001AF8E0(void)
{
    em_actor_class_lists_reset(&w.lists);
}

const EmActorClassLists *em_collision_world_lists(void)
{
    return &w.lists;
}

int em_collision_world_publish_001B1B70(const EmActor *actor)
{
    return em_actor_class_publish_001B1B70(&w.lists, actor);
}

int em_collision_world_push4_001B1D20(const EmActor *actor)
{
    if (!w.loaded) return -1;
    return em_actor_class_push4_001B1D20(&w.lists, actor);
}

int em_collision_world_retransform_001A2370(const EmActor *actor, const float matrix[16])
{
    if (!w.loaded || !actor || !matrix) return -1;
    return em_actor_cells_retransform_001A2370(&w.cells, actor->uid, matrix);
}

/* The live list's original cursor: slot i sits at base - 4(i + 1), and the
 * cursor names the newest slot. */
static uint32_t cursor(uint32_t base, const EmActorClassList *list)
{
    return base - 4u * (uint32_t)list->live;
}

int em_collision_world_close_out_001AAD00(const EmSceneState *scene, int16_t d28A9A0, uint32_t *fault)
{
    if (fault) *fault = 0;
    if (!w.loaded || !scene) {
        if (fault) *fault = 0x001AAD00u;
        return -1;
    }
    EmCollListGlobals *g = &w.globals;
    const EmActorClassList *l = w.lists.list;
    g->d275BB0 = cursor(BASE_CLASS1, &l[EM_ACTOR_LIST_CLASS1]);
    g->d275BB8 = l[EM_ACTOR_LIST_CLASS1].live;
    g->d275BA0 = cursor(BASE_CLASS_D, &l[EM_ACTOR_LIST_CLASS_D]);
    g->d275BA8 = l[EM_ACTOR_LIST_CLASS_D].live;
    g->d275B90 = cursor(BASE_CLASS2, &l[EM_ACTOR_LIST_CLASS2]);
    g->d275B98 = l[EM_ACTOR_LIST_CLASS2].live;
    g->d275B80 = cursor(BASE_CLASS4, &l[EM_ACTOR_LIST_CLASS4]);
    g->d275B88 = l[EM_ACTOR_LIST_CLASS4].live;
    /* 0x70003B86 / 0x70003B88 are the walkers' span words too: one storage
     * (docs/COLL_LIST_PASSES.md section 4 item 5). */
    g->s3B86 = w.state.span_lo;
    g->s3B88 = w.state.span_hi;
    g->s3B8D = scene->spad3B8D;
    g->d28A9A0 = d28A9A0;
    g->d810700 = scene->d810700;
    g->d810702 = scene->d810702;
    /* D_0081070A is not canonical yet; 001A8660 reads it only after the
     * +0x34 behaviour, which faults (above), so the value is never read. */
    g->d81070A = 0;
    /* 0x700038A0..AC: written by 001A8660's knock-back only (never reached
     * here, as above). */
    memset(g->s38A0, 0, sizeof g->s38A0);
    w.passes.fault = 0;
    list_words_build();
    int result = em_coll_list_passes_001AAD00_hooks(&w.passes, EM_COLL_LIST_PLAYER);
    w.state.span_lo = g->s3B86;
    w.state.span_hi = g->s3B88;
    if (result < 0) {
        if (fault) *fault = w.passes.fault ? w.passes.fault : 0x001AAD00u;
        return -1;
    }
    /* The list counts the passes may have shortened live in the globals;
     * the passes only read the cursors and counts, so the lists are
     * unchanged. Then the list block. */
    em_actor_class_lists_swap_001AAD00(&w.lists);
    em_collision_world_dump_if_requested();
    return 0;
}

int em_collision_world_0019A910(const float from[3], const float to[3], unsigned mask,
                                EmCollSegmentHit *hit)
{
    if (!w.loaded || !from || !to || !hit) return -1;
    int result = em_coll_segment_0019A910(&w.seg, from, to, mask);
    if (result < 0) return -1;
    memset(hit, 0, sizeof *hit);
    if (result && em_coll_segment_hit(&w.seg, hit) < 0) return -1;
    return result;
}

int em_collision_world_0019B7D0(const float from[3], const float to[3], EmCollSegmentHit *hit)
{
    if (!w.loaded || !from || !to || !hit) return -1;
    int result = em_coll_list_passes_0019B7D0(&w.grid, &w.state, from, to);
    if (result < 0) return -1;
    memset(hit, 0, sizeof *hit);
    if (result && em_coll_segment_hit(&w.seg, hit) < 0) return -1;
    return result;
}

/* 00179450's 0019BC40 over the world. The SDK workers record a fault in
 * their context instead of returning one: checked after the call, so a
 * faulted sqrt/atan fails the column (no value is used). A fault an earlier
 * SDK call of the same floor service recorded is kept for the service's own
 * check (EmPlayerStatesBinding.sdk_fault). */
static int player_column(void *context, const float position[3], EmPlayerFloorTable *table)
{
    uint32_t earlier = w.math.fault;
    w.math.fault = 0;
    int result = em_actor_collision_player_column(context, position, table);
    uint32_t mine = w.math.fault;
    w.math.fault = earlier ? earlier : mine;
    return mine ? -1 : result;
}

/* The player's wall-probe workers (EmPlayerStatesBinding.probes). One
 * context serves every slot of EmPlayerProbeWorkers, so these trampolines
 * reach the world's own storage. */
static struct {
    int (*pose)(void *context, EmPlayerLiveActor *actor, float blend);
    void *pose_context;
    EmPlayerLiveActor *actor;
} s_probe;

static int pw_move(void *c, const float position[3], const float target[3], unsigned mask,
                   EmPlayerProbeHit *hit)
{
    (void)c;
    return em_coll_move_player_move(&w.move_player, position, target, mask, hit);
}
static int pw_sweep(void *c, const float from[3], const float to[3], unsigned mask,
                    EmPlayerProbeHit *hit)
{
    (void)c;
    return em_coll_move_player_sweep(&w.move_player, from, to, mask, hit);
}
/* 001760C0(p, at, 1, height): 001764E0 and 00176C80 pass 1. */
static int pw_column(void *c, const float at[3], float height, EmPlayerProbeHit *hit)
{
    (void)c;
    return em_actor_collision_player_001760C0(&w.ground_player, NULL, at, 1, height, hit);
}
/* 00176180 (the class-2 hull shove after a mask-bit-0 hit) and 001762E0's
 * area-2 target shove: untranslated; AREA11 publishes no class-2 owner and
 * is not area 2, so neither is reached. Reaching one faults. */
static int pw_hull_shove(void *c, const float target[3])
{
    (void)c; (void)target;
    fprintf(stderr, "collision world: 00176180 (class-2 hull shove) is not translated\n");
    return -1;
}
static int pw_target_shove(void *c)
{
    (void)c;
    fprintf(stderr, "collision world: 001762E0's area-2 target shove is not translated\n");
    return -1;
}
/* 00174A50(p, 12.0) from 001756E0: the bound row request over the record. */
static int pw_pose(void *c, float blend)
{
    (void)c;
    if (!s_probe.pose || !s_probe.actor) return -1;
    return s_probe.pose(s_probe.pose_context, s_probe.actor, blend);
}
static float pw_sqrt(void *c, float x) { (void)c; return em_sdk_math_original_float_0011E748(&w.math, x); }
static float pw_atan(void *c, float x) { (void)c; return em_sdk_math_original_float_0011DBB8(&w.math, x); }

void em_collision_world_bind_player_pose(int (*pose)(void *context, EmPlayerLiveActor *actor,
                                                     float blend),
                                         void *context, EmPlayerLiveActor *actor)
{
    s_probe.pose = pose;
    s_probe.pose_context = context;
    s_probe.actor = actor;
}

int em_collision_world_bind_player(EmPlayerStatesBinding *b, const void *self, uint8_t cls)
{
    if (!w.loaded || !b) return -1;
    /* 0019AD00 / 0019AFE0 with the live record as the query actor. */
    w.move_player = (EmCollMovePlayer){ &w.move, &w.move_scratch, (const EmPlayerLiveActor *)self,
                                        self };
    b->probes = (EmPlayerProbeWorkers){ NULL, pw_move, pw_sweep, pw_column, pw_hull_shove,
                                        pw_target_shove, pw_pose, pw_sqrt, pw_atan };
    w.probe_workers = (EmCollProbeWorkers){ &w.seg, em_coll_segment_face_worker,
                                            em_coll_segment_round_worker };
    w.probe_player = (EmCollProbePlayer){ &w.probe, &w.probe_workers, &w.state,
                                          { self, cls, NULL } };
    w.ground_player = (EmActorCollisionPlayer){ &w.acw, { self, cls, NULL }, NULL };
    w.column_math = (EmCollColumnMath){ em_sdk_math_original_float_0011E748,
                                        em_sdk_math_original_float_0011DBB8, &w.math };
    w.column_player = (EmActorCollisionPlayerColumn){ &w.acw, &w.column_math };
    b->ground = em_actor_collision_player_ground;
    b->ground_context = &w.ground_player;
    b->grid = w.acw.grid;
    b->head = em_coll_probe_player_head;
    b->object = em_coll_probe_player_object;
    b->probe_context = &w.probe_player;
    b->link_test = em_actor_collision_player_link;
    b->link_context = &w.ground_player;
    b->column = player_column;
    b->column_context = &w.column_player;
    /* 00175CF0's SDK calls: the original translations over the world's SDK
     * context (the user's table and soft-float exports), the same one the
     * column's sqrt/atan use (docs/SDK_MATH_ORIGINAL.md; oracles
     * test_sdk_math_original_reference, test_sdk_soft_float_reference).
     * Their faults land in w.math.fault, which the floor service and the
     * fall check test after every call. atan2f 0011E620 and sqrtf 0011E748
     * are complete: their domain-error tails (which the route reaches, errno
     * 0x21 from beat 03 on, SDK_SOFT_FLOAT.md section 5) run the bound
     * soft-float workers. They run only once FLOOR engages. */
    b->atan2 = em_sdk_math_original_float_0011E620;
    b->tangent = em_sdk_math_original_float_0011E398;
    b->atan = em_sdk_math_original_float_0011DBB8;
    b->sqrt = em_sdk_math_original_float_0011E748;
    b->sdk_context = &w.math;
    b->sdk_fault = &w.math.fault;
    return 0;
}

const EmCollMoveWorld *em_collision_world_move(void) { return w.loaded ? &w.move : NULL; }
EmCollMoveScratch *em_collision_world_move_scratch(void) { return w.loaded ? &w.move_scratch : NULL; }
const EmCollSegment *em_collision_world_segment(void) { return w.loaded ? &w.seg : NULL; }
EmActorCollisionWorld *em_collision_world_cells(void) { return w.loaded ? &w.acw : NULL; }
EmSdkMathContext *em_collision_world_sdk(void) { return w.loaded ? &w.math : NULL; }
const EmCollColumnMath *em_collision_world_column_math(void)
{
    return w.loaded && w.column_math.sqrt ? &w.column_math : NULL;
}
EmActorCollisionPlayer *em_collision_world_player(void)
{
    return w.loaded && w.ground_player.world ? &w.ground_player : NULL;
}
EmActorCollisionPlayerColumn *em_collision_world_column_player(void)
{
    return w.loaded && w.column_player.world ? &w.column_player : NULL;
}
EmCollMovePlayer *em_collision_world_move_player(void)
{
    return w.loaded && w.move_player.world ? &w.move_player : NULL;
}

/* ---- instrumentation ------------------------------------------------------ */

static void put_u32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("EMCW", 1, 4, f);
    put_u32(f, 1);
    put_u32(f, w.cells.size);
    fwrite(w.cells.bytes, 1, w.cells.size, f);
    const EmActorClassList *list = &w.lists.list[EM_ACTOR_LIST_CLASS4];
    put_u32(f, (uint32_t)list->published);
    for (int j = 0; j < list->published; ++j) {
        const EmActor *a = em_actor_class_list_entry(&w.lists, EM_ACTOR_LIST_CLASS4, j);
        put_u32(f, a ? (uint32_t)a->uid | (uint32_t)a->cls << 16 : 0xFFFFFFFFu);
    }
    fclose(f);
}

void em_collision_world_dump_if_requested(void)
{
    const char *path = getenv("EM_COLL_WORLD_DUMP");
    if (!path || !*path || !w.loaded) return;
    if (!w.dumped_first) {
        char first[1024];
        snprintf(first, sizeof first, "%s.first", path);
        dump(first);
        w.dumped_first = 1;
    }
    dump(path);
}
