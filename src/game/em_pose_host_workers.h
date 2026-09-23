/* em_pose_host_workers.h - the original animation-clip and skeleton routines
 * every player state calls, as bindable workers over the raw records
 * (docs/POSE_HOST_WORKERS.md).
 *
 * Translations of the original routines, not models of them:
 *   001749A0            clip request (same-clip gate, then anim_clip_init)
 *   001749F0            anim_clip_arbiter (init first, then the +20C compare)
 *   001C6120            clip-bank directory lookup
 *   001C61D0            clip frame count (sets D_00275BF8)
 *   001C8480            anim_clip_resolve (D_00275BF8/BF4/BF0/BEC)
 *   001C63E0            bone_init_default_2
 *   001C67E0            anim_clip_init (zero blend: sample + advance by 1)
 *   001C8710            direct channel sample at a frame
 *   001C87C0            channel advance by a step (+ the clip-end reset)
 *   001C8D50            anim_sample_bones (transition seed)
 *   001C8F10 / 001C90D0 / 001C92C0  rotation / translation / scale key walk
 *   001C84D0 / 001C85D0 rotation / translation key decode
 *   001C86A0            scaled difference (the channel velocity)
 *   001CA0A0            quat_nlerp (upper clamp, sign branch, unnormalized)
 *   001CA1C0            quat_to_mat3
 *   001C6DA0            anim_eval_skeleton
 *   001C9940            node hierarchy evaluation under a root matrix
 *   001C68C0 / 001C6960 build_trs_matrix + 001C9940 / identity root + 001C9940
 *   00178910            ledge-top column search (the hang lane's ledge_top)
 and thin adapters over translations that already exist elsewhere:
 *   0017C540            em_player_reaction_0017C540 (the hand-off)
 *   001C64F0            em_player_stage_anim_advance (reached by 001C67E0)
 *   001281C0 / 001B1470 em_player_float_to_int / em_player_001B1470
 *   00128250            em_stream_lanes_00128250 (float to unsigned)
 *   001029C0 / 00102C58 / 001C94B0 (with 00102B08 / 00102BB0 / 00102A60 and
 *                       00102918)  em_owner_services_identity_001029C0 /
 *                       _euler_00102C58 / _build_trs_matrix
 *
 * Memory. The routines follow EE addresses: the record words +40 (the clip
 * bank) and +110.. (the node records) and the resolved-clip pointers
 * D_00275BF8.. are EE addresses. The host maps them through EmPoseRegion
 * (EE address range -> host bytes). The globals the originals keep outside
 * the records are EmPoseGlobals fields: D_00275BF8.. and D_008111F0 (the
 * sampler's scratch channel record) by value, since no other port module
 * holds them; D_008106F3 and the scratchpad words by pointer, so that the
 * storage other modules also read or write (the area script's D_008106F3,
 * the stage lane's 0x70003A20, the column table) is one copy.
 *
 * Fail-stop. Every routine checks, before its first write, that every
 * worker it can reach is bound and that the records it will touch
 * (the +110 node array, every node record, the destination matrices) are
 * mapped; otherwise it returns -1 with nothing written. A key walk or table
 * read that leaves every mapped region (where the original would read
 * unrelated memory) and a worker that returns a negative value also fault;
 * the writes made before that point stay, as the original order leaves
 * them. Arithmetic, compares and VU0 lanes go through em_ee_float.h.
 *
 * Oracle: tools/test_pose_host_workers_reference.py executes the original
 * instructions over synthetic data and over the captured AREA11 RAM (the
 * playable image and the route beats) and compares every record byte, every
 * node record, every global and scratchpad word and every worker call. */
#ifndef EM_POSE_HOST_WORKERS_H
#define EM_POSE_HOST_WORKERS_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_stage_workers.h"

/* A node record (the words +110.. point at them) is read and written up to
 * +0xCF: the channels +0..+6B, +64 parent, +70 Euler, +7C translation,
 * +88/8A/8C 4.12 scales, +8E hold frame and the world matrix +90..+CF. */
#define EM_POSE_NODE_BYTES 0xD0u
/* The anim fields of an animated record: +C node count, +2C clip, +3C
 * clock, +40 bank, +60 scale, +B0 position, +C0 rotation, +D0 matrix,
 * +110 the node pointers. A record must hold at least this much. */
#define EM_POSE_RECORD_MIN 0x114u
#define EM_POSE_REGION_MAX 8
/* D_008111F0 .. D_0081125B: the sampler's scratch channel record. */
#define EM_POSE_SCRATCH_BYTES 0x6Cu
/* The largest event table the clip_resolve adapter copies for the stage. */
#define EM_POSE_EVENT_MAX 256

/* An EE address range backed by host bytes. The bank may be read-only
 * (writable 0); a write into it faults. */
typedef struct EmPoseRegion {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
    int writable;
} EmPoseRegion;

/* The globals and scratchpad words the routines read or leave behind. Every
 * word is the raw little-endian value the original holds there. The pointer
 * fields name storage the host shares with other modules (every one must be
 * bound: a null pointer is a fault before any write). */
typedef struct EmPoseGlobals {
    uint32_t d275BF8;              /* the resolved clip header (EE address) */
    uint32_t d275BF4;              /* its rotation section (header + [+8]) */
    uint32_t d275BF0;              /* translation section (header + [+C]) */
    uint32_t d275BEC;              /* scale section (header + [+10]) */
    uint8_t d8111F0[EM_POSE_SCRATCH_BYTES]; /* anim_sample_bones' scratch channels */
    uint8_t *d8106F3;              /* 001C87C0: nonzero forces the velocity reset */
    uint32_t *spad3400;            /* 0x70003400 (16 words): R (quat_to_mat3 output, row-scaled) */
    uint32_t *spad3440;            /* 0x70003440 (16 words): L (Euler basis, then R x L) */
    uint32_t *spad3600;            /* 0x70003600 (4 words): key decode staging / nlerp output */
    uint32_t *spad3760;            /* 0x70003760..0x7000378B (11 words): quat_to_mat3 products */
    uint32_t *spad3A3C;            /* 0x70003A3C: 001C86A0's reciprocal */
    uint32_t *spad38B0;            /* 0x700038B0 (4 words): 00178910's column query point */
    uint32_t *spad3A20;            /* 0x70003A20: 00178910's |dy|, then the atan2 */
    /* 00178910: the column table as its 0019BC40 call left it (D_70003170
     * flags, D_700030F0 heights, D_00282250 aux, the count 0x700031E0). */
    EmPlayerFloorTable *column;
} EmPoseGlobals;

/* 00178910's view of the sweep hit that precedes it: the scratch point
 * 0x700031B0 / 0x700031B8 and the hit record *(0x700031D0) +24 / +2C,
 * as raw words, read when the original reads them. */
typedef struct EmPoseLedgeHit {
    uint32_t x, z;      /* 0x700031B0, 0x700031B8 */
    uint32_t nx, nz;    /* *(0x700031D0) + 0x24, + 0x2C */
} EmPoseLedgeHit;

typedef struct EmPoseHostCallees {
    void *context;
    /* 00178910: 0019BC40(point) filling the column table, the sweep-hit
     * view, and SDK 0011E620 atan2(y, x). */
    int (*column)(void *context, const float position[3], EmPlayerFloorTable *table);
    int (*ledge_hit)(void *context, EmPoseLedgeHit *hit);
    float (*atan2)(void *context, float y, float x);
    /* 001C64F0 anim_advance_time(record, step), which anim_clip_init's zero
     * blend path calls (its flags result is not kept). For the player bind
     * em_pose_host_player_advance with advance_context = the
     * EmPlayerStageHost whose clip workers are this module's. */
    void *advance_context;
    int (*advance)(void *advance_context, uint8_t *record, float step);
} EmPoseHostCallees;

typedef struct EmPoseHost {
    EmPoseRegion region[EM_POSE_REGION_MAX];
    unsigned region_count;
    EmPoseGlobals *globals;
    EmPoseHostCallees callees;
    /* The stage clip_resolve adapter's copy of the event pairs. */
    int16_t events[2 * EM_POSE_EVENT_MAX];
} EmPoseHost;

/* ---- Core routines over a raw record (bytes, size) ----------------------
 * Each returns 0 (or the result through *result) and -1 on a fault. */

int em_pose_host_001749A0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, int flags,
                          float blend, int *result);
int em_pose_host_001749F0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, float blend,
                          float frame, int *result);
int em_pose_host_001C67E0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip, float blend,
                          float frame);
int em_pose_host_001C63E0(EmPoseHost *h, uint8_t *record, uint32_t size, int clip);
/* bones = the EE address of a node-pointer array (the record's +110 when a
 * caller passes record + 0x110: use the _record forms below). */
int em_pose_host_001C8710(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int nodes,
                          float frame);
int em_pose_host_001C87C0(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int nodes,
                          float step);
int em_pose_host_001C8D50(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int nodes,
                          float new_t, float prev_t);
int em_pose_host_001C8480(EmPoseHost *h, uint32_t bank, int clip);
int em_pose_host_001C61D0(EmPoseHost *h, uint32_t bank, int clip, int32_t *frames);
int em_pose_host_001C6120(EmPoseHost *h, uint32_t bank, int clip, uint32_t *header);
int em_pose_host_001C6DA0(EmPoseHost *h, uint8_t *record, uint32_t size);   /* anim_eval_skeleton */
int em_pose_host_001C68C0(EmPoseHost *h, uint8_t *record, uint32_t size);
int em_pose_host_001C6960(EmPoseHost *h, uint8_t *record, uint32_t size);
/* 001C9940(bones, n, root): root is the caller's +D0 matrix (64 bytes, read
 * where the original reads it). */
int em_pose_host_001C9940(EmPoseHost *h, const uint8_t *bones, uint32_t bones_size, int nodes,
                          const uint8_t *root);
/* build_trs_matrix (001C94B0)(out, position, rotation, scale): an adapter
 * over em_owner_services_build_trs_matrix; the signature of
 * EmPlayerFallWorkers.trs (and the closure lanes' trs). The host is not
 * read. The arguments may lie inside out (each is read where the original
 * reads it); an argument that only partly overlaps out is refused (-1). */
int em_pose_host_build_trs_matrix(void *host, uint32_t out[16], const uint32_t position[3],
                                  const uint32_t rotation[3], const uint32_t scale[3]);
int em_pose_host_00178910(EmPoseHost *h, uint8_t *record, uint32_t size, int arg, int *result);

/* Leaves, bit patterns in and out (no host state). */
void em_pose_host_001C84D0(const uint8_t src[10], uint32_t out[4], uint32_t spad3600[4]);
void em_pose_host_001C85D0(const uint8_t src[10], uint32_t out[3], uint32_t spad3600[4]);
void em_pose_host_001CA0A0(uint32_t out[4], const uint32_t a[4], const uint32_t b[4], uint32_t t);
void em_pose_host_001CA1C0(uint32_t m[16], const uint32_t q[4], const uint32_t translation[3],
                           uint32_t spad3760[11]);

/* ---- Player adapters (context = EmPoseHost) ---------------------------- */

/* The request / arbiter / clip-frame / skeleton workers of the player state
 * lanes (em_player_fall, _hang, _reaction, _recovery, _major2, _climb, ...).
 * Their return values are not read by those callers. */
int em_pose_host_request(void *host, EmPlayerLiveActor *actor, int clip, int flags, float blend);
int em_pose_host_arbiter(void *host, EmPlayerLiveActor *actor, int clip, float blend, float frame);
int em_pose_host_clip_frames(void *host, uint32_t bank, int clip, int32_t *frames);
int em_pose_host_clip_frames_actor(void *host, EmPlayerLiveActor *actor, int clip, int *frames);
int em_pose_host_eval_skeleton(void *host, EmPlayerLiveActor *actor);      /* 001C6DA0 */
int em_pose_host_skeleton(void *host, EmPlayerLiveActor *actor);           /* 001C68C0 */
int em_pose_host_handoff(void *host, EmPlayerLiveActor *actor);            /* 0017C540 */
int em_pose_host_ledge_top(void *host, EmPlayerLiveActor *actor, int arg, int *result); /* 00178910 */
/* A node record word, for the hang/reaction `node` readers: the raw word at
 * node_address + offset. */
int em_pose_host_node_word(void *host, uint32_t node_address, unsigned offset, uint32_t *out);

/* EmPlayerStageCallees entries (em_player_stage_workers.h), context =
 * EmPoseHost: bone_init, clip_init, clip_resolve, skeleton_frame,
 * w001C8710, w001C87C0, sample_bones, request. */
int em_pose_host_stage_bone_init(void *host, EmPlayerLiveActor *actor, int clip);
int em_pose_host_stage_clip_init(void *host, EmPlayerLiveActor *actor, int clip, float blend,
                                 float frame);
int em_pose_host_stage_clip_resolve(void *host, uint32_t bank, int clip, EmPlayerClipHeader *header);
int em_pose_host_stage_skeleton_frame(void *host, uint32_t skeleton, int16_t *frame);
int em_pose_host_stage_8710(void *host, EmPlayerLiveActor *actor, int nodes, float frame);
int em_pose_host_stage_87C0(void *host, EmPlayerLiveActor *actor, int nodes, float step);
int em_pose_host_stage_sample_bones(void *host, EmPlayerLiveActor *actor, int nodes, float new_t,
                                    float prev_t);
int em_pose_host_stage_request(void *host, EmPlayerLiveActor *actor, int clip, int flags, float blend);

/* ---- Bound-actor view (context = EmPoseActorView) -----------------------
 * For the lanes whose workers take no actor (em_player_slide, _climb) and
 * the skeleton node readers. d275B40 points at the coordinator's live copy
 * of D_00275B40 (anim_bone_array_setup sets it to D_00275B48 + 0x110, the
 * current owner's node-pointer array); the readers use it as the original
 * does, whatever it holds at the call. */
typedef struct EmPoseActorView {
    EmPoseHost *host;
    EmPlayerLiveActor *actor;
    const uint32_t *d275B40;
} EmPoseActorView;

/* EmPlayerSlideWorkers / EmPlayerClimbWorkers request, arbiter, clip_frames:
 * 001749A0(p, ...), anim_clip_arbiter(p, ...), 001C61D0(p+40, clip). */
int em_pose_view_request(void *view, int clip, int force, float blend);
int em_pose_view_arbiter(void *view, int clip, float blend, float frame);
int em_pose_view_clip_frames(void *view, int clip, int *frames);
/* The word *(D_00275B40 + 4 * node) + offset: EmPlayerHangWorkers.node
 * (as a float), EmPlayerClosure0E18 node (bits), EmPlayerMajor2Workers
 * root_node (node 0, bits). */
int em_pose_view_node_bits(void *view, int node, unsigned offset, uint32_t *bits);
int em_pose_view_node_float(void *view, int node, unsigned offset, float *value);
int em_pose_view_root_node(void *view, unsigned offset, uint32_t *bits);
/* EmPlayerRecoveryWorkers.skeleton (00162A40): anim_eval_skeleton(a), then
 * node 1's +C0..+CC (node 1 = *(D_00275B40 + 4)). */
int em_pose_view_eval_node1(void *view, EmPlayerLiveActor *actor, float node1[4]);
/* EmPlayerReactionWorkers.skeleton: the same, node 1's +C0..+C8. */
int em_pose_view_eval_node1_xyz(void *view, EmPlayerLiveActor *actor, float node1[3]);
/* The climb skeleton step on the view's actor: anim_eval_skeleton(p), then
 * node 1's +C4 (world y) and +8. */
int em_pose_view_eval_hip(void *view, float *hip_y, float *hip_8);
/* Data reads through D_00275B40 (raw bits, no evaluation):
 *   hip_xz      node 1 +C0 / +C8        EmPlayerFallWorkers.hip
 *   node1_words node 1 +C0..+CC         EmPlayerLadderWorkers.node
 *   root_clock  node 0 +8               EmPlayerRunningJumpWorkers.root_clock
 *   bone        node `slot` +90..+CF    weapon states A / B `bone` */
int em_pose_view_hip_xz(void *view, uint32_t *x, uint32_t *z);
int em_pose_view_node1_words(void *view, uint32_t out[4]);
int em_pose_view_root_clock(void *view, uint32_t *value);
int em_pose_view_bone(void *view, unsigned slot, uint32_t words[16]);

/* Signature variants: 001749A0 with the blend as raw bits (weapon states B
 * request), and 00102C58 (out, in, angles) with a void context
 * (EmPlayerLadderWorkers.euler): an adapter over
 * em_owner_services_euler_00102C58; the host is not read; in may equal
 * out, angles may lie inside out (read after each rotation, as the
 * original reloads them); a partial overlap is refused (-1). */
int em_pose_host_request_bits(void *host, EmPlayerLiveActor *actor, int clip, int force, uint32_t blend);
int em_pose_host_euler(void *host, uint32_t out[16], const uint32_t in[16], const uint32_t angles[3]);

/* EmPoseHostCallees.advance for the player: stage_host is the
 * EmPlayerStageHost; record must be the bytes of an EmPlayerLiveActor. */
int em_pose_host_player_advance(void *stage_host, uint8_t *record, float step);

#endif
