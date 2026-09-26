/* AREA11 script host: the original event-script interpreter 001BA1A0 (start)
 * / 001BA1F0 (poll) with the command handlers of ftab_0024D880 that the
 * AREA11 owners' scripts use (docs/AREA_SCRIPT.md lists them per script).
 *
 * Sequencing is em_script (the verified 001BA1F0 translation); this module
 * supplies the typed command handlers. 001B82D0 (op07) runs through the
 * verified em_interaction_frame / em_interaction_cinematic translations
 * (adapted to the canonical storage below); 001B7D60 (op0C) is a worker
 * bound to em_message_op0c; the fades, camera-track and other services are
 * workers. The remaining handlers are translations of the original function
 * named at each case, restricted to the sub-commands docs/AREA_SCRIPT.md
 * records as oracle-verified. Any other opcode or sub-command faults
 * (fail-stop) instead of being skipped or approximated.
 *
 * Data. The handlers read and write original globals, the player object
 * D_008102B0, the camera object D_008101E0 and the script owner (the actor
 * whose +0x1F0 block runs the script, the handlers' arg0). Every such datum is
 * reached through one pointer of EmAreaScriptWorld, named by its original
 * address, pointing at the canonical storage the binder owns. A NULL pointer
 * that a handler reaches is a fault. Vectors are four floats (the original
 * quad copies and VU0 subtractions move all four lanes).
 *
 * Calls. Every original callee is one worker of EmAreaScriptWorkers, named
 * by its original address, with the callee's decomp parameter list. Workers
 * return a negative value on failure (latched as a fault); a NULL worker that
 * is reached is a fault. Addresses of original objects passed as arguments
 * (D_008102B0, D_008101E0, the owner) are passed as the original address;
 * vectors the callee reads or writes are passed as pointers to the canonical
 * storage (or the record bytes) exactly as the original passes them.
 *
 * Script state. The original block actor+0x1F0..+0x21F is EmScript
 * (+0x00 active, +0x04 phase, +0x08 pc, +0x0C skip byte) plus st_0E
 * (op0B's anim flags, +0x0E) and st_10/st_20 (op00 kinds 1/5 and op01 kinds
 * 3/5 save their start vectors at +0x10/+0x20). 3B91 has one storage, the
 * world pointer; EmScript.skip_request is a per-tick view published before
 * each tick and kept in step with every handler write.
 *
 * Floats follow the EE model used by every reference test: FPU add/sub with
 * the single guard bit, mul truncating, div rounding (em_pose_math.h); VU0
 * macro arithmetic truncating (em_effect_float32). */
#ifndef EM_AREA_SCRIPT_H
#define EM_AREA_SCRIPT_H

#include <stdint.h>

#include "game/em_script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA_SCRIPT_D_008101E0 0x008101E0u /* camera object */
#define EM_AREA_SCRIPT_D_008102B0 0x008102B0u /* player object */
#define EM_AREA_SCRIPT_D_00810E40 0x00810E40u /* 001B6250 argument (op18) */
#define EM_AREA_SCRIPT_D_0028A490 0x0028A490u /* bank table, 4 bytes per entry */
#define EM_AREA_SCRIPT_D_0028A580 0x0028A580u /* default player bank word */

typedef struct EmAreaScriptWorld {
    /* EE scratchpad. */
    uint16_t *spad3B84;
    uint8_t *spad3B8D, *spad3B8F, *spad3B91, *spad3B92;
    float *spad3600;          /* 0x70003600 vec4 temporary (op01 kinds 3/5) */
    /* D_008106B0 request block; d8106D4 is the 12 activity bytes 001BA510
     * clears (inlined by em_interaction_frame / em_interaction_cinematic). */
    uint8_t *d8106EF, *d8106F3, *d8106F4, *d8106D4;
    /* Progress: D_00810758[256] event flags, D_008107D8[256] counters. */
    uint8_t *d810758, *d8107D8, *d81078F;
    /* Camera object D_008101E0 (+0x04 is D_008101E4, the mode byte). */
    uint8_t *d8101E1, *d8101E2, *d8101E3, *d8101E4, *d8101E6;
    float *cam_0C, *cam_10, *cam_20, *cam_50, *cam_54; /* _10/_20 vec4 */
    int16_t *cam_6E;
    uint32_t *cam_70;         /* track pointer (original address value) */
    float *cam_74, *cam_78;
    int16_t *cam_A0;
    /* Working camera vectors D_008105D0 / D_008105E0 / D_008105F0 (vec4). */
    float *d8105D0, *d8105E0, *d8105F0;
    /* D_002821B0 message request words (em_message_service's block). */
    int32_t *d2821B0, *d2821B4, *d2821BC;
    uint32_t *d2821B8;
    /* Player D_008102B0. p0A0 = D_00810350, p0B0 = D_00810360,
     * p0C0 = D_00810370 (vec4 each; +0xC4 is D_00810374, +0xCC D_0081037C). */
    uint32_t *p040;
    float *p0A0, *p0B0, *p0C0;
    int16_t *p1F2;            /* D_008104A2 */
    float *p1F4, *p1F8;       /* D_008104A4, D_008104A8 */
    uint32_t *p200;           /* D_008104B0 */
    int16_t *p20C;
    uint8_t *p25C, *p2F3, *p2FF;
    /* Script owner (handler arg0): original address and fields. */
    uint32_t self;
    uint32_t *s040;
    float *s0B0, *s0C0;       /* vec4 each */
    /* Read-only data owned elsewhere. */
    const int16_t *d28A9A0;   /* transition substate (lh) */
    const int8_t *d282157;    /* stream busy byte (lb) */
    uint8_t *d275C78, *d821058;
    const int16_t *d24D8F0;   /* ELF table used by op01 kinds 3/5/8 */
    uint32_t d24D8F0_count;
} EmAreaScriptWorld;

typedef struct EmAreaScript EmAreaScript;

typedef struct EmAreaScriptWorkers {
    void *ctx;
    /* Readers of data owned elsewhere. */
    int (*r_0028A490)(void *ctx, uint32_t address, uint32_t *value); /* bank words */
    int (*r_track_head)(void *ctx, uint32_t track, float *value);   /* **(g+0x70) */
    int (*r_player_bone_C0)(void *ctx, float out[4]); /* *(D_008102B0+0x114)+0xC0 */

    /* 001B82D0 (op07, through em_interaction_frame / em_interaction_cinematic)
     * and 001B81D0. */
    int (*w_001AEB60)(void *ctx, int16_t a0);
    int (*w_001AEBA0)(void *ctx, int16_t a0);
    int (*w_001AEDE0)(void *ctx, int16_t a0, uint8_t a1);
    int (*w_001AEE10)(void *ctx, int16_t a0, uint8_t a1);
    int (*w_001FD4C0)(void *ctx, int32_t a0);
    int (*w_00119828)(void *ctx, int a0, int a1, int a2);
    int (*w_001D2610)(void *ctx, float a0);
    int (*w_001D25F0)(void *ctx, float a0);
    int (*w_001CA770)(void *ctx, uint32_t actor);
    int (*w_001FAE70)(void *ctx, int a0);
    int (*w_001CA700)(void *ctx, uint32_t actor, uint32_t bank, int16_t a2, int32_t *result);
    int (*w_001D06D0)(void *ctx, uint32_t actor, uint8_t a1);

    /* 001B8FC0 (op00). */
    int (*w_001DD980)(void *ctx, const float eye[4], const float target[4]);
    int (*w_0011E2A8)(void *ctx, float a0, float *result); /* SDK sine */
    int (*w_001C6120)(void *ctx, uint32_t table, int32_t index, uint32_t *result);
    int (*w_0022EC30)(void *ctx, uint32_t camera);

    /* 001B94F0 (op01), 001B6F80. */
    int (*w_00182F90)(void *ctx, uint32_t actor, const float target[4]);
    int (*w_001B1240)(void *ctx, const float object[4], float x, float z, float *result);
    int (*w_001B12B0)(void *ctx, float target, float current, float step, float *result);

    /* 001B99F0 (op09): the record's own callback (record +0x04), called
     * with the owner, the script block and the record; returns the
     * callback's original result. */
    int (*c_record)(void *ctx, uint32_t callback, EmAreaScript *host,
                    unsigned char *record, int32_t *result);

    /* 001B7D60 (op0C): bind to em_message_op0c. handshake is script +0x04. */
    int (*w_001B7D60)(void *ctx, uint8_t *handshake, const unsigned char *record,
                      int32_t *result);

    /* 001B8020 (op0B). */
    int (*w_001C67E0)(void *ctx, uint32_t actor, int16_t clip, float a, float b);

    /* 001B7B30 (op0D). */
    int (*w_001B0250)(void *ctx);
    int (*w_0021B9A0)(void *ctx, int mode, float scale, float bias);
    int (*w_001D2830)(void *ctx, int a0, int a1);
    int (*w_0018CBD0)(void *ctx, uint32_t camera, uint32_t actor, float a2);
    int (*w_0018D7B0)(void *ctx, uint32_t camera, int a1);
    int (*w_001B0460)(void *ctx, int a0);

    /* 001B7A30 (op0F). */
    int (*w_001FBC50)(void *ctx);
    int (*w_001FABB0)(void *ctx);

    /* 001B7840 (op10). */
    int (*w_001AED80)(void *ctx, uint8_t a0);
    int (*w_001AEDB0)(void *ctx, uint8_t a0);

    /* 001B6FA0 (op15). */
    int (*w_001B1380)(void *ctx, const float from[4], const float to[4], float yaw,
                      int32_t *result);
    int (*w_001B1470)(void *ctx, float a0, float *result);

    /* 001B6E40 (op16). */
    int (*w_00182BF0)(void *ctx, uint32_t actor, int32_t *result);

    /* 001B6BF0 (op18). */
    int (*w_001B0C00)(void *ctx, int a0);
    int (*w_001B6250)(void *ctx, uint32_t address);

    /* 001B8020 (op0B) sub 6: 001FBD50(owner, id, a2, radius), the sound
     * before the clip init (census L18: the door program's 0x24DC40). */
    int (*w_001FBD50)(void *ctx, uint32_t actor, int32_t id, int32_t a2, float radius);
} EmAreaScriptWorkers;

struct EmAreaScript {
    EmScript script;            /* owner +0x1F0 .. +0x1FC */
    int16_t st_0E;              /* owner +0x1FE */
    float st_10[4], st_20[4];   /* owner +0x200, +0x210 */
    EmScriptImage *image;       /* borrowed; records are mutated in place */
    const EmAreaScriptWorld *world;
    const EmAreaScriptWorkers *workers;
    /* First fault: the handler/worker address and the record. */
    uint32_t fault_address, fault_pc;
    int faulted;
};

/* Bind an image, world and workers. The block starts inactive. */
void em_area_script_init(EmAreaScript *, EmScriptImage *, const EmAreaScriptWorld *,
                         const EmAreaScriptWorkers *);
/* 001BA1A0: active 1, phase 0, pc = entry, skip byte 0. Returns -1 when the
 * entry record is outside the image (nothing is changed). */
int em_area_script_start(EmAreaScript *, uint32_t entry);
/* One 001BA1F0 call: 0 running, 1 finished (or not active), 3 aborted by
 * the skip path, -1 fault (latched; later calls return -1). */
int em_area_script_tick(EmAreaScript *);

/* 0011E2A8 (SDK sinf) over the range the ease uses: the kernels 0011D770 /
 * 0011CCC8 and the two first-step branches of the reducer 0011C7B0
 * (|x| <= 0x4016CBE3, about 3*pi/4). add.s/sub.s use the EE single-guard-bit
 * model and mul.s truncates (em_pose_math.h): the PCSX2 route captures of
 * the truck preview and director beat 0 camera eases reproduce only under
 * this add model (docs/AREA_SCRIPT.md section 4). Returns -1 and writes
 * nothing outside the translated range. */
int em_area_script_sin_0011E2A8(float x, float *result);
/* The same, shaped as the w_0011E2A8 worker (ctx is unused). */
int em_area_script_w_0011E2A8(void *ctx, float a0, float *result);

#ifdef __cplusplus
}
#endif

#endif
