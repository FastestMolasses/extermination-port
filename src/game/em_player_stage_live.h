/* em_player_stage_live.h - the live binding of the player stage (census lane
 * L01, docs/PLAYER_STAGE_WORKERS.md section 2 "Binding").
 *
 * Fills the stage workers' host (em_player_stage_workers.h) over the
 * canonical scene storage and binds em_player.c's live layer with it, so
 * that every player stage from first control runs the original 0015BA50 /
 * 0015B130 / 0015BCF0 tail with the translated 0021C440, 0015D100, 0015D000,
 * 00182B30, 00182D70, 00174A50, 0011A070, D_00248C98 and the +4 = 4 / 6
 * handlers 0015B530 / 0015D460. The FLOOR and USE mechanisms stay gated
 * (their workers are not part of this binding).
 *
 * Worker by worker (what each binds to, or why it is a fail-stop worker
 * that faults when reached):
 *   clip_rate  D_00248C98 from the local export assets/player_clip_rates.emcr
 *              (tools/export_player_tables.py; never committed)
 *   advance    the live display's 001C64F0 (em_player_pose_advance through
 *              player_pose_stage_advance): the one bound translation of
 *              anim_advance_time on the live path. The record-level
 *              translation em_player_stage_anim_advance (and its clip
 *              resolve / sampler callees) waits for the display lane.
 *   sound      the live 001FBD50 (em_sfx_play_at_mode_track at the record's
 *              +B0; a nonzero flat byte measures the distance in x/z)
 *   sound_stop the live 0011A070 body (em_sfx_stop_track)
 *   major[4]   0015B530 with 001837A0 (byte-matched, empty) and 00182DF0
 *              (em_player_stage_00182DF0 through the pose host, which also
 *              ends the interaction host's staged token); 001837B0,
 *              001838B0, 00183910 and FLOOR's 00162DB0 / 00163B40 are
 *              the closure binder's (em_player_001837B0 / _001838B0 /
 *              _00183910 / em_player_fall_state5 / _state8, with an
 *              original collision world); without one they fault
 *   major[6]   0015D460 with the live 001AEDE0 (em_frame_fade_start_colour)
 *   commit     00183090 with 001D0C70 = the AREA11 interaction host's face
 *              tick and bone_init 001C63E0 / anim_clip_init 001C67E0 on the
 *              record pose
 *   effects    001EFE00 / 001F00A0 / 001F0060 (em_effect_original's
 *              translations on em_effects_live, the area's effect binder:
 *              001EFE00 takes the record's +B0 / +C0 quadwords and +14 word;
 *              001F00A0's record is handed out as an image of the node's
 *              +D0..+10F that record_written stores back after 0021CBB0's
 *              copy). A node whose callback has no pool binding faults in
 *              the binder (0x80000048's 001F8350, 0x80000044's 0021AE90,
 *              0x80000061..63's 001E7310 / 001E7440 / 001E7570 are not
 *              bound in AREA11); 0x8000001B is the bound driver 001EA240.
 *   atan2      SDK 0011E620 on the area's SDK context (em_collision_world_
 *              sdk, em_sdk_math_original); the slot has no error channel,
 *              so a fault reports and quits the frame
 *   link20     the +20 object is the equipment node 0015C310 stored there
 *              (0018A880(0, 0)); its +C0 / +C8 are em_equipment_live's
 *   w0015C9D0  em_player_0015C9D0 (byte-matched C) with 001749A0 on the
 *              record pose; D_00248A00..08 from the pose's table region,
 *              D_002754C0..DA from the status pages' exported windows
 *              (em_area11_interaction_host_data16)
 *   takeover   the AREA11 interaction host at 0015B130's prelude position
 *              (player_pose_stage_hook): 1 the runtime consumed the stage
 *              (the panel, terminal and items), 2 a script owner's frame,
 *              whose takeover 0015B130's prelude performs itself
 *   cue        001B61C0 on the pad block (em_pad_actuator)
 *   major[5]   0015B610 with 00183240 (byte-matched, empty) and the closure
 *              binder's 00183250; 001833F0 / 00183440 / 001834E0 are
 *              translated (em_player_floor) but unwired: their 001662D0
 *              (the ladder climb lane) and 00175900 are the closure
 *              binder's, and 001834E0's 0016BAE0 is not bound there
 *   fail-stop  the unwired major[5] routines (link1C, the +1C object, is the
 *              infection node's +4 through em_scene_bindings_pool_set_u4). The effect, atan2, link20 and
 *              0015C9D0 paths are reached only by a hit, pending damage or
 *              infection, a low health latch or the area-8 room-2 effect;
 *              none occurs on the first-level route (every route capture:
 *              health 100, +F 0, +224 = +22C = 0, +234 0, D_0081083C 0,
 *              D_008106C8 bit 2 clear, +1C 0).
 *
 * The scene views the workers read are loaded before every stage from the
 * canonical storage: D_008106C8 (the request word), D_00810701, D_0081083C
 * and D_00810C7E (progress bytes, D2), and D_00810707 / D_008106F1 by
 * pointer. D_00810770 (event 0x18) is the progress block's byte; its
 * only reader, 0021C3F0, reads it in area 8 room 2, where the load refuses. */
#ifndef EM_PLAYER_STAGE_LIVE_H
#define EM_PLAYER_STAGE_LIVE_H

/* Bind (area build, 001AF5C0's position). 0, or -1 when the clip-rate
 * export is missing or invalid (the stage stays unbound). */
int em_player_stage_live_bind(void);
/* Faults the fail-stop workers reported (a reached untranslated worker). */
unsigned em_player_stage_live_faults(void);
/* 00182B30(D_008102B0) on the live player record through the bound stage
 * host (em_player_stage_scripted_check): *result = its return value. 0, or
 * -1 before the bind. */
int em_player_stage_live_00182B30(int *result);

#endif
