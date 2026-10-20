/* em_memcard.c - see em_memcard.h (docs/OPTIONS.md section 5,
 * docs/SAVE_GAME.md section 5). */
#define _POSIX_C_SOURCE 200809L
#include "game/em_memcard.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FDS 3                   /* the card server's handle table: descriptors 0..2 */
#define NAME_MAX_SDK 0x3FFu     /* 00114360 / 00114AC0 copy at most 0x3FF bytes of a name */
#define ENTRY_NAME 32u          /* a card name: at most 31 bytes and its NUL */
#define IO_MAX 0x800000         /* a Write / Read size above a card's 8 MiB is refused */

typedef struct {
    int open;
    int host;                   /* the host file's descriptor */
    int32_t mode;               /* the Open mode (1 read, 2 write, 3 both; | 0x200) */
    int64_t offset;
} Fd;

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
    Fd fd[FDS];
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

static char root[512] = EM_MEMCARD_DIR;

void em_memcard_set_root(const char *dir)
{
    snprintf(root, sizeof root, "%s", dir ? dir : EM_MEMCARD_DIR);
}

const char *em_memcard_root(void) { return root; }

void em_memcard_reset(void)
{
    for (unsigned i = 0; i < FDS; ++i)
        if (M.initialised && M.fd[i].open) close(M.fd[i].host);
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
    const int n = snprintf(path, size, "%s/slot%d", root, port + 1);
    return n > 0 && n < (int)size;
}

static int present(int port)
{
    char path[600];
    struct stat st;
    return slot_dir(port, path, sizeof path) && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int em_memcard_boot_check(void)
{
    ensure();
    if (strcmp(root, EM_MEMCARD_DIR) == 0 && mkdir("data", 0700) && errno != EEXIST) return 0;
    if (mkdir(root, 0700) && errno != EEXIST) return 0;
    for (int port = 0; port < 2; ++port) {
        char path[600];
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

/* ---- the file commands ------------------------------------------------ */

/* Issue: D_00241D68 nonzero returns it (no call is made); else the command
 * is served now and pending until the next Sync. */
static int busy(int32_t *v0)
{
    if (M.pending == 0) return 0;
    *v0 = M.pending;
    return 1;
}

static void issue(int32_t number, int32_t result, int32_t *v0)
{
    M.pending = number;
    M.result = result;
    *v0 = 0;
}

/* The name at `at` as the SDK copies it (at most 0x3FF bytes). */
static int read_name(const EmMemcardMemory *m, uint32_t at, char *out)
{
    for (uint32_t i = 0; i < NAME_MAX_SDK; ++i) {
        uint8_t c;
        if (m->read(m->ctx, at + i, &c, 1) < 0) return fail("a card name outside the caller's memory");
        out[i] = (char)c;
        if (!c) return 0;
    }
    out[NAME_MAX_SDK] = 0;
    return 0;
}

static int name_char(char c, int wild)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
           c == '-' || (wild && (c == '*' || c == '?'));
}

/* A relative card path of valid components; `wild`: the last component
 * may hold '*' / '?'. */
static int valid_name(const char *name, int wild)
{
    const char *c = name;
    if (!*c) return 0;
    for (;;) {
        const char *end = strchr(c, '/');
        const size_t n = end ? (size_t)(end - c) : strlen(c);
        if (n == 0 || n >= ENTRY_NAME || (n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.'))
            return 0;
        for (size_t i = 0; i < n; ++i)
            if (!name_char(c[i], wild && !end)) return 0;
        if (!end) return 1;
        c = end + 1;
    }
}

static int host_path(int32_t port, const char *name, char *path, size_t size)
{
    char dir[600];
    if (!slot_dir(port, dir, sizeof dir)) return 0;
    const int n = snprintf(path, size, "%s/%s", dir, name);
    return n > 0 && n < (int)size;
}

/* The checks every command that names a card makes. */
static int card_name(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t at, int wild, char *name)
{
    if (!m || !m->read || !m->write) return fail("a card file command without the caller's memory");
    if (port < 0 || port > 1 || slot != 0)
        return fail("a card file command on a port other than 0 / 1 or a slot other than 0 (no recording)");
    if (!present(port)) return fail("a card file command on a missing data/memcard/slotN (no recording)");
    if (read_name(m, at, name) < 0) return -1;
    if (!valid_name(name, wild))
        return fail("a card name that is not a relative path of card names (no recording shows the server's "
                    "answer)");
    return 0;
}

static int open_file(int32_t port, const char *name, int32_t mode)
{
    const int access = mode & ~0x200;
    if (access < 1 || access > 3 || (mode & ~0x203)) return fail("an Open mode other than 1 / 2 / 3 (| 0x200)");
    char path[1200];
    if (!host_path(port, name, path, sizeof path)) return fail("a card path too long for the host");
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode)) return fail("an Open of a card directory (no recording)");
    } else if (!(mode & 0x200)) {
        return fail("an Open of a missing card file without create (no recording)");
    }
    int slot = -1;
    for (int i = 0; i < FDS && slot < 0; ++i)
        if (!M.fd[i].open) slot = i;
    if (slot < 0) return fail("a fourth open card descriptor (no recording)");
    const int flags = (access == 1 ? O_RDONLY : access == 2 ? O_WRONLY : O_RDWR) | ((mode & 0x200) ? O_CREAT : 0);
    const int host = open(path, flags, 0600);
    if (host < 0)
        return fail(errno == ENOENT ? "an Open in a missing card directory (no recording)"
                                    : "a host card file could not be opened");
    M.fd[slot] = (Fd){1, host, mode, 0};
    return slot;
}

int em_memcard_00114360(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t mode,
                        int32_t *v0)
{
    ensure();
    if (M.fault) return -1;
    if (!v0) return fail("00114360 without its output");
    if (busy(v0)) return 0;
    char text[NAME_MAX_SDK + 1];
    if (card_name(m, port, slot, name, 0, text) < 0) return -1;
    if (mode == 0x40) {
        /* 00114448's Open: the server makes the directory */
        char path[1200];
        struct stat st;
        if (!host_path(port, text, path, sizeof path)) return fail("a card path too long for the host");
        if (stat(path, &st) == 0) return fail("a Mkdir of a card name that exists (no recording)");
        if (mkdir(path, 0700) != 0)
            return fail(errno == ENOENT ? "a Mkdir in a missing card directory (no recording)"
                                        : "a host card directory could not be made");
        issue(2, 0, v0);
        return 0;
    }
    const int fd = open_file(port, text, mode);
    if (fd < 0) return -1;
    issue(2, fd, v0);
    return 0;
}

/* 00114448 (byte-matched C): 00114360(port, slot, name, 0x40); on 0 the
 * pending number is 0xB. */
int em_memcard_00114448(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t *v0)
{
    if (em_memcard_00114360(m, port, slot, name, 0x40, v0) < 0) return -1;
    if (*v0 == 0) M.pending = 0xB;
    return 0;
}

static Fd *descriptor(int32_t fd)
{
    if (fd < 0 || fd >= FDS || !M.fd[fd].open) {
        fail("a card command on a descriptor that is not open (no recording)");
        return NULL;
    }
    return &M.fd[fd];
}

int em_memcard_00114480(int32_t fd, int32_t *v0)
{
    ensure();
    if (M.fault) return -1;
    if (!v0) return fail("00114480 without its output");
    if (busy(v0)) return 0;
    Fd *f = descriptor(fd);
    if (!f) return -1;
    const int bad = close(f->host);
    *f = (Fd){0};
    if (bad) return fail("a host card file could not be closed");
    issue(3, 0, v0);
    return 0;
}

static int io_checks(const EmMemcardMemory *m, int32_t size)
{
    if (!m || !m->read || !m->write) return fail("a card Write / Read without the caller's memory");
    if (size < 0 || size > IO_MAX) return fail("a card Write / Read size outside 0..8 MiB (no recording)");
    return 0;
}

int em_memcard_00114708(const EmMemcardMemory *m, int32_t fd, uint32_t buf, int32_t size, int32_t *v0)
{
    ensure();
    if (M.fault) return -1;
    if (!v0) return fail("00114708 without its output");
    if (busy(v0)) return 0;
    if (io_checks(m, size) < 0) return -1;
    Fd *f = descriptor(fd);
    if (!f) return -1;
    if ((f->mode & 3) == 1) return fail("a card Write on a read-only descriptor (no recording)");
    uint8_t *data = malloc(size ? (size_t)size : 1u);
    if (!data) return fail("no memory for a card Write");
    if (size && m->read(m->ctx, buf, data, (uint32_t)size) < 0) {
        free(data);
        return fail("a card Write of bytes outside the caller's memory");
    }
    const ssize_t n = size ? pwrite(f->host, data, (size_t)size, (off_t)f->offset) : 0;
    free(data);
    if (n != size) return fail("a host card file could not be written");
    f->offset += size;
    issue(6, size, v0);
    return 0;
}

int em_memcard_00114630(const EmMemcardMemory *m, int32_t fd, uint32_t buf, int32_t size, int32_t *v0)
{
    ensure();
    if (M.fault) return -1;
    if (!v0) return fail("00114630 without its output");
    if (busy(v0)) return 0;
    if (io_checks(m, size) < 0) return -1;
    Fd *f = descriptor(fd);
    if (!f) return -1;
    if ((f->mode & 3) == 2) return fail("a card Read on a write-only descriptor (no recording)");
    uint8_t *data = malloc(size ? (size_t)size : 1u);
    if (!data) return fail("no memory for a card Read");
    const ssize_t n = size ? pread(f->host, data, (size_t)size, (off_t)f->offset) : 0;
    if (n < 0 || (n && m->write(m->ctx, buf, data, (uint32_t)n) < 0)) {
        free(data);
        return fail(n < 0 ? "a host card file could not be read" : "a card Read into bytes outside the caller's memory");
    }
    free(data);
    f->offset += n;
    issue(5, (int32_t)n, v0);
    return 0;
}

static int match(const char *pattern, const char *name)
{
    if (!*pattern) return !*name;
    if (*pattern == '*') return match(pattern + 1, name) || (*name && match(pattern, name + 1));
    return *name && (*pattern == '?' || *pattern == *name) && match(pattern + 1, name + 1);
}

static int by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int em_memcard_00114AC0(const EmMemcardMemory *m, int32_t port, int32_t slot, uint32_t name, int32_t mode,
                        int32_t max, uint32_t table, int32_t *v0)
{
    ensure();
    if (M.fault) return -1;
    if (!v0) return fail("00114AC0 without its output");
    if (busy(v0)) return 0;
    char text[NAME_MAX_SDK + 1];
    if (card_name(m, port, slot, name, 1, text) < 0) return -1;
    if (mode != 0) return fail("a GetDir that continues a listing (mode != 0; no caller, no recording)");
    if (max < 0 || max > 0x1000) return fail("a GetDir table size outside 0..0x1000 entries (no recording)");
    char *cut = strrchr(text, '/');
    const char *pattern = cut ? cut + 1 : text;
    const int wild = strpbrk(pattern, "*?") != NULL;
    if (wild && !cut) return fail("a GetDir wildcard at the card's root (no recording)");
    char dir[1200];
    if (cut) *cut = 0;
    if (!host_path(port, cut ? text : "", dir, sizeof dir)) return fail("a card path too long for the host");
    if (!cut) dir[strlen(dir) - 1] = 0;          /* the slot directory itself (no trailing '/') */
    /* the entries: "." and ".." below a directory, then the host's */
    char (*names)[ENTRY_NAME] = NULL;
    size_t count = 0, cap = 0;
    struct stat st;
    if (wild) {
        if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode))
            return fail("a GetDir of a missing card directory (no recording)");
    }
    DIR *d = opendir(dir);
    if (!d) return fail(cut ? "a GetDir of a missing card directory (no recording)"
                            : "the host card directory could not be read");
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= ENTRY_NAME) continue;
        if (!(wild ? match(pattern, e->d_name) : strcmp(pattern, e->d_name) == 0)) continue;
        if (count == cap) {
            cap = cap ? 2 * cap : 16;
            void *grown = realloc(names, cap * sizeof *names);
            if (!grown) {
                closedir(d);
                free(names);
                return fail("no memory for a GetDir");
            }
            names = grown;
        }
        snprintf(names[count++], ENTRY_NAME, "%s", e->d_name);
    }
    closedir(d);
    qsort(names, count, sizeof *names, by_name);
    static const char *const dots[2] = {".", ".."};
    int32_t stored = 0;
    for (size_t i = 0; i < (wild ? 2u : 0u) + count && stored < max; ++i) {
        const char *n = wild && i < 2 ? dots[i] : names[i - (wild ? 2u : 0u)];
        if (wild && i < 2 && !match(pattern, n)) continue;
        uint8_t entry[0x40] = {0};
        memcpy(entry + 0x20, n, strlen(n));
        if (m->write(m->ctx, table + 0x40u * (uint32_t)stored, entry, sizeof entry) < 0) {
            free(names);
            return fail("a GetDir table outside the caller's memory");
        }
        ++stored;
    }
    free(names);
    issue(0xD, stored, v0);
    return 0;
}

/* ---- by address --------------------------------------------------------- */

int em_memcard_is_sdk(uint32_t target)
{
    switch (target) {
    case 0x00114988u: case 0x00114848u: case 0x00114360u: case 0x00114448u:
    case 0x00114480u: case 0x00114708u: case 0x00114630u: case 0x00114AC0u:
        return 1;
    default:
        return 0;
    }
}

static int put_stores(const EmMemcardMemory *m, const EmMemcardStore *stores, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        const uint32_t v = (uint32_t)stores[i].value;
        const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
        if (stores[i].address & 3u) return fail("a card store to an unaligned word");
        if (!m || !m->write || m->write(m->ctx, stores[i].address, b, 4) < 0)
            return fail("a card store outside the caller's memory");
    }
    return 0;
}

int em_memcard_call(uint32_t target, const uint64_t *a, unsigned na, const EmMemcardMemory *m, uint64_t *v0)
{
    ensure();
    if (M.fault) return -1;
    static const unsigned need[8] = {5, 3, 4, 3, 1, 3, 3, 6};
    static const uint32_t order[8] = {0x00114988u, 0x00114848u, 0x00114360u, 0x00114448u,
                                      0x00114480u, 0x00114708u, 0x00114630u, 0x00114AC0u};
    unsigned k = 0;
    while (k < 8 && order[k] != target) ++k;
    if (k == 8) return fail("em_memcard_call on an address that is not a libmc entry");
    if (!a || !v0 || na < need[k]) return fail("em_memcard_call without its argument registers");
#define I(i) ((int32_t)(uint32_t)a[i])
#define U(i) ((uint32_t)a[i])
    int32_t r = 0;
    int rc;
    EmMemcardStore stores[3];
    unsigned n = 0;
    switch (k) {
    case 0: rc = em_memcard_00114988(I(0), I(1), U(2), U(3), U(4), &r, stores, &n); break;
    case 1: rc = em_memcard_00114848(I(0), U(1), U(2), &r, stores, &n); break;
    case 2: rc = em_memcard_00114360(m, I(0), I(1), U(2), I(3), &r); break;
    case 3: rc = em_memcard_00114448(m, I(0), I(1), U(2), &r); break;
    case 4: rc = em_memcard_00114480(I(0), &r); break;
    case 5: rc = em_memcard_00114708(m, I(0), U(1), I(2), &r); break;
    case 6: rc = em_memcard_00114630(m, I(0), U(1), I(2), &r); break;
    default: rc = em_memcard_00114AC0(m, I(0), I(1), U(2), I(3), I(4), U(5), &r); break;
    }
#undef I
#undef U
    if (rc < 0 || put_stores(m, stores, n) < 0) return -1;
    *v0 = (uint64_t)(int64_t)r;
    return 0;
}
