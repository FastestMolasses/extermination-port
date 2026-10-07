/* Test-only borrowed scene view for 00159B90's terminal-copy prefix. */
#include "game/em_area01_scene_view.h"
#include <assert.h>
#include <string.h>

static EmSceneState scene;

int terminal_progress_contract(void)
{
    memset(&scene, 0xA5, sizeof scene);
    uint8_t *all = em_scene_progress_at(&scene, 0x00810710u, 32);
    assert(all == scene.progress.bytes + 0x10);
    for (uint32_t i = 0; i < 32; ++i) {
        assert(em_scene_progress_at(&scene, 0x00810710u + i, 32 - i) == all + i);
        assert(em_area01_scene_view(&scene, 0x00810710u + i, 1) == all + i);
        all[i] = (uint8_t)(17 * i + 3);
    }
    static const uint32_t bad[][2] = {
        {0x0081070Fu, 1}, {0x0081070Fu, 2}, {0x00810710u, 0},
        {0x00810710u, 33}, {0x00810720u, 17}, {0x0081072Fu, 2},
        {0x00810730u, 1}, {0xFFFFFFF0u, 32}, {0x00810710u, 0xFFFFFFFFu}
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
        assert(!em_scene_progress_at(&scene, bad[i][0], bad[i][1]));
    assert(!em_area01_scene_view(&scene, 0x0081070Fu, 2));
    assert(!em_area01_scene_view(&scene, 0x0081072Fu, 2));
    assert(!em_scene_progress_at(NULL, 0x00810710u, 16));
    /* The neighboring area-history array retains its separate owner. */
    assert(em_area01_scene_view(&scene, 0x00810730u, 16) == scene.d810730);
    assert(scene.progress.bytes[0x0F] == 0xA5 && scene.progress.bytes[0x30] == 0xA5);
    uint8_t saved[32]; memcpy(saved, all, sizeof saved);
    memset(scene.req, 0, sizeof scene.req);
    scene.d810700 = 0; scene.d810701 = 2;
    assert(em_area01_scene_view(&scene, 0x00810710u, 32) == all);
    assert(!memcmp(saved, all, sizeof saved));
    em_scene_progress_reset_001AF2C0(&scene);
    assert(em_scene_progress_at(&scene, 0x00810710u, 32) == all);
    for (unsigned i = 0; i < 32; ++i) assert(all[i] == 0);
    return 0;
}

#ifdef EM_TERMINAL_CONTRACT_ONLY
int main(void) { return terminal_progress_contract(); }
#else
#include "game/em_area01_sys.h"
#include "game/em_aim_fire_sdk_memory.h"

typedef struct {
    uint8_t *ram, *scratch;
    uint32_t calls[32][3], count, stop;
    int deny;
} Terminal;

static uint8_t *terminal_bytes(void *ctx, uint32_t address, uint32_t size, int write)
{
    Terminal *t = ctx;
    (void)write;
    if (!size || (uint64_t)address + size > UINT64_C(0x100000000)) return NULL;
    /* Reserve count is em_weapon-owned in production, not EmProgress.
     * This read-only prefix borrows its captured halfword at that boundary. */
    if (address == 0x00810CB4u && size == 2 && !write) return t->ram + address;
    if (address < 0x00810D40u && (uint64_t)address + size > 0x00810700u) {
        if (t->deny && address < 0x00810730u && (uint64_t)address + size > 0x00810710u)
            return NULL;
        return em_area01_scene_view(&scene, address, size);
    }
    if ((uint64_t)address + size <= 0x02000000u) return t->ram + address;
    if (address >= 0x70000000u && (uint64_t)address + size <= 0x70004000u)
        return t->scratch + address - 0x70000000u;
    return NULL;
}

static void *terminal_sdk_bytes(void *ctx, uint32_t address, size_t size, int write)
{
    return size <= UINT32_MAX ? terminal_bytes(ctx, address, (uint32_t)size, write) : NULL;
}

static int terminal_worker(void *ctx, EmArea01SysCall *c)
{
    Terminal *t = ctx;
    if (t->count == 32) return -1;
    t->calls[t->count][0] = c->fn;
    t->calls[t->count][1] = (uint32_t)c->a[0];
    t->calls[t->count++][2] = (uint32_t)c->a[1];
    if (c->fn == 0x00102948u && c->na == 2 && !c->nf) {
        EmAimFireTargetCall sdk = {.function = c->fn, .sp = c->sp, .na = c->na};
        memcpy(sdk.a, c->a, 2 * sizeof sdk.a[0]);
        return em_aim_fire_sdk_memory_call(t, terminal_sdk_bytes, &sdk);
    }
    if (c->fn == 0x001B6F00u || c->fn == 0x001BA1A0u || c->fn == 0x001BA1F0u) {
        c->v0 = 0; return 0; /* Explicit approach/script boundaries. */
    }
    /* Stop after the two copies at audio; with no interaction, stop at draw.
     * The test deliberately proves this prefix, not terminal UI/IOP work. */
    t->stop = c->fn;
    return -1;
}

int terminal_progress_prefix(uint8_t *ram, uint8_t *scratch, uint32_t node,
                             int deny, uint8_t out[32], uint32_t calls[32][3],
                             uint32_t result[3])
{
    memset(&scene, 0, sizeof scene);
    memcpy(scene.progress.bytes, ram + EM_SCENE_PROGRESS_BASE, EM_SCENE_PROGRESS_SIZE);
    memcpy(&scene.d810700, ram + 0x00810700u, 3);
    memcpy(scene.d810730, ram + 0x00810730u, sizeof scene.d810730);
    Terminal t = {.ram = ram, .scratch = scratch, .deny = deny};
    EmArea01Sys owner = {.ctx = &t, .view = terminal_bytes, .call = terminal_worker, .sp = 0x7F0F0000u};
    int rc = em_area01_sys_00159B90(&owner, node);
    memcpy(out, scene.progress.bytes + 0x10, 32);
    memcpy(calls, t.calls, sizeof t.calls);
    result[0] = t.count; result[1] = t.stop; result[2] = owner.fault_address;
    return rc;
}
#endif
