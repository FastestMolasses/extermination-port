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
 *   sound      the live 001FBD50 (em_sfx_play_at at the record's +B0)
 *   sound_stop the live 0011A070 body (em_sfx_stop_track)
 *   major[4]   0015B530 with 001837A0 (byte-matched, empty); its other six
 *              routines fault (00182DF0's record side, 001837B0, 001838B0,
 *              00183910 are untranslated; 00162DB0 / 00163B40 are FLOOR's)
 *   major[6]   0015D460 with the live 001AEDE0 (em_frame_fade_start_colour)
 *   takeover   the AREA11 interaction runtime (player_pose_stage_hook)
 *   fail-stop  001D0C70, bone_init 001C63E0, anim_clip_init 001C67E0 (the
 *              +4 = 4 commit), 001B61C0 (rumble), 001EFE00, 001F00A0,
 *              001F0060 (effects), SDK 0011E620 and link20 (the +20
 *              object, whose handle the port does not keep), 0017B490 and
 *              001749A0 on the record (00174A50 / 0017C370), 0015C9D0 and
 *              link1C (the +1C object; 0 in every AREA11 capture). Each is
 *              reached only by a hit, pending damage or infection, a low
 *              health latch, the +4 = 4 takeover or the area-8 room-2
 *              effect; none occurs on the first-level route (every route
 *              capture: health 100, +F 0, +224 = +22C = 0, +234 0,
 *              D_0081083C 0, D_008106C8 bit 2 clear, +1C 0).
 *
 * The scene views the workers read are loaded before every stage from the
 * canonical storage: D_008106C8 (the request word), D_00810701, D_0081083C
 * and D_00810C7E (progress bytes, D2), and D_00810707 / D_008106F1 by
 * pointer. D_00810770 (event 0x18) is not canonical yet (census L19); its
 * only reader, 0021C3F0, reads it in area 8 room 2, where the load refuses. */
#ifndef EM_PLAYER_STAGE_LIVE_H
#define EM_PLAYER_STAGE_LIVE_H

/* Bind (area build, 001AF5C0's position). 0, or -1 when the clip-rate
 * export is missing or invalid (the stage stays unbound). */
int em_player_stage_live_bind(void);
/* Faults the fail-stop workers reported (a reached untranslated worker). */
unsigned em_player_stage_live_faults(void);

#endif
