/* EM_NEW_GAME=1, the developer switch (em_new_game_switch.h), and its state
 * test EM_NEW_GAME_STATE_TEST (tools/test_new_game_switch.py). */
#include "game/em_new_game_switch.h"
#include "game/em_frame.h"
#include "game/em_frontend.h"
#include "game/em_sfx.h"
#include "game/em_startup_audio.h"
#include "game/em_module_loader.h"
#include "game/em_iop_stream.h"
#include "game/em_stream_live.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

int em_new_game_switch_requested(void)
{
    const char *v = getenv("EM_NEW_GAME");
    return v && strcmp(v, "1") == 0;
}

int em_new_game_switch_check(void)
{
    const char *v = getenv("EM_NEW_GAME");
    if (!v || !v[0] || strcmp(v, "0") == 0)
        return 0;
    if (strcmp(v, "1") != 0) {
        fprintf(stderr, "EM_NEW_GAME=%s: expected 0 or 1\n", v);
        return -1;
    }
    const char *skip = getenv("EM_SKIP_STARTUP");
    if (skip && strcmp(skip, "1") == 0) {
        fprintf(stderr, "EM_NEW_GAME=1 refused: EM_SKIP_STARTUP=1 selects the old debug fixture, not New Game\n");
        return -1;
    }
    /* The New Game fixtures (em_frontend.c new_game_test) run after the
     * handoff as well: only their title part is not run. Any other startup
     * fixture tests the frontend itself. */
    const char *test = getenv("EM_STARTUP_TEST");
    if (test && strcmp(test, "newgame") != 0 && strcmp(test, "newgame-control") != 0 &&
        strcmp(test, "newgame-skip") != 0 && strcmp(test, "newgame-level") != 0) {
        fprintf(stderr, "EM_NEW_GAME=1 refused: EM_STARTUP_TEST=%s tests the frontend, which the switch skips\n",
                test);
        return -1;
    }
    return 0;
}

/* ---- EM_NEW_GAME_STATE_TEST ---------------------------------------------
 * The image: "EMNGSTAT" and a version word, then blocks, each a 24-byte NUL
 * padded name, the block's runtime address and its size (little-endian
 * u64) and its bytes. Block "meta" holds three u64: the runtime address of
 * this function (the Python side takes the image slide from it and the
 * binary's symbol table), and the image's runtime bounds (a word inside
 * them is an image pointer, compared after the slide). Then the writable
 * static sections of the executable (every file-scope and function-scope
 * static the port holds), and the two heap objects the game state lives in
 * outside them: the module loader (its whole object and the modelled
 * original bytes em_module_loader_snapshot gives) and the IOP. Block
 * "frame_parity" holds D_00810E80; "host:*" blocks give the address and
 * size of host-only state (the frontend's, the sfx and pacing clocks). */
static struct {
    int done, failed;
} state_test;

static int put_block(FILE *out, const char *name, const void *bytes, uint64_t size)
{
    char label[24] = {0};
    snprintf(label, sizeof label, "%s", name);
    uint64_t head[2] = {(uint64_t)(uintptr_t)bytes, bytes ? size : 0};
    return fwrite(label, 1, sizeof label, out) == sizeof label &&
           fwrite(head, sizeof head[0], 2, out) == 2 &&
           (!bytes || !size || fwrite(bytes, 1, (size_t)size, out) == size) ? 0 : -1;
}

static int put_sections(FILE *out, uint64_t bounds[2])
{
#if defined(__APPLE__)
    static const char *const names[] = {"__data", "__bss", "__common"};
    unsigned long size = 0;
    uint8_t *text = getsegmentdata(&_mh_execute_header, "__TEXT", &size);
    uint8_t *data = getsegmentdata(&_mh_execute_header, "__DATA", &size);
    if (!text || !data)
        return -1;
    bounds[0] = (uint64_t)(uintptr_t)&_mh_execute_header;
    bounds[1] = (uint64_t)(uintptr_t)data + size;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
        uint8_t *p = getsectiondata(&_mh_execute_header, "__DATA", names[i], &size);
        if (p && put_block(out, names[i], p, size) < 0)
            return -1;
    }
    return 0;
#elif defined(__linux__)
    extern char __executable_start[], __data_start[], _edata[], __bss_start[], _end[];
    bounds[0] = (uint64_t)(uintptr_t)__executable_start;
    bounds[1] = (uint64_t)(uintptr_t)_end;
    if (put_block(out, ".data", __data_start, (uint64_t)(_edata - __data_start)) < 0)
        return -1;
    return put_block(out, ".bss", __bss_start, (uint64_t)(_end - __bss_start));
#else
    (void)out;
    (void)bounds;
    return -1;
#endif
}

/* Block "regions": every mapped range of the process as (start, end)
 * u64 pairs. A word that points into one of them is a pointer: its value
 * says where an allocation landed in this run, not what the game holds. */
static int put_regions(FILE *out)
{
    enum { MAX_REGIONS = 8192 };
    static uint64_t ranges[2 * MAX_REGIONS];
    size_t n = 0;
#if defined(__APPLE__)
    mach_vm_address_t address = 0;
    for (;;) {
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        if (mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                           (vm_region_info_t)&info, &count, &object) != KERN_SUCCESS)
            break;
        if (n < MAX_REGIONS) {
            ranges[2 * n] = address;
            ranges[2 * n + 1] = address + size;
            ++n;
        }
        address += size;
    }
#elif defined(__linux__)
    FILE *maps = fopen("/proc/self/maps", "r");
    unsigned long long lo, hi;
    char line[512];
    while (maps && fgets(line, sizeof line, maps))
        if (sscanf(line, "%llx-%llx", &lo, &hi) == 2 && n < MAX_REGIONS) {
            ranges[2 * n] = lo;
            ranges[2 * n + 1] = hi;
            ++n;
        }
    if (maps)
        fclose(maps);
#endif
    return n ? put_block(out, "regions", ranges, n * 16u) : -1;
}

static int write_image(const char *path)
{
    FILE *out = fopen(path, "wb");
    if (!out)
        return -1;
    int rc = fwrite("EMNGSTAT", 1, 8, out) == 8 ? 0 : -1;
    const uint32_t version = 1;
    if (rc == 0 && fwrite(&version, sizeof version, 1, out) != 1)
        rc = -1;
    uint64_t meta[3] = {(uint64_t)(uintptr_t)&em_new_game_state_test_before_frame, 0, 0};
    /* "meta" first, with its bounds filled after the sections are known: the
     * sections are written to a second pass below, so take the bounds now. */
    long meta_at = rc == 0 ? ftell(out) : -1;
    if (rc == 0 && put_block(out, "meta", meta, sizeof meta) < 0)
        rc = -1;
    uint64_t bounds[2] = {0, 0};
    if (rc == 0 && put_sections(out, bounds) < 0)
        rc = -1;
    const EmModuleLoader *ml = em_module_loader_live();
    size_t size = 0;
    const void *image = em_module_loader_image(ml, &size);
    if (rc == 0 && put_block(out, "loader", image, size) < 0)
        rc = -1;
    if (rc == 0 && ml) {
        static uint8_t snapshot[EM_MODULE_LOADER_SNAPSHOT_SIZE];
        em_module_loader_snapshot(ml, NULL, snapshot);
        if (put_block(out, "loader_snapshot", snapshot, sizeof snapshot) < 0)
            rc = -1;
    }
    image = em_iop_stream_image(em_stream_live_iop(), &size);
    if (rc == 0 && put_block(out, "iop", image, size) < 0)
        rc = -1;
    /* The field parity D_00810E80 at the opening: the test compares the
     * switch with a title run of the same parity. */
    const uint32_t parity = em_frame_parity();
    if (rc == 0 && put_block(out, "frame_parity", &parity, sizeof parity) < 0)
        rc = -1;
    /* What the game reads of the frontend after New Game, and the title
     * sequencer's sounds still to play (both routes must agree). */
    int32_t service[5];
    em_frontend_service_state(service);
    service[4] = (int32_t)em_startup_audio_pending();
    if (rc == 0 && put_block(out, "frontend_service", service, sizeof service) < 0)
        rc = -1;
    /* The frontend's own host state: its address and size, so the test
     * leaves exactly these bytes to the service comparison above. */
    const struct { const char *name; const void *(*state)(size_t *); } owners[] = {
        {"host:em_frontend", em_frontend_host_state},
        {"host:em_startup_audio", em_startup_audio_host_state},
        {"host:em_sfx_clock", em_sfx_host_clock},
        {"host:em_frame_pacing", em_frame_host_pacing},
    };
    for (size_t i = 0; rc == 0 && i < sizeof owners / sizeof owners[0]; ++i) {
        size_t bytes = 0;
        const void *at = owners[i].state(&bytes);
        uint64_t range[2] = {(uint64_t)(uintptr_t)at, bytes};
        if (put_block(out, owners[i].name, range, sizeof range) < 0)
            rc = -1;
    }
    /* The IOP driver's command ring (.bss 0x66C0) and its two byte counters
     * as offsets into the "iop" block: the test compares what is still to
     * run (the counters' difference) instead of the ring's history. */
    const EmIopStream *iop = em_stream_live_iop();
    if (rc == 0 && iop) {
        const uint8_t *base = em_iop_stream_image(iop, &size);
        const EmIopDriver *d = em_iop_stream_driver((EmIopStream *)iop);
        uint64_t ring[4] = {(uint64_t)((const uint8_t *)d->ring - base), sizeof d->ring,
                            (uint64_t)((const uint8_t *)&d->ring_write - base),
                            (uint64_t)((const uint8_t *)&d->ring_read - base)};
        if (put_block(out, "iop_ring", ring, sizeof ring) < 0)
            rc = -1;
        size_t mixer_offset = 0, mixer_size = 0;
        em_iop_stream_mixer_range(&mixer_offset, &mixer_size);
        uint64_t mixer[2] = {mixer_offset, mixer_size};
        if (rc == 0 && put_block(out, "iop_mixer", mixer, sizeof mixer) < 0)
            rc = -1;
    }
    if (rc == 0 && put_regions(out) < 0)
        rc = -1;
    if (rc == 0) {
        meta[1] = bounds[0];
        meta[2] = bounds[1];
        if (fseek(out, meta_at + 24 + 16, SEEK_SET) != 0 || fwrite(meta, sizeof meta[0], 3, out) != 3)
            rc = -1;
    }
    if (fclose(out) != 0)
        rc = -1;
    return rc;
}

void em_new_game_state_test_before_frame(void)
{
    const char *path = getenv("EM_NEW_GAME_STATE_TEST");
    if (!path || !path[0] || state_test.done)
        return;
    state_test.done = 1;
    if (write_image(path) < 0) {
        fprintf(stderr, "new game state test: could not write %s\n", path);
        state_test.failed = 1;
    } else {
        fprintf(stderr, "new game state test: image written at the opening's first frame\n");
    }
    em_frame_request_quit();
}

int em_new_game_state_test_failed(void)
{
    return state_test.failed;
}
