/* em_gs_world - the Original profile's world frame drawn by the CPU GS model
 * (docs/GS_EXACT.md section 9, "Binding").
 *
 * The world frame's GS packets are already the original's: the step V kick's
 * draw environment and clear (001D2300's REFs of the GS blocks), the
 * background's channel-3 list (the grid program 0x0023C990), the static
 * world's run (the level kernel and its clip kernel), the object units (the
 * object kernel, its clip pass and the face program), the drop shadow's
 * passes (001DA6A0) and the chain page D_007635C0. The renderer hands them
 * here in the order it receives them; this module records them as GS
 * register writes and, at the kick, runs the head and then the body through
 * em_gs_raster (strict mode) into GS local memory. The field FRAME_1 names
 * (512x224 PSMCT32) is then read back for the platform layer, which only
 * presents it (how a field is shown is the user's open decision,
 * docs/LAUNCHER_OPTIONS.md "Field presentation").
 *
 * GS local memory persists across frames, as the GS's does: the two field
 * buffers, the Z buffer, the shadow's 128x128 target and the uploaded
 * textures (the area load's uploads as the loader sends them,
 * em_gs_world_upload_chain; the boot library from the disc export
 * tools/export_gs_memory.py, em_gs_world_memory_load).
 *
 * The body is recorded, not drawn at once: the draw environment the kick
 * REFs carries the field's half-line XYOFFSET, which 001D2300 writes at step
 * V after the frame's draws were built; the GS executes the whole list after
 * the kick. Recording keeps that order. The kick hands the frame to worker
 * threads (row bands, EmGs.band_*), which draw it while the game builds the
 * next frame; the next kick waits for it.
 *
 * Fail-stop: a refused primitive, a span fault (GS_EXACT.md section 6), a
 * texture that reads GS memory no upload wrote, a register write outside a
 * recording or a malformed head latch the fault (em_gs_world_fault); the
 * caller turns it into the scene fault.
 *
 * Platform-independent C (no GPU API): every backend calls the same code. */
#ifndef EM_GS_WORLD_H
#define EM_GS_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "em_gfx.h"
#include "gs/em_gs_raster.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_GS_WORLD_FIELD_W 512u
#define EM_GS_WORLD_FIELD_H 224u
#define EM_GS_WORLD_MEMORY_MAGIC "EMGM"
#define EM_GS_WORLD_MEMORY_VERSION 1u

typedef struct EmGsWorld EmGsWorld;

EmGsWorld *em_gs_world_create(void);
void em_gs_world_destroy(EmGsWorld *w);

/* A GS memory image (EMGM, written by tools/export_gs_memory.py from the
 * user's disc: the library module 0x1B the boot uploads, docs/
 * DISC_TEXTURES.md section 2). Copies every block it holds into local
 * memory and marks it resident. 0, or -1 (missing or malformed: reported). */
int em_gs_world_memory_load(EmGsWorld *w, const char *path);
int em_gs_world_memory_loaded(const EmGsWorld *w);

/* An upload the game sends (00200830 / 00200890's buffers: the area's A
 * sections, the player texture packet): a VIF1 source chain at the start
 * of `buf` (CNT tags to a RET or END) whose DIRECT GIF data are A+D writes
 * of the transfer registers and host-to-local PSMCT32 IMAGE data. Waits for
 * the workers, then writes local memory as the GS does and marks the
 * blocks resident. Anything else faults (the rule of
 * tools/export_disc_textures_gs.py). 0, or -1. */
int em_gs_world_upload_chain(EmGsWorld *w, const uint8_t *buf, size_t size);

/* Start recording a world frame (drops a body that was never kicked). */
void em_gs_world_begin(EmGsWorld *w);
int em_gs_world_recording(const EmGsWorld *w);

/* Body: one register write in A+D form, in the order the GS receives it. A
 * state register (not PRIM and not a vertex register) is recorded only when
 * it changes the value recorded last in this frame. */
void em_gs_world_write(EmGsWorld *w, unsigned reg, uint64_t value);
/* Body: `count` primitives with the state each carries (EmGfxGsPrim.set) and,
 * when envs is not NULL, the environment registers each one's list set
 * (EmGfxGsEnv.set): the state and environment first, then PRIM and the
 * vertex registers (RGBAQ with Q, ST, UV for FST 1, XYZF2 when the vertex
 * carries F, XYZ2 otherwise). A textured primitive's TEX0 must be known
 * to the recorder (written in this frame by a write or by recorded GIF data,
 * after the last em_gs_world_env_again) and read only resident memory or a
 * buffer drawn earlier in the frame; otherwise the fault latches. */
void em_gs_world_prims(EmGsWorld *w, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count);
/* Body: the draw environment the kick REFs again at this point (001DA6A0
 * after the silhouette: the frame's FRAME_1, ZBUF_1, XYOFFSET_1 and
 * SCISSOR_1 again). */
void em_gs_world_env_again(EmGsWorld *w);
/* Body: GIF data the list sends at this point (a GS state block the list
 * REFs), run through em_gs_gif at the kick. Its TEX0 / CLAMP / PRIM writes
 * are decoded (the texture state), and its textured vertex kicks are
 * checked as em_gs_world_prims checks a primitive. */
void em_gs_world_gif(EmGsWorld *w, const void *gif, size_t bytes);
/* Note a buffer the body draws into (FRAME_1), so a later texture read of
 * it is not a residency fault (the shadow's target). */
void em_gs_world_drawn_buffer(EmGsWorld *w, uint64_t frame, uint32_t height);

/* The kick (step V): the draw environment's and the clear's GIF packets, as
 * the list REFs them (VIF DIRECT data). Waits for the previous frame, then
 * hands this one (the head, then the body) to the workers, which draw it
 * while the game builds the next frame, as the GS draws a kicked list while
 * the EE builds the next one. 0, or -1 (a fault latched: this frame's
 * recording, or the previous frame's drawing). */
int em_gs_world_kick(EmGsWorld *w, const void *env, size_t env_bytes, const void *clear, size_t clear_bytes);

/* A frame whose whole GS list was walked with its environment (the load
 * veil's, em_gfx_gs_frame): the primitives and environments in GS order,
 * handed to the workers as a kick does; its field is (display_frame's FBP,
 * FBW) at the height display_scissor gives. 0, or -1. */
int em_gs_world_list_frame(EmGsWorld *w, const EmGfxGsPrim *prims, const EmGfxGsEnv *envs, uint32_t count,
                           uint64_t display_frame, uint64_t display_scissor);

/* Waits until the workers have drawn the frame handed to them (if any) and
 * checks it: 0, or -1 (a refused primitive, a span fault, a malformed
 * packet: the fault latched). */
int em_gs_world_wait(EmGsWorld *w);
/* A frame was handed to the workers and not yet waited for. */
int em_gs_world_busy(const EmGsWorld *w);

/* The field of the last frame waited for: RGBA8 rows (the GS bytes R, G,
 * B, A), width * height * 4 bytes. NULL before the first, or while a frame
 * is being drawn. */
const uint8_t *em_gs_world_field(const EmGsWorld *w, uint32_t *width, uint32_t *height, uint64_t *frame);
/* That field's XYOFFSET_1 (the kicked draw environment's; 0 for a list
 * frame): its half-line OFY. */
uint64_t em_gs_world_field_xyoffset(const EmGsWorld *w);

/* Rows of a PSMCT32 buffer in local memory, after waiting for the workers
 * (a test hook: the veil's surfaces, the shadow target). 0, or -1. */
int em_gs_world_read(EmGsWorld *w, uint32_t fbp, uint32_t fbw, uint32_t width, uint32_t height, uint8_t *rgba);

/* The GIF data of a VIF1 transfer: from word `word` of `p` (`bytes`),
 * NOP / FLUSHE / FLUSH / FLUSHA pass and DIRECT / DIRECTHL append their
 * qword-aligned data to out (`*used` bytes so far, `cap` at most). Any other
 * VIF code is a shape this reader does not take. 0, or -1. */
static inline int em_gs_vif_direct(const uint8_t *p, size_t bytes, size_t word, uint8_t *out, size_t cap, size_t *used)
{
    if (!p || !out || !used) return -1;
    for (size_t at = word * 4u; at + 4u <= bytes;) {
        const uint32_t code = (uint32_t)p[at] | (uint32_t)p[at + 1] << 8 | (uint32_t)p[at + 2] << 16 |
                              (uint32_t)p[at + 3] << 24;
        const uint32_t cmd = (code >> 24) & 0x7Fu;
        at += 4u;
        if (cmd == 0x00u || cmd == 0x10u || cmd == 0x11u || cmd == 0x13u) continue;
        if (cmd != 0x50u && cmd != 0x51u) return -1;
        const size_t qw = (code & 0xFFFFu) ? (code & 0xFFFFu) : 0x10000u;
        at = (at + 15u) & ~(size_t)15u;
        if (at + qw * 16u > bytes || *used + qw * 16u > cap) return -1;
        memcpy(out + *used, p + at, qw * 16u);
        *used += qw * 16u;
        at += qw * 16u;
    }
    return 0;
}


/* The number of workers drawing a frame (row bands, EmGs.band_*): EM_GS_THREADS
 * or the host's processors less three (at least one), at most 8. Every count draws the same
 * bytes (tools/test_gs_raster_reference.py part F). */
uint32_t em_gs_world_workers(const EmGsWorld *w);

/* Test hooks (tools/test_gs_memory_reference.py): the local memory after
 * waiting for the workers, and whether a 256-byte block is resident (an
 * upload wrote it). */
const uint8_t *em_gs_world_memory(EmGsWorld *w);
int em_gs_world_resident(const EmGsWorld *w, uint32_t block);

/* The latched fault's reason, or NULL. */
const char *em_gs_world_fault(const EmGsWorld *w);

/* Counters of the last frame waited for (the frame-cost report). */
typedef struct {
    uint64_t writes;          /* body register writes executed */
    uint64_t prims, pixels;   /* the model's drawn primitives and pixels */
    uint32_t span_faults, refused;
    double ns;                /* wall time of the kick's execution */
    double cpu_max_ns;        /* the busiest worker's CPU time (the critical path) */
    double cpu_sum_ns;        /* all workers' CPU time */
    uint64_t runs;            /* frames run so far (kicks and list frames) */
} EmGsWorldStats;
void em_gs_world_stats(const EmGsWorld *w, EmGsWorldStats *out);

#ifdef __cplusplus
}
#endif

#endif
