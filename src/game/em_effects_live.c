/* em_effects_live.c - the effect originals bound live (see em_effects_live.h,
 * docs/EFFECT_MANAGER.md section 8).
 *
 * Nothing here computes: every worker below stands for the original callee
 * it names and calls that callee's one translation over the shared storage
 * (the render context of em_render_context_live, the actor pool, the scene
 * state, the player record and Roger's). */
#include "game/em_effects_live.h"

#include "game/em_area11_roger.h"
#include "game/em_aim_fire_tables.h"
#include "game/em_area01_render_hud.h"
#include "game/em_area02_misc.h"
#include "game/em_level8_port.h"
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
#include "game/em_sfx.h"
#include "game/em_sfx_bank.h"

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
    uint32_t parent24;          /* KIND_OTHER: first written by its actual spawner */
    int parent24_valid;
} Slot;

static struct {
    int loaded, load_tried;
    uint8_t *elf;                             /* the sparse image the loaders read */
    EmEffectOriginalTables etables;
    EmEffectManagerTables mtables;
    EmEffectKindsTables ktables;
    EmHeadSpriteOriginalTables htables;
    uint8_t sources[0x480];                   /* D_002565E0..D_00256A60 */
    struct { uint32_t address, size; } window[64];   /* the exported blocks */
    uint32_t windows;
    /* 001CFBE0 source blocks an overlay owner hands 001D04B0 (the AREA11
     * flame's D_00828340): their 0x90 bytes, readable by the page. */
    struct { uint32_t address; uint8_t bytes[0x90]; } overlay_source[4];
    uint32_t overlay_sources;

    EmActorPool *pool;
    EmSceneState *scene;
    EmEffectsLiveBind bind;
    EmEffectsLiveOtherTick other_tick;
    EmEffectsLiveHeadRecord head_record;
    EmEffectsLiveHeadBytes head_bytes;
    void *head_context;
    uint8_t head_image[EM_ACTOR_RECORD_SIZE];
    EmEffectsLiveParticleCall particle_call;     /* 001F3620 / 001F3E30 */
    void *particle_context;
    void *other_context;
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
    /* The last barrel's 001F4D40 calls (the capture log): each call's
     * colour words, the rand() value it drew, the rgb it handed 001CD520
     * and the emitted primitive's colour words +0x10..+0x1C. */
    EmEffectsLiveMarker markers[EM_EFFECTS_LIVE_MARKERS];
    int nmarkers;
    uint32_t markers_frame;
    int32_t marker_value;
    u32 marker_rgb;
    const uint8_t *marker_packet;
    /* 001D04B0's last call (the capture log): the packets its 001CFBE0
     * opened through 001CB5F0, in order (7, 1 and 16 qwords). */
    int overlay_recording;
    const uint8_t *opk[4];
    uint32_t opk_size[4];
    int nopk;
    EmEffectsLiveOverlayLog overlay_log;
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
uint8_t *em_effects_live_scratch_3660(uint32_t address,uint32_t size)
{
    if (!size || address<0x70003660u || address-0x70003660u>=sizeof S.xs.spad3660 ||
        size>sizeof S.xs.spad3660-(address-0x70003660u)) return NULL;
    return (uint8_t *)S.xs.spad3660+address-0x70003660u;
}
int em_effects_live_attached(void) { return S.attached; }

static u32 crc32_bytes(u32 crc, const uint8_t *p, u32 n);
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
    ok = ok && count > 0 && count <= sizeof S.window / sizeof S.window[0] &&
         0x10u + 0x10u * count <= (u32)size;
    for (u32 i = 0; ok && i < count; ++i) {
        const uint8_t *e = file + 0x10 + 0x10 * i;
        const u32 address = rd32(e), bytes = rd32(e + 4), at = rd32(e + 8);
        ok = address >= 0x00100000u && ELF_AT(address) + bytes <= ELF_SIZE && at <= (u32)size &&
             bytes <= (u32)size - at;
        if (ok) memcpy(elf + ELF_AT(address), file + at, bytes);
        if (ok && S.windows < sizeof S.window / sizeof S.window[0]) {
            S.window[S.windows].address = address;
            S.window[S.windows].size = bytes;
            S.windows++;
        }
    }
    free(file);
    /* The chain page's five program packets (the page CALLs them). */
    static const u32 programs[5][2] = { {0x00231770u, 0xDD0u}, {0x00233290u, 0x570u}, {0x00233800u, 0xDE0u},
                                        {0x00230800u, 0xF70u}, {0x00232540u, 0xD50u} };
    for (unsigned k = 0; ok && k < 5; ++k) {
        int placed = 0;
        for (u32 i = 0; i < S.windows; ++i)
            placed |= S.window[i].address == programs[k][0] && S.window[i].size == programs[k][1];
        if (!placed) {
            fprintf(stderr, "effects: %s lacks the program packet %08X (re-run tools/export_effect_tables.py)\n",
                    EM_EFFECTS_LIVE_TABLES_PATH, (unsigned)programs[k][0]);
            ok = 0;
        }
    }
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

const uint8_t *em_effects_live_window(uint32_t address, uint32_t size)
{
    if (!S.loaded) return NULL;
    for (uint32_t i = 0; i < S.windows; ++i)
        if (address >= S.window[i].address && size <= S.window[i].size &&
            address - S.window[i].address <= S.window[i].size - size)
            return S.elf + ELF_AT(address);
    for (uint32_t i = 0; i < S.overlay_sources; ++i)
        if (address >= S.overlay_source[i].address && size <= 0x90u &&
            address - S.overlay_source[i].address <= 0x90u - size)
            return S.overlay_source[i].bytes + (address - S.overlay_source[i].address);
    return em_aim_fire_tables_bytes(address, size);
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
    S.eglobals.d8101E4 = g.cam.top_mode;   /* the camera block's +0x04 (em_camera_live) */
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

int em_effects_live_node_identity(const EmActor *a, uint32_t *address)
{
    if (!S.attached || S.fault || !S.pool || !a) return 0;
    /* Integer bounds also reject an unrelated native object without C's
     * undefined ordered pointer comparison across distinct allocations. */
    uintptr_t p = (uintptr_t)a, lo = (uintptr_t)S.pool->records;
    if (p < lo || p-lo >= sizeof S.pool->records || (p-lo) % sizeof *a) return 0;
    size_t i = (p-lo) / sizeof *a;
    if (!a->allocated || a->self != a || S.slot[i].generation != a->generation ||
        S.slot[i].kind == KIND_NONE) return 0;
    if (address) *address = EM_ACTOR_POOL_BASE + (uint32_t)i * EM_ACTOR_RECORD_SIZE;
    return 1;
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

static void *field_span(void *p, uint32_t offset, size_t count, uint32_t field, size_t bytes)
{
    return offset >= field && count <= bytes && (size_t)(offset-field) <= bytes-count
        ? (uint8_t *)p + (offset-field) : NULL;
}
void *em_effects_live_node_field(uint32_t address, size_t size, int write)
{
    if (!S.attached || S.fault || !size || address < EM_ACTOR_POOL_BASE) return NULL;
    uint32_t index = (address-EM_ACTOR_POOL_BASE) / EM_ACTOR_RECORD_SIZE;
    uint32_t offset = (address-EM_ACTOR_POOL_BASE) % EM_ACTOR_RECORD_SIZE;
    if (index >= EM_ACTOR_POOL_CAPACITY || size > EM_ACTOR_RECORD_SIZE-offset) return NULL;
    Slot *s = &S.slot[index]; EmActor *a = &S.pool->records[index];
    if (!a->allocated || s->generation != a->generation || s->kind == KIND_NONE) return NULL;
    void *p;
#define FIELD(value,off) do { p=field_span(&(value),offset,size,(off),sizeof(value)); if(p)return p; }while(0)
    /* The pool owns these header bytes as one contiguous original range. */
    _Static_assert(offsetof(EmActor,callback)==0x10, "pool header layout");
    p=field_span(&a->status,offset,size,0,0x14); if(p)return p;
    if (offset >= 0x24 && (uint64_t)offset+size <= 0x28) {
        if (s->kind == KIND_HEAD) return field_span(&s->h.owner,offset,size,0x24,4);
        if (write) {
            if (offset != 0x24 || size != 4) return NULL;
            return &s->parent24;
        }
        return s->parent24_valid ? field_span(&s->parent24,offset,size,0x24,4) : NULL;
    }
    FIELD(a->flags2,0x2E); FIELD(a->w30,0x30); FIELD(a->h36,0x36);
    FIELD(s->e.live38,0x38); FIELD(a->h52,0x52); FIELD(a->kind,0x54);
    FIELD(a->link,0x56); FIELD(a->w58,0x58); FIELD(a->w5C,0x5C);
    FIELD(a->f60,0x60); FIELD(a->f80,0x80); FIELD(a->w90,0x90);
    FIELD(a->h94,0x94); FIELD(a->h96,0x96); FIELD(a->b98,0x98);
    FIELD(a->b99,0x99); FIELD(a->table_index,0x9A); FIELD(a->b9C,0x9C);
    FIELD(a->b9D,0x9D); FIELD(a->b9E,0x9E);
    FIELD(a->pos,0xB0); FIELD(a->rot,0xC0);
    if (s->kind == KIND_HEAD) {
        FIELD(s->h.local,0xA0); FIELD(s->h.matrix,0xD0); FIELD(s->h.bone,0x28);
        FIELD(s->h.timer,0x1F0); FIELD(s->h.ramp,0x244); FIELD(s->h.scalar,0x24C);
    } else {
        FIELD(s->e.matrix,0xD0);
        if (s->kind == KIND_DRIVER) {
            p=field_span(&s->e.work.seed,offset,size,0x1F0,16); if(p)return p;
            FIELD(s->e.work.accumulator,0x244); FIELD(s->e.work.fraction,0x24C);
        } else FIELD(a->scratch,0x1F0);
    }
#undef FIELD
    return NULL;
}
int em_effects_live_node_written(uint32_t address, size_t size)
{
    if (address < EM_ACTOR_POOL_BASE || size != 4 ||
        (address-EM_ACTOR_POOL_BASE)%EM_ACTOR_RECORD_SIZE != 0x24 ||
        !em_effects_live_node_field(address,size,1)) return -1;
    Slot *s=&S.slot[(address-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE];
    if (s->kind != KIND_HEAD) s->parent24_valid=1;
    return 0;
}
size_t em_effects_live_node_regions(uint32_t address, EmEffectsLiveNodeRegion *regions, size_t capacity)
{
    if (address < EM_ACTOR_POOL_BASE ||
        (address-EM_ACTOR_POOL_BASE)%EM_ACTOR_RECORD_SIZE ||
        !em_effects_live_node_field(address,0x14,0)) return 0;
    static const struct { uint16_t offset, size; } fields[]={
        {0,0x14},{0x24,4},{0x2E,2},{0x30,4},{0x36,2},{0x38,4},
        {0x52,2},{0x54,2},{0x56,2},{0x58,4},{0x5C,4},{0x60,0x20},
        {0x80,0x10},{0x90,4},{0x94,2},{0x96,2},{0x98,1},{0x99,1},
        {0x9A,1},{0x9C,1},{0x9D,1},{0x9E,1},{0xB0,16},{0xC0,16},{0xD0,64}
    };
    size_t count=0;
#define REGION(off,len) do { \
        void *bytes=em_effects_live_node_field(address+(off),(len),0); \
        if(bytes) { \
            if(regions && count<capacity) regions[count]=(EmEffectsLiveNodeRegion){address+(off),(len),bytes}; \
            ++count; \
        } \
    } while(0)
    for(size_t i=0;i<sizeof fields/sizeof fields[0];++i) REGION(fields[i].offset,fields[i].size);
    Slot *s=&S.slot[(address-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE];
    if(s->kind==KIND_HEAD) {
        REGION(0xA0,16);REGION(0x28,4);REGION(0x1F0,4);REGION(0x244,4);REGION(0x24C,4);
    } else if(s->kind==KIND_DRIVER) {
        REGION(0x1F0,16);REGION(0x244,4);REGION(0x24C,4);
    } else REGION(0x1F0,0x100);
#undef REGION
    return count;
}
int32_t *em_effects_live_d275C04(void)
{
    return S.attached && !S.fault ? &S.eglobals.d275C04 : NULL;
}
int em_effects_live_set_other_tick(EmEffectsLiveOtherTick worker, void *context)
{
    if (!S.attached || S.fault) return -1;
    S.other_tick=worker;S.other_context=context;return 0;
}
int em_effects_live_set_head_owner(EmEffectsLiveHeadRecord record, EmEffectsLiveHeadBytes bytes, void *context)
{
    if (!S.attached || S.fault || !record || !bytes) return -1;
    S.head_record=record;S.head_bytes=bytes;S.head_context=context;return 0;
}
int em_effects_live_set_particle_call(EmEffectsLiveParticleCall call, void *context)
{
    if (!S.attached || S.fault) return -1;
    S.particle_call=call;S.particle_context=context;return 0;
}
size_t em_effects_live_particle_regions(EmEffectsLiveNodeRegion *out, size_t capacity)
{
    if (!S.attached || S.fault) return 0;
    /* D_00275C44 is the barrel's copy while 001F0360 runs (it is synced
     * from and back to 001F3FA0's around it), else 001F3FA0's. */
    const EmEffectsLiveNodeRegion r[3] = {
        {EM_EFFECT_MANAGER_ENTITY_BASE, sizeof S.particles.record, S.particles.record},
        {0x00275C40u, 4, &S.kglobals.d275C40},
        {0x00275C44u, 4, S.recording ? &S.mglobals.d275C44 : &S.kglobals.d275C44}};
    for (size_t k = 0; k < 3 && k < capacity; ++k) out[k] = r[k];
    return 3;
}

/* ------------------------------------------------------------ workers */

/* 001F40C0's 001F3620(entity, kind) and 001F3E30(...): the particle
 * owner's translations (em_area00_fx_debris) through the hook's
 * composition (em_aim_fire_runtime), over the records' bytes; the entity's
 * +0x80 / +0x82 halfwords cross as the record's own bytes. Without the
 * hook a live entity is a fault, as before. */
static int w_001F3620(void *ctx, uint32_t entity_address, int32_t kind, EmEffectManagerEntity *entity)
{
    (void)ctx;
    if (!S.particle_call || !entity || entity_address < EM_EFFECT_MANAGER_ENTITY_BASE) return -1;
    const uint32_t i = (entity_address - EM_EFFECT_MANAGER_ENTITY_BASE) / EM_EFFECT_KINDS_PARTICLE_BYTES;
    if (i >= EM_EFFECT_MANAGER_ENTITIES ||
        (entity_address - EM_EFFECT_MANAGER_ENTITY_BASE) % EM_EFFECT_KINDS_PARTICLE_BYTES)
        return -1;
    uint8_t *p = S.particles.record[i];
    p[0x80] = (uint8_t)entity->live; p[0x81] = (uint8_t)((uint16_t)entity->live >> 8);
    p[0x82] = (uint8_t)entity->kind; p[0x83] = (uint8_t)((uint16_t)entity->kind >> 8);
    const uint32_t a[5] = {entity_address, (uint32_t)kind, 0, 0, 0};
    const int rc = S.particle_call(S.particle_context, 0x001F3620u, a);
    entity->live = (int16_t)(uint16_t)(p[0x80] | p[0x81] << 8);
    entity->kind = (int16_t)(uint16_t)(p[0x82] | p[0x83] << 8);
    return rc;
}
static int w_001F3E30(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t t0)
{
    (void)ctx;
    if (!S.particle_call) return -1;
    const uint32_t a[5] = {a0, a1, (uint32_t)a2, (uint32_t)a3, (uint32_t)t0};
    return S.particle_call(S.particle_context, 0x001F3E30u, a);
}

static int w_rand(void *ctx, int32_t *value)
{
    (void)ctx;
    *value = (int32_t)em_random_next();
    if (S.marker_set) S.marker_value = *value;   /* 001F4D40's draw (the capture log) */
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
    EmPointLightPool *pool = em_rcl_point_lights();   /* the context's +0x210.. */
    if (!pool) return -1;
    (void)em_point_light_register(pool, pos, color, type, fa, fb);
    return 0;
}

/* 001EF940's 001FBF50(scratch, &a, &b, 0, rec +0x28, rec +0x2C) and
 * 001FB9F0(rec +0x24, 0x1000, a, b): the positional submit em_sfx_play_at
 * runs for 001FBD50 (the verified 001FBF50 gain words over the listener
 * em_sfx_listener mirrors), here split at the original's call boundary.
 * em_sfx's solver is 001FBF50 with f13 = 4096.0 and flat2d = 0 (its every
 * caller's), so another volume faults. The first-level record with a sound
 * is 0x80000027 (0x14A, 300.0, 4096.0: the flame's contact, 001EFE00). */
static int w_001FBF50(void *ctx, const float pos[4], float f12, float f13, int32_t *a, int32_t *b, int32_t *result)
{
    (void)ctx;
    *a = *b = *result = 0;
    if (fbits(f13) != 0x45800000u) return -1;
    float gl, gr;
    if (!em_sfx_compute_gains(pos, f12, &gl, &gr)) return 0;
    *a = em_sfx_request_word(gl);
    *b = em_sfx_request_word(gr);
    *result = 1;
    return 0;
}
static int w_001FB9F0(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    (void)ctx;
    if (id < 0 || a1 != 0x1000) return -1;
    em_sfx_submit_001FB9F0((unsigned)id, a2, a3);
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

/* The untranslated handler checked to be packet-only: 001EC270
 * (0x80000012, subtype 0xB; its split listing). It stores only the work
 * block's +0x1F4 (D_00275C34 + 4, twice: the LCG), which 001EA240 rewrites
 * from +0x1F0 before every handler call and reads nowhere else, and calls
 * only 001CFB50 (it rewrites D_0081F8F0 +0x00..+0x57 in full; in the port
 * its only reader is the 001CFBE0 each translated handler calls right after
 * it) and 001CFBE0 (the packets). Skipping it changes only the packets. The
 * skid's 001EAD70 (0x80000033, subtype 1), the other one, runs its
 * translation since the BRANCH step (handler_001EAD70). */
static int packet_only_handler(u32 handler)
{
    return handler == 0x001EC270u;
}

/* D_00255434[subtype]: the handler, em_effect_kinds' translation. The
 * packet-only handler is the counted gap (effects_gap: its packets are
 * missing, nothing else differs). Every other handler em_effect_kinds does
 * not translate faults: none is checked, and some do more than draw
 * (001EF510, subtype 6, spawns 001EFD90(0x80000036) from inside the
 * handler). The port reaches none of them in AREA11 (EFFECT_MANAGER.md
 * 8.2). */
/* The subtype-0x0D handler 001EBD20 (the box break's second effect
 * 0x80000015, 001551B0; BRANCH br_04 / br_06): em_area02_misc's
 * translation, its one owner, run over the views it addresses and with its
 * three callees bound here (handler_001EBD20_call):
 *   0x00275C30 / 0x00275C34  the node and its work block (node + 0x1F0), as
 *                            001EA240 sets them for the handler;
 *   node +0x38, +0x244, +0x24C  the node's live flag and the work block's
 *                            accumulator and fraction (read);
 *   0x70003400..+0x3F       the scratch matrix it copies the node's +0xD0 to
 *                            and moves up by 5.0 (+0x34);
 *   a stack frame below `sp`.
 * a0 is node + 0xD0 (001EA240's argument). */
static int w_001CFB50(void *ctx, u32 dst, u32 a1, const float src[16], u32 f12, u32 f13, u32 f14,
                      u32 f15, u32 f16);
static int w_001CFBE0(void *ctx, int32_t id, int32_t kind, u32 source, u32 xf, int32_t copy);
enum { HANDLER_SP = 0x01FFF000u, HANDLER_FRAME = 0x40u };
typedef struct {
    const EmEffectOriginalNode *node;
    u32 node_address;
    uint8_t *scratch;
} HandlerCall;

static int handler_001EBD20_call(void *ctx, EmArea02MiscCall *c)
{
    HandlerCall *h = ctx;
    if (c->fn == 0x00102958u) {
        /* copy_qw4(0x70003400, node + 0xD0): the node's matrix. */
        if (c->na != 2 || (u32)c->a[0] != 0x70003400u || (u32)c->a[1] != h->node_address + 0xD0u) return -1;
        memcpy(h->scratch, h->node->matrix, 0x40);
        return 0;
    }
    if (c->fn == 0x001CFB50u) {
        /* 001CFB50(D_0081F8F0, 0, 0x70003400; f12, f13, 1.0, 1e-6, 6.0). */
        if (c->na != 3 || c->nf != 5 || (u32)c->a[2] != 0x70003400u) return -1;
        float src[16];
        memcpy(src, h->scratch, sizeof src);
        return w_001CFB50(NULL, (u32)c->a[0], (u32)c->a[1], src, c->f[0], c->f[1], c->f[2], c->f[3], c->f[4]);
    }
    if (c->fn == 0x001CFBE0u) {
        if (c->na != 5) return -1;
        return w_001CFBE0(NULL, (int32_t)(u32)c->a[0], (int32_t)(u32)c->a[1], (u32)c->a[2], (u32)c->a[3],
                          (int32_t)(u32)c->a[4]);
    }
    return -1;
}

static int handler_001EBD20(EmEffectOriginalNode *node, int32_t depth, EmEffectOriginalWork *work)
{
    Slot *s = slot_of_node(node);
    if (!s || !work || work != &node->work) return fail(0x001EBD20u, "001EBD20 without its node");
    const u32 at = em_actor_pool_address(S.pool, actor_of(s));
    u32 globals[2] = {at, at + 0x1F0u};          /* D_00275C30, D_00275C34 */
    uint8_t scratch[0x40], stack[HANDLER_FRAME];
    memset(scratch, 0, sizeof scratch);
    memset(stack, 0, sizeof stack);
    int32_t live38 = node->live38;
    float accumulator = work->accumulator, fraction = work->fraction;
    const EmArea02MiscRegion regions[] = {
        {0x00275C30u, 8, (uint8_t *)globals},
        {at + 0x38u, 4, (uint8_t *)&live38},
        {at + 0x244u, 4, (uint8_t *)&accumulator},
        {at + 0x24Cu, 4, (uint8_t *)&fraction},
        {0x70003400u, 0x40, scratch},
        {HANDLER_SP - HANDLER_FRAME, HANDLER_FRAME, stack},
    };
    HandlerCall hc = {node, at, scratch};
    EmArea02Misc m;
    memset(&m, 0, sizeof m);
    m.regions = regions;
    m.region_count = sizeof regions / sizeof regions[0];
    m.call = handler_001EBD20_call;
    m.ctx = &hc;
    m.sp = HANDLER_SP;
    if (em_area02_misc_001EBD20(&m, at + 0xD0u, (u32)depth) < 0 || m.fault)
        return fail(m.fault_address ? m.fault_address : 0x001EBD20u, "001EBD20 faulted (em_area02_misc)");
    return 0;
}

/* The subtype-1 handler 001EAD70 (the skid 0x80000033 a landing spawns:
 * BRANCH br_05's step-off from the corridor box): em_level8_port's
 * translation, its one owner (byte-matched decomp C; test_level8_port_
 * reference), over the views it addresses: D_00275C34 (the work block,
 * node + 0x1F0), the work block's +4 (the LCG copy 001EA240 wrote, which it
 * steps twice) and +0x54 (the accumulator); its 001CFB50 / 001CFBE0 are this
 * module's (the two pairs: kind 1 with D_002556B0, kind 0 with D_00255740,
 * the exported blocks). a0 is node + 0xD0. */
typedef struct {
    const EmEffectOriginalNode *node;
    EmEffectOriginalWork *work;
    u32 node_address, d275C34;
} SkidCall;

static uint8_t *skid_bytes(void *ctx, uint32_t address, uint32_t size)
{
    SkidCall *c = ctx;
    if (address == 0x00275C34u && size == 4) return (uint8_t *)&c->d275C34;
    if (address == c->d275C34 + 4u && size == 4) return (uint8_t *)&c->work->seed_copy;
    if (address == c->d275C34 + 0x54u && size == 4) return (uint8_t *)&c->work->accumulator;
    return NULL;
}

static int skid_001CFB50(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, float f4, float f5, float f6,
                         float f7)
{
    SkidCall *c = ctx;
    if ((u32)n2 != c->node_address + 0xD0u) return -1;
    u32 b[5];
    const float f[5] = {f3, f4, f5, f6, f7};
    memcpy(b, f, sizeof b);
    return w_001CFB50(NULL, a0, (u32)n1, c->node->matrix, b[0], b[1], b[2], b[3], b[4]);
}

static int skid_001CFBE0(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4)
{
    (void)ctx;
    return w_001CFBE0(NULL, n0, n1, a2, a3, n4);
}

static int handler_001EAD70(EmEffectOriginalNode *node, int32_t depth, EmEffectOriginalWork *work)
{
    Slot *s = slot_of_node(node);
    if (!s || !work || work != &node->work) return fail(0x001EAD70u, "001EAD70 without its node");
    const u32 at = em_actor_pool_address(S.pool, actor_of(s));
    SkidCall c = {node, work, at, at + 0x1F0u};
    EmLevel8PortHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = &c;
    h.bytes = skid_bytes;
    h.w_001CFB50 = skid_001CFB50;
    h.w_001CFBE0 = skid_001CFBE0;
    EmLevel8PortFault fault = {0, 0};
    if (em_level8_port_001EAD70(&h, (int32_t)(at + 0xD0u), depth, &fault) < 0)
        return fail(fault.address ? fault.address : 0x001EAD70u, "001EAD70 faulted (em_level8_port)");
    return 0;
}

/* The splash handlers 001EAF00 / 001EAF80 / 001EB020 (D_00255434
 * entries, reached in AREA01 when the player wades a floor field:
 * 001A8840's contact event 6): em_area01_render_hud's translations, their
 * one owner (test_area01_render_reference.py runs the original
 * instructions), over the views they address: D_00275C34 (the work block,
 * node + 0x1F0), the work block's +4 (the LCG copy 001EA240 wrote; the
 * random rounds step it), +0x54 (the accumulator) and +0x5C (the
 * fraction); their 001CFB50 / 001CFBE0 are this module's. a0 is node +
 * 0xD0 (the node's matrix), a1 the depth. */
typedef struct {
    const EmEffectOriginalNode *node;
    EmEffectOriginalWork *work;
    u32 node_address, d275C34;
} SplashCall;

static uint8_t *splash_bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    SplashCall *c = ctx;
    if (size != 4) return NULL;
    if (address == 0x00275C34u) return write ? NULL : (uint8_t *)&c->d275C34;
    if (address == c->d275C34 + 4u) return (uint8_t *)&c->work->seed_copy;
    if (address == c->d275C34 + 0x54u) return write ? NULL : (uint8_t *)&c->work->accumulator;
    if (address == c->d275C34 + 0x5Cu) return write ? NULL : (uint8_t *)&c->work->fraction;
    return NULL;
}

static int splash_001CFB50(void *ctx, uint32_t dst, int32_t a1, uint32_t src, uint32_t f12, uint32_t f13,
                           uint32_t f14, uint32_t f15, uint32_t f16)
{
    SplashCall *c = ctx;
    if (src != c->node_address + 0xD0u) return -1;
    return w_001CFB50(NULL, dst, (u32)a1, c->node->matrix, f12, f13, f14, f15, f16);
}

static int splash_001CFBE0(void *ctx, uint32_t a0, int32_t kind, uint32_t table, uint32_t xf, int32_t t0)
{
    (void)ctx;
    return w_001CFBE0(NULL, (int32_t)a0, kind, table, xf, t0);
}

static int splash_handler(u32 handler)
{
    return handler == 0x001EAF00u || handler == 0x001EAF80u || handler == 0x001EB020u;
}

static int handler_splash(u32 handler, EmEffectOriginalNode *node, int32_t depth, EmEffectOriginalWork *work)
{
    Slot *s = slot_of_node(node);
    if (!s || !work || work != &node->work) return fail(handler, "splash handler without its node");
    const u32 at = em_actor_pool_address(S.pool, actor_of(s));
    SplashCall c = {node, work, at, at + 0x1F0u};
    EmArea01RenderHud h;
    memset(&h, 0, sizeof h);
    h.core.world.view = splash_bytes;
    h.core.world.view_ctx = &c;
    h.workers.ctx = &c;
    h.workers.w_001CFB50 = splash_001CFB50;
    h.workers.w_001CFBE0 = splash_001CFBE0;
    const u32 a0 = at + 0xD0u, a1 = (u32)depth;
    int rc = handler == 0x001EAF00u ? em_area01_render_001EAF00(&h, a0, a1)
           : handler == 0x001EAF80u ? em_area01_render_001EAF80(&h, a0, a1)
                                    : em_area01_render_001EB020(&h, a0, a1);
    if (rc < 0 || h.core.fault.code)
        return fail(h.core.fault.address ? h.core.fault.address : handler, "splash handler faulted (em_area01_render_hud)");
    return 0;
}

static int w_handler(void *ctx, u32 handler, EmEffectOriginalNode *node, int32_t depth,
                     EmEffectOriginalWork *work)
{
    (void)ctx;
    if (em_effect_kinds_translates(handler))
        return em_effect_kinds_handler(&S.k, handler, node->matrix, depth, work);
    if (handler == 0x001EBD20u) return handler_001EBD20(node, depth, work);
    if (handler == 0x001EAD70u) return handler_001EAD70(node, depth, work);
    if (splash_handler(handler)) return handler_splash(handler, node, depth, work);
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
    /* The source block: the exported handler sources (D_002565E0.. and
     * since chain step AIMCAM's fix round D_00255620 / D_002560D0 /
     * D_00256160, since AIMLIVE's fix round D_002561F0 and D_00255590,
     * the skid's D_002556B0 / D_00255740 and the AREA01 splash handlers'
     * D_002557D0 + 0x90 n (n = 0..5), export_effect_tables.py), 0x90 bytes. */
    const uint8_t *bytes = source >= 0x002565E0u && source - 0x002565E0u <= sizeof S.sources - 0x90u
                               ? S.sources + (source - 0x002565E0u)
                           : (source == 0x00255620u || source == 0x002560D0u || source == 0x00256160u ||
                              source == 0x002561F0u || source == 0x00255590u || source == 0x002563A0u ||
                              source == 0x00256430u || source == 0x002556B0u || source == 0x00255740u ||
                              (source >= 0x002557D0u && source <= 0x00255AA0u && (source - 0x002557D0u) % 0x90u == 0))
                               ? em_effects_live_window(source, 0x90)
                               : NULL;
    if (xf != EM_EFFECT_KINDS_XF || !bytes)
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
    const EmHeadSpriteOriginalSource st = {source, bytes};
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
    S.marker_value = -1;
    S.marker_packet = NULL;
    int r = em_effect_manager_001F4D40(&S.m, MARKER_POSITION_HANDLE, colour, f12, f13);
    S.marker_set = 0;
    if (r >= 0 && S.recording && S.nmarkers < EM_EFFECTS_LIVE_MARKERS) {
        EmEffectsLiveMarker *m = &S.markers[S.nmarkers++];
        memcpy(m->colour, colour, sizeof m->colour);
        m->value = S.marker_value;
        m->rgb = S.marker_rgb;
        m->emitted = S.marker_packet != NULL;
        for (int i = 0; i < 4; ++i)
            m->packet[i] = S.marker_packet ? (u32)S.marker_packet[0x10 + 4 * i] |
                                                 (u32)S.marker_packet[0x11 + 4 * i] << 8 |
                                                 (u32)S.marker_packet[0x12 + 4 * i] << 16 |
                                                 (u32)S.marker_packet[0x13 + 4 * i] << 24
                                           : 0;
    }
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

/* 001CB5F0 for the head sprite's 001CFBE0: during 001D04B0 the packets
 * are noted for the capture log. */
static int w_head_001CB5F0(void *ctx, u32 chain, int32_t id, int32_t count, uint8_t **out)
{
    int r = em_packet_chain_w_001CB5F0(ctx, chain, id, count, out);
    if (r >= 0 && S.overlay_recording && S.nopk < 4) {
        S.opk[S.nopk] = *out;
        S.opk_size[S.nopk] = (u32)count * 16u;
        ++S.nopk;
    }
    return r;
}

/* 001CB5F0 for 001CD520 (the glow markers): noted during the barrel. */
static int w_sprite_001CB5F0(void *ctx, u32 chain, int32_t z, int32_t count, uint8_t **packet)
{
    int r = em_packet_chain_w_001CB5F0(ctx, chain, z, count, packet);
    if (r >= 0 && S.recording && S.nsp < 16) S.sp[S.nsp++] = *packet;
    if (r >= 0 && S.marker_set) S.marker_packet = *packet;
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
    S.marker_rgb = rgb;
    int32_t z = 0;
    if (em_player_equipment_001CD520(&S.sprite, a0, a1, S.marker, tex0, f12, f13, f14, rgb, &z) < 0)
        return -1;
    ++S.counters.sprites;
    return 0;
}

/* 001CD520 for the effect handlers (001EAB50's fading sprite at the node
 * matrix's translation row): the same translation over `point`. */
static int w_kinds_001CD520(void *ctx, int32_t a0, int32_t a1, const float point[4], uint64_t tex0,
                            uint64_t colour, u32 f12, u32 f13, u32 f14)
{
    (void)ctx;
    u32 at[4];
    memcpy(at, point, sizeof at);
    int32_t z = 0;
    /* Not in counters.sprites: that counts the barrel's glow markers. */
    return em_player_equipment_001CD520(&S.sprite, a0, a1, at, tex0, f12, f13, f14, colour, &z) < 0 ? -1 : 0;
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
    const uint8_t *p = em_area11_roger_slot_bytes(address, size);
    return p || !S.head_bytes ? p : S.head_bytes(S.head_context, address, size);
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
    ew->w_001FBF50 = w_001FBF50;
    ew->w_001FB9F0 = w_001FB9F0;
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
    kw->w_001CD520 = w_kinds_001CD520;
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
    mw->w_001F3620 = w_001F3620;
    mw->w_001F3E30 = w_001F3E30;
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
    hw->w_001CB5F0 = w_head_001CB5F0;
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

/* ------------------------------------------------------------ room lights */

/* 001D7FA0(pos, preset, 1, 1.0, 0.0) as 001F6640 calls it: the register on
 * the context's pool; its handle (-1 for a full pool) goes to the record. */
static int w_room_001D7FA0(void *ctx, const float pos[4], uint32_t template_address, const float color[4],
                           int32_t type, uint32_t f12, uint32_t f13, int32_t *handle)
{
    (void)ctx;
    (void)template_address;
    EmPointLightPool *pool = em_rcl_point_lights();
    if (!pool) return -1;
    *handle = em_point_light_register(pool, pos, color, type, bfloat(f12), bfloat(f13));
    return 0;
}

/* 001D80B0(handle) over the context (em_rcl_001D80B0). */
static int w_room_001D80B0(void *ctx, int32_t handle)
{
    (void)ctx;
    return em_rcl_001D80B0(handle);
}

/* 001D7BB0's tail (src/func_001D7BB0.c): 001F68B0() then 001F6E40() (the
 * room point-light lists, AREA11_POINT_LIGHT.md). See the header. */
int em_effects_live_room_lights(EmSceneState *scene)
{
    if (!scene || em_effects_live_load() < 0 || !em_rcl_point_lights()) return -1;
    /* 001F68B0's latch bytes, from their canonical storage; a key whose case
     * reads one that is not canonical faults. */
    static const struct { uint32_t address; uint16_t keys[2]; } latch[] = {
        {0x0081075Du, {0x0000, 0x0000}}, {0x0081075Eu, {0x0001, 0x0100}}, {0x00810761u, {0x0200, 0x0200}},
        {0x00810778u, {0x1301, 0x1301}}, {0x0081077Bu, {0x1301, 0x1301}}, {0x00810784u, {0x0002, 0x0E00}},
        {0x00810785u, {0x1100, 0x1100}}, {0x0081079Eu, {0x1301, 0x1301}}};
    EmEffectKindsGlobals globals;
    memset(&globals, 0, sizeof globals);
    globals.d810700 = scene->d810700;
    globals.d810701 = scene->d810701;
    const uint16_t key = (uint16_t)(scene->d810700 << 8 | scene->d810701);
    uint8_t *bytes[8] = {&globals.d81075D, &globals.d81075E, &globals.d810761, &globals.d810778,
                         &globals.d81077B, &globals.d810784, &globals.d810785, &globals.d81079E};
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *b = em_scene_progress_at(scene, latch[i].address, 1);
        if (b) {
            *bytes[i] = *b;
        } else if (latch[i].keys[0] == key || latch[i].keys[1] == key) {
            fprintf(stderr, "effects: 001F68B0 reads D_%08X for key %04X, which is not canonical\n",
                    (unsigned)latch[i].address, (unsigned)key);
            S.fault = latch[i].address;
            return -1;
        }
    }
    EmEffectKindsWorkers workers;
    memset(&workers, 0, sizeof workers);
    workers.w_001D7FA0 = w_room_001D7FA0;
    workers.w_001D80B0 = w_room_001D80B0;
    /* The lists window is S.ktables (D_0025AD80..D_0025D800, the one copy:
     * the records' +0x24 handles persist from area load to area load). */
    EmEffectKinds k = {&S.ktables, &globals, NULL, NULL, &workers, {0, 0}};
    if (em_effect_kinds_001F68B0(&k) < 0 || em_effect_kinds_001F6E40(&k) < 0) {
        fprintf(stderr, "effects: the room point-light lists faulted at %08X (code %d)\n",
                (unsigned)k.fault.address, (int)k.fault.code);
        S.fault = k.fault.address ? k.fault.address : 0x001D7BB0u;
        return -1;
    }
    return 0;
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
    memset(S.overlay_source, 0, sizeof S.overlay_source);
    S.overlay_sources = 0;
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
    S.other_tick = NULL;
    S.head_record = NULL;
    S.head_bytes = NULL;
    S.head_context = NULL;
    S.particle_call = NULL;
    S.other_context = NULL;
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

static int spawn_result(int status, u32 entry, EmEffectOriginalNode *n, u32 *out)
{
    if (finish(status, entry) < 0) return -1;
    if (out) {
        Slot *s = slot_of_node(n);
        if (n && !s) return fail(entry, "spawn result outside effect slots");
        *out = s ? em_actor_pool_address(S.pool, actor_of(s)) : 0;
    }
    return 0;
}

int em_effects_live_001EF9D0(uint32_t id, const float pos[4], uint32_t f12, uint32_t *out)
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    EmEffectOriginalNode *n = NULL;
    int status = em_effect_original_001EF9D0(&S.e, id, pos, bfloat(f12), &n);
    return spawn_result(status, 0x001EF9D0u, n, out);
}

int em_effects_live_001EFD90_result(uint32_t id, const float pos[4], const float rot[4], uint32_t *out)
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    EmEffectOriginalNode *n = NULL;
    int status = em_effect_original_001EFD90(&S.e, id, pos, rot, &n);
    return spawn_result(status, 0x001EFD90u, n, out);
}

int em_effects_live_001EFD90(uint32_t id, const float pos[4], const float rot[4])
{
    return em_effects_live_001EFD90_result(id, pos, rot, NULL);
}

int em_effects_live_001EFD20_result(uint32_t id, const float pos[4], uint32_t *out)
{
    READY();
    if (view_refresh() < 0) return -1;
    globals_refresh();
    EmEffectOriginalNode *n = NULL;
    int status = em_effect_original_001EFD20(&S.e, id, pos, &n);
    return spawn_result(status, 0x001EFD20u, n, out);
}

int em_effects_live_001EFD20(uint32_t id, const float pos[4])
{ return em_effects_live_001EFD20_result(id, pos, NULL); }

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

/* 001D04B0(m, kind, source, f12, f13) (an asm function; its three calls read from
 * the .s): 001CCF70(m + 0x30) -> the depth key, 001CFA60(block, m, f12,
 * f13), 001CFBE0(key, kind, source, block, 0). The source block is an
 * overlay owner's (it is not in the ELF windows): its bytes are kept by
 * address so the page's REF of it reads them. */
static const uint8_t *map_overlay_source(uint32_t address, const uint8_t bytes[0x90])
{
    for (uint32_t i = 0; i < S.overlay_sources; ++i)
        if (S.overlay_source[i].address == address) {
            memcpy(S.overlay_source[i].bytes, bytes, 0x90);
            return S.overlay_source[i].bytes;
        }
    if (S.overlay_sources >= sizeof S.overlay_source / sizeof S.overlay_source[0]) return NULL;
    S.overlay_source[S.overlay_sources].address = address;
    memcpy(S.overlay_source[S.overlay_sources].bytes, bytes, 0x90);
    return S.overlay_source[S.overlay_sources++].bytes;
}

int em_effects_live_001D04B0(const float m[16], int32_t kind, uint32_t source, const uint8_t source_bytes[0x90],
                             uint32_t f12, uint32_t f13)
{
    READY();
    if (!m || !source_bytes || (source >= 0x00100000u && source < 0x00275B00u))
        return fail(0x001D04B0u, "001D04B0 without its matrix or overlay source block");
    if (view_refresh() < 0) return -1;
    globals_refresh();
    const uint8_t *src = map_overlay_source(source, source_bytes);
    if (!src) return fail(source, "001D04B0: no room for another overlay source block");
    int32_t key = 0;
    if (check(em_effect_original_001CCF70(&S.e, m + 12, &key), 0x001CCF70u) < 0) return -1;
    EmHeadSpriteOriginalXf x;
    memset(&x, 0, sizeof x);
    if (check(em_head_sprite_original_001CFA60(&x, m, f12, f13, &S.hworkers, &S.hfault), 0x001CFA60u) < 0)
        return -1;
    const EmHeadSpriteOriginalSource st = {source, src};
    S.overlay_recording = 1;
    S.nopk = 0;
    const int r = chain_001CFBE0(key, (u32)kind, &st, &x, 0);
    S.overlay_recording = 0;
    if (check(r, 0x001CFBE0u) < 0) return -1;
    ++S.counters.overlay_draws;
    /* The capture log: packet 1 (7 qwords: the parameters, the matrix,
     * MSCAL) without its phase (+0x10) and seed (+0x1C) words, and packet 4
     * (16 qwords: the projection rows, the fog, the GIF tag row). */
    EmEffectsLiveOverlayLog *o = &S.overlay_log;
    o->frame = em_frame_counter();
    o->calls++;
    o->source = source;
    o->key = key;
    o->p1_digest = o->p4_digest = 0;
    if (S.nopk == 3 && S.opk_size[0] == 0x70u && S.opk_size[2] == 0x100u) {
        uint8_t p1[0x70];
        memcpy(p1, S.opk[0], sizeof p1);
        memset(p1 + 0x10, 0, 4);
        memset(p1 + 0x1C, 0, 4);
        o->p1_digest = crc32_bytes(0, p1, sizeof p1);
        o->p4_digest = crc32_bytes(0, S.opk[2], 0x100u);
    }
    return 0;
}

static void xf_read(EmHeadSpriteOriginalXf *x, const uint8_t block[0x60])
{
    memcpy(x->q, block, 64);
    x->m40 = rd32(block + 0x40);
    x->m40_bytes = em_rcl_bytes(x->m40, 64);
    x->w44 = rd32(block + 0x44); x->w48 = rd32(block + 0x48);
    x->w4C = rd32(block + 0x4C); x->w50 = rd32(block + 0x50); x->w54 = rd32(block + 0x54);
}
static void xf_write(uint8_t block[0x60], const EmHeadSpriteOriginalXf *x)
{
    const u32 tail[6] = {x->m40, x->w44, x->w48, x->w4C, x->w50, x->w54};
    memcpy(block, x->q, 64); memcpy(block + 64, tail, sizeof tail);
}
int em_effects_live_001CCF70(const float pos[4], int32_t *key)
{
    READY();
    if (!pos || !key) return fail(0x001CCF70u, "missing position or result");
    if (view_refresh() < 0) return -1;
    return check(em_effect_original_001CCF70(&S.e, pos, key), 0x001CCF70u);
}
int em_effects_live_001CFA60(uint8_t block[0x60], const float matrix[16], u32 f12, u32 f13)
{
    READY();
    if (!block || !matrix) return fail(0x001CFA60u, "missing local transform block or matrix");
    /* The original builder does not read its destination. In particular its
     * caller may supply a previously unwritten local block. The five scalar
     * stores precede CD370; matrix pointer and rows follow only on success. */
    EmHeadSpriteOriginalXf x = {0};
    int prior_fault = S.hfault.code != 0;
    int status = em_head_sprite_original_001CFA60(&x, matrix, f12, f13, &S.hworkers, &S.hfault);
    if (status == 0) xf_write(block, &x);
    else if (!prior_fault) {
        const u32 written[5] = {x.w44,x.w48,x.w4C,x.w50,x.w54};
        memcpy(block+0x44,written,sizeof written);
    }
    return check(status, 0x001CFA60u);
}
int em_effects_live_001CFB50(uint8_t block[0x60], int32_t index, const float matrix[16], const u32 f[5])
{
    READY();
    if (!block || !matrix || !f) return fail(0x001CFB50u, "missing local transform inputs");
    if (view_refresh() < 0) return -1;
    EmEffectKindsXf x; u32 words[16];
    memcpy(&x, block, sizeof x); memcpy(words, matrix, sizeof words);
    int status = em_effect_kinds_001CFB50(&S.k, &S.xs, &x, index, words, f[0], f[1], f[2], f[3], f[4]);
    memcpy(block, &x, sizeof x);
    return check(status, 0x001CFB50u);
}
int em_effects_live_001CFBE0(int32_t key, int32_t kind, u32 source, const uint8_t block[0x60], int32_t copy)
{
    READY();
    if (!block) return fail(0x001CFBE0u, "missing local transform block");
    const uint8_t *src = em_effects_live_window(source, 0x90);
    if (!src) return fail(source, "source block has no exported owner");
    EmHeadSpriteOriginalXf x;
    xf_read(&x, block);
    if (!x.m40_bytes) return fail(x.m40, "transform matrix has no render-context owner");
    const EmHeadSpriteOriginalSource st = {source, src};
    return check(chain_001CFBE0(key, (u32)kind, &st, &x, copy), 0x001CFBE0u);
}

int em_effects_live_001CFBE0_bytes(int32_t key, int32_t kind, u32 source, const uint8_t source_bytes[0x90],
                                   const uint8_t block[0x60], int32_t copy)
{
    READY();
    if (!block || !source_bytes) return fail(0x001CFBE0u, "missing local transform block or source");
    EmHeadSpriteOriginalXf x;
    xf_read(&x, block);
    if (!x.m40_bytes) return fail(x.m40, "transform matrix has no render-context owner");
    const EmHeadSpriteOriginalSource st = {source, source_bytes};
    return check(chain_001CFBE0(key, (u32)kind, &st, &x, copy), 0x001CFBE0u);
}

void em_effects_live_overlay_log(EmEffectsLiveOverlayLog *out)
{
    if (out) *out = S.overlay_log;
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
        if (!r && S.head_record && S.head_record(S.head_context, address, S.head_image) == 0)
            r = S.head_image;
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
        s->e.b03=actor->model; s->e.state=actor->u04[0];
        s->e.b09=actor->bones; s->e.b0C=actor->u0A[2];
        s->e.subtype=actor->param; s->e.callback=actor->callback;
        memcpy(s->e.pos,actor->pos,sizeof s->e.pos);
        memcpy(s->e.rot,actor->rot,sizeof s->e.rot);
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
        s->h.lifecycle=actor->u04[0]; s->h.sub=actor->u04[1];
        s->h.b09=actor->bones; s->h.b0C=actor->u0A[2]; s->h.key=actor->param;
        memcpy(s->h.pos,actor->pos,sizeof s->h.pos);
        int r = em_head_sprite_original_tick(&s->h, have_owner ? &owner : NULL, &S.hworld, &S.hworkers,
                                             &S.hfault);
        if (r == 1) sync_head(s);
        if (finish(r, EM_EFFECTS_LIVE_HEAD_SPRITE) < 0) return -1;
        return r;
    }
    if (S.other_tick) {
        int status = S.other_tick(S.other_context, em_actor_pool_address(S.pool, actor), actor->callback);
        if (status < 0) return fail(actor->callback, "extended effect behaviour failed");
        if ((status == 0 && actor->allocated) ||
            (status > 0 && (!actor->allocated || actor->generation != s->generation)))
            return fail(actor->callback, "extended effect behaviour returned inconsistent allocation state");
        if (status == 0) s->kind = KIND_NONE;
        return finish(status, actor->callback) < 0 ? -1 : status;
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

int em_effects_live_markers(EmEffectsLiveMarker *out, int max, uint32_t *frame)
{
    int n = S.nmarkers < max ? S.nmarkers : max;
    if (out && n > 0) memcpy(out, S.markers, (size_t)n * sizeof *out);
    if (frame) *frame = S.markers_frame;
    return n;
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
    S.nmarkers = 0;
    S.markers_frame = em_frame_counter();
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
