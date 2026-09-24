/* em_player_record_pose.h - the player's one pose owner: the clip clock, the
 * node channels and the skeleton live in the player's own record, worked by
 * em_pose_host_workers (docs/PLAYER_CLIPS.md section 6).
 *
 * The owner of 001749A0 / 001749F0 / 001C61D0 / 001C64F0 / anim_eval_skeleton
 * for the player is em_pose_host_workers over the live record: the fields
 * +20C, +2C, +3C, node 0's +8E and every node record are the original's own
 * bytes, so the stale hold frame of a chain, the +20C / +2C split and the
 * VU0 lanes of the skeleton are exact (tools/test_pose_host_workers_reference
 * .py). This module only holds the storage those routines work on and
 * binds it:
 *
 *   the record    the caller's EmPlayerLiveActor (the stage's +C, +2C, +3C,
 *                 +40, +60, +B0, +C0, +D0, +110.., +20C, +2F3, +303)
 *   the bank      assets/player_clips_full.bank, read-only at EE 0xD689C0
 *                 (the record's +40; tools/export_player_clips.py checks it
 *                 byte for byte against captured RAM)
 *   the nodes     21 node records of 0xD0 bytes at EE 0x7D5840.. (the
 *                 player's +110 words in every captured AREA11 image)
 *   the record's  mapped at its EE address 0x8102B0 as well, so the view
 *   own bytes     adapters that reach the +110 array through D_00275B40
 *                 read the record's own bytes (POSE_HOST_WORKERS.md
 *                 section 3, "Storage constraint")
 *   the rows      D_00248C90's +0 halfword per clip
 *                 (assets/player_clip_row0.emch, tools/export_player_tables.py)
 *   the globals   D_00275BF8.., D_008111F0, the scratchpad words the pose
 *                 routines use, and D_008106F3 by pointer
 *
 * The routines themselves are em_pose_host_workers' (and 001C64F0 is
 * em_player_stage_anim_advance bound to that module's clip workers); nothing
 * here re-implements them. The one original block this module translates is
 * 0015BCF0's animate step (em_player_record_pose_animate).
 *
 * Fail-stop: every entry returns -1 (nothing displayed from it) when the
 * storage is not loaded / attached or when a pose routine faults. */
#ifndef EM_PLAYER_RECORD_POSE_H
#define EM_PLAYER_RECORD_POSE_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_stage_workers.h"
#include "game/em_pose_host_workers.h"

#define EM_PLAYER_POSE_BANK_ADDRESS UINT32_C(0x00D689C0)   /* the player's +40 */
#define EM_PLAYER_POSE_NODE_ADDRESS UINT32_C(0x007D5840)   /* the player's +110 word 0 */
#define EM_PLAYER_POSE_RECORD_ADDRESS UINT32_C(0x008102B0) /* the player record (D_00275B44) */
#define EM_PLAYER_POSE_NODES 21                            /* the player's +C */
#define EM_PLAYER_POSE_ROWS 459                            /* D_00248C90 rows = bank clips */
#define EM_PLAYER_POSE_PALETTE_BONES 22                    /* 21 nodes + the model's identity slot */

typedef struct EmPlayerRecordPose {
    /* Loaded (em_player_record_pose_load). */
    uint8_t *bank;
    uint32_t bank_size;
    uint32_t bank_clips;                   /* the bank's count word */
    /* The ELF span D_00248740..D_00248ACC (tools/export_player_tables.py
     * assets/player_loco_tables.emrg): 0017B460 / 0017B490's row tables,
     * 0017C440's tier speeds; mapped read-only at its base when loaded. */
    uint8_t *tables;
    uint32_t tables_base, tables_size;
    int16_t row0[EM_PLAYER_POSE_ROWS];     /* D_00248C90[clip * 6] */
    int loaded;
    /* Attached (em_player_record_pose_attach). */
    EmPlayerLiveActor *actor;
    uint8_t nodes[EM_PLAYER_POSE_NODES * EM_POSE_NODE_BYTES];
    EmPoseHost host;
    EmPoseGlobals globals;
    uint32_t spad3400[16], spad3440[16], spad3600[4], spad3760[11];
    uint32_t spad3A3C, spad38B0[4], spad3A20;
    EmPlayerFloorTable column;
    /* 001C64F0's host: only its clip workers are read (em_pose_host_stage_*);
     * its scene and globals are the player stage's own (the stage workers'
     * readiness test asks for them). */
    EmPlayerStageHost advance;
    int attached;
} EmPlayerRecordPose;

/* The bank and the row column. 0, or -1 (nothing kept) when either file is
 * missing or malformed: the bank must hold its count word, a directory entry
 * per clip and exactly EM_PLAYER_POSE_ROWS clips; the column must be an EMCH
 * v1 file of EM_PLAYER_POSE_ROWS halfwords. */
int em_player_record_pose_load(EmPlayerRecordPose *pose, const char *bank_path,
                               const char *row0_path);
/* The same from memory (the caller keeps nothing; the bytes are copied). */
int em_player_record_pose_load_bytes(EmPlayerRecordPose *pose, const uint8_t *bank, uint32_t bank_size,
                                     const uint8_t *row0, uint32_t row0_size);
void em_player_record_pose_free(EmPlayerRecordPose *pose);
/* The locomotion table span ("EMRG" v1: base, size, bytes), mapped by the
 * next attach. 0, or -1 (nothing kept) when the file is missing or is not
 * the span 0x248740..0x248ACC. Kept across em_player_record_pose_load. */
int em_player_record_pose_load_tables(EmPlayerRecordPose *pose, const char *path);

/* Bind the storage to `actor` and write the record's structural words the
 * original's actor setup leaves there (every captured AREA11 image): +C = 21,
 * +40 = 0xD689C0, +60..+68 = 1.0 and the +110 words = 0x7D5840 + 0xD0 * i.
 * The node records start zeroed; the first pose comes from
 * em_player_record_pose_default (001C63E0), as 00182DF0 releases the player.
 * d8106F3 is D_008106F3 (001C87C0's velocity reset). scene and globals are
 * the player stage's views (EmPlayerStageScene / EmPlayerStageGlobals):
 * 001C64F0 reads neither, but the stage workers refuse to run until the
 * stage has loaded their D_008106F1 / D_00810707 pointers. None may be
 * NULL. 0, or -1. */
int em_player_record_pose_attach(EmPlayerRecordPose *pose, EmPlayerLiveActor *actor,
                                 uint8_t *d8106F3, EmPlayerStageScene *scene,
                                 EmPlayerStageGlobals *globals);
int em_player_record_pose_ready(const EmPlayerRecordPose *pose);

/* The EmPoseHost every player-state worker slot binds (context of
 * em_pose_host_request / _arbiter / _clip_frames / _eval_skeleton /
 * _skeleton and the em_pose_host_stage_* callees), or NULL. */
EmPoseHost *em_player_record_pose_host(EmPlayerRecordPose *pose);
/* The EmPlayerStageHost whose clip workers are that host's: the context of
 * em_player_stage_anim_advance (001C64F0) for the player record. */
EmPlayerStageHost *em_player_record_pose_advance_host(EmPlayerRecordPose *pose);

/* ---- The original routines on the record (em_pose_host_workers) --------- */
int em_player_record_pose_default(EmPlayerRecordPose *pose, int clip);                 /* 001C63E0 */
int em_player_record_pose_request(EmPlayerRecordPose *pose, int clip, int flags, float blend,
                                  int *result);                                        /* 001749A0 */
int em_player_record_pose_arbiter(EmPlayerRecordPose *pose, int clip, float blend, float frame,
                                  int *result);                                        /* 001749F0 */
/* 001C64F0 anim_advance_time(p, step): *flags gets the signed halfword result. */
int em_player_record_pose_advance(EmPlayerRecordPose *pose, float step, uint32_t *flags);
int em_player_record_pose_frames(EmPlayerRecordPose *pose, int clip, int32_t *frames);  /* 001C61D0 */
int em_player_record_pose_eval_skeleton(EmPlayerRecordPose *pose);                     /* 001C6DA0 */
int em_player_record_pose_skeleton(EmPlayerRecordPose *pose);                          /* 001C68C0 */
/* D_00248C90's +0 halfword of row `clip` (0015BCF0 / 00182DF0 read it). A
 * row outside the table is not data the table defines: -1. */
int em_player_record_pose_row0(const EmPlayerRecordPose *pose, int clip, int16_t *value);

/* 0015BCF0's animate step, after 0015BA50 and +BC = 1.0: with +2F3 == 0 and
 * +303 == 0, anim_eval_skeleton (001C6DA0) when the +20C row's +0 halfword is
 * nonzero, else 001C68C0; with +2F3 == 3 or 4, 001C68C0; with any other
 * nonzero +2F3, 001C6960; with +2F3 == 0 and +303 != 0, nothing. */
int em_player_record_pose_animate(EmPlayerRecordPose *pose);

/* ---- Views of the record ------------------------------------------------ */
unsigned em_player_record_pose_clip(const EmPlayerRecordPose *pose);      /* +2C & 0x7FFF */
int em_player_record_pose_transition(const EmPlayerRecordPose *pose);     /* +2C & 0x8000 */
float em_player_record_pose_clock(const EmPlayerRecordPose *pose);        /* +3C */
int16_t em_player_record_pose_requested(const EmPlayerRecordPose *pose);  /* +20C */
/* The node world matrices (+90..+CF of nodes 0..20, as the last evaluation
 * left them) and, for the model's trailing slot (the identity in actor
 * space), the record's owner matrix +D0: 22 column-major 4x4 matrices (the
 * original's row-vector words are the same memory layout).
 * 0, or -1 when a matrix word is not finite. */
int em_player_record_pose_palette(const EmPlayerRecordPose *pose,
                                  float out[EM_PLAYER_POSE_PALETTE_BONES * 16]);
/* The clip's parents (the header's halfword +20 + 4i), for the cinematic
 * bank's compatibility check. */
int em_player_record_pose_parents(EmPlayerRecordPose *pose, int clip,
                                  int32_t parents[EM_PLAYER_POSE_NODES]);

#endif
