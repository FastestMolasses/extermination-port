/* em_memcard.c - see em_memcard.h (docs/OPTIONS.md section 5). */
#include "game/em_memcard.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* The boot values of the card helpers' globals: D_00275C58..6C are .bss
 * (0), D_00264E30..38 .data words 0 and D_00275840 / 44 .sdata words -1 in
 * the boot ELF; every capture (the status hub, opt_00..08, dmg_05) holds
 * these bytes. */
static struct {
    uint8_t c58[0x18];      /* D_00275C58..D_00275C6F */
    uint8_t e30[0x0C];      /* D_00264E30..D_00264E3B */
    uint8_t s840[0x08];     /* D_00275840..D_00275847 */
    int32_t pending;        /* D_00241D68: the command Sync waits for (0 none, 1 GetInfo) */
    int32_t result;         /* D_0027C680: the server's result of the pending command */
    int known[2];           /* the card server's per-port "asked since the boot" */
    int initialised;
    const char *fault;
} M;

static int fail(const char *why)
{
    if (!M.fault) {
        M.fault = why;
        fprintf(stderr, "memory card: %s\n", why);
    }
    return -1;
}

void em_memcard_reset(void)
{
    memset(&M, 0, sizeof M);
    memset(M.s840, 0xFF, sizeof M.s840);
    M.initialised = 1;
}

static void ensure(void)
{
    if (!M.initialised) em_memcard_reset();
}

const char *em_memcard_fault(void) { return M.fault; }

static int slot_dir(int port, char *path, size_t size)
{
    return snprintf(path, size, "%s/slot%d", EM_MEMCARD_DIR, port + 1) < (int)size;
}

static int present(int port)
{
    char path[64];
    struct stat st;
    return slot_dir(port, path, sizeof path) && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int em_memcard_boot_check(void)
{
    ensure();
    if (mkdir("data", 0700) && errno != EEXIST) return 0;
    if (mkdir(EM_MEMCARD_DIR, 0700) && errno != EEXIST) return 0;
    for (int port = 0; port < 2; ++port) {
        char path[64];
        if (!slot_dir(port, path, sizeof path) || (mkdir(path, 0700) && errno != EEXIST) ||
            !present(port))
            return 0;
        M.known[port] = 1;
    }
    return 1;
}

uint8_t *em_memcard_globals(uint32_t address, uint32_t size)
{
    ensure();
    const struct { uint32_t base, size; uint8_t *bytes; } views[] = {
        {0x00275C58u, sizeof M.c58, M.c58}, {0x00264E30u, sizeof M.e30, M.e30},
        {0x00275840u, sizeof M.s840, M.s840}};
    for (unsigned i = 0; i < 3; ++i)
        if (address >= views[i].base && (uint64_t)address + size <= (uint64_t)views[i].base + views[i].size)
            return views[i].bytes + (address - views[i].base);
    return NULL;
}

/* 00114988: D_00241D68 nonzero returns it; the RPC is bound since the
 * boot's sceMcInit (D_0027B0C0 + 0x24 nonzero), so -100 is not returned;
 * the call is issued and D_00241D68 = 1. The end callback 00114930 stores
 * the type, free and format words through the pointers given. */
int em_memcard_00114988(int32_t port, int32_t slot, uint32_t type, uint32_t free_words, uint32_t format,
                        int32_t *v0, EmMemcardStore stores[3], unsigned *n)
{
    ensure();
    if (M.fault) return -1;
    if (!v0 || !stores || !n) return fail("00114988 without its outputs");
    *n = 0;
    if (M.pending != 0) {
        *v0 = M.pending;
        return 0;
    }
    if (port < 0 || port > 1 || slot != 0)
        return fail("00114988 on a port other than 0 / 1 or a slot other than 0 (no recording)");
    if (free_words || format)
        return fail("00114988 asked for the free or format words (no recording shows them)");
    if (!present(port))
        return fail("00114988 on a slot whose directory data/memcard/slotN is missing (no recording "
                    "shows a missing card)");
    M.result = M.known[port] ? 0 : -1;
    M.known[port] = 1;
    M.pending = 1;
    if (type) stores[(*n)++] = (EmMemcardStore){type, 2};
    *v0 = 0;
    return 0;
}

/* 00114848: no command pending returns -1; else (the server answered at
 * host speed) *cmd = D_00241D68, D_00241D68 = 0, *result = D_0027C680 and
 * 1. */
int em_memcard_00114848(int32_t mode, uint32_t cmd, uint32_t result, int32_t *v0, EmMemcardStore stores[2],
                        unsigned *n)
{
    (void)mode;
    ensure();
    if (M.fault) return -1;
    if (!v0 || !stores || !n) return fail("00114848 without its outputs");
    *n = 0;
    if (M.pending == 0) {
        *v0 = -1;
        return 0;
    }
    if (cmd) stores[(*n)++] = (EmMemcardStore){cmd, M.pending};
    M.pending = 0;
    if (result) stores[(*n)++] = (EmMemcardStore){result, M.result};
    *v0 = 1;
    return 0;
}
