/* em_examine.h — placed EXAMINE objects: the engine's use-scan examine
 * interaction, first read 2026-06-11 (decomp repo Extermination/docs/
 * FINDINGS.md "EXAMINE INTERACTION DECODED", session 66).
 *
 * PROVENANCE (audit, 2026-07). This header's claims split cleanly in two,
 * and readers should not treat them as one tier:
 *   VERIFIABLE — the use scan and the script interpreter live in the boot
 *     ELF and ARE recovered: func_00183EF0 (archetype geometry, NEARMISS),
 *     func_001BA1A0 (interpreter prime, BYTE-MATCHED), func_001BA1F0
 *     (interpreter pump, NEARMISS). All three were re-read in the audit;
 *     the scan's facing half-angle was CORRECTED as a result (see below).
 *   NOT VERIFIABLE — everything about the per-AREA behaviors and scripts
 *     (0x824FA0, 0x823DA0, 0x827B10, 0x824340, 0x827670, 0x82A990, the
 *     op00/op02/op04/op07/op0C/op0D numbering, the desc tuples, the face
 *     yaws, the elevator/battery flow). Those are AREA OVERLAY code at
 *     0x823500+, which the decompilation has NOT recovered — there is no
 *     Extermination/src/func_*.c for any of them — and the cited
 *     INVESTIGATION_area11_elevator.md / INVESTIGATION_examine_walk_face.md
 *     do not exist in the decomp repo's docs/. Read every "decoded" in
 *     that group as ONE SESSION'S DISASSEMBLY READING, not source truth.
 *
 * ENGINE TRUTH (subject to the split above). Examine-able objects are
 * MAIN PLACEMENT-TABLE records
 * with the interactive class flag 0x80 whose behavior function lives
 * in the AREA OVERLAY (the per-area .text at 0x823500+). The shape,
 * read in full from four overlay behaviors (AREA02 0x824FA0, AREA07
 * 0x823DA0, AREA11 0x827B10, AREA06 0x824340):
 *
 *   INIT     model bind (func_001B0FD0) + TRS bake (func_001C6380);
 *            +0x08 = use-scan ARCHETYPE (3 = the item branch for the
 *            office corpse; 1 = desc-point for the AREA11 switch;
 *            0 default for AREA06), +0x30 = the use-scan DESC —
 *            the examine desc D_002758E0 = {20.0, 10.0} (XZ disc
 *            radius, dy window; vs the item desc {10.0, 3.5}).
 *   ARMED    the player use scan (func_00184BA0, CROSS press edge,
 *            s58) walks the interactive list, runs the archetype
 *            geometry test (func_00183EF0) and sets +0x0B = 4 on the
 *            nearest passer. The behavior's sub-state 0 polls
 *            +0x0B & 4 and QUEUES the area's EXAMINE SCRIPT
 *            (func_001BA1A0 primes the 0x40-byte-record interpreter,
 *            func_001BA1F0 pumps it).
 *
 *            CONFIRMED (audit, re-checked a second time 2026-07-31)
 *            against the recovered C. func_001BA1A0 (BYTE-MATCHED) is
 *            four stores and nothing else — `a0[0] = 1; a0[1] = 0;
 *            a0[2] = a1; ((unsigned char *)a0)[0xC] = 0;` — i.e. the
 *            cursor block func_001BA1F0 reads at actor+0x1F0:
 *            [0]=1 (running), [4]=0 (timer), [8]=script pointer,
 *            byte[0xC]=0. func_001BA1F0 (NEARMISS) is the pump:
 *            `ftab_0024D880[*(int *)(n + 0) & 0xFFF](arg0, e, n)` — a
 *            0x1000-entry table indexed by the record's low 12 bits.
 *            The pump idles out on `*(int *)(e + 0) <= 0`. Record flag
 *            bit31 = STOP (cursor[0] = -1), bit30 = JUMP to *(rec+4),
 *            else the cursor advances by 0x40 — so the 0x40-byte record
 *            stride and the opcode dispatch are engine truth. PRECISION
 *            (audit-2): the return code is 3 only when a HANDLER returns
 *            3; the bit31 STOP reached from the advance path sets
 *            cursor[0] = -1 and returns 1. Nothing in the port keys on
 *            that distinction, but do not quote "bit31 -> returns 3".
 *            The op NUMBERS below come from the AREA OVERLAY scripts,
 *            which are not in the decomp's recovered set: they are
 *            observation, not source-derived.
 *   SCRIPT   the observed examine scripts share one shape:
 *              op07 sub2   enter scripted mode (input pause, fade arm)
 *              [op00]      camera cue: fixed view pos/target vectors
 *              [op02]      wait N frames
 *              op0C sub0/1 MESSAGE — the mode-2 radio/examine machine
 *                          (em_hud.h): line word rec+0x14 (bit 31 =
 *                          GLOBAL slot-0x16 bank; else the per-AREA
 *                          line-record table D_00264DD0[area+1] —
 *                          a CHAIN: record index advances bank line
 *                          +1 per record until the terminal record
 *                          {dur 0, wait 1}), pre-delay rec+0x18
 *              [op0D sub5] chase-camera restore cue
 *              op07 sub4/5 exit scripted mode | STOP (sub5 also sets
 *                          an event flag — story state, not modeled)
 *   DONE     pump returns done -> +0x0B = 0, sub-state 0: the examine
 *            RE-ARMS (repeatable). The AREA11 switch refusal also
 *            sets a 300-frame cooldown (+0x2A) before the next arm.
 *
 * Census of the exported scenes (the engine's own placements):
 *   scene_office0  AREA02 sub-0 [40]: the examined office prop (per-
 *                  area model 0x17, baked into 03_placed.emdl), pos
 *                  (-48, 0.2, -58) — archetype 3, desc {20, 10},
 *                  script 0x827670: AREA-bank line 0 chain (durations
 *                  158/54/128, no voice) + TWO op00 camera cues.
 *                  AREA 2 SHIPS NO id-0x41 TEXT BANK — the engine
 *                  presents whatever bank loaded last (stale-bank
 *                  quirk); entered from AREA01 (the port's only route
 *                  in) that is the drawbridge bank lines 0..2.
 *   scene_snow     AREA11 [19] is NOT an examine object in the port since
 *                  WP-4: the internal elevator control terminal is the
 *                  original owner 00827B10 (refusal 0x82A990 / powered
 *                  0x82A750 and the carry 00828050), bound in the AREA11
 *                  interaction host (em_area11_interaction_host.c). The
 *                  scene loader skips the manifest's legacy `terminal`
 *                  examine line.
 *                  AREA06 switch (the same world mesh): message
 *                  script 0x827040 — camera cue + AREA06 bank line 0
 *                  (208 frames, voice cue 40). FLAGGED: in the engine
 *                  this message is the POST-THROW reminder state; the
 *                  initial state runs the throw cutscene (player anim
 *                  0x29 + fades + native), which is not ported.
 *   scene_drawbridge AREA01 [0]: the drawbridge crank (lib model
 *                  0x47), desc {10, 20} — counter-0 script 0x829E60:
 *                  op15 cutscene compound, AREA01 bank line-0 chain
 *                  (9 records: 5 text lines + gaps, voice cues 2/3).
 *                  FLAGGED: the op15 player-anim arm and the
 *                  VOICE.DAT streams are not ported — the port plays
 *                  the dialogue chain as a radio examine.
 *
 * PORT MAPPING (deviations FLAGGED):
 *   - Manifest lines (decomp repo tools/export_level.py --examine):
 *       examine <x> <y> <z> <yaw> <dist> <dy> [gline 0xNN]
 *               [delay N] [cooldown N] [cam <ex> <ey> <ez>]
 *               [face <yaw> [walk N]]
 *     `face <yaw>` = the op04 scripted target heading the player pivots
 *     to before the message (optional trailing `walk N` = the op01
 *     walk-to duration in frames, default/0 = no walk).
 *       examinetext <dur> <gap> <text-with-\n-escapes...>
 *     `examinetext` appends one chained AREA-bank record (text shown
 *     `dur` frames, then `gap` blank frames) to the LAST examine.
 *     `gline` = GLOBAL bank line: presented through em_hud_radio
 *     (text from messages.emsg group 9, the engine duration table).
 *   - Scan = archetype-3 geometry, re-read from the recovered
 *     func_00183EF0 (`case 3: case 4:`) during the 2026-07 audit:
 *       * XZ DISC (not a ring): sqrt(dx^2+dz^2) <= desc[0];
 *       * dy = player.y - object.y, accepted for dy in
 *         [-(desc[1]+17), +desc[1]] — asymmetric, wider BELOW;
 *       * facing: bd <= 7 auto-passes (ang forced to 0), otherwise
 *         ang = wrap(player_yaw - atan2(bx, bz)) accepted while
 *         |ang| <= 1.5707964f — pi/2, NOT pi/4 (CORRECTED; the pi/4
 *         at func_00183EF0's tail is archetype 0/1/2 only);
 *       * nearest passer wins (the engine leaves the planar distance
 *         at SPR 0x70003B98 for func_00184BA0 to compare).
 *       * CORRECTED (audit-2, 2026-07-31) — ONE WINNER PER PRESS ACROSS
 *         MODULES. func_00184BA0's recovered C walks a SINGLE
 *         interactive list that contains items and examine objects
 *         alike, keeps one `winner` by the smallest parked distance,
 *         and arms only it (`winner[0xB] = 4; return 1`). The port
 *         scans items (em_pickup) and examines here separately, so a
 *         CROSS press near both used to take the item AND start the
 *         examine script in the same frame. em_examine_update now runs
 *         second and arbitrates through em_pickup_scan_dist /
 *         em_pickup_scan_release (em_pickup.h "ONE WINNER PER PRESS").
 *         STILL FLAGGED: doors are a third scanner and are gated only
 *         doors-first by em_game's movement lock.
 *     Anchored at the manifest position. The AREA11 archetype-1 desc
 *     point ships AS the manifest position (engine values) — note
 *     func_00183EF0's real archetype-1 branch differs (desc-point xz,
 *     desc[3] radius, SYMMETRIC |dy| <= desc[4], pi/4 facing); the port
 *     runs the archetype-3 test for every instance. FLAGGED.
 *   - Sequence: input pause (movement + menu) for the whole script,
 *     then the op04 FACE pre-roll (em_examine_set_face: the player pivots
 *     in place to the scripted yaw at the standing turn-in-place rate,
 *     22.5 deg/frame, SNAP-when-within — the missing interaction
 *     animation, INVESTIGATION_examine_walk_face.md §3), then the
 *     pre-delay, then the message; the camera cue (when present) cuts
 *     at sequence start and holds until done — the engine starts the
 *     message first and runs its op00 records during it (the exact
 *     op00 spline/second-vector semantics are undecoded) — FLAGGED.
 *     The op01 walk-to LERP is duration-0 for the snow terminals (the
 *     use-scan already places the player within dist), so only the FACE
 *     pivot is visible; a nonzero walk-to duration (office/drawbridge)
 *     is FLAGGED-unimplemented. No enter/exit fade (op07 sub2's fade
 *     arm) — FLAGGED.
 *   - The chain presenter mirrors the engine mode-2 presentation
 *     (tall gray, centered max-of-two-segments, canvas y 388, 24-px
 *     '\n' steps, timer dismiss, terminal bookkeeping frame) for
 *     AREA-bank text the global machine can't address.
 *   - Area voice cues (VOICE.DAT streams) are not played — FLAGGED.
 */
#ifndef EM_EXAMINE_H
#define EM_EXAMINE_H

#include "em_gfx.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_EXAMINE_MAX        8   /* richest scene ships 2 */
#define EM_EXAMINE_RECS       12  /* longest OBSERVED chain = 9 records
                                   * (read off an AREA01 overlay script,
                                   * which the decomp has not recovered —
                                   * not source-derived) */
#define EM_EXAMINE_TEXT_MAX   160

/* Examine use-scan desc D_002758E0 (the default when the manifest line
 * carries no explicit pair).
 * DOWNGRADED (audit-2, 2026-07-31): {20.0, 10.0} is NOT source-derived.
 * D_002758E0 is .rodata; the recovered C (func_00183EF0) only ever
 * DEREFERENCES the desc pointer at candidate+0x30 — the two floats
 * themselves appear nowhere in the decompilation, only in FINDINGS
 * "EXAMINE INTERACTION DECODED" §the-desc-table. Treat them as
 * FINDINGS-asserted data (same tier as the item desc D_00275488
 * {10.0, 3.5} — see em_pickup.h). The three constants BELOW are
 * different: they are literals in the recovered C. */
#define EM_EXAMINE_RADIUS     20.0f  /* desc[0] (FINDINGS-asserted)     */
#define EM_EXAMINE_DY         10.0f  /* desc[1] (FINDINGS-asserted)     */
/* CONFIRMED literals in src/func_00183EF0.c `case 3: case 4:` —
 * `fabs(dy) <= 17.0f + desc[1]` for the item-above half of the window,
 * and `bd <= 7.0f -> ang = 0.0f` for the facing auto-pass. */
#define EM_EXAMINE_DY_EXTRA   17.0f  /* archetype dy widening, item-above */
#define EM_EXAMINE_AUTO_RING   7.0f  /* facing auto-pass distance */
/* Archetype-3 facing half-angle. CORRECTED (audit): func_00183EF0's
 * `case 3: case 4:` accepts while `fabs(ang) <= 1.5707964f` and returns
 * from inside that branch — the 0.7853982f (pi/4) test at the tail of
 * func_00183EF0 belongs to archetypes 0/1/2 only. */
#define EM_EXAMINE_FACE_HALF  1.5707964f

/* Add one examine object. gline = GLOBAL bank line id, or -1 for an
 * AREA-bank text chain (append records with em_examine_text). `cam` =
 * the op00 camera-cue eye, or NULL for no cue (the chase keeps the
 * camera). Returns the slot index or -1. */
int em_examine_add(const float pos[3], float yaw, float dist, float dy,
                   int gline, int delay, int cooldown, const float cam[3]);

/* Append one chained area-bank record (text for `dur` frames, then
 * `gap` blank frames) to instance `slot`. Returns 0 / -1. */
int em_examine_text(int slot, int dur, int gap, const char *text);

/* Attach the op04 FACE pre-roll (INVESTIGATION_examine_walk_face.md) to
 * instance `slot`: the SCRIPTED target heading-yaw the player pivots to
 * BEFORE the message (engine script record+0x24), plus the op01 walk-to
 * duration in frames (record+0x0C; 0 = no walk). The interaction sequence
 * turns the player body heading toward `face_yaw` at the standing
 * turn-in-place rate (22.5 deg/frame, SNAP-when-within, via
 * em_game_player_face_step) while the input lock holds, then proceeds to
 * the existing pre-delay/message/cooldown path UNCHANGED. The manifest
 * `examine` line's trailing `face <yaw>` token routes here (em_game.c
 * parser). Decoded yaws: snow INTERNAL terminal (224,230,250.7) =
 * -1.3037 rad (script 0x82A990 record+0x24). `walk_frames` > 0 (the
 * straight-line position LERP) is FLAGGED-unimplemented — no shipped
 * scene sets it; the snow terminals are duration-0 (the use-scan already
 * places the player within dist). Returns 0 / -1 (bad slot). */
int em_examine_set_face(int slot, float face_yaw, int walk_frames);

/* Free all instances (scene clear / shutdown). */
void em_examine_reset(void);

/* Per-frame: the use scan (CROSS edge -> arm, `scan` gates it like
 * em_pickup_update) + the sequence pump. */
void em_examine_update(const float player_pos[3], float player_yaw,
                       const EmFrameInput *in, int scan);

/* Chain-text presenter draw (the mode-2 presentation for AREA-bank
 * chains; GLOBAL lines draw through em_hud's own radio machine).
 * Call once per frame from the close-out, after em_hud_found_render. */
void em_examine_render(EmGfx *gfx);

/* Sequence introspection:
 *  - input_locked: the scripted-mode window (movement + menu pause)
 *  - camera: 1 while a camera cue is pinned (writes eye + target)
 *  - active: the running instance's slot, or -1 */
int em_examine_input_locked(void);
int em_examine_camera(float out_eye[3], float out_tgt[3]);
int em_examine_active(void);
int em_examine_count(void);

/* Instance position (capture harness / tests). Returns 0 on a bad
 * index. */
int em_examine_pos(int i, float out_pos[3]);

#ifdef __cplusplus
}
#endif

#endif /* EM_EXAMINE_H */
