/* Status scene workers: the status-screen model owners and their static
 * actor pool, and the screen module loader behind 001FF080 (the ITEM root's
 * state-3 load wait). Docs: docs/STATUS_SCENE.md. WP-5 (FIRST_LEVEL_AUDIT)
 * components.
 *
 * Hand translations of the original functions, verified by
 * tools/test_status_scene_reference.py, which executes the ORIGINAL
 * instructions of every translated function from the user's pinned boot ELF
 * and compares every modelled byte and every worker call.
 *
 *   001AFF10  static actor alloc          001AFF90  static actor free
 *   001AFEB0  pool bone release           001AFE60  pool clear
 *   001AF800  a record's bone release     001B0000  pool walk (+0x10 calls)
 *   0020E1E0  letter-model spawn          0020E250  letter sequence
 *   0020E3A0  code -> glyph               0020E460  letter behaviour
 *   0020E6F0  menu player (Dennis)        0020EC80  menu player publish
 *   001F4BF0  menu player glow (D_008104E4 == 1)
 *   001FF080  module-load request         001FF0D0  slot-2 loader task
 *   001FF830  bank streamer (state 0)     001FF3F0  chunk sub-streamer
 *   001FEF70  follow-up bank selector     001AB7D0  task stop
 *
 * The status background 0020A7A0 is not here: the live, oracle-checked
 * translation is em_status_background.c (one D_002655A0 owner).
 *
 * Only the original bytes these functions read or write are modelled; each
 * field names its original address or record offset. Everything they reach
 * outside that is an explicit worker named by original address. A reached
 * NULL worker, a negative worker result, a result outside the original range
 * or an original index outside the modelled storage latches a fault
 * (fail-stop) and the call returns -1; a fault already latched makes every
 * later call return -1 without doing anything.
 *
 * Float arithmetic is the EE COP1 model of docs/EE_FLOAT_MODEL.md through
 * the shared em_ee_float.h (add/sub pre-trim and truncation, truncated mul,
 * nearest div, DAZ/FTZ, saturation, compares on the saturated values).
 *
 * No dependency on any port subsystem beyond em_ee_float.h.
 */
#ifndef EM_STATUS_SCENE_ORIGINAL_H
#define EM_STATUS_SCENE_ORIGINAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original addresses the translation stores or passes as values. */
#define EM_STATUS_SCENE_CB_0020E460 0x0020E460u /* letter behaviour, actor +0x10 */
#define EM_STATUS_SCENE_CB_0020E6F0 0x0020E6F0u /* menu player behaviour */
#define EM_STATUS_SCENE_TASK_001FF0D0 0x001FF0D0u /* slot-2 task function */
#define EM_STATUS_SCENE_D_00289BC0 0x00289BC0u  /* 0x800-byte bank header buffer */
#define EM_STATUS_SCENE_D_0028A480 0x0028A480u  /* header file descriptor */
#define EM_STATUS_SCENE_D_0028A488 0x0028A488u  /* payload file descriptor */
#define EM_STATUS_SCENE_FIXED_BUFFER 0x01800000u /* 001FF830 kind-1 fixed buffer */
#define EM_STATUS_SCENE_D_0028B020 0x0028B020u  /* static actor pool */
#define EM_STATUS_SCENE_D_0024A340 0x0024A340u  /* 0020E460's 'm' marker vector */
#define EM_STATUS_SCENE_SPR_3400 0x70003400u    /* 0020EC80 object matrix */
#define EM_STATUS_SCENE_SPR_3430 0x70003430u    /* its row 3 */
#define EM_STATUS_SCENE_SPR_3440 0x70003440u    /* the axis flip */
#define EM_STATUS_SCENE_SPR_36A0 0x700036A0u    /* copy of the object matrix */
#define EM_STATUS_SCENE_SPR_38A0 0x700038A0u    /* 001F4BF0 position */
#define EM_STATUS_SCENE_SPR_38B0 0x700038B0u    /* 001F4BF0 colour words */
#define EM_STATUS_SCENE_TEX0_001F4BF0 UINT64_C(0x20045B0599421EF0) /* its sprite TEX0 */
/* D_0028A490[] words modelled: 0x28A490 up to the next global D_0028A5A0. */
#define EM_STATUS_SCENE_RELOC_WORDS 68
/* Static actor records (D_0028B020, 0x2F0 bytes): +0x110 bone slots that fit. */
#define EM_STATUS_SCENE_BONE_SLOTS 120
#define EM_STATUS_SCENE_POOL_RECORDS 24
#define EM_STATUS_SCENE_RECORD_SIZE 0x2F0u

/* Fault codes (numerically the EM_SCENE_FAULT_* codes of em_scene_state.h). */
enum {
    EM_STATUS_SCENE_FAULT_NONE = 0,
    EM_STATUS_SCENE_FAULT_NULL_WORKER = 1,   /* reached worker or view is NULL */
    EM_STATUS_SCENE_FAULT_WORKER_FAILED = 2, /* worker returned < 0 */
    EM_STATUS_SCENE_FAULT_BAD_RESULT = 3,    /* worker value outside the original range */
    EM_STATUS_SCENE_FAULT_BAD_INDEX = 4      /* original access outside modelled storage */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_STATUS_SCENE_FAULT_* */
} EmStatusSceneFault;

/* ---------------------------------------------------------------------------
 * Static actor record (D_0028B020[24][0x2F0]): the bytes the translated
 * functions read or write, by original offset. 001AFE60 clears a whole
 * record; here that is every field below. */
typedef struct {
    uint8_t b00;          /* +0x00: in use (001AFF10 writes 2; 001B0000 tests it) */
    uint8_t b01, b02, b03;/* +0x01..+0x03: cleared by 001AFF90 */
    uint8_t b04;          /* +0x04: behaviour state (0 init, 1 run, other free) */
    uint8_t b05;          /* +0x05: 0020E6F0 breathe direction (0 up, else down) */
    uint8_t b06, b07, b08;/* +0x06..+0x08: cleared by 001AFF90 */
    uint8_t b09;          /* +0x09: bone slots held (001AF800 loop count) */
    uint8_t b0A;          /* +0x0A: cleared by 001AFF90 */
    uint8_t b0B;          /* +0x0B: 0020E6F0: 1 while the low-health clip 0xA is bound */
    uint8_t b0C;          /* +0x0C: 001C6150 result (bone count) */
    uint8_t b0D;          /* +0x0D: 0020E1E0 argument (glyph code) */
    uint8_t b0E, b0F;     /* +0x0E, +0x0F: cleared by 001AFF90 */
    uint32_t w10;         /* +0x10: behaviour callback (0x20E460 / 0x20E6F0) */
    uint32_t w14;         /* +0x14: the record's own address (001AFF10) */
    uint16_t h36;         /* +0x36: cleared by 001AFF90 */
    float f38;            /* +0x38: 0020E6F0 breathe scale */
    uint32_t w40;         /* +0x40: D_0028A580 (0020E6F0 state 0) */
    uint32_t w44;         /* +0x44: model word; written by the 001CA6E0 worker */
    uint32_t w4C;         /* +0x4C: draw method; written by the 001CA5F0 worker */
    float f60[4];         /* +0x60..+0x6C: scale (001AFF10: 1.0) */
    float f80[4];         /* +0x80..+0x8C: colour (001AFF10: 1.0; 0020E460: 1.5; 0020E6F0 tint) */
    uint32_t w90;         /* +0x90: cleared by 001AFF90 */
    int16_t h94;          /* +0x94: 001AFF10: -1 */
    uint8_t b98, b99, b9A;/* +0x98 (001AFF90), +0x99/+0x9A (001AFF10) */
    float fB0[3];         /* +0xB0..+0xB8: position */
    float fC0[3];         /* +0xC0..+0xC8: rotation (+0xC4 yaw) */
    uint32_t w110[EM_STATUS_SCENE_BONE_SLOTS]; /* +0x110..+0x2EF: bone slot words */
} EmStatusSceneActor;

typedef struct {
    EmStatusSceneActor record[EM_STATUS_SCENE_POOL_RECORDS]; /* D_0028B020 */
} EmStatusScenePool;

/* The EE scratchpad bytes 0020EC80 writes itself (the SDK routines it calls
 * are workers that read and write the same storage by address). */
typedef struct {
    float s3400[16];     /* 0x70003400: object matrix (row 3 = 0x70003430) */
    float s3440[16];     /* 0x70003440: the -1 diagonal */
    float s36A0[16];     /* 0x700036A0: copy_qw4 of 0x70003400 */
    uint32_t s38A0[4];   /* 0x700038A0: 001F4BF0 position (D_008104E4 == 1) */
    uint32_t s38B0[4];   /* 0x700038B0: 001F4BF0 colour words */
} EmStatusSceneScratch;

/* Globals 0020E6F0 reads. */
typedef struct {
    uint8_t d8104E4;  /* D_008104E4: player variant (0, 1, other) */
    uint8_t d810C60;  /* D_00810C60: costume (1, 2, else base) */
    uint32_t d28A57C; /* D_0028A57C: model, variant 0 base */
    uint32_t d28A580; /* D_0028A580: -> actor +0x40 */
    uint32_t d28A584; /* D_0028A584: model, variant other */
    uint32_t d28A588; /* D_0028A588: model, costume 1 */
    uint32_t d28A58C; /* D_0028A58C: model, costume 2 */
    uint32_t d28A590; /* D_0028A590: model, variant 1 base */
    float view[12];   /* D_00810610..D_0081063C, row-major 3x4 */
    float health;     /* D_00810858: > 35 selects clip 0x1C2 */
    float infection;  /* D_0081085C: tint source */
} EmStatusScenePlayerGlobals;

/* Globals 0020E460 reads. */
typedef struct {
    int16_t d275BCC;        /* D_00275BCC: free bone slots (signed halfword) */
    const uint32_t *d275B40;/* D_00275B40: the current bone array; word 0 is read */
    float view[12];         /* D_00810610..D_0081063C, row-major 3x4 */
} EmStatusSceneLetterGlobals;

/* ---------------------------------------------------------------------------
 * Module loader state (001FF0D0 / 001FF830 / 001FF3F0). The task record is
 * the frame-task slot-2 record (0x28A790): its state byte +0 and its private
 * bytes +8..+0x1F (`user`, user[0] = +8). */
typedef struct {
    uint8_t d282157;          /* D_00282157: nonzero skips the whole dispatch */
    uint8_t d275BD8;          /* D_00275BD8: busy flag; cleared at 0x63 */
    uint8_t spad3B90;         /* 0x70003B90: module 0x2A/0x2B buffer choice */
    uint8_t d810CA4, d810CA6; /* 001FEF70 inputs */
    uint32_t d275C70;         /* D_00275C70: current header (PS2 address) */
    uint32_t d275C74;         /* D_00275C74: destination buffer (PS2 address) */
    uint32_t d28A5A0, d28A738, d28A73C, d28A744, d28A748; /* buffer cursors */
    uint32_t d28A490[EM_STATUS_SCENE_RELOC_WORDS];        /* relocation table */
    uint8_t header[0x800];    /* D_00289BC0 */
} EmStatusSceneLoader;

/* ---------------------------------------------------------------------------
 * Workers, one per original callee. All return >= 0 on success and < 0 on a
 * port failure (fault). Values come back through out-parameters. Scratch
 * and bone addresses are original addresses (0x70003400, a bone slot word
 * + 0x90, ...): the binder maps them to its storage. */
typedef struct {
    void *ctx;
    /* pool */
    int (*w_001AF800_slot)(void *ctx, uint32_t slot);  /* 001AF800: one slot's clear + push */
    int (*w_001CB590)(void *ctx, EmStatusSceneActor *actor, int32_t stride, uint8_t count);
    /* A behaviour/draw pointer call: +0x10 (001B0000) or +0x4C (0020E460,
     * 0020EC80) of `actor`, `fn` = the pointer's value. */
    int (*w_call)(void *ctx, EmStatusSceneActor *actor, uint32_t fn);
    /* 0020E1E0 / 0020E6F0 / 0020E460 */
    int (*w_001AFF10)(void *ctx, EmStatusSceneActor **actor); /* NULL: pool full */
    int (*w_001C6120)(void *ctx, uint32_t bank, int32_t code, uint32_t *model);
    int (*w_001CA6E0)(void *ctx, EmStatusSceneActor *actor, uint32_t model); /* sets +0x44 */
    int (*w_001C6150)(void *ctx, uint32_t word44, uint32_t *value);         /* byte model+8 */
    int (*w_001AF7C0)(void *ctx, uint32_t *value);
    int (*w_001CB5B0)(void *ctx, uint32_t count);                  /* anim_bone_array_setup */
    int (*w_001C63E0)(void *ctx, EmStatusSceneActor *actor, int32_t clip); /* bone_init_default_2 */
    int (*w_001C62C0)(void *ctx, EmStatusSceneActor *actor);       /* bone_init_default_1 */
    int (*w_001CA5F0)(void *ctx, EmStatusSceneActor *actor, int32_t mode); /* sets +0x4C */
    int (*w_001C67E0)(void *ctx, EmStatusSceneActor *actor, int32_t clip, float f12,
                      float f13);                                  /* anim_clip_init */
    int (*w_001C64F0)(void *ctx, EmStatusSceneActor *actor, float t); /* anim_advance_time */
    int (*w_0020EC80)(void *ctx, EmStatusSceneActor *actor);
    int (*w_001AFF90)(void *ctx, EmStatusSceneActor *actor);       /* static-actor free */
    int (*w_001C6380)(void *ctx, EmStatusSceneActor *actor);       /* placement */
    int (*w_001D2040)(void *ctx, int32_t a0, int32_t a1);          /* GS state packet REF */
    /* 0020EC80's SDK VU0 routines, on original addresses; angles are f12 bits. */
    int (*w_001029C0)(void *ctx, uint32_t m);                                   /* identity */
    int (*w_00102B08)(void *ctx, uint32_t dst, uint32_t src, uint32_t angle);   /* rotate x */
    int (*w_00102BB0)(void *ctx, uint32_t dst, uint32_t src, uint32_t angle);   /* rotate y */
    int (*w_00102A60)(void *ctx, uint32_t dst, uint32_t src, uint32_t angle);   /* rotate z */
    int (*w_001026D0)(void *ctx, uint32_t out, uint32_t a, uint32_t b);         /* out = b x a */
    int (*w_001026A0)(void *ctx, uint32_t out, uint32_t m, uint32_t v);         /* out = v x m */
    int (*w_001C69A0)(void *ctx, EmStatusSceneActor *actor);                    /* bone pose */
    /* 001F4BF0 */
    int (*w_00122BB8)(void *ctx, int32_t *value);    /* rand: the shared LCG, 0..0x7FFFFFFF */
    int (*w_001CD520)(void *ctx, int32_t a0, int32_t a1, uint32_t position, uint64_t tex0,
                      uint32_t rgb, float f12, float f13, float f14);
    /* loader */
    int (*w_001FFCD0)(void *ctx, uint8_t user[24]); /* slot +8 == 1: area streamer */
    int (*w_00200360)(void *ctx, uint8_t user[24]); /* slot +8 == 2: bank set streamer */
    /* Start a read: file descriptor address, destination address, byte offset,
     * size. `header` is the D_00289BC0 buffer when buf is 0x289BC0 (the worker
     * fills it before 00200730 reports 1), otherwise NULL. */
    int (*w_00200780)(void *ctx, uint32_t file, uint32_t buf, int32_t offset, int32_t size,
                      uint8_t *header);
    int (*w_00200730)(void *ctx, int32_t *status); /* 0 busy, 1 done, other error */
    int (*w_00200830)(void *ctx, uint32_t address);
    int (*w_001FB370)(void *ctx, uint32_t address, uint32_t *result); /* 0: not yet */
} EmStatusSceneWorkers;

/* ---- the static actor pool D_0028B020 ---- */

/* 001AFF10: the first record with +0x00 == 0, initialised (NULL when all 24
 * are in use). Never faults. */
EmStatusSceneActor *em_status_scene_alloc_001AFF10(EmStatusScenePool *pool);
/* 001AFF90(actor): frees the record its +0x14 names (001AF800 first).
 * Returns 0, or -1 (a +0x14 outside the pool faults). */
int em_status_scene_free_001AFF90(EmStatusScenePool *pool, EmStatusSceneActor *actor,
                                  const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);
/* 001AF800(actor): returns +0x110[0..+9) and clears +9 and +0xC. 0 or -1. */
int em_status_scene_bones_001AF800(EmStatusSceneActor *actor, const EmStatusSceneWorkers *w,
                                   EmStatusSceneFault *fault);
/* 001AFEB0: 001AF800 on every record in use. 0 or -1. */
int em_status_scene_release_001AFEB0(EmStatusScenePool *pool, const EmStatusSceneWorkers *w,
                                     EmStatusSceneFault *fault);
/* 001AFE60: clears all 24 records. */
void em_status_scene_clear_001AFE60(EmStatusScenePool *pool);
/* 001B0000: for each record in use, 001CB590(record, 0x2F0, +9) then the
 * +0x10 call. 0 or -1. */
int em_status_scene_walk_001B0000(EmStatusScenePool *pool, const EmStatusSceneWorkers *w,
                                  EmStatusSceneFault *fault);

/* 0020E3A0: glyph for a status code (-2..21 table, '/' otherwise). */
int32_t em_status_scene_glyph_0020E3A0(int32_t code);

/* 0020E1E0(code). Returns 1 with an actor spawned, 0 when 001AFF10 found
 * no free record (the original then does nothing), -1 on a fault. */
int em_status_scene_letter_0020E1E0(int32_t code, uint32_t bank_28A56C,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 0020E250: the code sequence of D_00810CA4..CA7 through 0020E3A0 and
 * 0020E1E0. ca[0..3] = D_00810CA4..D_00810CA7. Returns 0, or -1. */
int em_status_scene_letters_0020E250(const uint8_t ca[4], uint32_t bank_28A56C,
                                     const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 0020E6F0(actor). Returns 1 while allocated, 0 after the 001AFF90 free,
 * -1 on a fault. */
int em_status_scene_player_0020E6F0(EmStatusSceneActor *actor, const EmStatusScenePlayerGlobals *g,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 0020E460(actor): the letter behaviour. Returns 1 while allocated (states
 * 0 and 1), 0 after the 001AFF90 free, -1 on a fault. */
int em_status_scene_letter_0020E460(EmStatusSceneActor *actor, const EmStatusSceneLetterGlobals *g,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 0020EC80(actor): the menu player's object matrix and draw. d8104E4 is
 * D_008104E4. Returns 0, or -1. */
int em_status_scene_publish_0020EC80(EmStatusSceneActor *actor, uint8_t d8104E4,
                                     EmStatusSceneScratch *spr, const EmStatusSceneWorkers *w,
                                     EmStatusSceneFault *fault);

/* 001F4BF0(position, colour): the rand-pulsed sprite 001CD520 at `position`
 * (an original address) with colour words colour[0..3]. Returns 0, or -1. */
int em_status_scene_glow_001F4BF0(uint32_t position, const uint32_t colour[4],
                                  const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 001FF080(state, module) on the slot-2 record: 001AB740's record writes
 * (state byte 1, +8..+0x17 cleared; +0x18..+0x1F kept; the function word
 * +4 = 001FF0D0 is the caller's), then +8 = state, +0xE = module. */
void em_status_scene_loader_request_001FF080(uint8_t *slot_state, uint8_t user[24],
                                             uint8_t state, uint8_t module);

/* One 001FF0D0 call (the slot-2 task body; the dispatcher 001AB6A0 calls it
 * once per frame while the slot state is 2). Returns 0, or -1. */
int em_status_scene_loader_001FF0D0(uint8_t *slot_state, uint8_t user[24], EmStatusSceneLoader *ld,
                                    const EmStatusSceneWorkers *w, EmStatusSceneFault *fault);

/* 001FEF70 over D_00810CA4/CA6: 0x35/0x32/0x33/0x34 or -1. */
int32_t em_status_scene_bank_001FEF70(uint8_t ca4, uint8_t ca6);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATUS_SCENE_ORIGINAL_H */
