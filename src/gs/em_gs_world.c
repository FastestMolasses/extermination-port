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
#include <stdatomic.h>
#include <unistd.h>
#define GSW_THREADS 1
#else
#define GSW_THREADS 0
#endif
extern char **environ;   /* (em_gs_world_raster_wanted) */
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <sys/sysctl.h>
#endif

#define BLOCKS (EM_GS_MEM_BYTES / 256u)
#define HEAD_MAX 0x800u
#define CHECKED_MAX 1024u
#define CHECKED_SLOTS 4096u           /* checked[]'s hash (a power of two, 4 x CHECKED_MAX) */
#define MARKED_MAX 8u
#define WORKERS_MAX 16u
#define SEG_MAX 1024u                 /* barriers a frame may hold, plus one */
#define BANDS_PER_WORKER 2u           /* a segment's bands (EM_GS_BANDS): workers x this */
#define BAND_MIN_ROWS 4               /* the least rows a band takes */
#define LIST_CHUNK 32u                /* spans a worker waits for while the parse leads */
#define READ_ROWS 16u                 /* rows of the field a readout task copies */

/* The parse's progress, read by the workers without the lock (seq_cst, so a
 * worker that announces its wait and a parser that publishes see each other). */
#if GSW_THREADS
typedef atomic_uint gsw_au;
#define AU_LOAD(a) atomic_load(&(a))
#define AU_STORE(a, v) atomic_store(&(a), (v))
#define AU_ADD(a, v) atomic_fetch_add(&(a), (v))
#define AU_SUB(a, v) atomic_fetch_sub(&(a), (v))
#else
typedef unsigned gsw_au;
#define AU_LOAD(a) (a)
#define AU_STORE(a, v) ((a) = (v))
#define AU_ADD(a, v) ((a) += (v))
#define AU_SUB(a, v) ((a) -= (v))
#endif

enum { OP_WRITE = 0, OP_ENV = 1, OP_GIF = 2, OP_BARRIER = 3 };

/* A segment of a parsed frame: spans [first, end) and the rows they can
 * draw (ylo > yhi: none), known once the parse closed it. */
typedef struct {
    uint32_t first, end;
    int32_t ylo, yhi;
} Seg;

/* A frame's parse (docs/PERFORMANCE.md M4): its list and segments, how far
 * the parse got (published spans, closed segments, done; read by the
 * drawing workers without the lock), and where it goes on (a parse that
 * the game's thread began while the previous frame was drawn, early_parse,
 * is finished by a worker). The parser's counts are taken at its start and
 * end, so a frame's outcome is its own (the parser goes on to the next
 * frame before this one is waited for). */
typedef struct {
    EmGsList *list;
    Seg *seg;                        /* SEG_MAX */
    gsw_au published, segs_closed, done;
    uint32_t last_wake;              /* published at the parser's last wake-up */
    int failed;                      /* a malformed packet, a barrier inside a span, no memory */
    int began;                       /* the head was parsed */
    int started;                     /* the frame was handed to the workers */
    int quiet;                       /* early_parse: no worker draws the frame yet (no wake-ups) */
    size_t early_ops;                /* ops early_parse parsed (EM_GS_LIST_COUNTERS) */
    uint32_t prior_waits;            /* its waits for the frame before (a CLUT load) */
    uint32_t own_waits;              /* its CLUT loads from its own spans (own_drain) */
    gsw_au predrawn;                 /* segments [0, predrawn) drawn by own_drain's thread */
    uint64_t early_pixels;           /* pixels own_drain drew before the frame was handed over */
    size_t op_next;                  /* the next op to parse */
    uint32_t k;                      /* the segment being parsed */
    double ns;                       /* the parse's CPU time */
    uint64_t prims0, prims1;         /* the parser's drawn_prims at the start and the end */
    uint32_t refusals, refused, span_faults;
    char reason[128];
} Parse;

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
    Parse p;
} Job;

struct EmGsWorld;

/* One worker: its own EmGs over the shared local memory. With one worker
 * (and no EM_GS_LIST=1) it runs the frame's stream itself and draws every
 * row. Otherwise the frame is parsed once (EmGsWorld.parser, by whichever
 * worker takes the parse) into the list (EmGsList, em_gs_raster.h), and the
 * workers draw it a band of rows at a time (EmGs.band_rows), each band of a
 * segment (the ops between two barriers) by one worker, its primitives in
 * the list's order (docs/PERFORMANCE.md M4). */
typedef struct {
    struct EmGsWorld *w;
    EmGs gs;
    int failed;                       /* its run of the frame stopped (direct) */
    int32_t seg_seen;                 /* the frame's segments [0, seg_seen] its texture cache saw */
    uint32_t index;
    /* The band it draws (under the lock, except pos and split): its segment
     * and rows, the span it draws next, and a split another worker (thief)
     * asked for. The answer goes to the asker's hand: 1 with the rows
     * [hy0, hy1] from span hspan on, -1 when there were none to give. */
    int active;
    uint32_t aseg;
    int32_t ay0, ay1;
    gsw_au pos, split;
    uint32_t thief;
    int hand;
    int32_t hy0, hy1;
    uint32_t hspan;
    double cpu_ns;                    /* its CPU time in the last frame */
#if GSW_THREADS
    pthread_t thread;
#endif
} Worker;

/* A worker's piece of the frame. */
enum { T_NONE, T_PARSE, T_BAND, T_READ };
typedef struct {
    int kind;
    uint32_t seg;                     /* T_BAND: its segment */
    int32_t y0, y1;                   /* T_BAND, T_READ: its rows */
    uint32_t start;                   /* T_BAND: its first span (the segment's, or a split's) */
} Task;

struct EmGsWorld {
    uint8_t *mem;
    uint8_t resident[BLOCKS];        /* 1: an upload wrote the block */
    int memory_loaded;
    Worker worker[WORKERS_MAX];
    uint32_t workers;
    int threaded;                    /* the workers are threads (else the caller runs them) */
#if GSW_THREADS
    pthread_mutex_t lock;
    pthread_cond_t go, done, cv;
    uint64_t generation;             /* bumped for every frame the workers run */
    uint32_t running;                /* workers still running the frame */
    int quit;
#endif
    gsw_au live;                     /* running, read without the lock (early_parse) */
    /* Parse once (docs/PERFORMANCE.md M4; the Worker comment) */
    int parse_once;                  /* the frame is parsed once (else worker 0 runs it) */
    /* The headless raster skip (em_gs_world_create_raster(0); docs/
     * PERFORMANCE.md P18): the frame is recorded and parsed as always (every
     * check of the recorder and of the parser runs), and no span is drawn. */
    int no_raster;
    uint32_t bands_per_worker;       /* EM_GS_BANDS */
    EmGs parser;                     /* the parsing model (EmGs.list): registers, span, CLUT */
    Job *parsing;                    /* the job the parser takes (own_drain) */
    int early;                       /* the game's thread parses while it waits (EM_GS_EARLY_PARSE) */
    size_t early_max;                /* EM_GS_EARLY_OPS (a test switch): always, at most this many ops */
    gsw_au waiters;                  /* workers waiting on cv */
    /* the schedule, under the lock: the segment being drawn, its bands (the
     * rows [band_base + i band_h, band_base + (i + 1) band_h), the first from
     * the top, the last to the bottom), then the readout of the field */
    int parse_claimed;
    int32_t seg_cur;
    uint32_t band_next, band_planned, band_n, band_done;   /* band_n: planned plus split off */
    int32_t band_base, band_h, band_lo, band_hi;          /* band_lo..band_hi: the rows split */
    uint32_t splits;                                      /* this frame's (EM_GS_LIST_COUNTERS) */
    int read_phase;
    uint32_t read_next, read_n, read_done;
    /* EM_GS_LIST_COUNTERS=<file> (docs/PERFORMANCE.md M4): one line per
     * frame waited for. A diagnostic: it changes nothing drawn. */
    FILE *lc_file;
    uint64_t lc_frames, lc_prev[3];
    uint32_t bands_total, waits_claim, waits_avail;   /* this frame's (under the lock) */
    double wait_claim_ns, wait_avail_ns, wait_read_ns; /* their wall time, summed over the workers */

    Job job[2];
    int rec;                         /* job[rec] is recorded; job[rec ^ 1] may be running */
    Job *run;                        /* the job the workers run */
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
    /* A textured primitive since the last barrier (its reads of uploaded
     * blocks no draw has touched are not in fread: em_gs_world_drawn_buffer) */
    int tread_any;
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
    /* The drawing target (FRAME_1 and SCISSOR_1 as written last in the frame
     * being recorded): a barrier for a read empties fdrawn, and the target's
     * blocks go back into it, as the draws after the barrier write them.
     * Before such a write, at the frame's start and after the environment
     * again, the GS holds the head's, which the recorder does not read: a
     * register not written since is unknown until the field is declared,
     * and is then the field's (em_gs_world_field_declare; the kick checks
     * the declared field against the head's FRAME_1 and SCISSOR_1). */
    uint64_t tgt_frame, tgt_scissor;
    uint8_t tgt_frame_known, tgt_scissor_known;
    /* The Z buffer the last kicked head writes (its ZBUF_1 with ZMSK 0: its
     * ZBP, the field's FBW and SCISSOR_1 rows): its blocks hold what the
     * frames' draws wrote (drawn), and in a frame being recorded they are
     * drawn from the frame's start, as the field is (fdrawn with it). A
     * texture whose reach covers them (001DF110's sprite over the other
     * field with REPEAT: its 256-row texture covers the Z buffer's first
     * rows, ZBP 0x70 after FBP 0x38's 224) is ordered after the bands that
     * write them. Unknown before the first kick. */
    uint32_t head_zbp, head_zbw, head_zh;
    int head_z_known;
    int prim_known;
    uint8_t drawn[BLOCKS];           /* blocks of buffers the model has drawn into */
    /* The field declared in the frame being recorded (em_gs_world_field_declare). */
    uint64_t field_decl;
    uint32_t field_decl_height;
    int field_declared;
    /* textures already checked (TEX0 texture + CLUT fields, CLAMP_1): the
     * blocks they read that lie in drawn buffers (pool[first .. first + n)) */
    struct { uint64_t tex0, clamp; uint32_t first, n; } checked[CHECKED_MAX];
    uint32_t checked_count;
    /* checked[]'s index by (TEX0 key, CLAMP_1): open addressing, linear
     * probing; a slot holds an index + 1, 0 is empty. Entries are only added
     * after a search found no equal key, so a key is in checked[] at most
     * once and the hash finds what a scan of checked[] finds. */
    uint16_t checked_slot[CHECKED_SLOTS];
    uint32_t *pool;
    uint32_t pool_used, pool_cap;
    /* EM_GS_TEX_COUNTERS=<file> (docs/PERFORMANCE.md Q7): texture_entry's
     * counters, one line per frame handed to the workers. A diagnostic: it
     * changes nothing recorded or drawn. */
    FILE *tc_file;
    uint64_t tc_frames;
    struct {
        uint64_t searches, probes, hits, misses, cliff, texels, resets;
        double hit_ns, miss_ns;
    } tc;
    /* EM_GS_Z_COUNTERS=<file> (docs/PERFORMANCE.md P12): the workers' pixel
     * test counters (EmGs.pix), summed over the workers, one line per frame
     * waited for. A diagnostic: it changes nothing recorded or drawn. */
    FILE *zc_file;
    uint64_t zc_frames, zc_prev[6];
    /* EM_GS_TC_COUNTERS=<file> (docs/PERFORMANCE.md M3): the workers' decoded
     * texture cache counters (EmGs.tcs), summed over the workers, one line
     * per frame waited for. A diagnostic: it changes nothing drawn. */
    FILE *tcc_file;
    uint64_t tcc_frames, tcc_prev[11];
    /* EM_GS_SPEC_COUNTERS=<file> (docs/PERFORMANCE.md M2): the specialized
     * span loops' counters (EmGs.spc), summed over the workers, one line per
     * frame waited for. A diagnostic: it changes nothing drawn. */
    FILE *spc_file;
    uint64_t spc_frames, spc_prev[7];
    /* the field of the last finished job */
    uint8_t *field;
    uint32_t field_w, field_h;
    uint64_t field_frame, field_xyoffset;
    int field_xyoffset_known;
    int have_field;
    EmGsWorldStats stats;
    char fault[192];
};

/* Local memory written outside the workers' models (a memory image, a
 * restore, an upload), while they wait: each drops the decoded textures over
 * those blocks (em_gs_memory_written). */
static void workers_memory_written(EmGsWorld *w, uint32_t first, uint32_t blocks)
{
    for (uint32_t i = 0; i < w->workers; ++i) em_gs_memory_written(&w->worker[i].gs, first, blocks);
}

static int fail(EmGsWorld *w, const char *what, unsigned long long detail)
{
    if (!w->fault[0]) {
#if defined(__linux__) && defined(__GNUC__) && !defined(__clang__)
        /* Bound the diagnostic prefix; GCC checks the maximum suffix too. */
        snprintf(w->fault, sizeof w->fault, "%.*s (%#llx)", (int)sizeof w->fault - 25, what, detail);
#else
        snprintf(w->fault, sizeof w->fault, "%s (%#llx)", what, detail);
#endif
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

/* The checked textures are forgotten (memory they read may have changed). */
static void checked_reset(EmGsWorld *w)
{
    w->checked_count = 0;
    w->pool_used = 0;
    memset(w->checked_slot, 0, sizeof w->checked_slot);
    w->tc.resets++;
}

/* ------------------------------------------------------------ the workers */

/* The field's rows y0..y1 out of local memory (the frame's last draw is done). */
static void read_rows(EmGsWorld *w, const Job *j, uint32_t y0, uint32_t y1)
{
    const uint32_t fbp = (uint32_t)j->frame & 0x1FFu, fbw = (uint32_t)(j->frame >> 16) & 0x3Fu;
    for (uint32_t y = y0; y <= y1 && y < j->height; ++y) {
        uint8_t *row = w->field + (size_t)y * 64u * fbw * 4u;
        for (uint32_t x = 0; x < 64u * fbw; ++x)
            memcpy(row + 4u * x, w->mem + (size_t)em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u, 4);
    }
}

/* One worker alone (direct): the head, every op, the end of the span, then
 * the field. At a barrier no primitive may be queued: the span must have
 * ended there by itself (a flush here would end it where the GS does not). */
static void run_direct(Worker *k)
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
            if (gs->pend_n) k->failed = 1;
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
    read_rows(w, j, 0, j->height ? j->height - 1u : 0u);
    k->cpu_ns = cpu_now_ns() - c0;
}

/* ---- parse once (docs/PERFORMANCE.md M4) ----
 *
 * Why the bytes cannot change. The parser runs the frame's stream as the
 * direct worker does (the same EmGs calls: registers, vertex queue, setup,
 * span and chain, CLUT loads) and appends each span instead of drawing it;
 * em_gs_list_draw then makes the calls the direct worker's span_end would
 * have made, with the same state, for the primitives that can reach a
 * band's rows, and draws only those rows. Each row of a segment is drawn by
 * one worker, in the list's order, so each pixel of the segment's buffers
 * sees the direct worker's writes in the direct worker's order. Between the
 * segments every band of the earlier one is done before any band of the
 * later one starts (the schedule below), which orders the frame's reads of
 * drawn buffers against their draws as the barriers did before
 * (barrier_op: a read of a block drawn since the last barrier, and a draw
 * into a block read since it, start a segment). Every worker's decoded
 * texture cache sees every span's pages (em_gs_list_draw stamps the spans
 * of a band it skips, em_gs_list_pass the segments it drew none of). The
 * parser reads memory only for CLUT loads. The direct worker reads a CLUT
 * with every span that ended before the load drawn and the span still
 * queued not; so one from a page the list draws first ends the segment
 * there and has every segment so far drawn (own_drain), and reads what the
 * direct worker reads. The bands are a host matter, as the worker count
 * is: any partition draws the same bytes. */

static void lk(EmGsWorld *w)
{
#if GSW_THREADS
    if (w->threaded) pthread_mutex_lock(&w->lock);
#else
    (void)w;
#endif
}

static void ulk(EmGsWorld *w)
{
#if GSW_THREADS
    if (w->threaded) pthread_mutex_unlock(&w->lock);
#else
    (void)w;
#endif
}

/* Wakes the workers waiting on cv (with the lock held, or taking it). */
static void wake(EmGsWorld *w, int locked)
{
#if GSW_THREADS
    if (!w->threaded) return;
    if (!locked) pthread_mutex_lock(&w->lock);
    pthread_cond_broadcast(&w->cv);
    if (!locked) pthread_mutex_unlock(&w->lock);
#else
    (void)w;
    (void)locked;
#endif
}

/* The parser: the spans appended so far are drawable (a parse that runs
 * while no worker draws its frame, early_parse, wakes nobody). */
static void publish(EmGsWorld *w, Parse *p)
{
    const uint32_t n = em_gs_list_spans(p->list);
    if (n == AU_LOAD(p->published)) return;
    AU_STORE(p->published, n);
    if (!p->quiet && AU_LOAD(w->waiters) && n >= p->last_wake + LIST_CHUNK) {
        p->last_wake = n;
        wake(w, 0);
    }
}

/* The parser: segment `k` ends here (a barrier, or the frame's end). */
static void close_seg(EmGsWorld *w, Parse *p, uint32_t k)
{
    const uint32_t n = em_gs_list_spans(p->list);
    Seg *g = &p->seg[k];
    g->end = n;
    em_gs_list_rows(p->list, g->first, n, &g->ylo, &g->yhi);
    if (k + 1u < SEG_MAX) p->seg[k + 1u].first = n;
    AU_STORE(p->published, n);
    AU_STORE(p->segs_closed, k + 1u);
    if (!p->quiet) wake(w, 0);
}

/* A frame's parse, empty (em_gs_world_begin: the job is recorded again). */
static void parse_reset(Parse *p)
{
    em_gs_list_clear(p->list);
    p->seg[0].first = 0;
    AU_STORE(p->published, 0u);
    AU_STORE(p->segs_closed, 0u);
    AU_STORE(p->done, 0u);
    p->last_wake = 0;
    p->failed = 0;
    p->began = 0;
    p->started = 0;
    p->op_next = 0;
    p->k = 0;
    p->ns = 0;
    p->early_ops = 0;
    p->prior_waits = 0;
    p->own_waits = 0;
    AU_STORE(p->predrawn, 0u);
    p->early_pixels = 0;
}

/* The parse: the head and the ops from p->op_next through the parser, the
 * spans appended to the job's list; a barrier closes a segment. A worker's
 * parse (early 0) runs to the frame's end; the game's thread's (early 1,
 * early_parse) stops at an op boundary once the running frame is drawn
 * (EmGsWorld.live 0), or (EM_GS_EARLY_OPS) after early_max ops, and a
 * worker goes on from there. */
static void parse_ops(EmGsWorld *w, Job *j, int early)
{
    const size_t stop = early && w->early_max ? j->p.op_next + w->early_max : (size_t)-1;
    const double c0 = cpu_now_ns();
    Parse *p = &j->p;
    EmGs *gs = &w->parser;
    int failed = p->failed;
    gs->list = p->list;
    w->parsing = j;
    if (!p->began) {
        p->began = 1;
        p->prims0 = gs->drawn_prims;
        if (j->env_bytes && em_gs_gif(gs, j->env, j->env_bytes) != j->env_bytes) failed = 1;
        if (j->clear_bytes && em_gs_gif(gs, j->clear, j->clear_bytes) != j->clear_bytes) failed = 1;
        publish(w, p);
    }
    size_t i = p->op_next;
    for (; i < j->op_count; ++i) {
        if (early && (i >= stop || (!w->early_max && !(i & 63u) && !AU_LOAD(w->live)))) break;
        const Op *o = &j->ops[i];
        if (o->op == OP_BARRIER) {
            if (gs->pend_n) failed = 1;
            if (p->k + 2u < SEG_MAX) close_seg(w, p, p->k++);
            else failed = 1;
            continue;
        }
        if (failed) continue;
        /* (a CLUT load from the frame's own spans closes a segment here:
         * own_drain) */
        if (o->op == OP_WRITE)
            em_gs_write(gs, o->reg, o->value);
        else if (o->op == OP_GIF) {
            if (em_gs_gif(gs, j->gif + o->value, o->reg) != o->reg) failed = 1;
        } else if (em_gs_gif(gs, j->env, j->env_bytes) != j->env_bytes)
            failed = 1;
        publish(w, p);
    }
    p->op_next = i;
    p->failed = failed;
    p->ns += cpu_now_ns() - c0;
    if (i < j->op_count) {
        gs->list = NULL;               /* (early: a worker goes on) */
        return;
    }
    em_gs_flush(gs);
    gs->list = NULL;
    p->failed = failed || em_gs_list_failed(p->list);
    p->prims1 = gs->drawn_prims;
    p->refusals = gs->refusals;
    p->refused = gs->refused_prims;
    p->span_faults = gs->span_faults;
    memcpy(p->reason, gs->reason, sizeof p->reason);
    close_seg(w, p, p->k);
    AU_STORE(p->done, 1u);             /* (the parser is free from here) */
    if (!p->quiet) wake(w, 0);
}

/* The running frame is drawn (early_parse's wait before a CLUT load that
 * reads memory the frame draws). */
static void prior_wait(void *ctx)
{
#if GSW_THREADS
    EmGsWorld *w = ctx;
    if (!w->threaded) return;
    pthread_mutex_lock(&w->lock);
    w->job[w->rec].p.prior_waits++;    /* (the early parse's job is the recorded one) */
    while (w->running) pthread_cond_wait(&w->done, &w->lock);
    pthread_mutex_unlock(&w->lock);
#else
    (void)ctx;
#endif
}

/* A CLUT load the parse meets reads a page a span of the frame's own list
 * writes (em_gs_list_drain). The direct worker reads it with every span
 * that ended before the load drawn and the span still queued not: so the
 * segment ends here (the spans so far; the queued span goes into the next
 * one), and the load waits until every segment so far is drawn. A worker's
 * parse waits for the other workers to draw them. Where no other thread
 * draws this frame (one worker, or the early parse on the game's thread,
 * once the frame before is drawn) the parsing thread draws them itself,
 * every row, through worker 0's EmGs (its decoded texture cache sees their
 * pages; the other workers pass them, run_tasks), and the schedule skips
 * them (Parse.predrawn). 1, or 0 (no segment left: the load is refused and
 * the frame faults, as at a barrier). */
static int own_drain(void *ctx)
{
    EmGsWorld *w = ctx;
    Job *j = w->parsing;
    Parse *p = &j->p;
    if (p->k + 2u >= SEG_MAX) return 0;
    const uint32_t k = p->k++;
    close_seg(w, p, k);
    p->own_waits++;
#if GSW_THREADS
    if (w->threaded && !p->quiet) {
        pthread_mutex_lock(&w->lock);
        AU_ADD(w->waiters, 1u);
        while (w->seg_cur < (int32_t)k || (w->seg_cur == (int32_t)k && w->band_done < w->band_n))
            pthread_cond_wait(&w->cv, &w->lock);
        AU_SUB(w->waiters, 1u);
        pthread_mutex_unlock(&w->lock);
        return 1;
    }
    if (p->quiet && w->threaded && w->busy && w->run != j) {
        prior_wait(w);                 /* (the frame before draws first) */
        em_gs_list_prior(p->list, NULL, NULL, NULL);
    }
#endif
    if (w->no_raster) {
        AU_STORE(p->predrawn, k + 1u);  /* (the raster skip draws no span) */
        return 1;
    }
    Worker *d = &w->worker[0];
    const uint32_t from = AU_LOAD(p->predrawn);
    const uint64_t px0 = d->gs.drawn_pixels;
    d->gs.band_rows = 0;
    (void)em_gs_list_draw(&d->gs, p->list, p->seg[from].first, p->seg[k].end, NULL, NULL);
    if (p->quiet)
        p->early_pixels += d->gs.drawn_pixels - px0;   /* (this frame's: em_gs_world_wait, start) */
    else
        d->seg_seen = (int32_t)k;      /* (worker 0 parses: run_tasks) */
    AU_STORE(p->predrawn, k + 1u);
    return 1;
}

/* Early parse (docs/PERFORMANCE.md M4): the game's thread, about to wait
 * for the running frame (a kick waits for it), parses the next frame
 * meanwhile, until the running frame is drawn; its workers then go on from
 * there. The parser is the running frame's until that frame's parse is
 * done, so the early parse waits for that. The parse reads memory only for
 * CLUT loads: one that reads a page the running frame draws waits for the
 * frame first (em_gs_list_prior), so it reads what it would read after the
 * frame; one from a page this frame draws has its spans so far drawn first
 * (own_drain). EM_GS_EARLY_PARSE=0 turns it off (a developer switch; the
 * same bytes either way). */
static void early_parse(EmGsWorld *w, Job *j)
{
    if (!w->parse_once || !w->early || j->p.began) return;
    const int running = w->busy && w->run && w->run != j;
    if (running && !AU_LOAD(w->run->p.done)) return;        /* the parser is the running frame's */
    if (!w->early_max && (!w->threaded || !running || !AU_LOAD(w->live))) return;
    em_gs_list_prior(j->p.list, running && w->threaded ? w->run->p.list : NULL, prior_wait, w);
    j->p.quiet = 1;
    parse_ops(w, j, 1);
    j->p.quiet = 0;
    j->p.early_ops = j->p.op_next;
    em_gs_list_prior(j->p.list, NULL, NULL, NULL);
}

/* Segment `k`'s bands (under the lock): workers x bands_per_worker bands of
 * equal rows (at least BAND_MIN_ROWS) over the rows it draws, the first open
 * to the top and the last to the bottom. Segment 0 may still be parsed: its
 * bands cover the field's rows (its rows past them are the last band's). A
 * segment that draws no rows, or that own_drain drew, has no band. */
static void seg_start(EmGsWorld *w, uint32_t k)
{
    int32_t lo, hi;
    if (k < AU_LOAD(w->run->p.predrawn)) {
        lo = 0;                        /* drawn already (own_drain) */
        hi = -1;
    } else if (AU_LOAD(w->run->p.segs_closed) > k) {
        lo = w->run->p.seg[k].ylo;
        hi = w->run->p.seg[k].yhi;
    } else {
        lo = 0;
        hi = (int32_t)w->run->height - 1;
    }
    w->seg_cur = (int32_t)k;
    w->band_next = w->band_done = 0;
    w->band_planned = w->band_n = 0;
    w->band_lo = lo;
    w->band_hi = hi;
    if (lo > hi) {
        wake(w, 1);                    /* (a parse waiting for this segment: own_drain) */
        return;
    }
    const int64_t rows = (int64_t)hi - lo + 1, want = (int64_t)w->workers * w->bands_per_worker;
    int64_t h = (rows + want - 1) / want;
    if (h < BAND_MIN_ROWS) h = BAND_MIN_ROWS;
    w->band_base = lo;
    w->band_h = (int32_t)h;
    w->band_planned = w->band_n = (uint32_t)((rows + h - 1) / h);
    w->bands_total += w->band_n;
}

/* The rows of a band that the segment can draw: the band's, inside the
 * segment's (band_lo..band_hi). */
static int32_t band_rows(const EmGsWorld *w, int32_t y0, int32_t y1, int32_t *e0, int32_t *e1)
{
    *e0 = y0 > w->band_lo ? y0 : w->band_lo;
    *e1 = y1 < w->band_hi ? y1 : w->band_hi;
    return *e1 >= *e0 ? *e1 - *e0 + 1 : 0;
}

/* A split (under the lock): an idle worker asks the band with the most work
 * left (its rows that the segment draws, times the spans it has not drawn)
 * for the lower half of its rows, and waits for the answer: the band's
 * owner gives them at its next span (split_answer), from that span on, as
 * the rows' earlier spans are drawn. 1 with the task, 0 when no band could
 * give rows (or the band finished first). */
#if GSW_THREADS
/* (-1: it waited and got none; the state may have changed) */
static int steal(EmGsWorld *w, uint32_t me, Task *t)
{
    const uint32_t k = (uint32_t)w->seg_cur;
    const uint32_t end = AU_LOAD(w->run->p.segs_closed) > k ? w->run->p.seg[k].end : AU_LOAD(w->run->p.published);
    Worker *best = NULL;
    uint64_t score = 0;
    for (uint32_t i = 0; i < w->workers; ++i) {
        Worker *v = &w->worker[i];
        int32_t e0, e1;
        if (i == me || !v->active || v->aseg != k || AU_LOAD(v->split)) continue;
        const int32_t rows = band_rows(w, v->ay0, v->ay1, &e0, &e1);
        if (rows < 2 * BAND_MIN_ROWS) continue;
        const uint32_t pos = AU_LOAD(v->pos), left = end > pos ? end - pos : 1u;
        const uint64_t sc = (uint64_t)rows * left;
        if (sc > score) {
            score = sc;
            best = v;
        }
    }
    if (!best) return 0;
    Worker *me_k = &w->worker[me];
    me_k->hand = 0;
    best->thief = me;
    AU_STORE(best->split, 1u);
    AU_ADD(w->waiters, 1u);
    const double t0 = w->lc_file ? now_ns() : 0;
    /* the band answers at its next span, or when it finishes (finish) */
    while (me_k->hand == 0)
        pthread_cond_wait(&w->cv, &w->lock);
    if (w->lc_file) w->wait_claim_ns += now_ns() - t0;
    AU_SUB(w->waiters, 1u);
    if (me_k->hand != 1) return -1;
    t->kind = T_BAND;
    t->seg = k;
    t->y0 = me_k->hy0;
    t->y1 = me_k->hy1;
    t->start = me_k->hspan;
    return 1;
}
#endif

/* The owner's side of a split, before span s (em_gs_list_draw's `before`):
 * the lower half of its rows that the segment draws, from span s on, to the
 * asker; it keeps the upper half. */
static int split_answer(void *ctx, uint32_t s)
{
    Worker *k = ctx;
    AU_STORE(k->pos, s);
    if (!AU_LOAD(k->split)) return 1;
    EmGsWorld *w = k->w;
    lk(w);
    if (AU_LOAD(k->split)) {
        Worker *t = &w->worker[k->thief];
        int32_t e0, e1;
        const int32_t rows = band_rows(w, k->ay0, k->ay1, &e0, &e1);
        if (rows >= 2 * BAND_MIN_ROWS) {
            const int32_t m = e0 + rows / 2 - 1;
            t->hy0 = m + 1;
            t->hy1 = k->ay1;
            t->hspan = s;
            k->ay1 = m;
            k->gs.band_y1 = m;
            w->band_n++;
            w->splits++;
            t->hand = 1;
        } else {
            t->hand = -1;
        }
        AU_STORE(k->split, 0u);
        wake(w, 1);
    }
    ulk(w);
    return 1;
}

/* The next task for a worker: the parse (first), a band of the segment
 * being drawn, rows split off a band still being drawn, the next segment's
 * first band once every band of this one is done and the parse closed the
 * next, then the field's readout once every segment is drawn. 0: the frame
 * is done. */
static int claim(EmGsWorld *w, uint32_t me, Task *t)
{
    lk(w);
    for (;;) {
        if (!w->parse_claimed) {
            w->parse_claimed = 1;
            t->kind = T_PARSE;
            break;
        }
        if (!w->read_phase && w->seg_cur >= 0 && w->band_next < w->band_planned) {
            const uint32_t b = w->band_next++;
            t->kind = T_BAND;
            t->seg = (uint32_t)w->seg_cur;
            t->y0 = b == 0 ? INT32_MIN : w->band_base + (int32_t)b * w->band_h;
            t->y1 = b + 1u == w->band_planned ? INT32_MAX : w->band_base + (int32_t)(b + 1u) * w->band_h - 1;
            t->start = w->run->p.seg[t->seg].first;
            break;
        }
        if (w->read_phase) {
            if (w->read_next < w->read_n) {
                const uint32_t r = w->read_next++;
                t->kind = T_READ;
                t->y0 = (int32_t)(r * READ_ROWS);
                t->y1 = (int32_t)(r * READ_ROWS + READ_ROWS - 1u);
                break;
            }
            if (w->read_done == w->read_n) {
                t->kind = T_NONE;
                break;
            }
        } else if (w->seg_cur < 0 || w->band_done == w->band_n) {
            const uint32_t next = (uint32_t)(w->seg_cur + 1), closed = AU_LOAD(w->run->p.segs_closed);
            if (next == 0 || next < closed) {
                seg_start(w, next);
                continue;
            }
            if (AU_LOAD(w->run->p.done)) {
                w->read_phase = 1;
                w->read_n = (w->run->height + READ_ROWS - 1u) / READ_ROWS;
                w->read_next = w->read_done = 0;
                continue;
            }
        }
#if GSW_THREADS
        if (w->threaded) {
            if (!w->read_phase && w->seg_cur >= 0 && w->band_done < w->band_n) {
                const int got = steal(w, me, t);
                if (got > 0) break;
                if (got < 0) continue;         /* it waited: look again */
            }
            w->waits_claim++;
            AU_ADD(w->waiters, 1u);
            const double t0 = w->lc_file ? now_ns() : 0;
            pthread_cond_wait(&w->cv, &w->lock);
            if (w->lc_file) {
                if (w->read_phase) w->wait_read_ns += now_ns() - t0;
                else w->wait_claim_ns += now_ns() - t0;
            }
            AU_SUB(w->waiters, 1u);
            continue;
        }
#endif
        /* (one thread: the parse ran first, so nothing is ever waited for) */
        (void)me;
        w->run->p.failed = 1;
        t->kind = T_NONE;
        break;
    }
    if (t->kind == T_BAND) {
        Worker *k = &w->worker[me];
        k->active = 1;
        k->aseg = t->seg;
        k->ay0 = t->y0;
        k->ay1 = t->y1;
        AU_STORE(k->pos, t->start);
    }
    ulk(w);
    return t->kind != T_NONE;
}

/* A task is done: the segment's (or the readout's) last wakes the waiters,
 * and so does a band asked for a split it did not answer. */
static void finish(EmGsWorld *w, uint32_t me, const Task *t)
{
    if (t->kind != T_BAND && t->kind != T_READ) return;
    lk(w);
    if (t->kind == T_BAND) {
        Worker *k = &w->worker[me];
        k->active = 0;
        const int asked = AU_LOAD(k->split) != 0;
        if (asked) {
            w->worker[k->thief].hand = -1;     /* nothing left to split */
            AU_STORE(k->split, 0u);
        }
        if (++w->band_done == w->band_n || asked) wake(w, 1);
    } else if (++w->read_done == w->read_n) {
        wake(w, 1);
    }
    ulk(w);
}

/* The spans of segment k a band may draw from span s on: [s, *end). 1 when
 * *end is the segment's end; waits while the parse has not gone LIST_CHUNK
 * spans past s (or closed the segment). */
static int avail(EmGsWorld *w, uint32_t k, uint32_t s, uint32_t *end)
{
    for (;;) {
        /* published first: when the segment is not closed after it, those
         * spans are all the segment's (the parse closes it before it
         * publishes the next segment's spans) */
        const uint32_t pub = AU_LOAD(w->run->p.published);
        if (AU_LOAD(w->run->p.segs_closed) > k) {
            *end = w->run->p.seg[k].end;
            return 1;
        }
        if (pub >= s + LIST_CHUNK) {
            *end = pub;
            return 0;
        }
#if GSW_THREADS
        if (w->threaded) {
            pthread_mutex_lock(&w->lock);
            w->waits_avail++;
            AU_ADD(w->waiters, 1u);
            const double t0 = w->lc_file ? now_ns() : 0;
            while (AU_LOAD(w->run->p.segs_closed) <= k && AU_LOAD(w->run->p.published) < s + LIST_CHUNK)
                pthread_cond_wait(&w->cv, &w->lock);
            if (w->lc_file) w->wait_avail_ns += now_ns() - t0;
            AU_SUB(w->waiters, 1u);
            pthread_mutex_unlock(&w->lock);
            continue;
        }
#endif
        *end = pub;                    /* (one thread: the parse is done) */
        return 1;
    }
}

/* A band task: the segments before its own that this worker's cache did not
 * see, the segment's spans before the task's first (rows split off a band:
 * the band drew them), then the segment's spans in its rows, answering
 * splits. */
static void draw_band(Worker *k, const Task *t)
{
    EmGsWorld *w = k->w;
    EmGs *gs = &k->gs;
    for (int32_t j = k->seg_seen + 1; j < (int32_t)t->seg; ++j)
        em_gs_list_pass(gs, w->run->p.list, w->run->p.seg[j].first, w->run->p.seg[j].end);
    if (t->start > w->run->p.seg[t->seg].first && k->seg_seen < (int32_t)t->seg)
        em_gs_list_pass(gs, w->run->p.list, w->run->p.seg[t->seg].first, t->start);
    gs->band_rows = 1;
    gs->band_y0 = t->y0;
    gs->band_y1 = t->y1;
    uint32_t s = t->start;
    for (;;) {
        uint32_t end;
        const int closed = avail(w, t->seg, s, &end);
        if (end > s) s = em_gs_list_draw(gs, w->run->p.list, s, end, w->threaded ? split_answer : NULL, k);
        if (closed && s >= end) break;
    }
    k->seg_seen = (int32_t)t->seg;
}

/* One worker's run of a parsed frame: tasks until the frame is done, then
 * the pages of the segments it drew none of (for the next frame's cache). */
static void run_tasks(Worker *k)
{
    EmGsWorld *w = k->w;
    const double c0 = cpu_now_ns();
    k->failed = 0;
    /* (worker 0's EmGs drew the segments an early parse drew: own_drain) */
    k->seg_seen = k->index == 0 ? (int32_t)AU_LOAD(w->run->p.predrawn) - 1 : -1;
    Task t;
    while (claim(w, k->index, &t)) {
        if (t.kind == T_PARSE)
            parse_ops(w, w->run, 0);
        else if (t.kind == T_BAND)
            draw_band(k, &t);
        else
            read_rows(w, w->run, (uint32_t)t.y0, (uint32_t)t.y1);
        finish(w, k->index, &t);
    }
    const uint32_t closed = AU_LOAD(w->run->p.segs_closed);
    for (uint32_t j = (uint32_t)(k->seg_seen + 1); j < closed; ++j)
        em_gs_list_pass(&k->gs, w->run->p.list, w->run->p.seg[j].first, w->run->p.seg[j].end);
    k->cpu_ns = cpu_now_ns() - c0;
}

/* The raster skip's one worker: the frame's parse, quiet (no other worker
 * waits for its spans), and no band; em_gs_world_wait reads the parse's
 * outcome as ever. */
static void run_parse_only(Worker *k)
{
    EmGsWorld *w = k->w;
    const double c0 = cpu_now_ns();
    k->failed = 0;
    if (!AU_LOAD(w->run->p.done)) {
        w->run->p.quiet = 1;
        parse_ops(w, w->run, 0);
        w->run->p.quiet = 0;
    }
    k->cpu_ns = cpu_now_ns() - c0;
}

static void run_frame(Worker *k)
{
    if (k->w->no_raster)
        run_parse_only(k);
    else if (k->w->parse_once)
        run_tasks(k);
    else
        run_direct(k);
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
        AU_SUB(w->live, 1u);
        if (--w->running == 0) pthread_cond_signal(&w->done);
        pthread_mutex_unlock(&w->lock);
    }
}
#endif

/* The worker count: EM_GS_THREADS (1..16), else a host default that leaves
 * two cores of headroom (the game's thread, and the audio, movie and window
 * threads), at most 8. A host property: every count draws the same bytes. 1
 * runs the frame on the caller's thread at the kick (no worker threads).
 *
 * Apple hosts report their core types (hw.perflevel0 = performance cores,
 * hw.perflevel1 = efficiency cores). A frame waits for its slowest worker
 * (since M4 only for the band it draws last, split as it goes): where the
 * performance cores less the headroom still give 4 workers (M1 Pro: 8 - 2 =
 * 6), the workers stay off the efficiency cores; with fewer performance
 * cores (iPhone Air: 2 + 4) every core less the headroom draws (4). Other
 * hosts give no core types: the online processors less three, as before
 * (docs/PERFORMANCE.md Q5). */
#define WORKER_HEADROOM 2

#if defined(__APPLE__)
static long sysctl_cores(const char *name)
{
    int v = 0;
    size_t n = sizeof v;
    return sysctlbyname(name, &v, &n, NULL, 0) == 0 && n == sizeof v && v > 0 ? v : 0;
}
#endif

static uint32_t worker_count(void)
{
#if GSW_THREADS
    const char *e = getenv("EM_GS_THREADS");
    if (e && *e) {
        const long n = strtol(e, NULL, 10);
        return n < 1 ? 1u : n > (long)WORKERS_MAX ? WORKERS_MAX : (uint32_t)n;
    }
    long n = 0;
#if defined(__APPLE__)
    const long perf = sysctl_cores("hw.perflevel0.logicalcpu");
    const long eff = sysctl_cores("hw.nperflevels") >= 2 ? sysctl_cores("hw.perflevel1.logicalcpu") : 0;
    if (perf > 0)
        n = perf - WORKER_HEADROOM >= 4 ? perf - WORKER_HEADROOM : perf + eff - WORKER_HEADROOM;
#endif
    if (n == 0) {
        const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        n = cpus > 3 ? cpus - 3 : 1;
    }
    return n < 1 ? 1u : n > 8 ? 8u : (uint32_t)n;
#else
    return 1u;
#endif
}

EmGsWorld *em_gs_world_create(void) { return em_gs_world_create_raster(1); }

EmGsWorld *em_gs_world_create_raster(int raster)
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
    w->no_raster = !raster;
    /* (the raster skip: one worker thread, which parses the frame while the
     * game builds the next one, as the drawing workers do) */
    w->workers = w->no_raster ? 1u : worker_count();
    const char *tc = getenv("EM_GS_TEX_COUNTERS");
    if (tc && tc[0]) {
        w->tc_file = fopen(tc, "w");
        if (w->tc_file)
            fprintf(w->tc_file, "# frame searches probes hits misses cliff texels resets entries pool hit_ns miss_ns\n");
    }
    const char *tcc = getenv("EM_GS_TC_COUNTERS");
    if (tcc && tcc[0]) {
        w->tcc_file = fopen(tcc, "w");
        if (w->tcc_file)
            fprintf(w->tcc_file, "# frame binds uncached uncached_written created renewed evicted resets rows texels "
                                 "samples samples_bil bytes_max\n");
    }
    /* EM_GS_TEXCACHE=0: the decoded texture cache off (every texel read from
     * memory; a developer switch for comparisons) */
    const char *tco = getenv("EM_GS_TEXCACHE");
    const int tc_off = tco && tco[0] == '0';
    /* EM_GS_SPEC=0: the specialized span loops off (every primitive through
     * the generic path); EM_GS_SPEC_CHECK=1: every primitive a loop takes
     * drawn both ways and compared, a difference aborts (developer
     * switches for comparisons; the same bytes either way) */
    const char *spo = getenv("EM_GS_SPEC");
    const int spec_off = spo && spo[0] == '0';
    const char *spk = getenv("EM_GS_SPEC_CHECK");
    const int spec_check = spk && spk[0] == '1';
    const char *spf = getenv("EM_GS_SPEC_COUNTERS");
    if (spf && spf[0]) {
        w->spc_file = fopen(spf, "w");
        if (w->spc_file)
            fprintf(w->spc_file, "# frame sprite_prims tex_tri_prims untex_tri_prims sprite_pixels tex_tri_pixels "
                                 "untex_tri_pixels checked drawn\n");
    }
    const char *lcf = getenv("EM_GS_LIST_COUNTERS");
    if (lcf && lcf[0]) {
        w->lc_file = fopen(lcf, "w");
        if (w->lc_file)
            fprintf(w->lc_file, "# frame workers spans segments bands splits prims spans_walked spans_drawn prims_drawn "
                                "parse_ns waits_claim waits_progress wall_ns busiest_ns cpu_sum_ns wait_claim_ns wait_progress_ns "
                                "wait_read_ns own_clut_waits prior_waits early_ops\n");
    }
    const char *zc = getenv("EM_GS_Z_COUNTERS");
    if (zc && zc[0]) {
        w->zc_file = fopen(zc, "w");
        if (w->zc_file)
            fprintf(w->zc_file, "# frame samples samples_bil zfail zfail_tex zfail_bil datefail_tex drawn\n");
    }
    /* Parse once (docs/PERFORMANCE.md M4): with more than one worker, or
     * EM_GS_LIST=1 (one worker draws the parsed list too: a developer switch
     * for comparisons), the frame is parsed once and drawn in bands of rows;
     * EM_GS_BANDS=<n> (1..64) sets the bands per worker in each segment
     * (the same bytes for every value). Without memory for the list, one
     * worker runs the frame directly. */
    const char *lst = getenv("EM_GS_LIST");
    const char *bnd = getenv("EM_GS_BANDS");
    w->bands_per_worker = BANDS_PER_WORKER;
    if (bnd && *bnd) {
        const long n = strtol(bnd, NULL, 10);
        w->bands_per_worker = n < 1 ? 1u : n > 64 ? 64u : (uint32_t)n;
    }
    /* EM_GS_EARLY_PARSE=0: no early parse (early_parse; a developer switch,
     * the same bytes either way); EM_GS_EARLY_OPS=<n> (a test switch): the
     * early parse runs at every kick, also with no frame being drawn, and
     * parses at most n ops before the workers go on */
    const char *ear = getenv("EM_GS_EARLY_PARSE");
    w->early = !(ear && ear[0] == '0') && !w->no_raster;
    const char *eops = getenv("EM_GS_EARLY_OPS");
    if (eops && *eops) {
        const long n = strtol(eops, NULL, 10);
        w->early_max = n > 0 ? (size_t)n : 0;
    }
    if (w->workers > 1 || (lst && lst[0] == '1') || w->no_raster) {
        int ok = 1;
        for (int i = 0; i < 2; ++i) {
            w->job[i].p.list = em_gs_list_create();
            w->job[i].p.seg = calloc(SEG_MAX, sizeof *w->job[i].p.seg);
            ok = ok && w->job[i].p.list && w->job[i].p.seg;
        }
        if (ok) {
            w->parse_once = 1;
            em_gs_init(&w->parser, w->mem);
            w->parser.strict = 1;
            w->parser.tc_off = 1;      /* it draws nothing */
            for (int i = 0; i < 2; ++i) {
                parse_reset(&w->job[i].p);
                em_gs_list_drain(w->job[i].p.list, own_drain, w);
            }
        } else {
            for (int i = 0; i < 2; ++i) {
                em_gs_list_destroy(w->job[i].p.list);
                free(w->job[i].p.seg);
                w->job[i].p.list = NULL;
                w->job[i].p.seg = NULL;
            }
            w->workers = 1;
            if (w->no_raster) {        /* (no parser: the skip cannot run) */
                em_gs_world_destroy(w);
                return NULL;
            }
        }
    }
#if GSW_THREADS
    if (w->workers > 1 || w->no_raster) {
        pthread_mutex_init(&w->lock, NULL);
        pthread_cond_init(&w->go, NULL);
        pthread_cond_init(&w->done, NULL);
        pthread_cond_init(&w->cv, NULL);
        w->threaded = 1;
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) {
        Worker *k = &w->worker[i];
        k->w = w;
        k->index = i;
        em_gs_init(&k->gs, w->mem);
        k->gs.strict = 1;
        k->gs.tc_off = tc_off;
        k->gs.spec_off = spec_off;
        k->gs.spec_check = spec_check;
    }
#if GSW_THREADS
    if (w->threaded) {
        /* The workers are the frame's critical path, as the game's thread
         * is: the same QoS (USER_INTERACTIVE, the macOS main thread's and
         * the iOS game thread's), so the scheduler prefers performance
         * cores for them. Scheduling only: no effect on the bytes. */
        pthread_attr_t attr;
        const int attr_ok = pthread_attr_init(&attr) == 0;
#if defined(__APPLE__)
        if (attr_ok) (void)pthread_attr_set_qos_class_np(&attr, QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        uint32_t started = 0;
        for (uint32_t i = 0; i < w->workers; ++i, ++started)
            if (pthread_create(&w->worker[i].thread, attr_ok ? &attr : NULL, worker_main, &w->worker[i]) != 0) break;
        if (attr_ok) pthread_attr_destroy(&attr);
        if (started < w->workers) {
            /* fewer threads: the bands are dealt over those that run (none:
             * the caller's thread runs the parsed frame) */
            if (started == 0) {
                w->threaded = 0;
                started = 1;
            }
            w->workers = started;
        }
    }
#endif
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
        pthread_cond_destroy(&w->cv);
    }
#endif
    for (uint32_t i = 0; i < w->workers; ++i) em_gs_release(&w->worker[i].gs);
    /* (a span the parser queued in a list's storage, an early parse having
     * stopped inside it before a fault, goes with that list: not flushed) */
    if (w->parse_once) w->parser.pend_n = 0;
    if (w->parse_once) em_gs_release(&w->parser);
    for (int i = 0; i < 2; ++i) {
        em_gs_list_destroy(w->job[i].p.list);
        free(w->job[i].p.seg);
    }
    for (int i = 0; i < 2; ++i) {
        free(w->job[i].ops);
        free(w->job[i].gif);
    }
    if (w->tc_file) fclose(w->tc_file);
    if (w->zc_file) fclose(w->zc_file);
    if (w->tcc_file) fclose(w->tcc_file);
    if (w->spc_file) fclose(w->spc_file);
    if (w->lc_file) fclose(w->lc_file);
    free(w->pool);
    free(w->mem);
    free(w->field);
    free(w);
}

uint32_t em_gs_world_workers(const EmGsWorld *w) { return w ? w->workers : 0; }
int em_gs_world_raster(const EmGsWorld *w) { return w && !w->no_raster; }

int em_gs_world_raster_wanted(int headless)
{
    const char *forced = getenv("EM_HEADLESS_RASTER");
    if (forced && forced[0]) return forced[0] != '0';
    if (!headless) return 1;
    static const char *const picture[] = {"CAPTURE", "PRESENT", "_PX", "DUMP", "DIGEST", "_FB", "GS_", "PIXEL"};
    for (char **e = environ; e && *e; ++e) {
        const char *v = *e;
        if (strncmp(v, "EM_", 3) != 0) continue;
        const char *eq = strchr(v, '=');
        const size_t n = eq ? (size_t)(eq - v) : strlen(v);
        char name[128];
        if (n >= sizeof name) return 1;
        memcpy(name, v, n);
        name[n] = 0;
        for (size_t i = 0; i < sizeof picture / sizeof picture[0]; ++i)
            if (strstr(name, picture[i])) return 1;
    }
    return 0;
}

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
        workers_memory_written(w, first, count);
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

int em_gs_world_memory_restore(EmGsWorld *w, const uint8_t *mem, const uint8_t *resident)
{
    if (!w || !mem || !resident || w->fault[0]) return -1;
    if (w->recording) return fail(w, "memory restored inside a recorded world frame", 0);
    if (em_gs_world_wait(w) < 0) return -1;
    memcpy(w->mem, mem, EM_GS_MEM_BYTES);
    workers_memory_written(w, 0, BLOCKS);
    for (uint32_t b = 0; b < BLOCKS; ++b) {
        w->resident[b] = resident[b] ? 1 : 0;
        w->drawn[b] = resident[b] ? 0 : 1;
    }
    /* textures checked against the earlier memory are checked again */
    checked_reset(w);
    w->memory_loaded = 1;
    return 0;
}

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
    workers_memory_written(w, 0, BLOCKS);    /* (before a refusal too: memory may have changed) */
    if (used != gn || up.refusals) return fail(w, "the model refused an upload", up.refusals);
    /* textures read earlier may have changed: check them again */
    checked_reset(w);
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
    w->tread_any = 0;
    w->marked_count = 0;
    memset(w->tex0_known, 0, sizeof w->tex0_known);
    memset(w->clamp_known, 0, sizeof w->clamp_known);
    w->prim_known = 0;
    w->field_declared = 0;
    w->tgt_frame_known = w->tgt_scissor_known = 0;
    if (w->parse_once) {
        /* a frame parsed early (em_gs_world_preparse) and then dropped: the
         * parser took its writes, the GS never does */
        if (j->p.began && !j->p.started) fail(w, "a world frame parsed early was dropped before its kick", 0);
        parse_reset(&j->p);            /* (job[rec] is not the running one) */
    }
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
    case EM_GS_FRAME_1:
        w->tgt_frame = value;
        w->tgt_frame_known = 1;
        break;
    case EM_GS_SCISSOR_1:
        w->tgt_scissor = value;
        w->tgt_scissor_known = 1;
        break;
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

/* The blocks of a w x height Z buffer (PSMZ32 / Z24 layout) at zbp. */
static void mark_z(uint8_t *map, uint32_t zbp, uint32_t zbw, uint32_t height)
{
    for (uint32_t y = 0; y < height; y += 8u)
        for (uint32_t x = 0; x < 64u * zbw; x += 8u) {
            const uint32_t b = em_gs_addr32(zbp * 32u, zbw, x, y, 1) * 4u / 256u;
            if (b < BLOCKS) map[b] = 1;
        }
}

static void barrier_op(EmGsWorld *w)
{
    push(w, OP_BARRIER, 0, 0);
    memset(w->fdrawn, 0, sizeof w->fdrawn);
    memset(w->fread, 0, sizeof w->fread);
    w->fdrawn_any = w->fread_any = 0;
    w->tread_any = 0;
    w->marked_count = 0;
}

/* The buffer's blocks are drawn from now on (w->drawn). A texture checked
 * earlier lists only the drawn blocks it reads (texture_entry): when an
 * uploaded block is drawn over for the first time, the checks are made
 * again, so the workers' ordering covers it too. (No upload writes a buffer
 * the first level draws, GS_EXACT.md section 9; this keeps the ordering
 * right if one did.) 1 when an uploaded block was drawn over for the first
 * time. */
static int mark_drawn(EmGsWorld *w, uint32_t fbp, uint32_t fbw, uint32_t height)
{
    int over_upload = 0;
    for (uint32_t y = 0; y < height; y += 8u)
        for (uint32_t x = 0; x < 64u * fbw; x += 8u) {
            const uint32_t b = em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u / 256u;
            if (b >= BLOCKS || w->drawn[b]) continue;
            w->drawn[b] = 1;
            over_upload |= w->resident[b];
        }
    if (over_upload) {
        checked_reset(w);
    }
    return over_upload;
}

/* A buffer drawn from here on (em_gs_world_drawn_buffer), or (head 1) the
 * declared field: the head's drawing target, drawn from the frame's start,
 * so no read of the frame comes before its first draw. */
static void drawn_buffer(EmGsWorld *w, uint64_t frame, uint32_t height, int head)
{
    const uint32_t fbp = (uint32_t)frame & 0x1FFu, fbw = (uint32_t)(frame >> 16) & 0x3Fu;
    if (w->recording)
        for (uint32_t i = 0; i < w->marked_count; ++i)
            if (w->marked[i].fbp == fbp && w->marked[i].fbw == fbw && w->marked[i].height >= height) return;
    const int over_upload = mark_drawn(w, fbp, fbw, height);
    if (!w->recording) return;
    if (over_upload && w->tread_any && !head) {
        /* uploaded blocks drawn over for the first time, after a texture
         * read since the last barrier: the read may have taken them (fread
         * lists drawn blocks only), so every band reads first */
        barrier_op(w);
    } else if (w->fread_any) {
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
    /* the head's Z buffer is drawn from the frame's start with its field */
    if (head && w->head_z_known) mark_z(w->fdrawn, w->head_zbp, w->head_zbw, w->head_zh);
    w->fdrawn_any = 1;
    if (w->marked_count < MARKED_MAX) {
        w->marked[w->marked_count].fbp = fbp;
        w->marked[w->marked_count].fbw = fbw;
        w->marked[w->marked_count].height = height;
        w->marked_count++;
    }
}

void em_gs_world_drawn_buffer(EmGsWorld *w, uint64_t frame, uint32_t height)
{
    if (w) drawn_buffer(w, frame, height, 0);
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

/* checked[]'s hash slot for a key (64-bit multiplicative mixing; the top 12 bits). */
static uint32_t checked_hash(uint64_t key, uint64_t clamp)
{
    uint64_t h = key * UINT64_C(0x9E3779B97F4A7C15) ^ clamp * UINT64_C(0xC2B2AE3D27D4EB4F);
    h ^= h >> 31;
    h *= UINT64_C(0x94D049BB133111EB);
    return (uint32_t)(h >> 52);
}

/* The blocks a TEX0 reads (the texels CLAMP_1 lets it reach, and its CLUT)
 * are all resident (uploaded) or in a buffer the model has drawn into; the
 * entry lists the drawn ones. 0, or -1 (not). */
static int texture_entry(EmGsWorld *w, uint64_t tex0, uint64_t clamp, uint32_t *first, uint32_t *n)
{
    const uint64_t key = tex0 & ~(UINT64_C(7) << 61);    /* CLD is not a read */
    w->tc.searches++;
    uint32_t slot = checked_hash(key, clamp);
    for (;; slot = (slot + 1u) & (CHECKED_SLOTS - 1u)) {
        w->tc.probes++;
        const uint32_t e = w->checked_slot[slot];
        if (!e) break;
        if (w->checked[e - 1u].tex0 == key && w->checked[e - 1u].clamp == clamp) {
            *first = w->checked[e - 1u].first;
            *n = w->checked[e - 1u].n;
            w->tc.hits++;
            return 0;
        }
    }
    w->tc.misses++;
    const uint32_t tbp = (uint32_t)tex0 & 0x3FFFu, tbw = (uint32_t)(tex0 >> 14) & 0x3Fu;
    const uint32_t psm = (uint32_t)(tex0 >> 20) & 0x3Fu, tw = (uint32_t)(tex0 >> 26) & 15u;
    const uint32_t th = (uint32_t)(tex0 >> 30) & 15u, cbp = (uint32_t)(tex0 >> 37) & 0x3FFFu;
    const uint32_t cpsm = (uint32_t)(tex0 >> 51) & 15u;
    if (tw > 10u || th > 10u) return -1;
    uint32_t u0, u1, v0, v1;
    reach((uint32_t)clamp & 3u, 1u << tw, (uint32_t)(clamp >> 4) & 0x3FFu, (uint32_t)(clamp >> 14) & 0x3FFu, &u0, &u1);
    reach((uint32_t)(clamp >> 2) & 3u, 1u << th, (uint32_t)(clamp >> 24) & 0x3FFu, (uint32_t)(clamp >> 34) & 0x3FFu,
          &v0, &v1);
    w->tc.texels += (uint64_t)(u1 - u0 + 1u) * (v1 - v0 + 1u);
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
            if (w->drawn[b] && !seen[b]) {          /* a drawn block, uploaded or not */
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
        w->checked_slot[slot] = (uint16_t)w->checked_count;   /* the empty slot the search ended at */
    } else {
        w->tc.cliff++;
    }
    return 0;
}

/* A textured primitive's read: residency, then the workers' ordering (a
 * read of a block drawn since the last barrier needs one). 0, or -1. */
static int texture_read(EmGsWorld *w, uint64_t tex0, uint64_t clamp)
{
    uint32_t first = 0, n = 0;
    if (w->tc_file) {
        const uint64_t m0 = w->tc.misses;
        const double t0 = now_ns();
        const int r = texture_entry(w, tex0, clamp, &first, &n);
        const double dt = now_ns() - t0;
        if (w->tc.misses != m0) w->tc.miss_ns += dt; else w->tc.hit_ns += dt;
        if (r < 0) return -1;
    } else if (texture_entry(w, tex0, clamp, &first, &n) < 0) {
        return -1;
    }
    if (!n) {
        w->tread_any = 1;
        return 0;
    }
    if (w->fdrawn_any)
        for (uint32_t i = 0; i < n; ++i)
            if (w->fdrawn[w->pool[first + i]]) {
                barrier_op(w);
                /* the drawing target's blocks: the draws after the barrier write
                 * them (a read of them later in the segment needs a barrier) */
                if (w->tgt_frame_known && w->tgt_scissor_known) {
                    const uint32_t fbp = (uint32_t)w->tgt_frame & 0x1FFu, fbw = (uint32_t)(w->tgt_frame >> 16) & 0x3Fu;
                    const uint32_t height = (uint32_t)(w->tgt_scissor >> 48 & 0x7FFu) + 1u;
                    mark_buffer(w->fdrawn, fbp, fbw, height);
                    w->fdrawn_any = 1;
                    if (w->marked_count < MARKED_MAX) {
                        w->marked[w->marked_count].fbp = fbp;
                        w->marked[w->marked_count].fbw = fbw;
                        w->marked[w->marked_count].height = height;
                        w->marked_count++;
                    }
                }
                break;
            }
    w->tread_any = 1;                  /* (after its barrier: this read is the new stretch's) */
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
            if (e->set & EM_GFX_GS_ENV_FRAME) em_gs_world_write(w, EM_GS_FRAME_1, e->frame);
            if (e->set & EM_GFX_GS_ENV_ZBUF) em_gs_world_write(w, EM_GS_ZBUF_1, e->zbuf);
            if (e->set & EM_GFX_GS_ENV_XYOFFSET) em_gs_world_write(w, EM_GS_XYOFFSET_1, e->xyoffset);
            if (e->set & EM_GFX_GS_ENV_SCISSOR) em_gs_world_write(w, EM_GS_SCISSOR_1, e->scissor);
            if (e->set & EM_GFX_GS_ENV_PRMODECONT) em_gs_world_write(w, EM_GS_PRMODECONT, e->prmodecont);
            if (e->set & EM_GFX_GS_ENV_DTHE) em_gs_world_write(w, EM_GS_DTHE, e->dthe);
            if (e->set & EM_GFX_GS_ENV_FBA) em_gs_world_write(w, EM_GS_FBA_1, e->fba);
            if (e->set & EM_GFX_GS_ENV_PABE) em_gs_world_write(w, EM_GS_PABE, e->pabe);
            if (e->set & EM_GFX_GS_ENV_TEXA) em_gs_world_write(w, EM_GS_TEXA, e->texa);
            if (e->set & EM_GFX_GS_ENV_SCANMSK) em_gs_world_write(w, EM_GS_SCANMSK, e->scanmsk);
            /* the buffer drawn, after the environment's writes: a FRAME_1 or
             * SCISSOR_1 change has ended the span where the mark places a
             * barrier (the same buffer with taller rows changes SCISSOR_1
             * alone) */
            if ((e->set & (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_SCISSOR)) ==
                (EM_GFX_GS_ENV_FRAME | EM_GFX_GS_ENV_SCISSOR))
                em_gs_world_drawn_buffer(w, e->frame, (uint32_t)(e->scissor >> 48 & 0x7FFu) + 1u);
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
    /* the drawing target: the field when declared (the packets' FRAME_1 and
     * SCISSOR_1 are the kick's, checked against it), else unknown */
    w->tgt_frame_known = w->tgt_scissor_known = (uint8_t)w->field_declared;
    w->tgt_frame = w->field_decl;
    w->tgt_scissor = (uint64_t)(w->field_decl_height - 1u) << 48;
    /* the field is the drawing target again: a read of it earlier since the
     * last barrier waits for every band before the draws that follow */
    if (w->field_declared) drawn_buffer(w, w->field_decl, w->field_decl_height, 1);
}

void em_gs_world_field_declare(EmGsWorld *w, uint64_t frame, uint32_t height)
{
    if (!w || w->fault[0]) return;
    if (!w->recording) {
        fail(w, "a field declared outside a recorded world frame", frame);
        return;
    }
    const uint32_t fbw = (uint32_t)(frame >> 16) & 0x3Fu, psm = (uint32_t)(frame >> 24) & 0x3Fu;
    if (psm != EM_GS_PSMCT32 || fbw == 0u || 64u * fbw > EM_GS_WORLD_FIELD_W || height == 0u || height > 1024u) {
        fail(w, "the declared field is not a PSMCT32 field the port presents", frame);
        return;
    }
    const uint64_t key = frame & UINT64_C(0x3F3F01FF);   /* FBP, FBW, PSM */
    if (w->field_declared && (w->field_decl != key || w->field_decl_height != height)) {
        fail(w, "a second, other field declared in one world frame", frame);
        return;
    }
    w->field_decl = key;
    w->field_decl_height = height;
    w->field_declared = 1;
    /* a drawing target register not written since the frame's start or the
     * environment again is the head's: the field's (a barrier for a read
     * before the first FRAME_1 write puts the field back into fdrawn) */
    if (!w->tgt_frame_known) {
        w->tgt_frame = key;
        w->tgt_frame_known = 1;
    }
    if (!w->tgt_scissor_known) {
        w->tgt_scissor = (uint64_t)(height - 1u) << 48;
        w->tgt_scissor_known = 1;
    }
    drawn_buffer(w, key, height, 1);
}

int em_gs_world_page_pass(EmGsWorld *w, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                          const uint32_t *again, uint32_t again_count, uint64_t field_frame, uint64_t field_scissor)
{
    if (!w) return -1;
    if (w->fault[0]) return -1;
    if ((count && (!prims || !envs)) || (again_count && !again))
        return fail(w, "a depth-of-field pass without its primitives", count);
    for (uint32_t k = 0; k < again_count; ++k)
        if (again[k] > count || (k && again[k] < again[k - 1]))
            return fail(w, "a depth-of-field pass whose environment marks are out of order", again[k]);
    em_gs_world_field_declare(w, field_frame, (uint32_t)(field_scissor >> 48 & 0x7FFu) + 1u);
    uint32_t at = 0;
    for (uint32_t k = 0; k < again_count && !w->fault[0]; ++k) {
        em_gs_world_prims(w, prims + at, envs + at, again[k] - at);
        em_gs_world_env_again(w);
        at = again[k];
    }
    em_gs_world_prims(w, prims + at, envs + at, count - at);
    return w->fault[0] ? -1 : 0;
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

/* The EmGs that takes the frame's stream: the parser, or the direct worker
 * (its registers, refusals, span faults and primitive counts speak for the
 * frame). */
static EmGs *front(EmGsWorld *w)
{
    return w->parse_once ? &w->parser : &w->worker[0].gs;
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
    if (w->tc_file) {
        fprintf(w->tc_file, "%llu %llu %llu %llu %llu %llu %llu %llu %u %u %.0f %.0f\n",
                (unsigned long long)w->tc_frames, (unsigned long long)w->tc.searches,
                (unsigned long long)w->tc.probes, (unsigned long long)w->tc.hits, (unsigned long long)w->tc.misses,
                (unsigned long long)w->tc.cliff, (unsigned long long)w->tc.texels, (unsigned long long)w->tc.resets,
                w->checked_count, w->pool_used, w->tc.hit_ns, w->tc.miss_ns);
        memset(&w->tc, 0, sizeof w->tc);
    }
    w->tc_frames++;
    w->run = &w->job[w->rec];
    w->rec ^= 1;
    w->recording = 0;
    w->busy = 1;
    w->t_start = now_ns();
    w->prims0 = front(w)->drawn_prims;
    /* (pixels an early parse drew for this frame are this frame's: own_drain) */
    w->pixels0 = pixels_drawn(w) - (w->parse_once ? w->run->p.early_pixels : 0);
    w->run->p.early_pixels = 0;
    if (w->parse_once) {
        /* (the workers wait for the go: nothing below races them) the
         * frame's parse goes on where early_parse left it, or is done */
        w->parse_claimed = AU_LOAD(w->run->p.done) ? 1 : 0;
        w->run->p.started = 1;
        w->seg_cur = -1;
        w->band_next = w->band_planned = w->band_n = w->band_done = 0;
        w->splits = 0;
        for (uint32_t i = 0; i < w->workers; ++i) w->worker[i].active = 0;
        w->read_phase = 0;
        w->read_next = w->read_n = w->read_done = 0;
        w->bands_total = w->waits_claim = w->waits_avail = 0;
        w->wait_claim_ns = w->wait_avail_ns = w->wait_read_ns = 0;
    }
#if GSW_THREADS
    if (w->threaded) {
        pthread_mutex_lock(&w->lock);
        w->running = w->workers;
        AU_STORE(w->live, w->workers);
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
    /* the outcome: the stream ran whole, and the model refused nothing (the
     * front EmGs, the parser or the direct worker, holds the registers,
     * refusals and span faults; a drawing worker can refuse alone, for want
     * of memory for the address tables) */
    for (uint32_t i = 0; i < w->workers; ++i)
        if (w->worker[i].failed)
            return fail(w, "a GIF packet of the frame is malformed, or a barrier fell inside a span", i);
    if (w->parse_once && j->p.failed)
        return fail(w, "a GIF packet of the frame is malformed, a barrier fell inside a span, or no memory to "
                       "parse the frame", em_gs_list_spans(j->p.list));
    /* (parsed: the parser's counts at the end of this frame's parse) */
    const EmGs *gs = front(w);
    uint32_t refusals = w->parse_once ? j->p.refusals : gs->refusals;
    uint32_t refused = w->parse_once ? j->p.refused : gs->refused_prims;
    const char *reason = w->parse_once ? j->p.reason : gs->reason;
    for (uint32_t i = 0; i < w->workers && !refused && !refusals; ++i)
        if (w->worker[i].gs.refused_prims || w->worker[i].gs.refusals) {
            refusals = w->worker[i].gs.refusals;
            refused = w->worker[i].gs.refused_prims;
            reason = w->worker[i].gs.reason;
        }
    if (refused || refusals) {
        char what[192];
        snprintf(what, sizeof what, "%s: the GS model refused a primitive: %s",
                 j->list ? "list frame" : "world frame", reason);
        return fail(w, what, refusals);
    }
    const uint32_t span_faults = w->parse_once ? j->p.span_faults : gs->span_faults;
    if (span_faults)
        return fail(w, "a span fault (an unmeasured span boundary decides the grid)", span_faults);
    w->field_w = 64u * ((uint32_t)(j->frame >> 16) & 0x3Fu);
    w->field_h = j->height;
    w->field_frame = j->frame;
    w->field_xyoffset = j->xyoffset;
    w->field_xyoffset_known = j->xyoffset_known;
    w->have_field = 1;
    w->stats.writes = j->op_count;
    w->stats.prims = w->parse_once ? j->p.prims1 - j->p.prims0 : gs->drawn_prims - w->prims0;
    /* (less the pixels an early parse drew meanwhile for the next frame) */
    w->stats.pixels = pixels_drawn(w) - w->pixels0 - (w->parse_once ? w->job[w->rec].p.early_pixels : 0);
    w->stats.span_faults = span_faults;
    w->stats.refused = refused;
    w->stats.ns = now_ns() - w->t_start;
    w->stats.cpu_max_ns = w->stats.cpu_sum_ns = 0;
    for (uint32_t i = 0; i < w->workers; ++i) {
        if (w->worker[i].cpu_ns > w->stats.cpu_max_ns) w->stats.cpu_max_ns = w->worker[i].cpu_ns;
        w->stats.cpu_sum_ns += w->worker[i].cpu_ns;
    }
    if (w->zc_file) {
        uint64_t sum[6] = {0};
        for (uint32_t i = 0; i < w->workers; ++i) {
            const EmGs *g = &w->worker[i].gs;
            sum[0] += g->pix.samples; sum[1] += g->pix.samples_bil; sum[2] += g->pix.zfail;
            sum[3] += g->pix.zfail_tex; sum[4] += g->pix.zfail_bil; sum[5] += g->pix.datefail_tex;
        }
        fprintf(w->zc_file, "%llu", (unsigned long long)w->zc_frames);
        for (int k = 0; k < 6; ++k) {
            fprintf(w->zc_file, " %llu", (unsigned long long)(sum[k] - w->zc_prev[k]));
            w->zc_prev[k] = sum[k];
        }
        fprintf(w->zc_file, " %llu\n", (unsigned long long)w->stats.pixels);
    }
    w->zc_frames++;
    if (w->tcc_file) {
        uint64_t sum[11] = {0}, bytes = 0;
        for (uint32_t i = 0; i < w->workers; ++i) {
            const EmGs *g = &w->worker[i].gs;
            const uint64_t v[11] = { g->tcs.binds, g->tcs.uncached, g->tcs.uncached_written, g->tcs.created,
                                     g->tcs.renewed, g->tcs.evicted, g->tcs.resets, g->tcs.rows, g->tcs.texels,
                                     g->tcs.samples, g->tcs.samples_bil };
            for (int k = 0; k < 11; ++k) sum[k] += v[k];
            if (g->tcs.bytes > bytes) bytes = g->tcs.bytes;
        }
        fprintf(w->tcc_file, "%llu", (unsigned long long)w->tcc_frames);
        for (int k = 0; k < 11; ++k) {
            fprintf(w->tcc_file, " %llu", (unsigned long long)(sum[k] - w->tcc_prev[k]));
            w->tcc_prev[k] = sum[k];
        }
        fprintf(w->tcc_file, " %llu\n", (unsigned long long)bytes);
    }
    w->tcc_frames++;
    if (w->spc_file) {
        uint64_t sum[7] = {0};
        for (uint32_t i = 0; i < w->workers; ++i) {
            const EmGs *g = &w->worker[i].gs;
            for (int k = 0; k < 3; ++k) {
                sum[k] += g->spc.prims[k];
                sum[3 + k] += g->spc.pixels[k];
            }
            sum[6] += g->spc.checked;
        }
        fprintf(w->spc_file, "%llu", (unsigned long long)w->spc_frames);
        for (int k = 0; k < 7; ++k) {
            fprintf(w->spc_file, " %llu", (unsigned long long)(sum[k] - w->spc_prev[k]));
            w->spc_prev[k] = sum[k];
        }
        fprintf(w->spc_file, " %llu\n", (unsigned long long)w->stats.pixels);
    }
    w->spc_frames++;
    if (w->lc_file && w->parse_once) {
        uint64_t sum[3] = {0};
        for (uint32_t i = 0; i < w->workers; ++i) {
            sum[0] += w->worker[i].gs.lsc.spans;
            sum[1] += w->worker[i].gs.lsc.spans_drawn;
            sum[2] += w->worker[i].gs.lsc.prims;
        }
        fprintf(w->lc_file, "%llu %u %u %u %u %u %llu", (unsigned long long)w->lc_frames, w->workers,
                em_gs_list_spans(j->p.list), (unsigned)AU_LOAD(j->p.segs_closed), w->bands_total, w->splits,
                (unsigned long long)w->stats.prims);
        for (int k = 0; k < 3; ++k) {
            fprintf(w->lc_file, " %llu", (unsigned long long)(sum[k] - w->lc_prev[k]));
            w->lc_prev[k] = sum[k];
        }
        fprintf(w->lc_file, " %.0f %u %u %.0f %.0f %.0f %.0f %.0f %.0f %u %u %zu\n", j->p.ns, w->waits_claim,
                w->waits_avail, w->stats.ns, w->stats.cpu_max_ns, w->stats.cpu_sum_ns, w->wait_claim_ns,
                w->wait_avail_ns, w->wait_read_ns, j->p.own_waits, j->p.prior_waits, j->p.early_ops);
    }
    w->lc_frames++;
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
    Job *j = &w->job[w->rec];
    if (j->p.began && (j->env_bytes != env_bytes || memcmp(j->env, env, env_bytes) != 0 ||
                       j->clear_bytes != clear_bytes || (clear_bytes && memcmp(j->clear, clear, clear_bytes) != 0)))
        return fail(w, "the kick's head is not the one the frame was parsed with (em_gs_world_preparse)", env_bytes);
    memcpy(j->env, env, env_bytes);
    j->env_bytes = env_bytes;
    if (clear_bytes) memcpy(j->clear, clear, clear_bytes);
    j->clear_bytes = clear_bytes;
    j->list = 0;
    /* while the previous job is drawn, this thread parses this one */
    early_parse(w, j);
    /* the previous job finishes first (the workers and the field are one) */
    if (em_gs_world_wait(w) < 0) return -1;
    /* the field: the FRAME_1 and SCISSOR_1 of the kicked draw environment
     * (register packets only: a model run over them writes no memory) */
    EmGs probe;
    em_gs_init(&probe, w->mem);
    (void)em_gs_gif(&probe, j->env, j->env_bytes);
    j->frame = probe.ctx[0].frame;
    j->xyoffset = probe.ctx[0].xyoffset;
    j->xyoffset_known = 1;
    j->height = (uint32_t)(probe.ctx[0].scissor >> 48 & 0x7FFu) + 1u;
    const uint64_t zbuf = probe.ctx[0].zbuf;
    em_gs_release(&probe);
    if (field_check(w, j->frame, j->height) < 0) return -1;
    /* the field the body declared (its reads were ordered and checked
     * against it) is the head's */
    if (w->field_declared &&
        ((j->frame & UINT64_C(0x3F3F01FF)) != w->field_decl || j->height != w->field_decl_height))
        return fail(w, "the field the frame declared is not the kicked head's FRAME_1 / SCISSOR_1", j->frame);
    (void)mark_drawn(w, (uint32_t)j->frame & 0x1FFu, (uint32_t)(j->frame >> 16) & 0x3Fu, j->height);
    /* the head's Z buffer, when it writes one (ZMSK 0): drawn from here on */
    if (!((zbuf >> 32) & 1u)) {
        w->head_zbp = (uint32_t)zbuf & 0x1FFu;
        w->head_zbw = (uint32_t)(j->frame >> 16) & 0x3Fu;
        w->head_zh = j->height;
        w->head_z_known = 1;
        static uint8_t z[BLOCKS];
        memset(z, 0, sizeof z);
        mark_z(z, w->head_zbp, w->head_zbw, w->head_zh);
        int over_upload = 0;
        for (uint32_t b = 0; b < BLOCKS; ++b)
            if (z[b] && !w->drawn[b]) {
                w->drawn[b] = 1;
                over_upload |= w->resident[b];
            }
        if (over_upload) checked_reset(w);
    }
    start(w);
    return 0;
}

int em_gs_world_preparse(EmGsWorld *w, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes)
{
    if (!w || w->fault[0] || !w->recording || w->job[w->rec].p.began) return 0;
    /* (a head the kick would refuse is left to the kick) */
    if (!env || !env_bytes || env_bytes > HEAD_MAX || env_bytes % 16u || (clear_bytes && !clear) ||
        clear_bytes > HEAD_MAX || clear_bytes % 16u || !gif_registers_only(env, env_bytes) ||
        (clear_bytes && !gif_registers_only(clear, clear_bytes)))
        return 0;
    Job *j = &w->job[w->rec];
    memcpy(j->env, env, env_bytes);
    j->env_bytes = env_bytes;
    if (clear_bytes) memcpy(j->clear, clear, clear_bytes);
    j->clear_bytes = clear_bytes;
    j->list = 0;
    early_parse(w, j);
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
    /* while the previous job is drawn, this thread parses this one */
    early_parse(w, &w->job[w->rec]);
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

/* The blocks of a host-to-local transfer's destination rectangle (BITBLTBUF,
 * TRXPOS, TRXREG as the list set them) are written from here on: resident,
 * as an upload's. PSMCT32, PSMT8 and PSMT4 only. 0, or -1 (the fault latched). */
static int gif_frame_transfer(EmGsWorld *w, const uint64_t *regs, uint64_t trxdir)
{
    if ((trxdir & 3u) != 0u) return fail(w, "a GIF frame's transfer that is not host to local", trxdir);
    const uint64_t bb = regs[EM_GS_BITBLTBUF - 0x50], pos = regs[EM_GS_TRXPOS - 0x50],
                   rg = regs[EM_GS_TRXREG - 0x50];
    const uint32_t dbp = (uint32_t)(bb >> 32) & 0x3FFFu, dbw = (uint32_t)(bb >> 48) & 0x3Fu;
    const uint32_t dpsm = (uint32_t)(bb >> 56) & 0x3Fu;
    const uint32_t dx = (uint32_t)(pos >> 32) & 0x7FFu, dy = (uint32_t)(pos >> 48) & 0x7FFu;
    const uint32_t rw = (uint32_t)rg & 0xFFFu, rh = (uint32_t)(rg >> 32) & 0xFFFu;
    if (((pos >> 59) & 3u) != 0u) return fail(w, "a GIF frame's transfer with a pixel order other than 0", pos);
    if (dpsm != EM_GS_PSMCT32 && dpsm != EM_GS_PSMT8 && dpsm != EM_GS_PSMT4)
        return fail(w, "a GIF frame's transfer in a format the frame check does not take", dpsm);
    for (uint32_t y = dy; y < dy + rh; ++y)
        for (uint32_t x = dx; x < dx + rw; ++x) {
            const uint32_t b = dpsm == EM_GS_PSMCT32 ? em_gs_addr32(dbp, dbw, x, y, 0) * 4u / 256u
                             : dpsm == EM_GS_PSMT8   ? em_gs_addr8(dbp, dbw, x, y) / 256u
                                                     : em_gs_addr4(dbp, dbw, x, y) / 512u;
            if (b < BLOCKS) w->resident[b] = 1;
        }
    return 0;
}

/* The recorder's checks over a GIF frame's stream (PACKED, REGLIST and IMAGE
 * tags): its texture state (tex_state), every textured vertex kick's reads
 * (textured_kick: resident or drawn memory), and its host-to-local
 * transfers, whose blocks are resident from the transfer on. 0, or -1. */
static int gif_frame_check(EmGsWorld *w, const uint8_t *p, size_t bytes)
{
    uint64_t regs[3] = {0, 0, 0};   /* BITBLTBUF, TRXPOS, TRXREG */
    size_t off = 0;
    while (off + 16u <= bytes) {
        uint64_t lo, hi;
        memcpy(&lo, p + off, 8);
        memcpy(&hi, p + off + 8, 8);
        off += 16u;
        const unsigned nloop = (unsigned)(lo & 0x7FFFu), flg = (unsigned)((lo >> 58) & 3u);
        unsigned nreg = (unsigned)(lo >> 60);
        if (!nreg) nreg = 16u;
        if (flg == 0u) {
            if ((lo >> 46) & 1u) tex_state(w, EM_GS_PRIM, lo >> 47);
            if (off + (size_t)nloop * nreg * 16u > bytes) return fail(w, "a GIF frame's PACKED data past the end", off);
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
                        if (reg >= EM_GS_BITBLTBUF && reg <= EM_GS_TRXREG) regs[reg - EM_GS_BITBLTBUF] = v;
                        if (reg == EM_GS_TRXDIR && gif_frame_transfer(w, regs, v) < 0) return -1;
                        if (reg == EM_GS_HWREG) return fail(w, "a GIF frame's HWREG transfer", off);
                    } else if (desc == EM_GS_XYZ2 || desc == EM_GS_XYZF2) {
                        kick = !((v_hi >> 47) & 1u);
                    } else if (desc > EM_GS_CLAMP_2) {
                        continue;   /* FOG, XYZF3 / XYZ3, NOP: no texture state */
                    }
                    tex_state(w, reg, v);
                    if (kick) {
                        if (!w->prim_known)
                            return fail(w, "a vertex kick in a GIF frame whose PRIM the check does not know", off);
                        if (textured_kick(w, w->prim) < 0) return -1;
                    }
                }
        } else if (flg == 1u) {
            const size_t count = (size_t)nloop * nreg, need = ((count + 1u) / 2u) * 16u;
            if (off + need > bytes) return fail(w, "a GIF frame's REGLIST data past the end", off);
            for (size_t i = 0; i < count; ++i) {
                const unsigned desc = (unsigned)(hi >> (4u * (unsigned)(i % nreg))) & 15u;
                uint64_t v;
                memcpy(&v, p + off + i * 8u, 8);
                if (desc > EM_GS_CLAMP_2) continue;   /* FOG, XYZF3 / XYZ3, A+D and NOP (skipped) */
                tex_state(w, desc, v);
                if (desc == EM_GS_XYZ2 || desc == EM_GS_XYZF2) {
                    if (!w->prim_known)
                        return fail(w, "a vertex kick in a GIF frame whose PRIM the check does not know", off);
                    if (textured_kick(w, w->prim) < 0) return -1;
                }
            }
            off += need;
        } else {
            if (off + (size_t)nloop * 16u > bytes) return fail(w, "a GIF frame's IMAGE data past the end", off);
            off += (size_t)nloop * 16u;
        }
    }
    return off == bytes ? 0 : fail(w, "a GIF frame that is not whole GIF packets", off);
}

int em_gs_world_gif_frame(EmGsWorld *w, const void *gif, size_t bytes, uint64_t display_frame,
                          uint64_t display_scissor)
{
    if (!w) return -1;
    if (w->fault[0]) return -1;
    if (w->recording) return fail(w, "a GIF frame inside a recorded world frame", bytes);
    if (!gif || !bytes || bytes % 16u) return fail(w, "a GIF frame that is not whole quadwords", bytes);
    if (!w->memory_loaded) return fail(w, "a GIF frame before the GS memory image", bytes);
    /* the frame before is drawn first (the workers and the memory are one) */
    if (em_gs_world_wait(w) < 0) return -1;
    /* the recorder's checks, over a recording that is then dropped (the
     * orderings it notes are the workers', and the workers take no part) */
    em_gs_world_begin(w);
    const int checked = gif_frame_check(w, gif, bytes);
    Job *j = &w->job[w->rec];
    w->recording = 0;
    j->op_count = 0;
    j->gif_used = 0;
    if (checked < 0) return -1;
    /* the stream on the model that holds the GS registers (the parser, or
     * the direct worker), on this thread while the workers wait: as the GS
     * runs the list, transfers included */
    EmGs *gs = front(w);
    const uint32_t refusals = gs->refusals, refused = gs->refused_prims, span_faults = gs->span_faults;
    const uint64_t prims = gs->drawn_prims, pixels = gs->drawn_pixels;
    const double t0 = now_ns(), c0 = cpu_now_ns();
    /* the parser draws nothing and keeps no decoded textures (tc_off): for
     * this frame it draws, with a cache of its own, empty at the start
     * (memory changed under it) and dropped again at the end */
    const int tc_off = gs->tc_off;
    if (gs == &w->parser) {
        em_gs_memory_written(gs, 0, BLOCKS);
        gs->tc_off = 0;
    }
    const size_t used = em_gs_gif(gs, gif, bytes);
    em_gs_flush(gs);
    if (gs == &w->parser) {
        em_gs_memory_written(gs, 0, BLOCKS);
        gs->tc_off = tc_off;
    }
    /* the other models' decoded textures over what this frame wrote */
    workers_memory_written(w, 0, BLOCKS);
    checked_reset(w);
    if (used != bytes) return fail(w, "a GIF packet of the GIF frame is malformed", used);
    if (gs->refusals != refusals || gs->refused_prims != refused) {
        char what[192];
        snprintf(what, sizeof what, "GIF frame: the GS model refused a primitive: %s", gs->reason);
        return fail(w, what, gs->refusals - refusals);
    }
    if (gs->span_faults != span_faults)
        return fail(w, "a span fault (an unmeasured span boundary decides the grid)", gs->span_faults - span_faults);
    j->list = 1;
    j->frame = display_frame;
    j->height = (uint32_t)(display_scissor >> 48 & 0x7FFu) + 1u;
    if (field_check(w, j->frame, j->height) < 0) return -1;
    /* the field's draw offset: the XYOFFSET_1 the stream left, when its
     * FRAME_1 is the displayed buffer (FBP and FBW); unknown otherwise */
    j->xyoffset_known = (gs->ctx[0].frame & 0x3F01FFu) == (display_frame & 0x3F01FFu);
    j->xyoffset = j->xyoffset_known ? gs->ctx[0].xyoffset : 0;
    (void)mark_drawn(w, (uint32_t)j->frame & 0x1FFu, (uint32_t)(j->frame >> 16) & 0x3Fu, j->height);
    read_rows(w, j, 0, j->height - 1u);
    w->run = j;
    w->rec ^= 1;
    w->busy = 0;
    w->field_w = 64u * ((uint32_t)(j->frame >> 16) & 0x3Fu);
    w->field_h = j->height;
    w->field_frame = j->frame;
    w->field_xyoffset = j->xyoffset;
    w->field_xyoffset_known = j->xyoffset_known;
    w->have_field = 1;
    w->stats.writes = 0;
    w->stats.prims = gs->drawn_prims - prims;
    w->stats.pixels = gs->drawn_pixels - pixels;
    w->stats.span_faults = 0;
    w->stats.refused = 0;
    w->stats.ns = now_ns() - t0;
    w->stats.cpu_max_ns = w->stats.cpu_sum_ns = cpu_now_ns() - c0;
    w->stats.runs++;
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

int em_gs_world_handed_frame(const EmGsWorld *w, uint64_t *frame)
{
    if (!w || !w->run) return -1;
    if (frame) *frame = w->run->frame;
    return 1;
}

int em_gs_head_probe(uint8_t *scratch, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes,
                     int colour, EmGsHeadProbe *out)
{
    if (!scratch || !env || !out || !env_bytes || env_bytes > HEAD_MAX || env_bytes % 16u ||
        (clear_bytes && !clear) || clear_bytes > HEAD_MAX || clear_bytes % 16u)
        return -1;
    if (!gif_registers_only(env, env_bytes) || (clear_bytes && !gif_registers_only(clear, clear_bytes)))
        return -1;
    memset(out, 0, sizeof *out);
    out->black = 1;
    for (int pass = colour ? 0 : 1; pass < 2; ++pass) {
        const uint32_t fill = pass ? 0xFFFFFFFFu : 0u;
        if (colour) memset(scratch, pass ? 0xFF : 0x00, EM_GS_MEM_BYTES);
        EmGs gs;
        em_gs_init(&gs, scratch);
        gs.strict = 1;
        int ok = em_gs_gif(&gs, env, env_bytes) == env_bytes;
        /* the field: the environment's FRAME_1 / SCISSOR_1 / XYOFFSET_1, as
         * em_gs_world_kick takes them */
        out->frame = gs.ctx[0].frame;
        out->xyoffset = gs.ctx[0].xyoffset;
        out->height = (uint32_t)(gs.ctx[0].scissor >> 48 & 0x7FFu) + 1u;
        const uint32_t fbp = (uint32_t)out->frame & 0x1FFu, fbw = (uint32_t)(out->frame >> 16) & 0x3Fu;
        const uint32_t psm = (uint32_t)(out->frame >> 24) & 0x3Fu;
        if (ok && colour && clear_bytes) ok = em_gs_gif(&gs, clear, clear_bytes) == clear_bytes;
        em_gs_flush(&gs);
        ok = ok && !gs.refusals && !gs.refused_prims && !gs.span_faults;
        em_gs_release(&gs);
        if (!ok) return -1;
        if (!colour) break;
        if (psm != EM_GS_PSMCT32 || !fbw || 64u * fbw > EM_GS_WORLD_FIELD_W || out->height > 1024u) return -1;
        for (uint32_t y = 0; y < out->height; ++y)
            for (uint32_t x = 0; x < 64u * fbw; ++x) {
                uint32_t word;
                memcpy(&word, scratch + (size_t)em_gs_addr32(fbp * 32u, fbw, x, y, 0) * 4u, 4);
                if (word != fill) out->writes = 1;
                if (pass && (word & 0xFFFFFFu)) out->black = 0;
            }
    }
    if (!colour) out->black = 0;
    return 0;
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
