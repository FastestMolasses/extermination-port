/* em_gs_world - the Original profile's world frame on the CPU GS model
 * (em_gs_world.h, docs/GS_EXACT.md section 9). */
#include "gs/em_gs_world.h"
#include "gs/em_gs_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#define GSW_THREADS 1
#else
#define GSW_THREADS 0
#endif

#define BLOCKS (EM_GS_MEM_BYTES / 256u)
#define HEAD_MAX 0x800u
#define CHECKED_MAX 1024u
#define MARKED_MAX 8u
#define WORKERS_MAX 16u
#define BAND_SHIFT 2u                 /* bands of 4 rows, interleaved over the workers */

enum { OP_WRITE = 0, OP_ENV = 1, OP_GIF = 2, OP_BARRIER = 3 };

typedef struct {
    uint32_t op, reg;
    uint64_t value;
} Op;

/* One frame's work: the head (the kick's draw environment and clear), the
 * body (register writes, state blocks, the environment again, barriers)
 * and the field it shows. Two jobs alternate: the main thread records one
 * while the workers draw the other. */
typedef struct {
    Op *ops;
    size_t op_count, op_cap;
    uint8_t *gif;                    /* OP_GIF data: value = offset, reg = bytes */
    size_t gif_used, gif_cap;
    uint8_t env[HEAD_MAX], clear[HEAD_MAX];
    size_t env_bytes, clear_bytes;
    uint64_t frame;                  /* the field: FRAME_1 ... */
    uint32_t height;                 /* ... and its rows */
    uint64_t xyoffset;               /* the field's XYOFFSET_1 (em_gs_world_field_xyoffset) */
    int xyoffset_known;              /* 0: a list frame drew nothing into its displayed buffer */
    int list;                        /* a list frame (no head) */
} Job;

struct EmGsWorld;

/* One worker: its own EmGs (registers, span, CLUT) over the shared local
 * memory, drawing its band of rows (EmGs.band_*). Every worker runs every
 * write of every frame, so all their register states stay equal; together
 * they write what one EmGs writes (the bands partition the rows). */
typedef struct {
    struct EmGsWorld *w;
    EmGs gs;
    int failed;                       /* its run of the frame stopped */
    double cpu_ns;                    /* its CPU time in the last frame */
#if GSW_THREADS
    pthread_t thread;
#endif
} Worker;

struct EmGsWorld {
    uint8_t *mem;
    uint8_t resident[BLOCKS];        /* 1: an upload wrote the block */
    int memory_loaded;
    Worker worker[WORKERS_MAX];
    uint32_t workers;
    int threaded;                    /* the workers are threads (else the caller runs them) */
#if GSW_THREADS
    pthread_mutex_t lock;
    pthread_cond_t go, done, bar;
    uint64_t generation;             /* bumped for every frame the workers run */
    uint32_t running;                /* workers still running the frame */
    uint32_t bar_count;
    uint64_t bar_generation;
    int quit;
#endif
    Job job[2];
    int rec;                         /* job[rec] is recorded; job[rec ^ 1] may be running */
    const Job *run;                  /* the job the workers run */
    int busy;                        /* a job was started and not yet waited for */
    double t_start;
    uint64_t prims0, pixels0;
    /* recording */
    int recording;
    uint64_t cache[0x80];            /* the value recorded last per register */
    uint8_t cached[0x80];
    /* The ordering of the workers (OP_BARRIER): blocks of buffers drawn
     * (fdrawn) and blocks of drawn buffers read as textures (fread) since
     * the last barrier of the frame being recorded. A read of an fdrawn
     * block, or a draw into an fread block, needs every band before it to
     * be done: a barrier. */
    uint8_t fdrawn[BLOCKS], fread[BLOCKS];
    int fdrawn_any, fread_any;
    /* Buffers marked since the last barrier (or the frame's start): a mark
     * of one of them again changes nothing (its blocks are in fdrawn and
     * drawn, and fread holds none of them: a read of one barriers, which
     * empties this list), so it is skipped. */
    struct { uint32_t fbp, fbw, height; } marked[MARKED_MAX];
    uint32_t marked_count;
    /* The texture state a textured primitive reads, per context (0: _1,
     * 1: _2), as the recorder knows it: TEX0 (and CLAMP) written in this
     * frame by a recorded write or decoded from recorded GIF data. Unknown
     * (the frame's start, the environment again, a TEX2 write) faults a
     * textured primitive; an unknown CLAMP is taken as REPEAT (it reaches
     * every texel of the texture: the widest read). */
    uint64_t tex0[2], clamp[2];
    uint8_t tex0_known[2], clamp_known[2];
    uint32_t prim;                   /* PRIM as written last (for GIF data's vertex kicks) */
    int prim_known;
    uint8_t drawn[BLOCKS];           /* blocks of buffers the model has drawn into */
    /* textures already checked (TEX0 texture + CLUT fields, CLAMP_1): the
     * blocks they read that lie in drawn buffers (pool[first .. first + n)) */
    struct { uint64_t tex0, clamp; uint32_t first, n; } checked[CHECKED_MAX];
    uint32_t checked_count;
    uint32_t *pool;
    uint32_t pool_used, pool_cap;
    /* the field of the last finished job */
    uint8_t *field;
    uint32_t field_w, field_h;
    uint64_t field_frame, field_xyoffset;
    int field_xyoffset_known;
    int have_field;
    EmGsWorldStats stats;
    char fault[192];
};

static int fail(EmGsWorld *w, const char *what, unsigned long long detail)
{
    if (!w->fault[0]) {
        snprintf(w->fault, sizeof w->fault, "%s (%#llx)", what, detail);
        fprintf(stderr, "gs world: %s\n", w->fault);
    }
    return -1;
}

static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static double cpu_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

/* ------------------------------------------------------------ the workers */

/* All workers reach OP_BARRIER before any goes on. At a barrier no worker
 * may hold queued primitives: the span must have ended there by itself (a
 * flush here would end it where the GS does not). */
static void barrier(Worker *k)
{
    EmGsWorld *w = k->w;
    if (k->gs.pend_n) k->failed = 1;
#if GSW_THREADS
    if (w->threaded && w->workers > 1) {
        pthread_mutex_lock(&w->lock);
        const uint64_t gen = w->bar_generation;
        if (++w->bar_count == w->workers) {
            w->bar_count = 0;
            w->bar_generation++;
            pthread_cond_broadcast(&w->bar);
        } else {
            while (gen == w->bar_generation) pthread_cond_wait(&w->bar, &w->lock);
        }
        pthread_mutex_unlock(&w->lock);
    }
#else
    (void)w;
#endif
}

/* One worker's run of the job: the head, every op, the end of the span,
 * then its band's rows of the field. A worker that stops (a malformed GIF
 * packet) still meets every barrier, so the others finish. */
static void run_frame(Worker *k)
{
    EmGsWorld *w = k->w;
    const Job *j = w->run;
    EmGs *gs = &k->gs;
    const double c0 = cpu_now_ns();
    k->failed = 0;
    if (j->env_bytes && em_gs_gif(gs, j->env, j->env_bytes) != j->env_bytes) k->failed = 1;
    if (j->clear_bytes && em_gs_gif(gs, j->clear, j->clear_bytes) != j->clear_bytes) k->failed = 1;
    for (size_t i = 0; i < j->op_count; ++i) {
        const Op *o = &j->ops[i];
        if (o->op == OP_BARRIER) {
            barrier(k);
            continue;
        }
        if (k->failed) continue;
        if (o->op == OP_WRITE)
            em_gs_write(gs, o->reg, o->value);
        else if (o->op == OP_GIF) {
            if (em_gs_gif(gs, j->gif + o->value, o->reg) != o->reg) k->failed = 1;
        } else if (em_gs_gif(gs, j->env, j->env_bytes) != j->env_bytes)
            k->failed = 1;
    }
    em_gs_flush(gs);
    const uint32_t fbp = (uint32_t)j->frame & 0x1FFu, fbw = (uint32_t)(j->frame >> 16) & 0x3Fu;
    for (uint32_t y = 0; y < j->height; ++y) {
        if (gs->band_count > 1 && ((y >> gs->band_shift) % gs->band_count) != gs->band_index) continue;
        uint8_t *row = w->field + (size_t)y * 64u * fbw * 4u;
        for (uint32_t x = 0; x < 64u * fbw; ++x)
            memcpy(row + 4u * x, w->mem + (size_t)em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u, 4);
    }
    k->cpu_ns = cpu_now_ns() - c0;
}

#if GSW_THREADS
static void *worker_main(void *arg)
{
    Worker *k = arg;
    EmGsWorld *w = k->w;
    uint64_t seen = 0;
    for (;;) {
        pthread_mutex_lock(&w->lock);
        while (!w->quit && w->generation == seen) pthread_cond_wait(&w->go, &w->lock);
        if (w->quit) {
            pthread_mutex_unlock(&w->lock);
            return NULL;
        }
        seen = w->generation;
        pthread_mutex_unlock(&w->lock);
        run_frame(k);
        pthread_mutex_lock(&w->lock);
        if (--w->running == 0) pthread_cond_signal(&w->done);
        pthread_mutex_unlock(&w->lock);
    }
}
#endif

/* The worker count: EM_GS_THREADS (1..16), else the online processors less
 * three (the game's main thread and the audio and movie threads keep a
 * core), at most 8. A host property: every count draws the same bytes. 1
 * runs the frame on the caller's thread at the kick (no worker threads). */
static uint32_t worker_count(void)
{
#if GSW_THREADS
    const char *e = getenv("EM_GS_THREADS");
    if (e && *e) {
        const long n = strtol(e, NULL, 10);
        return n < 1 ? 1u : n > (long)WORKERS_MAX ? WORKERS_MAX : (uint32_t)n;
    }
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    const long n = cpus > 3 ? cpus - 3 : 1;
    return n > 8 ? 8u : (uint32_t)n;
#else
    return 1u;
#endif
}

EmGsWorld *em_gs_world_create(void)
{
    EmGsWorld *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->mem = calloc(1, EM_GS_MEM_BYTES);
    w->field = calloc(EM_GS_WORLD_FIELD_W * 1024u, 4);
    if (!w->mem || !w->field) {
        free(w->mem);
        free(w->field);
        free(w);
        return NULL;
    }
    w->workers = worker_count();
#if GSW_THREADS
    if (w->workers > 1) {
        pthread_mutex_init(&w->lock, NULL);
        pthread_cond_init(&w->go, NULL);
        pthread_cond_init(&w->done, NULL);
        pthread_cond_init(&w->bar, NULL);
        w->threaded = 1;
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) {
        Worker *k = &w->worker[i];
        k->w = w;
        em_gs_init(&k->gs, w->mem);
        k->gs.strict = 1;
        k->gs.band_index = i;
        k->gs.band_shift = BAND_SHIFT;
    }
#if GSW_THREADS
    if (w->threaded) {
        uint32_t started = 0;
        for (uint32_t i = 0; i < w->workers; ++i, ++started)
            if (pthread_create(&w->worker[i].thread, NULL, worker_main, &w->worker[i]) != 0) break;
        if (started < w->workers) {
            /* fewer threads: the bands are re-dealt over those that run */
            if (started == 0) {
                w->threaded = 0;
                started = 1;
            }
            w->workers = started;
        }
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) w->worker[i].gs.band_count = w->workers;
    return w;
}

void em_gs_world_destroy(EmGsWorld *w)
{
    if (!w) return;
    (void)em_gs_world_wait(w);
#if GSW_THREADS
    if (w->threaded) {
        pthread_mutex_lock(&w->lock);
        w->quit = 1;
        pthread_cond_broadcast(&w->go);
        pthread_mutex_unlock(&w->lock);
        for (uint32_t i = 0; i < w->workers; ++i) pthread_join(w->worker[i].thread, NULL);
        pthread_mutex_destroy(&w->lock);
        pthread_cond_destroy(&w->go);
        pthread_cond_destroy(&w->done);
        pthread_cond_destroy(&w->bar);
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) em_gs_release(&w->worker[i].gs);
    for (int i = 0; i < 2; ++i) {
        free(w->job[i].ops);
        free(w->job[i].gif);
    }
    free(w->pool);
    free(w->mem);
    free(w->field);
    free(w);
}

uint32_t em_gs_world_workers(const EmGsWorld *w) { return w ? w->workers : 0; }

const char *em_gs_world_fault(const EmGsWorld *w) { return w && w->fault[0] ? w->fault : NULL; }

void em_gs_world_stats(const EmGsWorld *w, EmGsWorldStats *out)
{
    if (w && out) *out = w->stats;
}

/* ------------------------------------------------------------ memory image */

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int em_gs_world_memory_load(EmGsWorld *w, const char *path)
{
    if (!w || !path) return -1;
    if (w->memory_loaded) return 0;
    if (em_gs_world_wait(w) < 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return fail(w, "no GS memory image (run tools/export_gs_memory.py)", 0);
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = size >= 16 ? malloc((size_t)size) : NULL;
    const int ok = data && fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    int bad = !ok || memcmp(data, EM_GS_WORLD_MEMORY_MAGIC, 4) != 0 ||
              rd32le(data + 4) != EM_GS_WORLD_MEMORY_VERSION;
    const uint32_t runs = bad ? 0 : rd32le(data + 8);
    size_t at = 16;
    for (uint32_t i = 0; !bad && i < runs; ++i) {
        if (at + 8 > (size_t)size) { bad = 1; break; }
        const uint32_t first = rd32le(data + at), count = rd32le(data + at + 4);
        at += 8;
        if (!count || first >= BLOCKS || count > BLOCKS - first || at + (size_t)count * 256u > (size_t)size) {
            bad = 1;
            break;
        }
        memcpy(w->mem + (size_t)first * 256u, data + at, (size_t)count * 256u);
        memset(w->resident + first, 1, count);
        at += (size_t)count * 256u;
    }
    if (!bad && at != (size_t)size) bad = 1;
    free(data);
    if (bad) {
        memset(w->resident, 0, sizeof w->resident);
        return fail(w, "the GS memory image is malformed (EMGM v1 expected)", 0);
    }
    w->memory_loaded = 1;
    return 0;
}

int em_gs_world_memory_loaded(const EmGsWorld *w) { return w && w->memory_loaded; }

/* ------------------------------------------------------------ uploads */

static uint64_t rd64le(const uint8_t *p)
{
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

int em_gs_world_upload_chain(EmGsWorld *w, const uint8_t *buf, size_t size)
{
    if (!w || !buf) return -1;
    if (w->fault[0]) return -1;
    if (w->recording) return fail(w, "an upload inside a recorded world frame", size);
    /* the workers have drawn the frame before the upload (they share the
     * memory the transfer writes) */
    if (em_gs_world_wait(w) < 0) return -1;
    /* the DMA source chain from the buffer's start (TTE 0): CNT tags, ended
     * by RET (an empty stack) or END; their data is the VIF1 stream */
    uint8_t *vif = malloc(size ? size : 1), *gif = malloc(size ? size : 1);
    if (!vif || !gif) {
        free(vif);
        free(gif);
        return fail(w, "out of memory for an upload", size);
    }
    size_t a = 0, vn = 0, gn = 0;
    int bad = 0;
    for (;;) {
        if (a + 16u > size) { bad = 1; break; }
        const uint32_t w0 = rd32le(buf + a), id = (w0 >> 28) & 7u, qwc = w0 & 0xFFFFu;
        if (id != 1u && id != 6u && id != 7u) { bad = 2; break; }
        if (a + 16u + 16u * (size_t)qwc > size) { bad = 1; break; }
        memcpy(vif + vn, buf + a + 16u, 16u * (size_t)qwc);
        vn += 16u * (size_t)qwc;
        if (id != 1u) break;
        a += 16u * ((size_t)qwc + 1u);
    }
    if (!bad && em_gs_vif_direct(vif, vn, 0, gif, size, &gn) < 0) bad = 3;
    free(vif);
    /* the GIF data: A+D writes of TEXFLUSH and the transfer registers, and
     * the IMAGE data of host-to-local PSMCT32 transfers (the uploads 00200830
     * sends; tools/export_disc_textures_gs.py models the same rule) */
    uint64_t regs[0x60] = {0};
    for (size_t g = 0; !bad && g + 16u <= gn;) {
        const uint64_t lo = rd64le(gif + g), hi = rd64le(gif + g + 8u);
        g += 16u;
        const unsigned nloop = (unsigned)(lo & 0x7FFFu), flg = (unsigned)((lo >> 58) & 3u);
        const unsigned nreg = (unsigned)(lo >> 60) ? (unsigned)(lo >> 60) : 16u;
        if (flg == 0u) {
            if ((lo >> 46) & 1u) { bad = 4; break; }
            for (unsigned r = 0; r < nreg; ++r)
                if (((hi >> (4u * r)) & 15u) != 0xEu) bad = 4;
            for (unsigned k = 0; !bad && k < nloop * nreg; ++k, g += 16u) {
                if (g + 16u > gn) { bad = 1; break; }
                const uint64_t v = rd64le(gif + g);
                const unsigned reg = gif[g + 8];
                if (reg != EM_GS_TEXFLUSH && reg != EM_GS_BITBLTBUF && reg != EM_GS_TRXPOS && reg != EM_GS_TRXREG &&
                    reg != EM_GS_TRXDIR) { bad = 4; break; }
                regs[reg] = v;
                if (reg == EM_GS_TRXDIR) {
                    const uint64_t bb = regs[EM_GS_BITBLTBUF], pos = regs[EM_GS_TRXPOS], rg = regs[EM_GS_TRXREG];
                    if ((v & 3u) != 0u || ((bb >> 56) & 0x3Fu) != EM_GS_PSMCT32 || ((pos >> 59) & 3u)) { bad = 5; break; }
                    const uint32_t dbp = (uint32_t)(bb >> 32) & 0x3FFFu, dbw = (uint32_t)(bb >> 48) & 0x3Fu;
                    const uint32_t dx = (uint32_t)(pos >> 32) & 0x7FFu, dy = (uint32_t)(pos >> 48) & 0x7FFu;
                    const uint32_t rw = (uint32_t)rg & 0xFFFu, rh = (uint32_t)(rg >> 32) & 0xFFFu;
                    for (uint32_t y = dy; y < dy + rh; ++y)
                        for (uint32_t x = dx; x < dx + rw; ++x) {
                            const uint32_t b = em_gs_addr32(dbp, dbw, x, y, 0) * 4u / 256u;
                            if (b < BLOCKS) w->resident[b] = 1;
                        }
                }
            }
        } else if (flg == 2u) {
            g += 16u * (size_t)nloop;
            if (g > gn) bad = 1;
        } else {
            bad = 4;
        }
    }
    if (bad) {
        free(gif);
        static const char *const why[] = {"", "an upload chain that runs past its buffer",
                                          "an upload chain tag other than CNT / RET / END",
                                          "an upload's VIF code other than NOP / FLUSH / DIRECT",
                                          "an upload's GIF data other than transfer A+D writes and IMAGE",
                                          "an upload other than a host-to-local PSMCT32 transfer"};
        return fail(w, why[bad], size);
    }
    /* the transfers through a model of their own (the drawing workers'
     * registers take no transfer: drawing never reads them) */
    EmGs up;
    em_gs_init(&up, w->mem);
    up.strict = 1;
    const size_t used = em_gs_gif(&up, gif, gn);
    em_gs_release(&up);
    free(gif);
    if (used != gn || up.refusals) return fail(w, "the model refused an upload", up.refusals);
    /* textures read earlier may have changed: check them again */
    w->checked_count = 0;
    w->pool_used = 0;
    w->memory_loaded = 1;
    return 0;
}

/* ------------------------------------------------------------ recording */

void em_gs_world_begin(EmGsWorld *w)
{
    if (!w) return;
    Job *j = &w->job[w->rec];
    w->recording = 1;
    j->op_count = 0;
    j->gif_used = 0;
    j->env_bytes = j->clear_bytes = 0;
    memset(w->cached, 0, sizeof w->cached);
    memset(w->fdrawn, 0, sizeof w->fdrawn);
    memset(w->fread, 0, sizeof w->fread);
    w->fdrawn_any = w->fread_any = 0;
    w->marked_count = 0;
    memset(w->tex0_known, 0, sizeof w->tex0_known);
    memset(w->clamp_known, 0, sizeof w->clamp_known);
    w->prim_known = 0;
}

int em_gs_world_recording(const EmGsWorld *w) { return w && w->recording; }

static void push(EmGsWorld *w, uint32_t op, uint32_t reg, uint64_t value)
{
    Job *j = &w->job[w->rec];
    if (j->op_count == j->op_cap) {
        const size_t cap = j->op_cap ? j->op_cap * 2u : 65536u;
        Op *n = realloc(j->ops, cap * sizeof *n);
        if (!n) {
            fail(w, "out of memory recording the frame", (unsigned long long)cap);
            return;
        }
        j->ops = n;
        j->op_cap = cap;
    }
    j->ops[j->op_count++] = (Op){ op, reg, value };
}

static int is_vertex_reg(unsigned reg)
{
    return reg == EM_GS_PRIM || reg == EM_GS_RGBAQ || reg == EM_GS_ST || reg == EM_GS_UV ||
           reg == EM_GS_XYZF2 || reg == EM_GS_XYZ2 || reg == EM_GS_XYZF3 || reg == EM_GS_XYZ3 ||
           reg == EM_GS_FOG;
}

/* The texture state a register write leaves (PRIM, TEX0, CLAMP; TEX2
 * rewrites part of TEX0: unknown). */
static void tex_state(EmGsWorld *w, unsigned reg, uint64_t value)
{
    switch (reg) {
    case EM_GS_PRIM:
        w->prim = (uint32_t)value & 0x7FFu;
        w->prim_known = 1;
        break;
    case EM_GS_TEX0_1: case EM_GS_TEX0_2:
        w->tex0[reg - EM_GS_TEX0_1] = value;
        w->tex0_known[reg - EM_GS_TEX0_1] = 1;
        break;
    case EM_GS_CLAMP_1: case EM_GS_CLAMP_2:
        w->clamp[reg - EM_GS_CLAMP_1] = value;
        w->clamp_known[reg - EM_GS_CLAMP_1] = 1;
        break;
    case EM_GS_TEX2_1: case EM_GS_TEX2_2:
        w->tex0_known[reg - EM_GS_TEX2_1] = 0;
        break;
    default:
        break;
    }
}

void em_gs_world_write(EmGsWorld *w, unsigned reg, uint64_t value)
{
    if (!w || w->fault[0]) return;
    if (!w->recording) {
        fail(w, "a GS write outside a recorded world frame", reg);
        return;
    }
    if (reg >= 0x80u) {
        fail(w, "a GS register outside the map", reg);
        return;
    }
    /* No transfer in a recorded frame: the textures are the resident
     * uploads, and the workers would each run a transfer again. */
    if (reg == EM_GS_BITBLTBUF || reg == EM_GS_TRXPOS || reg == EM_GS_TRXREG || reg == EM_GS_TRXDIR ||
        reg == EM_GS_HWREG) {
        fail(w, "a transfer register in a recorded frame", reg);
        return;
    }
    tex_state(w, reg, value);
    if (!is_vertex_reg(reg)) {
        if (w->cached[reg] && w->cache[reg] == value) return;
        w->cached[reg] = 1;
        w->cache[reg] = value;
    }
    push(w, OP_WRITE, reg, value);
}

/* The 256-byte blocks of a PSMCT32 buffer of `height` rows. */
static void mark_buffer(uint8_t *map, uint32_t fbp, uint32_t fbw, uint32_t height)
{
    for (uint32_t y = 0; y < height; y += 8u)
        for (uint32_t x = 0; x < 64u * fbw; x += 8u) {
            const uint32_t b = em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u / 256u;
            if (b < BLOCKS) map[b] = 1;
        }
}

static void barrier_op(EmGsWorld *w)
{
    push(w, OP_BARRIER, 0, 0);
    memset(w->fdrawn, 0, sizeof w->fdrawn);
    memset(w->fread, 0, sizeof w->fread);
    w->fdrawn_any = w->fread_any = 0;
    w->marked_count = 0;
}

void em_gs_world_drawn_buffer(EmGsWorld *w, uint64_t frame, uint32_t height)
{
    if (!w) return;
    const uint32_t fbp = (uint32_t)frame & 0x1FFu, fbw = (uint32_t)(frame >> 16) & 0x3Fu;
    if (w->recording)
        for (uint32_t i = 0; i < w->marked_count; ++i)
            if (w->marked[i].fbp == fbp && w->marked[i].fbw == fbw && w->marked[i].height >= height) return;
    mark_buffer(w->drawn, fbp, fbw, height);
    if (!w->recording) return;
    if (w->fread_any) {
        /* a draw into blocks a band may still read: every band reads first */
        static uint8_t scratch[BLOCKS];
        memset(scratch, 0, sizeof scratch);
        mark_buffer(scratch, fbp, fbw, height);
        for (uint32_t b = 0; b < BLOCKS; ++b)
            if (scratch[b] && w->fread[b]) {
                barrier_op(w);
                break;
            }
    }
    mark_buffer(w->fdrawn, fbp, fbw, height);
    w->fdrawn_any = 1;
    if (w->marked_count < MARKED_MAX) {
        w->marked[w->marked_count].fbp = fbp;
        w->marked[w->marked_count].fbw = fbw;
        w->marked[w->marked_count].height = height;
        w->marked_count++;
    }
}

/* The texel indices a coordinate can reach on one axis under CLAMP_1's
 * wrap mode (GS_EXACT.md 4.3): REPEAT and CLAMP reach the whole 2^n; the
 * region modes reach [min, max] and the texels (u & min) | max. */
static void reach(uint32_t mode, uint32_t size, uint32_t mn, uint32_t mx, uint32_t *lo, uint32_t *hi)
{
    *lo = 0;
    *hi = size - 1u;
    if (mode == 2u) {
        *lo = mn < size ? mn : size - 1u;
        *hi = mx < size ? mx : size - 1u;
        if (*lo > *hi) *lo = *hi;
    } else if (mode == 3u) {
        *lo = mx & (size - 1u);
        *hi = (mn | mx) & (size - 1u);
        if (*lo > *hi) *lo = 0;
    }
}

static int pool_add(EmGsWorld *w, uint32_t b)
{
    if (w->pool_used == w->pool_cap) {
        const uint32_t cap = w->pool_cap ? w->pool_cap * 2u : 4096u;
        uint32_t *n = realloc(w->pool, cap * sizeof *n);
        if (!n) return -1;
        w->pool = n;
        w->pool_cap = cap;
    }
    w->pool[w->pool_used++] = b;
    return 0;
}

/* The blocks a TEX0 reads (the texels CLAMP_1 lets it reach, and its CLUT)
 * are all resident (uploaded) or in a buffer the model has drawn into; the
 * entry lists the drawn ones. 0, or -1 (not). */
static int texture_entry(EmGsWorld *w, uint64_t tex0, uint64_t clamp, uint32_t *first, uint32_t *n)
{
    const uint64_t key = tex0 & ~(UINT64_C(7) << 61);    /* CLD is not a read */
    for (uint32_t i = 0; i < w->checked_count; ++i)
        if (w->checked[i].tex0 == key && w->checked[i].clamp == clamp) {
            *first = w->checked[i].first;
            *n = w->checked[i].n;
            return 0;
        }
    const uint32_t tbp = (uint32_t)tex0 & 0x3FFFu, tbw = (uint32_t)(tex0 >> 14) & 0x3Fu;
    const uint32_t psm = (uint32_t)(tex0 >> 20) & 0x3Fu, tw = (uint32_t)(tex0 >> 26) & 15u;
    const uint32_t th = (uint32_t)(tex0 >> 30) & 15u, cbp = (uint32_t)(tex0 >> 37) & 0x3FFFu;
    const uint32_t cpsm = (uint32_t)(tex0 >> 51) & 15u;
    if (tw > 10u || th > 10u) return -1;
    uint32_t u0, u1, v0, v1;
    reach((uint32_t)clamp & 3u, 1u << tw, (uint32_t)(clamp >> 4) & 0x3FFu, (uint32_t)(clamp >> 14) & 0x3FFu, &u0, &u1);
    reach((uint32_t)(clamp >> 2) & 3u, 1u << th, (uint32_t)(clamp >> 24) & 0x3FFu, (uint32_t)(clamp >> 34) & 0x3FFu,
          &v0, &v1);
    static uint8_t seen[BLOCKS];
    memset(seen, 0, sizeof seen);
    const uint32_t start = w->pool_used;
    for (uint32_t y = v0; y <= v1; ++y)
        for (uint32_t x = u0; x <= u1; ++x) {
            uint32_t b;
            switch (psm) {
            case EM_GS_PSMCT32: case EM_GS_PSMCT24: b = em_gs_addr32(tbp, tbw, x, y, 0) * 4u / 256u; break;
            case EM_GS_PSMT8: b = em_gs_addr8(tbp, tbw, x, y) / 256u; break;
            case EM_GS_PSMT4: b = em_gs_addr4(tbp, tbw, x, y) / 512u; break;
            default: w->pool_used = start; return -1;   /* a format the first level never samples */
            }
            if (b >= BLOCKS || (!w->resident[b] && !w->drawn[b])) {
                w->pool_used = start;
                return -1;
            }
            if (!w->resident[b] && !seen[b]) {
                seen[b] = 1;
                if (pool_add(w, b) < 0) {
                    w->pool_used = start;
                    return -1;
                }
            }
        }
    if (psm == EM_GS_PSMT8 || psm == EM_GS_PSMT4) {
        if (cpsm != EM_GS_PSMCT32) { w->pool_used = start; return -1; }
        const uint32_t cw = psm == EM_GS_PSMT8 ? 16u : 8u, ch = psm == EM_GS_PSMT8 ? 16u : 2u;
        for (uint32_t y = 0; y < ch; ++y)
            for (uint32_t x = 0; x < cw; ++x) {
                const uint32_t b = em_gs_addr32(cbp, 1, x, y, 0) * 4u / 256u;
                if (b >= BLOCKS || !w->resident[b]) { w->pool_used = start; return -1; }
            }
    }
    *first = start;
    *n = w->pool_used - start;
    if (w->checked_count < CHECKED_MAX) {
        w->checked[w->checked_count].tex0 = key;
        w->checked[w->checked_count].clamp = clamp;
        w->checked[w->checked_count].first = *first;
        w->checked[w->checked_count].n = *n;
        w->checked_count++;
    }
    return 0;
}

/* A textured primitive's read: residency, then the workers' ordering (a
 * read of a block drawn since the last barrier needs one). 0, or -1. */
static int texture_read(EmGsWorld *w, uint64_t tex0, uint64_t clamp)
{
    uint32_t first = 0, n = 0;
    if (texture_entry(w, tex0, clamp, &first, &n) < 0) return -1;
    if (!n) return 0;
    if (w->fdrawn_any)
        for (uint32_t i = 0; i < n; ++i)
            if (w->fdrawn[w->pool[first + i]]) {
                barrier_op(w);
                break;
            }
    for (uint32_t i = 0; i < n; ++i) w->fread[w->pool[first + i]] = 1;
    w->fread_any = 1;
    return 0;
}

/* A primitive kicked with PRIM `prim` (PRMODECONT 1: its TME and CTXT):
 * when textured, its context's TEX0 must be known and read only resident
 * or drawn memory. 0, or -1 (the fault latched). */
static int textured_kick(EmGsWorld *w, uint32_t prim)
{
    if (!((prim >> 4) & 1u)) return 0;
    const unsigned c = (prim >> 9) & 1u;
    if (!w->tex0_known[c])
        return fail(w, "a textured primitive whose TEX0 the recorder does not know", prim);
    if (texture_read(w, w->tex0[c], w->clamp_known[c] ? w->clamp[c] : 0) < 0)
        return fail(w, "a texture that reads GS memory no upload wrote", w->tex0[c]);
    return 0;
}

void em_gs_world_prims(EmGsWorld *w, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count)
{
    if (!w || w->fault[0]) return;
    for (uint32_t i = 0; i < count && !w->fault[0]; ++i) {
        const EmGfxGsPrim *p = &prims[i];
        if (envs) {
            const EmGfxGsEnv *e = &envs[i];
            if (e->set & EM_GFX_GS_ENV_FRAME) {
                em_gs_world_write(w, EM_GS_FRAME_1, e->frame);
                if (e->set & EM_GFX_GS_ENV_SCISSOR)
                    em_gs_world_drawn_buffer(w, e->frame, (uint32_t)(e->scissor >> 48 & 0x7FFu) + 1u);
            }
            if (e->set & EM_GFX_GS_ENV_ZBUF) em_gs_world_write(w, EM_GS_ZBUF_1, e->zbuf);
            if (e->set & EM_GFX_GS_ENV_XYOFFSET) em_gs_world_write(w, EM_GS_XYOFFSET_1, e->xyoffset);
            if (e->set & EM_GFX_GS_ENV_SCISSOR) em_gs_world_write(w, EM_GS_SCISSOR_1, e->scissor);
            if (e->set & EM_GFX_GS_ENV_PRMODECONT) em_gs_world_write(w, EM_GS_PRMODECONT, e->prmodecont);
            if (e->set & EM_GFX_GS_ENV_DTHE) em_gs_world_write(w, EM_GS_DTHE, e->dthe);
            if (e->set & EM_GFX_GS_ENV_FBA) em_gs_world_write(w, EM_GS_FBA_1, e->fba);
            if (e->set & EM_GFX_GS_ENV_PABE) em_gs_world_write(w, EM_GS_PABE, e->pabe);
            if (e->set & EM_GFX_GS_ENV_TEXA) em_gs_world_write(w, EM_GS_TEXA, e->texa);
            if (e->set & EM_GFX_GS_ENV_SCANMSK) em_gs_world_write(w, EM_GS_SCANMSK, e->scanmsk);
        }
        if (p->set & EM_GFX_GS_TEX0) em_gs_world_write(w, EM_GS_TEX0_1, p->tex0);
        if (p->set & EM_GFX_GS_CLAMP) em_gs_world_write(w, EM_GS_CLAMP_1, p->clamp);
        if (p->set & EM_GFX_GS_TEX1) em_gs_world_write(w, EM_GS_TEX1_1, p->tex1);
        if (p->set & EM_GFX_GS_ALPHA) em_gs_world_write(w, EM_GS_ALPHA_1, p->alpha);
        if (p->set & EM_GFX_GS_TEST) em_gs_world_write(w, EM_GS_TEST_1, p->test);
        if (p->set & EM_GFX_GS_COLCLAMP) em_gs_world_write(w, EM_GS_COLCLAMP, p->colclamp);
        const uint32_t prim = p->prim & 0x7FFu, fst = (prim >> 8) & 1u;
        if (textured_kick(w, prim) < 0) return;
        em_gs_world_write(w, EM_GS_PRIM, prim);
        const uint32_t n = p->count > 3u ? 3u : p->count;
        for (uint32_t k = 0; k < n; ++k) {
            const EmGfxGsVertex *v = &p->v[k];
            em_gs_world_write(w, EM_GS_RGBAQ, (uint64_t)v->rgba[0] | (uint64_t)v->rgba[1] << 8 |
                                              (uint64_t)v->rgba[2] << 16 | (uint64_t)v->rgba[3] << 24 |
                                              (uint64_t)v->q << 32);
            em_gs_world_write(w, EM_GS_ST, (uint64_t)v->s | (uint64_t)v->t << 32);
            if (fst) em_gs_world_write(w, EM_GS_UV, (uint64_t)(v->u & 0x3FFFu) | (uint64_t)(v->v & 0x3FFFu) << 16);
            if (v->has_f)
                em_gs_world_write(w, EM_GS_XYZF2, (uint64_t)v->x | (uint64_t)v->y << 16 |
                                                  (uint64_t)(v->z & 0xFFFFFFu) << 32 | (uint64_t)v->f << 56);
            else
                em_gs_world_write(w, EM_GS_XYZ2, (uint64_t)v->x | (uint64_t)v->y << 16 | (uint64_t)v->z << 32);
        }
    }
}

void em_gs_world_env_again(EmGsWorld *w)
{
    if (!w || w->fault[0]) return;
    if (!w->recording) {
        fail(w, "the draw environment REFed outside a recorded world frame", 0);
        return;
    }
    push(w, OP_ENV, 0, 0);
    memset(w->cached, 0, sizeof w->cached);    /* the environment rewrote state */
    /* its packets are the kick's (not known while recording) */
    memset(w->tex0_known, 0, sizeof w->tex0_known);
    memset(w->clamp_known, 0, sizeof w->clamp_known);
    w->prim_known = 0;
}

/* GIF tags of register data only (PACKED, no IMAGE; no A+D transfer
 * register): the state blocks and strips a recorded frame takes. */
static int gif_registers_only(const uint8_t *p, size_t bytes)
{
    size_t off = 0;
    while (off + 16u <= bytes) {
        uint64_t lo, hi;
        memcpy(&lo, p + off, 8);
        memcpy(&hi, p + off + 8, 8);
        off += 16u;
        const unsigned nloop = (unsigned)(lo & 0x7FFFu), flg = (unsigned)((lo >> 58) & 3u);
        unsigned nreg = (unsigned)(lo >> 60);
        if (!nreg) nreg = 16u;
        if (flg != 0u) return 0;
        if (off + (size_t)nloop * nreg * 16u > bytes) return 0;
        for (unsigned r = 0; r < nreg; ++r)
            if (((hi >> (4u * r)) & 15u) == 0xEu)
                for (unsigned l = 0; l < nloop; ++l) {
                    const unsigned reg = p[off + ((size_t)l * nreg + r) * 16u + 8u];
                    if (reg == EM_GS_BITBLTBUF || reg == EM_GS_TRXPOS || reg == EM_GS_TRXREG ||
                        reg == EM_GS_TRXDIR || reg == EM_GS_HWREG)
                        return 0;
                }
        off += (size_t)nloop * nreg * 16u;
    }
    return off == bytes;
}

/* The texture state GIF register data leave (gif_registers_only's shape:
 * PACKED tags), and their textured primitives' reads (the PRIM of a PRE tag,
 * an A+D or PACKED PRIM; a kick is an XYZ2 / XYZF2 write, PACKED with ADC
 * 0). 0, or -1 (the fault latched). */
static int gif_learn(EmGsWorld *w, const uint8_t *p, size_t bytes)
{
    size_t off = 0;
    while (off + 16u <= bytes) {
        uint64_t lo, hi;
        memcpy(&lo, p + off, 8);
        memcpy(&hi, p + off + 8, 8);
        off += 16u;
        const unsigned nloop = (unsigned)(lo & 0x7FFFu);
        unsigned nreg = (unsigned)(lo >> 60);
        if (!nreg) nreg = 16u;
        if ((lo >> 46) & 1u) tex_state(w, EM_GS_PRIM, lo >> 47);
        for (unsigned l = 0; l < nloop; ++l)
            for (unsigned r = 0; r < nreg; ++r, off += 16u) {
                const unsigned desc = (unsigned)(hi >> (4u * r)) & 15u;
                uint64_t v, v_hi;
                memcpy(&v, p + off, 8);
                memcpy(&v_hi, p + off + 8, 8);
                unsigned reg = desc;
                int kick = 0;
                if (desc == 0xEu) {
                    reg = (unsigned)(v_hi & 0xFFu);
                    kick = reg == EM_GS_XYZ2 || reg == EM_GS_XYZF2;
                } else if (desc == EM_GS_XYZ2 || desc == EM_GS_XYZF2) {
                    kick = !((v_hi >> 47) & 1u);
                }
                tex_state(w, reg, v);
                if (kick) {
                    if (!w->prim_known)
                        return fail(w, "a vertex kick in GIF data whose PRIM the recorder does not know", 0);
                    if (textured_kick(w, w->prim) < 0) return -1;
                }
            }
    }
    return 0;
}

void em_gs_world_gif(EmGsWorld *w, const void *gif, size_t bytes)
{
    if (!w || w->fault[0]) return;
    if (!w->recording) {
        fail(w, "GIF data outside a recorded world frame", bytes);
        return;
    }
    if (!gif || !bytes || bytes % 16u || bytes > 0xFFFFFFFFu) {
        fail(w, "GIF data that are not whole quadwords", bytes);
        return;
    }
    if (!gif_registers_only(gif, bytes)) {
        fail(w, "GIF data that are not register packets (a transfer in a recorded frame)", bytes);
        return;
    }
    if (gif_learn(w, gif, bytes) < 0) return;
    Job *j = &w->job[w->rec];
    if (j->gif_used + bytes > j->gif_cap) {
        size_t cap = j->gif_cap ? j->gif_cap : 0x10000u;
        while (cap < j->gif_used + bytes) cap *= 2u;
        uint8_t *n = realloc(j->gif, cap);
        if (!n) {
            fail(w, "out of memory recording GIF data", cap);
            return;
        }
        j->gif = n;
        j->gif_cap = cap;
    }
    memcpy(j->gif + j->gif_used, gif, bytes);
    push(w, OP_GIF, (uint32_t)bytes, j->gif_used);
    j->gif_used += bytes;
    memset(w->cached, 0, sizeof w->cached);    /* the packet wrote state */
}

/* ------------------------------------------------------------ execution */

/* The field a job's workers read: a PSMCT32 buffer at most 512 wide and
 * 1024 rows. 0, or -1. */
static int field_check(EmGsWorld *w, uint64_t frame, uint32_t height)
{
    const uint32_t fbw = (uint32_t)(frame >> 16) & 0x3Fu, psm = (uint32_t)(frame >> 24) & 0x3Fu;
    if (psm != EM_GS_PSMCT32 || fbw == 0u || 64u * fbw > EM_GS_WORLD_FIELD_W || height == 0u || height > 1024u)
        return fail(w, "the displayed buffer is not a PSMCT32 field the port presents", frame);
    return 0;
}

static uint64_t pixels_drawn(const EmGsWorld *w)
{
    uint64_t n = 0;
    for (uint32_t i = 0; i < w->workers; ++i) n += w->worker[i].gs.drawn_pixels;
    return n;
}

/* Hands job[rec] to the workers and makes the other job the recorded one. */
static void start(EmGsWorld *w)
{
    w->run = &w->job[w->rec];
    w->rec ^= 1;
    w->recording = 0;
    w->busy = 1;
    w->t_start = now_ns();
    w->prims0 = w->worker[0].gs.drawn_prims;
    w->pixels0 = pixels_drawn(w);
#if GSW_THREADS
    if (w->threaded) {
        pthread_mutex_lock(&w->lock);
        w->running = w->workers;
        w->generation++;
        pthread_cond_broadcast(&w->go);
        pthread_mutex_unlock(&w->lock);
        return;
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) run_frame(&w->worker[i]);
}

int em_gs_world_wait(EmGsWorld *w)
{
    if (!w) return -1;
    if (!w->busy) return w->fault[0] ? -1 : 0;
#if GSW_THREADS
    if (w->threaded) {
        pthread_mutex_lock(&w->lock);
        while (w->running) pthread_cond_wait(&w->done, &w->lock);
        pthread_mutex_unlock(&w->lock);
    }
#endif
    w->busy = 0;
    const Job *j = w->run;
    /* the outcome: every worker ran the job whole, and the model refused
     * nothing (the registers, refusals and span faults are the same in every
     * worker; worker 0 speaks for them) */
    for (uint32_t i = 0; i < w->workers; ++i)
        if (w->worker[i].failed)
            return fail(w, "a GIF packet of the frame is malformed, or a barrier fell inside a span", i);
    const EmGs *gs = &w->worker[0].gs;
    if (gs->refused_prims || gs->refusals) {
        char what[192];
        snprintf(what, sizeof what, "%s: the GS model refused a primitive: %s",
                 j->list ? "list frame" : "world frame", gs->reason);
        return fail(w, what, gs->refusals);
    }
    if (gs->span_faults)
        return fail(w, "a span fault (an unmeasured span boundary decides the grid)", gs->span_faults);
    w->field_w = 64u * ((uint32_t)(j->frame >> 16) & 0x3Fu);
    w->field_h = j->height;
    w->field_frame = j->frame;
    w->field_xyoffset = j->xyoffset;
    w->field_xyoffset_known = j->xyoffset_known;
    w->have_field = 1;
    w->stats.writes = j->op_count;
    w->stats.prims = gs->drawn_prims - w->prims0;
    w->stats.pixels = pixels_drawn(w) - w->pixels0;
    w->stats.span_faults = gs->span_faults;
    w->stats.refused = gs->refused_prims;
    w->stats.ns = now_ns() - w->t_start;
    w->stats.cpu_max_ns = w->stats.cpu_sum_ns = 0;
    for (uint32_t i = 0; i < w->workers; ++i) {
        if (w->worker[i].cpu_ns > w->stats.cpu_max_ns) w->stats.cpu_max_ns = w->worker[i].cpu_ns;
        w->stats.cpu_sum_ns += w->worker[i].cpu_ns;
    }
    w->stats.runs++;
    return 0;
}

int em_gs_world_busy(const EmGsWorld *w) { return w && w->busy; }

int em_gs_world_kick(EmGsWorld *w, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes)
{
    if (!w) return -1;
    if (w->fault[0]) return -1;
    if (!w->recording) return fail(w, "a kick without a recorded world frame", 0);
    if (!env || !env_bytes || env_bytes > HEAD_MAX || env_bytes % 16u || (clear_bytes && !clear) ||
        clear_bytes > HEAD_MAX || clear_bytes % 16u)
        return fail(w, "the kick's head packets are missing, too long or not whole quadwords", env_bytes);
    if (!gif_registers_only(env, env_bytes) || (clear_bytes && !gif_registers_only(clear, clear_bytes)))
        return fail(w, "the kick's head packets are not register packets", env_bytes);
    /* the previous job finishes first (the workers and the field are one) */
    if (em_gs_world_wait(w) < 0) return -1;
    Job *j = &w->job[w->rec];
    memcpy(j->env, env, env_bytes);
    j->env_bytes = env_bytes;
    if (clear_bytes) memcpy(j->clear, clear, clear_bytes);
    j->clear_bytes = clear_bytes;
    j->list = 0;
    /* the field: the FRAME_1 and SCISSOR_1 of the kicked draw environment
     * (register packets only: a model run over them writes no memory) */
    EmGs probe;
    em_gs_init(&probe, w->mem);
    (void)em_gs_gif(&probe, j->env, j->env_bytes);
    j->frame = probe.ctx[0].frame;
    j->xyoffset = probe.ctx[0].xyoffset;
    j->xyoffset_known = 1;
    j->height = (uint32_t)(probe.ctx[0].scissor >> 48 & 0x7FFu) + 1u;
    em_gs_release(&probe);
    if (field_check(w, j->frame, j->height) < 0) return -1;
    mark_buffer(w->drawn, (uint32_t)j->frame & 0x1FFu, (uint32_t)(j->frame >> 16) & 0x3Fu, j->height);
    start(w);
    return 0;
}

int em_gs_world_list_frame(EmGsWorld *w, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                           uint64_t display_frame, uint64_t display_scissor)
{
    if (!w) return -1;
    if (w->fault[0]) return -1;
    if (count && (!prims || !envs)) return fail(w, "a list frame without its primitives", count);
    if (w->recording) return fail(w, "a list frame inside a recorded world frame", count);
    em_gs_world_begin(w);
    em_gs_world_prims(w, prims, envs, count);
    if (w->fault[0]) {
        w->recording = 0;
        return -1;
    }
    if (em_gs_world_wait(w) < 0) return -1;
    Job *j = &w->job[w->rec];
    j->list = 1;
    /* the field's draw offset: the XYOFFSET_1 of the last primitive the list
     * drew into the displayed buffer (its FBP and FBW); unknown when none did */
    j->xyoffset = 0;
    j->xyoffset_known = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const EmGfxGsEnv *e = &envs[i];
        if ((e->set & (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_XYOFFSET)) ==
                (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_XYOFFSET) &&
            (e->frame & 0x3F01FFu) == (display_frame & 0x3F01FFu))
            j->xyoffset = e->xyoffset, j->xyoffset_known = 1;
    }
    j->frame = display_frame;
    j->height = (uint32_t)(display_scissor >> 48 & 0x7FFu) + 1u;
    if (field_check(w, j->frame, j->height) < 0) return -1;
    start(w);
    return 0;
}

uint64_t em_gs_world_field_xyoffset(const EmGsWorld *w) { return w ? w->field_xyoffset : 0; }
int em_gs_world_field_xyoffset_known(const EmGsWorld *w) { return w && w->have_field && w->field_xyoffset_known; }

int em_gs_world_handed_xyoffset(const EmGsWorld *w, uint64_t *xyoffset)
{
    if (!w || !w->run) return -1;
    if (!w->run->xyoffset_known) return 0;
    if (xyoffset) *xyoffset = w->run->xyoffset;
    return 1;
}

const uint8_t *em_gs_world_field(const EmGsWorld *w, uint32_t *width, uint32_t *height, uint64_t *frame)
{
    if (!w || !w->have_field || w->busy) return NULL;
    if (width) *width = w->field_w;
    if (height) *height = w->field_h;
    if (frame) *frame = w->field_frame;
    return w->field;
}

int em_gs_world_read(EmGsWorld *w, uint32_t fbp, uint32_t fbw, uint32_t width, uint32_t height, uint8_t *rgba)
{
    if (!w || !rgba || !fbw || width > 64u * fbw || (uint64_t)fbp * 8192u >= EM_GS_MEM_BYTES) return -1;
    if (em_gs_world_wait(w) < 0) return -1;
    em_gs_read_frame_rgba(&w->worker[0].gs, fbp, fbw, width, height, rgba);
    return 0;
}

const uint8_t *em_gs_world_memory(EmGsWorld *w)
{
    if (!w || em_gs_world_wait(w) < 0) return NULL;
    return w->mem;
}

int em_gs_world_resident(const EmGsWorld *w, uint32_t block)
{
    return w && block < BLOCKS && w->resident[block];
}
