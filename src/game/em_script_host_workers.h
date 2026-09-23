/* em_script_host_workers.h - the untranslated callees of the AREA11 script
 * host (em_area_script.h, docs/AREA_SCRIPT.md section 5) and a loader for the
 * AREA11 overlay scripts the host runs (docs/SCRIPT_HOST_WORKERS.md).
 *
 * Translations of the original routines, not models of them:
 *   00182BF0  op16 frame predicate (NEARMISS C; read from its instructions)
 *   001B1240  bearing from an object's X/Z to a point: wrap(atan2(x - o.x, z - o.z))
 *   001B1380  side test: 1 when wrap(atan2(from - to) - yaw) >= 0
 *   001B12B0  turn `current` toward `target` by at most `step` (asm-word file)
 *   001B6250  pad actuator stop on the block D_00810E40
 *   001B0C00  scene fade-out plus the three stream channel fades
 *   001B0460  camera re-seat from the room's camera record (op0D sub1;
 *             also 00157360 state 4, 0016D130, 0016DE40, 001B07C0)
 *
 * Reused translations (called, not re-translated):
 *   0021BB00  em_player_0021BB00 (em_player_stage_workers.c)
 *   001B1470  em_player_001B1470 (em_player_stage_workers.c), over the
 *             argument domain below
 *   0011E620  em_sdk_math_original_0011E620 (em_sdk_math_original.c)
 * 00102948 (the four-word lq/sq copy) is written inline where 001B0460 calls
 * it. The other callees (00111018, 001AEDE0, 001FAD70; for 001B0460:
 * 001B0250, 001B0B50, 001B0080, 0018C0D0, 001DD980 and the VU0 leaves
 * 001029C0, 00102C58, 001026A0, 001028B8) are workers.
 *
 * Fail-stop. Every routine checks, before its first write or call, that each
 * pointer and worker it can reach is bound, and returns -1 otherwise. A worker
 * that returns a negative value is a fault too; writes made before the call
 * stay, as the original order leaves them. The first fault address is kept
 * in EmScriptHostWorkers.fault_address (the routine's address for a missing
 * pointer, the worker's address for a missing or failing worker, the SDK
 * module's own address for an atan2 fault).
 *
 * 001B1470 domain. The original loops (subtract or add 2*pi) until its
 * argument lies in (-pi, pi]. The native routines pass it only arguments
 * with |x| < 4096.0 (at most 652 iterations), with DAZ applied as the EE
 * does; any other argument faults at 0x001B1470 instead of spinning for
 * minutes or forever (the EE treats exponent-255 patterns as +-MAX). No
 * AREA11 script argument comes near that bound: they are angles and angle
 * differences below 4*pi.
 *
 * Arithmetic and float compares go through em_ee_float.h on raw bit
 * patterns, as the COP1 instructions execute (docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_script_host_workers_reference.py executes the original
 * instructions over synthetic and captured records and compares every
 * written byte, result and worker call. */
#ifndef EM_SCRIPT_HOST_WORKERS_H
#define EM_SCRIPT_HOST_WORKERS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_sdk_math_original.h"
#include "game/em_script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SCRIPT_HOST_D_008102B0 0x008102B0u   /* player record (00182BF0 a0) */
#define EM_SCRIPT_HOST_D_00810E40 0x00810E40u   /* pad block (001B6250 a0) */
#define EM_SCRIPT_HOST_PAD_BLOCK_SIZE 0x2Au    /* bytes 001B6250 reads or writes */
#define EM_SCRIPT_HOST_WRAP_LIMIT 0x45800000u  /* 4096.0: 001B1470 domain bound */

/* The data the routines read and write, in the binder's canonical storage. */
typedef struct EmScriptHostWorkersWorld {
    /* 00182BF0: the player record and its three flag bytes. */
    uint32_t player_address;        /* the a0 the record stands for (D_008102B0) */
    EmPlayerLiveActor *player;
    uint8_t *d8106BC;               /* read and written */
    const uint8_t *d81083C;
    const uint8_t *d8106F1;
    /* 001B6250: the pad block, EM_SCRIPT_HOST_PAD_BLOCK_SIZE bytes in its
     * original layout (+04 port word, +08 slot word, +12 ready byte, +16
     * active byte, +18 / +19 motor bytes, +28 duration halfword). */
    uint32_t pad_address;           /* the a0 the block stands for (D_00810E40) */
    uint8_t *d810E40;
    /* 0011E620: the SDK math tables, world and workers
     * (docs/SDK_MATH_ORIGINAL.md section 7). */
    const EmSdkMathTables *sdk_tables;
    const EmSdkMathWorld *sdk_world;
    const EmSdkMathWorkers *sdk_workers;
    /* 001B0460. The room camera tables D_0024D650 (per-area pointer arrays
     * to 0x30-byte records) and D_0024A8D0 (XYZ triples) are read from the
     * user's boot ELF image (file offset = vram - 0x100000 + 0x300, inside
     * the loaded 0x175B00 bytes); the game never writes them. */
    const uint8_t *elf;
    size_t elf_size;
    const uint8_t *d810700, *d810701, *d810702;    /* area, room, camera index */
    /* Camera object D_008101E0: +1/+2/+3, +5 (mode), +6 (sub), +7 bytes,
     * +8 halfword, +0xC float, +0x10 / +0x20 vec4. */
    uint8_t *d8101E1, *d8101E2, *d8101E3, *d8101E5, *d8101E6, *d8101E7;
    int16_t *d8101E8;
    float *d8101EC, *cam_10, *cam_20;
    float *d810244;
    /* D_008106C8: the 001B0250 worker writes it, then 001B0460 reads it
     * (the same storage must back both). */
    const int32_t *d8106C8;
    uint8_t *d8106CD, *d275BE0, *d8106BE;
    const int32_t *d8104E0;                         /* player +0x230 */
    const float *d810350, *d810370;                 /* player +0xA0 / +0xC0 (vec4) */
    float *d8105D0, *d8105E0;                       /* working eye / target (vec4) */
    float *spad3400;                                /* 0x70003400 matrix (16 floats) */
    float *spad3600;                                /* 0x70003600 vec4 */
} EmScriptHostWorkersWorld;

/* Original callees that are not translated here. Each returns 0, or a
 * negative value on a fault. */
typedef struct EmScriptHostWorkersCallees {
    void *ctx;
    /* 00111018(port, slot, act): libpad actuator write; act is the pad
     * block's +0x18 (original address D_00810E58), read by the callee. */
    int (*w_00111018)(void *ctx, int32_t port, int32_t slot, const uint8_t *act);
    /* 001AEDE0(a0, a1): the transition fade-out (001B0C00 passes its a0, 0). */
    int (*w_001AEDE0)(void *ctx, int32_t a0, int32_t a1);
    /* 001FAD70(channel, a1, a2): one stream channel fade (0/1/2, a0, 1). */
    int (*w_001FAD70)(void *ctx, int32_t channel, int32_t a1, int32_t a2);
    /* 001B0460's callees. 001B0250 and 001B0B50 take no arguments (the
     * registers 001B0460 leaves in a0/a1 are not read). `camera` is the
     * original address D_008101E0. The VU0 leaves get pointers into the
     * canonical storage and must allow the aliasing the original uses:
     * 00102C58(dst == src) and 001028B8(out == a). */
    int (*w_001B0250)(void *ctx);
    int (*w_001B0B50)(void *ctx);
    int (*w_001B0080)(void *ctx, uint32_t camera, float a1);
    int (*w_0018C0D0)(void *ctx, uint32_t camera, int32_t a1);
    int (*w_001DD980)(void *ctx, const float eye[4], const float target[4]);
    int (*w_001029C0)(void *ctx, float m[16]);
    int (*w_00102C58)(void *ctx, float dst[16], const float src[16], const float angles[4]);
    int (*w_001026A0)(void *ctx, float out[4], const float m[16], const float v[4]);
    int (*w_001028B8)(void *ctx, float out[4], const float a[4], const float b[4]);
} EmScriptHostWorkersCallees;

typedef struct EmScriptHostWorkers {
    EmScriptHostWorkersWorld world;
    EmScriptHostWorkersCallees callees;
    uint32_t fault_address;         /* 0, or the first fault */
} EmScriptHostWorkers;

/* ---- The routines on raw bit patterns ----------------------------------
 * Each returns 0, or -1 on a fault (recorded in h->fault_address when h is
 * not NULL). */

/* 00182BF0(actor): *result = the original v0 (0: the scripted frame may be
 * taken; 1: not now). actor must be world.player_address. */
int em_script_host_00182BF0(EmScriptHostWorkers *h, uint32_t actor, int32_t *result);
/* 001B1240(object, x, z): reads object[0] and object[2]. */
int em_script_host_001B1240(EmScriptHostWorkers *h, const uint32_t object[3], uint32_t x,
                            uint32_t z, uint32_t *result);
/* 001B1380(from, to, yaw): reads from/to [0] and [2]; *result 0 or 1. */
int em_script_host_001B1380(EmScriptHostWorkers *h, const uint32_t from[3], const uint32_t to[3],
                            uint32_t yaw, int32_t *result);
/* 001B12B0(target, current, step). No data; h may be NULL. */
int em_script_host_001B12B0(EmScriptHostWorkers *h, uint32_t target, uint32_t current,
                            uint32_t step, uint32_t *result);
/* 001B6250(address): address must be world.pad_address. */
int em_script_host_001B6250(EmScriptHostWorkers *h, uint32_t address);
/* 001B0C00(a0). */
int em_script_host_001B0C00(EmScriptHostWorkers *h, int32_t a0);
/* 001B0460(a0). */
int em_script_host_001B0460(EmScriptHostWorkers *h, int32_t a0);

/* ---- Worker adapters (ctx = EmScriptHostWorkers) ------------------------
 * Shaped as the EmAreaScriptWorkers slots of the same names (em_area_script.h). */
int em_script_host_w_00182BF0(void *ctx, uint32_t actor, int32_t *result);
int em_script_host_w_001B1240(void *ctx, const float object[4], float x, float z, float *result);
int em_script_host_w_001B12B0(void *ctx, float target, float current, float step, float *result);
int em_script_host_w_001B1380(void *ctx, const float from[4], const float to[4], float yaw,
                              int32_t *result);
int em_script_host_w_001B0C00(void *ctx, int a0);
int em_script_host_w_001B6250(void *ctx, uint32_t address);
int em_script_host_w_001B0460(void *ctx, int a0);
/* EmOwnerServicesWorkers.w_001B6250 shape: 001B6250(&D_00810E40). */
int em_script_host_owner_001B6250(void *ctx);
/* The player modules' `approach` slot shape (001B12B0 on raw bits,
 * em_player_fall.h and others). ctx is not read: those modules pass their
 * own context. */
int em_script_host_approach(void *ctx, uint32_t target, uint32_t current, uint32_t step,
                            uint32_t *out);

/* ---- AREA11 overlay scripts and director quads --------------------------
 * tools/export_area11_scripts.py copies two ranges of the user's AREA11
 * overlay (MWo3, loaded whole at 0x823500) into ignored EMSC files, the
 * format of em_script_image_load ("EMSC", u32 1, base, entry, length, then
 * the bytes):
 *   scripts.emsc         0x8292C0..0x82A3C0: the truck camera preview
 *                        0x8292C0 and director beats 0x8294C0 / 0x829A40 /
 *                        0x829CC0 / 0x829E80
 *   director_quads.emsc  0x82ABE0..0x82ACA0: the director's three 4-vertex
 *                        quads (XYZW floats) 0x82ABE0 / 0x82AC20 / 0x82AC60
 * The elevator's powered (0x82A750) and refusal (0x82A990) scripts are
 * elevator.emsc (tools/export_elevator.py) and Roger's are
 * roger/programs.emsc (tools/export_roger_resources.py); the loader opens
 * those too when their paths are given, so one lookup serves every AREA11
 * overlay script entry. */
#define EM_AREA11_SCRIPTS_BASE 0x008292C0u
#define EM_AREA11_SCRIPTS_END  0x0082A3C0u
#define EM_AREA11_QUADS_BASE   0x0082ABE0u
#define EM_AREA11_QUADS_END    0x0082ACA0u
#define EM_AREA11_ELEVATOR_BASE 0x0082A750u
#define EM_AREA11_ELEVATOR_END  0x0082AB10u
#define EM_AREA11_ROGER_BASE   0x008283D0u
#define EM_AREA11_ROGER_END    0x00828BD0u
#define EM_AREA11_SCRIPTS_PATH  "assets/scene_snow/area11_scripts/scripts.emsc"
#define EM_AREA11_QUADS_PATH    "assets/scene_snow/area11_scripts/director_quads.emsc"
#define EM_AREA11_ELEVATOR_PATH "assets/scene_snow/elevator.emsc"
#define EM_AREA11_ROGER_PATH    "assets/scene_snow/roger/programs.emsc"

enum {
    EM_AREA11_IMAGE_SCRIPTS, EM_AREA11_IMAGE_ELEVATOR, EM_AREA11_IMAGE_ROGER,
    EM_AREA11_IMAGE_COUNT
};

typedef struct EmArea11Scripts {
    EmScriptImage image[EM_AREA11_IMAGE_COUNT];   /* bytes NULL when not loaded */
    /* quad[q][v] = vertex v of quad q (x, y, z, w), q = 0x82ABE0 + 0x40 * q. */
    float quad[3][4][4];
    int quads_loaded;
} EmArea11Scripts;

/* Loads scripts.emsc and director_quads.emsc (required) and the elevator and
 * Roger images (each skipped when its path is NULL). Every image must have
 * exactly its range above as base and length, and every entry the owners
 * start (em_area11_scripts_entry list) must reach a stop record inside its
 * image, following jump records, within 64 records. Returns 0, or -1 with
 * nothing loaded. */
int em_area11_scripts_load(EmArea11Scripts *out, const char *scripts_path,
                           const char *quads_path, const char *elevator_path,
                           const char *roger_path);
void em_area11_scripts_free(EmArea11Scripts *scripts);
/* The loaded image that holds `entry`, or NULL. */
EmScriptImage *em_area11_scripts_image(EmArea11Scripts *scripts, uint32_t entry);
/* The owners' start entries (original addresses), in the order of
 * docs/SCRIPT_HOST_WORKERS.md section 5; *count receives the number. */
const uint32_t *em_area11_scripts_entries(size_t *count);
/* The director's quad pointers in the EmDirectorOriginalWorld.quad shape
 * (em_director_original.h). -1 when the quads are not loaded. */
int em_area11_scripts_director_quads(const EmArea11Scripts *scripts, const float (*quad[3])[4]);

#ifdef __cplusplus
}
#endif

#endif
