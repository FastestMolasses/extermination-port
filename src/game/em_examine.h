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
 *            CONFIRMED (audit) against the recovered C. func_001BA1A0
 *            (BYTE-MATCHED) writes the four-field cursor block that
 *            func_001BA1F0 reads at actor+0x1F0: [0]=1 (running),
 *            [4]=0 (timer), [8]=script pointer, byte[0xC]=0.
 *            func_001BA1F0 (NEARMISS) is the pump: opcode =
 *            *(int*)(rec+0) & 0xFFF dispatched through the 0x1000-entry
 *            table ftab_0024D880(actor, cursor, rec); flag bit31 = STOP
 *            (cursor[0] = -1, returns 3), bit30 = JUMP to *(rec+4),
 *            else the cursor advances by 0x40 — so the 0x40-byte record
 *            stride and the opcode-nibble dispatch are engine truth.
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
 *   scene_snow     AREA11 [19]: the unpowered switch, desc-point
 *                  archetype {(222, 230, 250.4), r 5, dy 20} —
 *                  refusal script 0x82A990: GLOBAL line 0x1A
 *                  ("Switch / No power...", 148 frames), chase cue
 *                  only, 300-frame re-arm cooldown. This is the INTERNAL
 *                  elevator control terminal (ov 0x00827B10, on the
 *                  platform): the powered path 0x82A750 (gated on the
 *                  D_00810841 unlock bit) plays anim 0x47 + installs the
 *                  descent actor 0x00828050. The CORRECTED two-terminal
 *                  flow (INVESTIGATION_area11_elevator.md "CORRECTED
 *                  FLOW") adds a SEPARATE OUTSIDE battery terminal (ov
 *                  0x008237E0, 331.7,290,192.5) that inserts the battery
 *                  (anim 0x14) and sets the power bit; the internal
 *                  terminal only CHECKS power and runs the ride.
 *                  (em_examine_set_terminal / em_examine_set_battery_terminal.)
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
#define EM_EXAMINE_RECS       12  /* longest decoded chain = 9 records */
#define EM_EXAMINE_TEXT_MAX   160

/* Decoded examine use-scan desc D_002758E0 (the default when the
 * manifest line carries no explicit pair). */
#define EM_EXAMINE_RADIUS     20.0f
#define EM_EXAMINE_DY         10.0f
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

/* Mark instance `slot` as the AREA-11 INTERNAL ELEVATOR CONTROL TERMINAL
 * (the manifest's trailing "terminal" token; the engine's interactive
 * record 19, behavior ov 0x00827B10 at 224,230,250.7 — the grey control
 * box that sits ON the descending platform). This is the POWER-GATED
 * RIDE terminal. The CORRECTED two-terminal flow
 * (INVESTIGATION_area11_elevator.md "CORRECTED FLOW") splits the old
 * single-terminal model: this internal terminal NO LONGER inserts the
 * battery or sets power — it only CHECKS power and runs the ride.
 * When used:
 *   - powered (em_game_terminal_powered()) -> the powered script
 *     0x82A750 path: em_game_player_interact_anim(0x47) (the lever-throw
 *     clip + input/movement lock) THEN em_game_elevator_start() (the
 *     opcode-9 install of the descent actor ov 0x00828050)
 *   - unpowered -> the EXISTING unpowered refusal (gline 0x1A +
 *     300-frame cooldown), unchanged
 * The ELEVATOR parser (em_game.c) calls this after em_examine_add when
 * it sees the "terminal" token. Returns 0 / -1 (bad slot). */
int em_examine_set_terminal(int slot);

/* Mark instance `slot` as the AREA-11 OUTSIDE BATTERY TERMINAL (the
 * manifest's trailing "battery_terminal" token; the engine's
 * interactive ov 0x008237E0 at 331.7,290,192.5, yaw 2.8449 — the
 * director-driven battery-insert object, a SEPARATE object from the
 * internal terminal, ~110 u away on the upper Y290 ledge). This is the
 * object the player inserts the battery into; it makes power available.
 * The CORRECTED two-terminal flow
 * (INVESTIGATION_area11_elevator.md "OUTSIDE BATTERY TERMINAL"). When
 * used:
 *   - has battery && !powered -> the insert path: the player insert
 *     clip + lock via em_game_player_interact_anim(0x14) THEN
 *     em_game_set_terminal_powered(1). No descent here.
 *   - already powered          -> brief no-op (already inserted)
 *   - no battery               -> a short "need battery" refusal (the
 *     refusal/cooldown path), so the player learns the battery is
 *     required.
 * FLAGGED (INVESTIGATION_area11_elevator.md): the insert clip 0x14 and
 * any insert cinematic/letterbox were decoded under a FORCED game state
 * and may be wrong — this ships the faithful-MINIMUM (anim + lock + set
 * power), not an elaborate vault-door cutscene.
 * The ELEVATOR parser (em_game.c) calls this after em_examine_add when
 * it sees the "battery_terminal" token. Returns 0 / -1 (bad slot). */
int em_examine_set_battery_terminal(int slot);

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
 * -1.3037 rad (script 0x82A990 record+0x24). FLAGGED: the OUTSIDE battery
 * terminal's face-yaw was NOT decoded — the manifest ships an APPROXIMATE
 * value (face toward the terminal actor). `walk_frames` > 0 (the
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
