/* em_player_stage_workers.h - the workers the player stage (0015BA50 /
 * 0015B130 / 0015B770, em_player_floor.h) calls every frame, and the +4 = 4
 * handler (docs/PLAYER_STAGE_WORKERS.md).
 *
 * Translations of the original routines, not models of them:
 *   D_00248C98  the clip-rate column of D_00248C90 (exported data + loader)
 *   001C64F0    anim_advance_time            (EmPlayerStageWorkers.advance)
 *   001281C0    float_to_int (soft-float __fixsfsi; 001278C0 unpack)
 *   00183090    scripted clip commit         (.commit)
 *   0021C440    damage / hit reaction        (.reaction) and its own callees
 *               0021BB00, 0021BC40, 0021C200, 0021C270, 0021C350, 0021C3F0,
 *               0021D4E0, 0021D640, 0021D6C0, 0017C370, 001B1470
 *   0015D100    passive health drain         (.drain), with 001B0070
 *   0015D000    low-health heartbeat         (.heartbeat)
 *   00182B30    scripted takeover check      (.scripted_check)
 *   00182D70    scripted takeover notify     (.scripted_notify)
 *   00174A50    idle row request             (.row_request)
 *   0011A070    loop-sound stop              (.stop_sound): the argument
 *               decode; the body is em_sfx_driver_stop (em_sfx_bank.c,
 *               verified by tools/test_area11_sfx_reference.py)
 *   0015B530    the +4 = 4 handler           (.major[4])
 *
 * Every routine works on the raw 0x320-byte player record (EmPlayerLiveActor)
 * by its original offsets. Every original callee that is not translated here
 * is an explicit worker in EmPlayerStageCallees. Each routine checks, before
 * its first write, that every worker it can reach is bound, and faults (-1)
 * otherwise; a worker that returns a negative value is a fault too, and the
 * writes made before it stay, as the original order leaves them. Arithmetic
 * and float compares follow the EE COP1 model (em_ee_float.h,
 * docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_stage_workers_reference.py executes the original
 * instructions (COP1 through tools/ee_float_model.py) over synthetic and
 * captured AREA11 player records and compares all 0x320 record bytes, the
 * globals and every callee call (order and arguments). */
#ifndef EM_PLAYER_STAGE_WORKERS_H
#define EM_PLAYER_STAGE_WORKERS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_player_floor.h"

/* ---- D_00248C98: the clip-rate column ----------------------------------
 * D_00248C90 is 459 rows of 12 bytes, one per clip of the player's clip
 * bank (the bank's count word is 459); the float at +8 of row +20C is the
 * rate 0015BA50 multiplies by +204. tools/export_player_tables.py writes
 * the column from the user's ELF into the ignored assets/ (never committed):
 *   "EMCR", u32 version 1, u32 count, then count little-endian floats. */
#define EM_PLAYER_CLIP_RATE_ROWS 459
#define EM_PLAYER_CLIP_RATE_PATH "assets/player_clip_rates.emcr"

typedef struct EmPlayerClipRates {
    uint32_t count;
    float rate[EM_PLAYER_CLIP_RATE_ROWS];
} EmPlayerClipRates;

/* 0, or -1 when the data is not an EMCR v1 file of exactly 459 rows. */
int em_player_clip_rates_parse(EmPlayerClipRates *out, const uint8_t *data, size_t size);
int em_player_clip_rates_load(EmPlayerClipRates *out, const char *path);

/* ---- Globals the workers read or write outside the record --------------
 * 0x70003B8D, 0x70003B8F, D_00810700 and D_008106F1 are shared with the
 * stage (EmPlayerStageScene, em_player_floor.h): 00183090 reads 0x70003B8F,
 * 00182D70 writes it, 0015B530 reads 0x70003B8D, 0021C3F0 reads D_00810700,
 * 00182B30 / 0021C200 / 0021C440 read D_008106F1 and 0021C270 writes it
 * (through the stage scene's pointer at the one canonical byte).
 * D_00810707 is a pointer at its canonical progress byte (D2), which
 * 0015CF90 and 0021E830 also write; a host without it (or without the stage
 * scene's D_008106F1) is not ready: every worker refuses before its first
 * write. */
typedef struct EmPlayerStageGlobals {
    int32_t d8106C8;   /* D_008106C8 word: 001B0070's value; 0015D100 tests & 4 and & 0x60 */
    uint8_t d810701;   /* D_00810701 (0021C3F0) */
    uint8_t d810770;   /* D_00810770 (0021C3F0) */
    uint8_t d81083C;   /* D_0081083C (0021C440: the +5 = 0xB reaction) */
    uint8_t d810C7E;   /* D_00810C7E (0015D100) */
    uint32_t spad3A20; /* 0x70003A20 scratch word: 0021C440 / 0021D6C0 store the atan2 there */
    uint8_t *d810707;  /* D_00810707: 0021C270 writes 1 */
} EmPlayerStageGlobals;

/* What anim_advance_time reads from the header anim_clip_resolve leaves in
 * D_00275BF8 (the clip-bank header of the resolved clip). */
typedef struct EmPlayerClipHeader {
    uint16_t frames;           /* +2 (lhu) */
    int16_t next;              /* +4: -2 hold, -1 loop, else the follow-on clip */
    int16_t start;             /* +6: the follow-on clip's start frame */
    uint32_t events;           /* +0x14: event-table offset, 0 = none */
    int16_t event_count;       /* *(short *)(header + events) */
    const int16_t *event_pairs;/* header + events + 4: {frame, flags} x event_count */
} EmPlayerClipHeader;

/* The original callees that are not translated here. Each returns 0, or a
 * negative value on a fault. `actor` is the record the original passes. */
typedef struct EmPlayerStageCallees {
    void *context;
    /* 00183090 */
    int (*w001D0C70)(void *context);                                          /* 0x70003B8F == 2 */
    int (*bone_init)(void *context, EmPlayerLiveActor *actor, int clip);      /* bone_init_default_2 001C63E0 */
    int (*clip_init)(void *context, EmPlayerLiveActor *actor, int clip, float blend,
                     float frame);                                            /* anim_clip_init 001C67E0 */
    /* 001C64F0. `bank` is the record word +40, `skeleton` the word +110. */
    int (*clip_resolve)(void *context, uint32_t bank, int clip,
                        EmPlayerClipHeader *header);                          /* anim_clip_resolve 001C8480 */
    int (*skeleton_frame)(void *context, uint32_t skeleton, int16_t *frame);  /* *(short *)(*(p+110) + 8E) */
    int (*w001C8710)(void *context, EmPlayerLiveActor *actor, int nodes, float frame);   /* (p+110, +C, f) */
    int (*w001C87C0)(void *context, EmPlayerLiveActor *actor, int nodes, float step);    /* (p+110, +C, f) */
    int (*sample_bones)(void *context, EmPlayerLiveActor *actor, int nodes, float a,
                        float b);                                             /* anim_sample_bones 001C8D50 */
    /* 0021C440 and its callees */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int a2, float radius); /* 001FBD50 */
    int (*cue)(void *context, int a0, int a1, int a2, int a3);                /* 001B61C0 rumble */
    int (*w001EFE00)(void *context, uint32_t id, EmPlayerLiveActor *actor);
    /* 001F00A0(id, p+B0, p+C0, a3): *record is the returned effect record
     * (NULL for 0), whose bytes +D0..+10F the caller then writes. */
    int (*w001F00A0)(void *context, uint32_t id, EmPlayerLiveActor *actor, int a3,
                     uint8_t **record);
    int (*w001F0060)(void *context, uint32_t id, int a1);
    float (*atan2)(void *context, float y, float x);                          /* SDK 0011E620 */
    /* The words +C0 and +C8 of the object the record word +20 points to. */
    int (*link20)(void *context, uint32_t word, uint32_t *c0, uint32_t *c8);
    int (*clip_lookup)(void *context, EmPlayerLiveActor *actor, int a1, int a2, int a3,
                       int16_t *clip);                                        /* 0017B490 */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags,
                   float blend);                                              /* 001749A0 */
    /* 0015D100 */
    int (*w0015C9D0)(void *context, EmPlayerLiveActor *actor);
    /* 00182D70: the byte store *(u8 *)(*(p+1C) + 4) = value (word != 0). */
    int (*link1C)(void *context, uint32_t word, uint8_t value);
    /* 0011A070's body over the SFX driver: em_sfx_driver_stop(track, hard). */
    int (*sound_stop)(void *context, int track, int hard);
} EmPlayerStageCallees;

/* The context of every EmPlayerStageWorkers entry bound by
 * em_player_stage_workers_bind. */
typedef struct EmPlayerStageHost {
    EmPlayerStageScene *stage;        /* shared with em_player_stage_* */
    EmPlayerStageGlobals *globals;
    const EmPlayerClipRates *rates;
    EmPlayerStageCallees callees;
} EmPlayerStageHost;

/* EmPlayerStageWorkers.context = host and clip_rate / advance / commit /
 * reaction / drain / heartbeat / scripted_check / scripted_notify /
 * row_request / stop_sound = the translations below. The callbacks
 * (major[], state[], ...) are left as they are. */
void em_player_stage_workers_bind(EmPlayerStageWorkers *workers, EmPlayerStageHost *host);

/* The EmPlayerStageWorkers entries (context = EmPlayerStageHost). */
int em_player_stage_clip_rate(void *host, int clip, float *rate);            /* D_00248C98[clip*3] */
int em_player_stage_anim_advance(void *host, EmPlayerLiveActor *actor, float step,
                                 uint32_t *flags);                           /* 001C64F0 */
int em_player_stage_commit(void *host, EmPlayerLiveActor *actor, int *result); /* 00183090 */
int em_player_stage_reaction(void *host, EmPlayerLiveActor *actor, int *result); /* 0021C440 */
int em_player_stage_drain(void *host, EmPlayerLiveActor *actor);             /* 0015D100 */
int em_player_stage_heartbeat(void *host, EmPlayerLiveActor *actor);         /* 0015D000 */
int em_player_stage_scripted_check(void *host, EmPlayerLiveActor *actor, int *result); /* 00182B30 */
int em_player_stage_scripted_notify(void *host, EmPlayerLiveActor *actor);   /* 00182D70 */
int em_player_stage_row_request(void *host, EmPlayerLiveActor *actor, float blend); /* 00174A50 */
int em_player_stage_stop_sound(void *host, int handle);                      /* 0011A070 */

/* 0021C440's own callees, exported for the +4 = 2 states that call them.
 * They cast their context to the stage host, while EmPlayerMajor2Workers
 * shares one `context` across all its workers, so the coordinator binds
 * them through a one-line adapter from the Major2 context to the stage
 * host (not directly). D_008106F1 / D_00810707 are pointers in the stage
 * scene, the stage globals and EmPlayerMajor2Scene: bind all of them to the
 * one canonical byte (em_scene_req_at 0x008106F1, em_scene_progress_at
 * 0x00810707). */
int em_player_0021C200(void *host, EmPlayerLiveActor *actor);
int em_player_0021C270(void *host, EmPlayerLiveActor *actor);
int em_player_0021C350(void *host, EmPlayerLiveActor *actor);
int em_player_0021D4E0(void *host, EmPlayerLiveActor *actor);
int em_player_0021D6C0(void *host, EmPlayerLiveActor *actor, int *result);
int em_player_0017C370(void *host, EmPlayerLiveActor *actor);
/* Leaf predicates over the record (and the scene for 0021C3F0). */
int em_player_0021BB00(const EmPlayerLiveActor *actor);
int em_player_0021BC40(const EmPlayerLiveActor *actor);
int em_player_0021D640(const EmPlayerLiveActor *actor);
int em_player_0021C3F0(const EmPlayerStageScene *stage, const EmPlayerStageGlobals *globals);
/* 001B1470 (angle wrap into (-pi, pi]) and 001281C0 (float_to_int), bit
 * patterns in and out. */
uint32_t em_player_001B1470(uint32_t angle);
int32_t em_player_float_to_int(uint32_t value);

/* ---- 0015B530: the +4 = 4 handler (EmPlayerStageWorkers.major[4]) -------
 * 0x70003B8D == 0: 00182DF0(p). Otherwise by +5: 0 -> 001837A0, 1 ->
 * 001837B0, 5 -> 00162DB0, 8 -> 00163B40, 0xC -> 001838B0, 0x17 ->
 * 00183910; any other +5 calls nothing. */
enum {
    EM_PLAYER_MAJOR4_00182DF0, EM_PLAYER_MAJOR4_001837A0, EM_PLAYER_MAJOR4_001837B0,
    EM_PLAYER_MAJOR4_00162DB0, EM_PLAYER_MAJOR4_00163B40, EM_PLAYER_MAJOR4_001838B0,
    EM_PLAYER_MAJOR4_00183910, EM_PLAYER_MAJOR4_COUNT
};
typedef struct EmPlayerStageMajor4 {
    const EmPlayerStageScene *stage;
    EmPlayerStateCallback routine[EM_PLAYER_MAJOR4_COUNT];
    void *routine_context[EM_PLAYER_MAJOR4_COUNT];
} EmPlayerStageMajor4;
int em_player_stage_0015B530(void *major4, EmPlayerLiveActor *actor);

#endif
