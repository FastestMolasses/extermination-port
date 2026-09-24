/* em_startup_load_gaps.h - translations of the title / New Game / load /
 * opening originals of census lane L34 (docs/FIRST_LEVEL_CENSUS.md,
 * docs/STARTUP_LOAD_GAPS.md).
 *
 * Translations of the original routines, read from the decomp C where it is
 * byte-matched and from the split listing for the asm-word and NEARMISS
 * units (001AF470, 001B5F40, 001BB0E0, 00199C50, 001FB3E0, 001FB910):
 *
 *   001AB4E0  double-buffer display environments (main loop step R)
 *   001AB590  DMA CHCR address-stack watchdog (main loop step L)
 *   001AC070  the title / continue screen-flow task
 *   001AF470  pad button-assignment block (spad 0x70003B74..0x70003B82)
 *   001AF5C0  player record reset          001AF690  status block reset
 *   001AF710  bone-slot stack init         001AFCA0  the state-0 re-arm
 *   001B0F60  script-driven node start (called by 001BBDA0)
 *   001B57E0  pad read + clear (main loop step C), with 001B5F40 (the
 *             libpad connection state machine) and 001B62A0 (its reset)
 *   001BB0E0  the opening-script actor behaviour (001BAC00 installs it)
 *   001FB100  sound output-mode commit + 001FC6E0 (main loop step H)
 *   001FC6E0  the ten delayed sound cues
 *   001FB370  sound-bank load gate, 001FB3E0 (the bank upload state
 *             machine) and 001FB910 (its SIF DMA kick)
 *   008237C0  the AREA11 overlay init (splat 00823780 + the 0x40 header)
 *   00199C50  collision table set-up in the scratchpad
 *
 * The task table itself (001AB6A0 / 001AB740 / 001AB790) is em_task.c; its
 * oracle lives in tools/test_startup_load_gaps_reference.py too.
 *
 * Conventions (house style of em_player_stage_workers.h):
 *   - Every original callee that is not translated here is an explicit
 *     worker. A worker returns a negative value on a fault; results come
 *     back through out-parameters. Each routine checks, before its first
 *     write, that every worker it can reach is bound, and returns -1
 *     otherwise (fail-stop). A worker fault returns -1 at once; the writes
 *     made before it stay, in the original order.
 *   - Storage is named by original address. Byte images (uint8_t arrays)
 *     hold original bytes at their original offsets, little-endian; words
 *     that hold EE addresses stay 32-bit EE addresses.
 *   - Routines that are pure memory in the original (the C runtime memset
 *     00121A28 and block copy 00121870) use the host memset / memcpy.
 *
 * Oracle: tools/test_startup_load_gaps_reference.py executes the original
 * instructions over synthetic states and the captured route RAM and
 * compares every written byte, every worker call (order and arguments) and
 * every return value. */
#ifndef EM_STARTUP_LOAD_GAPS_H
#define EM_STARTUP_LOAD_GAPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * 001AB4E0 - the two display environments (D_00810EA0, D_00810EC8)
 * ====================================================================== */

#define EM_SLG_DISPENV_SIZE 0x28          /* D_00810EC8 - D_00810EA0 */
#define EM_SLG_DISPFB_OFFSET 0x10         /* D_00810EB0 - D_00810EA0 */

typedef struct EmSlgDisplayWorkers {
    void *ctx;
    /* 001002E0 (SDK display-environment set-up, a boundary): env, psm,
     * width, height, dx, dy exactly as the original passes them. */
    int (*w_001002E0)(void *ctx, uint8_t *env, int32_t psm, int32_t width,
                      int32_t height, int32_t dx, int32_t dy);
} EmSlgDisplayWorkers;

/* env[0] = D_00810EA0, env[1] = D_00810EC8. dx, dy = the two scratchpad
 * halfwords 0x70003B94 / 0x70003B96 the main loop passes (sign-extended). */
int em_slg_001AB4E0(const EmSlgDisplayWorkers *w,
                    uint8_t env[2][EM_SLG_DISPENV_SIZE], int32_t dx, int32_t dy);

/* ======================================================================
 * 001AB590 - DMA CHCR ASP watchdog
 * ====================================================================== */

/* chcr_byte0[0..2] = byte 0 of D0/D1/D2 CHCR (0x10008000 / 0x10009000 /
 * 0x1000A000). The native port has no DMAC; see the doc for the binding. */
void em_slg_001AB590(uint8_t chcr_byte0[3]);

/* ======================================================================
 * 001AC070 - the title / continue screen-flow task
 * ====================================================================== */

#define EM_SLG_FN_001ACEC0 0x001ACEC0u   /* the game task 001AC070 installs */

typedef struct EmSlgFlowGlobals {
    int32_t d275BD4;    /* D_00275BD4: retry counter (wraps after 2) */
    uint8_t d275BDC;    /* D_00275BDC: from-death flag, read by state 0 */
    uint8_t d275BE0;    /* D_00275BE0 */
    uint8_t s3B90;      /* spad 0x70003B90: cleared on every tick */
} EmSlgFlowGlobals;

typedef struct EmSlgFlowWorkers {
    void *ctx;
    int (*w_001AEDB0)(void *ctx, int32_t a0);              /* fade */
    int (*w_001D1EF0)(void *ctx);
    int (*w_001AC3B0)(void *ctx, int32_t *ret);            /* state 1 wait */
    int (*w_001AC480)(void *ctx, int32_t *ret);            /* the title prompt */
    int (*w_00225A00)(void *ctx);
    int (*w_001ACA20)(void *ctx, int32_t *ret);            /* anim_frame_top_a */
    int (*w_001FBC50)(void *ctx);
    int (*w_001D2880)(void *ctx);
    int (*w_001AB790)(void *ctx, uint32_t fn);             /* replace the task */
    int (*w_00225AC0)(void *ctx, int32_t a0, int32_t *ret);
    int (*w_001AF150)(void *ctx);
    int (*w_00200A40)(void *ctx, int32_t *ret);
    int (*w_001D2830)(void *ctx, int32_t a0, int32_t a1);
} EmSlgFlowWorkers;

/* One tick. user[k - 8] is byte +k of the running task's 0x20-byte record
 * (the record the scratchpad cursor 0x70003B6C addresses); with em_task.h
 * that is EmTask.user. Only +8, +9, +0xA, +0xC, +0xE and +0xF are touched
 * here; w_001AB790 rewrites the record itself (state 4). */
int em_slg_001AC070(const EmSlgFlowWorkers *w, EmSlgFlowGlobals *g, uint8_t *user);

/* ======================================================================
 * 001AF470 - the pad button-assignment block
 * ====================================================================== */

/* map[i] = the halfword at spad 0x70003B74 + 2 * i (i = 0..7). Only the
 * low byte of `config` is used; a value other than 0, 1 or 2 writes nothing. */
void em_slg_001AF470(uint16_t map[8], int32_t config);

/* ======================================================================
 * 001AFCA0 - the state-0 re-arm: 001AF5C0, 001AF690, 001AF710,
 * 001AF8E0, 001D0660, spad 31F4 = 0
 * ====================================================================== */

#define EM_SLG_PLAYER_BASE 0x008102B0u     /* D_008102B0 */
#define EM_SLG_PLAYER_SIZE 0x320
#define EM_SLG_STATUS_BASE 0x00810130u     /* D_00810130 .. D_008102AF */
#define EM_SLG_STATUS_SIZE 0x180
#define EM_SLG_BONE_SLOTS 0x480            /* 001AF710 loop bound */
#define EM_SLG_BONE_SLOT_SIZE 0xD0         /* 13 quadwords */
#define EM_SLG_BONE_RECORDS 0x007D5840u    /* D_007D5840 */
#define EM_SLG_BONE_ARRAY 0x007D4640u      /* D_007D4640 */

/* The same views em_roger_actor_original.h's EmRogerActorWorld takes
 * (slots/slots_base, slot_stack/slot_stack_base, d00275BD0, d00275BCC):
 * slot words and the cursor are EE address words. */
typedef struct EmSlgBoneSlots {
    uint8_t *records;                           /* D_007D5840: 0x480 * 0xD0 bytes (caller storage) */
    uint32_t slot[EM_SLG_BONE_SLOTS];           /* D_007D4640[]: record address words */
    uint32_t head;                              /* D_00275BD0: the stack cursor (address word) */
    int16_t count;                              /* D_00275BCC */
} EmSlgBoneSlots;

typedef struct EmSlgState0 {
    uint8_t *player;           /* D_008102B0: the 0x320-byte player record */
    uint32_t player_self;      /* the word 001AF5C0 stores at player +0x14
                                * (the original stores D_008102B0 itself) */
    uint8_t *status;           /* D_00810130: 0x180 bytes (0xA0 + 0x10 + 0xD0) */
    uint32_t *d81060C;         /* D_0081060C: a float word */
    EmSlgBoneSlots *bones;
    uint32_t *s31F4;           /* spad 0x700031F4 */
} EmSlgState0;

typedef struct EmSlgState0Workers {
    void *ctx;
    int (*w_001D8BF0)(void *ctx, uint8_t *player, int32_t a1);
    int (*w_001AF8E0)(void *ctx);                           /* actor pool reset */
    int (*w_001D0660)(void *ctx);                           /* 001F0310 + 001E7780 */
} EmSlgState0Workers;

int em_slg_001AF5C0(const EmSlgState0Workers *w, EmSlgState0 *s);
void em_slg_001AF690(EmSlgState0 *s);
void em_slg_001AF710(EmSlgBoneSlots *b);
int em_slg_001AFCA0(const EmSlgState0Workers *w, EmSlgState0 *s);

/* ======================================================================
 * 001B0F60 - script-driven node start (001BBDA0's kickoff)
 * ====================================================================== */

typedef struct EmSlgNodeStartWorkers {
    void *ctx;
    int (*w_001B0EA0)(void *ctx, uint8_t *node, int32_t *ret);
    int (*w_001C63E0)(void *ctx, uint8_t *node, int32_t n);   /* bone_init_default_2 */
} EmSlgNodeStartWorkers;

/* node = the record's byte image (+0x04 and +0x40 are written). d28A574 =
 * the word at D_0028A574. *ret = the original's return value (0 or 1). */
int em_slg_001B0F60(const EmSlgNodeStartWorkers *w, uint8_t *node, int32_t n,
                    uint32_t d28A574, int32_t *ret);

/* ======================================================================
 * 001B57E0 / 001B5F40 / 001B62A0 - the pad block D_00810E40
 * ====================================================================== */

#define EM_SLG_PAD_BASE 0x00810E40u
#define EM_SLG_PAD_SIZE 0x3C               /* D_00810E40 .. D_00810E7B */
#define EM_SLG_PAD_OUT 0x30                /* D_00810E70 - D_00810E40 */

typedef struct EmSlgPadWorkers {
    void *ctx;
    /* libpad (a boundary): the arguments exactly as the original passes them */
    int (*w_00110B80)(void *ctx, int32_t port, int32_t slot, int32_t *ret);
    int (*w_00110E58)(void *ctx, int32_t port, int32_t slot, int32_t a2, int32_t a3, int32_t *ret);
    int (*w_00110F60)(void *ctx, int32_t port, int32_t slot, int32_t a2, int32_t a3, int32_t *ret);
    int (*w_001110B0)(void *ctx, int32_t port, int32_t slot, uint8_t *data, int32_t *ret);
    /* 001B5940 (em_input.c em_pad_unpack): out = block + 0x30, pad = block */
    int (*w_001B5940)(void *ctx, uint8_t *out, uint8_t *pad, int32_t analog, int32_t *ret);
} EmSlgPadWorkers;

void em_slg_001B62A0(uint8_t *pad);
int em_slg_001B5F40(const EmSlgPadWorkers *w, uint8_t *out, uint8_t *pad, int32_t *ret);
int em_slg_001B57E0(const EmSlgPadWorkers *w, uint8_t block[EM_SLG_PAD_SIZE]);

/* ======================================================================
 * 001BB0E0 - the opening-script actor behaviour
 * ====================================================================== */

typedef struct EmSlgScriptActor {
    uint8_t *node;           /* the record's byte image: +0x01, +0x04, +0x2E */
    const uint8_t *entry;    /* what node +0x20 points to: the 0x2C-byte spawn
                              * entry (001BAC00); +0x04 s16, +0x0A s16, +0x0C float */
    const uint8_t *owner;    /* what node +0x24 points to: +0x2E u16 is read */
} EmSlgScriptActor;

typedef struct EmSlgScriptActorWorkers {
    void *ctx;
    int (*w_001BAD40)(void *ctx, const EmSlgScriptActor *a, int32_t *ret);
    int (*w_001C5C90)(void *ctx, const EmSlgScriptActor *a);
    int (*w_001C68C0)(void *ctx, const EmSlgScriptActor *a);
    int (*w_001BA580)(void *ctx, const EmSlgScriptActor *a, int32_t a1);
    int (*w_001C64F0)(void *ctx, const EmSlgScriptActor *a, uint32_t dt_bits);  /* anim_advance_time */
    int (*w_001F9660)(void *ctx, const EmSlgScriptActor *a, int32_t key);
    int (*w_001BA540)(void *ctx, const EmSlgScriptActor *a);
    int (*w_001AFC10)(void *ctx, const EmSlgScriptActor *a);                   /* free */
    int (*w_method_4C)(void *ctx, const EmSlgScriptActor *a);                  /* jalr *(node + 0x4C) */
} EmSlgScriptActorWorkers;

int em_slg_001BB0E0(const EmSlgScriptActorWorkers *w, const EmSlgScriptActor *a);

/* ======================================================================
 * 001FB100 / 001FC6E0 - per-frame sound bookkeeping (main loop step H)
 * ====================================================================== */

#define EM_SLG_CUES 10

typedef struct EmSlgSoundFrame {
    uint8_t d821058;            /* D_00821058: 1 = the movie driver owns the frame */
    uint8_t d28215B;            /* D_0028215B: committed output mode */
    uint8_t d81011C;            /* D_0081011C: requested output mode */
    int32_t d281FD4;            /* D_00281FD4: shift count of the first channel mask */
    int32_t d2820F4;            /* D_002820F4: shift count of the second */
    uint8_t d281B70[0x180];     /* D_00281B70 .. D_00281CEF: source 0xC0 + copy 0xC0 */
    int32_t d281F30[EM_SLG_CUES][4];   /* D_00281F30: {delay, cue, a2, a3} */
} EmSlgSoundFrame;

typedef struct EmSlgSoundWorkers {
    void *ctx;
    int (*w_001F9CF0)(void *ctx, int32_t mode);
    int (*w_00119870)(void *ctx, int32_t a0);
    int (*w_0011A608)(void *ctx, uint64_t mask, int32_t a1, int32_t a2);
    int (*w_001FB9F0)(void *ctx, int32_t cue, int32_t a1, int32_t a2, int32_t a3);
} EmSlgSoundWorkers;

int em_slg_001FC6E0(const EmSlgSoundWorkers *w, EmSlgSoundFrame *s);
int em_slg_001FB100(const EmSlgSoundWorkers *w, EmSlgSoundFrame *s);

/* ======================================================================
 * 001FB370 / 001FB3E0 / 001FB910 - the sound-bank upload
 * ====================================================================== */

#define EM_SLG_BANK_BUCKETS 5          /* D_00264890: 5 base words */
#define EM_SLG_BANK_COUNTS 8           /* D_00281D30 .. D_00281D4F */
#define EM_SLG_BANK_HANDLE_WORDS 120   /* D_00281D50 .. D_00281F2F (0x50 per bucket) */

typedef struct EmSlgBankFile {
    uint32_t address;          /* the EE address the loader was handed (a0) */
    const uint8_t *bytes;      /* the file as loaded at that address */
    uint32_t size;
} EmSlgBankFile;

typedef struct EmSlgBankLoad {
    int8_t d282150;            /* D_00282150: outer state (001FB370) */
    int8_t d282151;            /* D_00282151: inner state (001FB3E0) */
    int8_t d282159;            /* D_00282159: records done */
    int8_t d28215C;            /* D_0028215C: 001FB910 state */
    int32_t d282190;           /* D_00282190: current bucket (0x63 = none) */
    int32_t d282194;           /* D_00282194: scratch buffer handle */
    int32_t d282198;           /* D_00282198: running destination */
    uint32_t d28219C;          /* D_0028219C: source EE address */
    int32_t d2821A0;           /* D_002821A0: SIF DMA id */
    uint32_t d2821A4;          /* D_002821A4: entry cursor (EE address) */
    int32_t d281D30[EM_SLG_BANK_COUNTS];        /* per-bucket handle counts */
    int32_t d281D50[EM_SLG_BANK_HANDLE_WORDS];  /* handles, 20 per bucket */
    int32_t d275B18;           /* D_00275B18: timeout */
    int32_t d275B1C;           /* D_00275B1C: busy flag */
    int32_t base[EM_SLG_BANK_BUCKETS];          /* D_00264890 (from the user's ELF) */
} EmSlgBankLoad;

typedef struct EmSlgBankWorkers {
    void *ctx;
    int (*w_001195A8)(void *ctx, int32_t handle);                     /* release */
    int (*w_0010F8F8)(void *ctx, int32_t size, int32_t *ret);         /* IOP alloc */
    int (*w_00119450)(void *ctx, int32_t a0, int32_t *ret);           /* poll */
    int (*w_001194B8)(void *ctx, int32_t a0, uint32_t a1, int32_t a2, int32_t *ret);
    int (*w_001199F0)(void *ctx, int32_t handle, int32_t a1);
    int (*w_0010F968)(void *ctx, int32_t handle);                     /* IOP free */
    /* 001FB910's kernel calls (syscall stubs): 0x78 at 0010BC00, 0x64 at
     * 0010BAA0, 0x77 at 0010BBE0 (desc = {src, dst, size, 0}, count 1),
     * 0x76 at 0010BBC0 */
    int (*w_0010BC00)(void *ctx);
    int (*w_0010BAA0)(void *ctx, int32_t a0);
    int (*w_0010BBE0)(void *ctx, const uint32_t desc[4], int32_t count, int32_t *ret);
    int (*w_0010BBC0)(void *ctx, int32_t id, int32_t *ret);
} EmSlgBankWorkers;

int em_slg_001FB910(const EmSlgBankWorkers *w, EmSlgBankLoad *s, uint32_t src,
                    int32_t dst, int32_t size, int32_t *ret);
/* *ret = 0 while working, else the 0x40-aligned EE address past the payload. */
int em_slg_001FB3E0(const EmSlgBankWorkers *w, EmSlgBankLoad *s, const EmSlgBankFile *f,
                    uint32_t *ret);
int em_slg_001FB370(const EmSlgBankWorkers *w, EmSlgBankLoad *s, const EmSlgBankFile *f,
                    uint32_t *ret);

/* ======================================================================
 * 008237C0 - AREA11 overlay init (splat func_overlay_AREA11_00823780)
 * ====================================================================== */

typedef struct EmSlgOverlayInit {
    uint32_t d275C1C;          /* D_00275C1C: the per-level record base */
    uint32_t d275C24;          /* D_00275C24 */
    uint32_t d275C28;          /* D_00275C28 */
    uint32_t d275C2C;          /* D_00275C2C: record count */
} EmSlgOverlayInit;

#define EM_SLG_AREA11_RECORDS 0x0082AD00u   /* D_overlay_AREA11_0082AD00 */

void em_slg_008237C0(EmSlgOverlayInit *g);

/* ======================================================================
 * 00199C50 - the collision tables in the scratchpad
 * ====================================================================== */

/* spad[i] = the word at spad 0x700031F8 + 4 * i, i = 0..0x16 (0x700031F8 ..
 * 0x70003253). 0x7000324C is a halfword: its bytes are the low half of
 * spad[0x15]. */
#define EM_SLG_COLL_SPAD_BASE 0x700031F8u
#define EM_SLG_COLL_SPAD_WORDS 0x17

typedef struct EmSlgCollFile {
    uint32_t address;          /* D_0028A598: the EE address of the file */
    const uint8_t *bytes;
    uint32_t size;
    uint32_t d28A5A8;          /* D_0028A5A8: EE address of the halfword it reads */
    int16_t d28A5A8_value;     /* the halfword at that address */
} EmSlgCollFile;

int em_slg_00199C50(const EmSlgCollFile *f, uint32_t spad[EM_SLG_COLL_SPAD_WORDS]);

#ifdef __cplusplus
}
#endif

#endif /* EM_STARTUP_LOAD_GAPS_H */
