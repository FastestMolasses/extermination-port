/* em_examine.h — placed EXAMINE objects: the engine's use-scan examine
 * interaction, decoded 2026-06-11 (decomp repo Extermination/docs/
 * FINDINGS.md "EXAMINE INTERACTION DECODED", session 66).
 *
 * ENGINE TRUTH. Examine-able objects are MAIN PLACEMENT-TABLE records
 * with the interactive class flag 0x80 whose behavior function lives
 * in the AREA OVERLAY (the per-area .text at 0x823500+). The shape,
 * read in full from four overlay behaviors (AREA02 0x824FA0, AREA07
 * 0x823DA0, AREA11 0x827B10, AREA06 0x824340):
 *
 *   INIT     model bind (func_001B0FD0) + TRS bake (func_001C6380);
 *            +0x08 = use-scan ARCHETYPE (3 = the item branch for the
 *            office corpse; 1 = desc-point for the AREA11 switch;
 *            0 default for AREA06), +0x30 = the use-scan DESC —
 *            the examine desc D_002758E0 = {20.0, 10.0} (XZ ring,
 *            dy window; vs the item desc {10.0, 3.5}).
 *   ARMED    the player use scan (func_00184BA0, CROSS press edge,
 *            s58) walks the interactive list, runs the archetype
 *            geometry test (func_00183EF0) and sets +0x0B = 4 on the
 *            nearest passer. The behavior's sub-state 0 polls
 *            +0x0B & 4 and QUEUES the area's EXAMINE SCRIPT
 *            (func_001BA1A0 primes the 0x40-byte-record interpreter,
 *            func_001BA1F0 pumps it).
 *   SCRIPT   the decoded examine scripts share one shape:
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
 *                  only, 300-frame re-arm cooldown. (The powered path
 *                  0x82A750 is gated on the D_00810841 unlock bit —
 *                  0 at new game; not modeled.)
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
 *       examinetext <dur> <gap> <text-with-\n-escapes...>
 *     `examinetext` appends one chained AREA-bank record (text shown
 *     `dur` frames, then `gap` blank frames) to the LAST examine.
 *     `gline` = GLOBAL bank line: presented through em_hud_radio
 *     (text from messages.emsg group 9, the engine duration table).
 *   - Scan = the decoded archetype-3 geometry (XZ ring, dy in
 *     [-(dy+17), +dy], pi/4 facing with the 7-u auto pass, nearest
 *     wins) anchored at the manifest position. The AREA11 archetype-1
 *     desc point ships AS the manifest position (engine values); its
 *     facing-yaw extra (desc[5]) is not modeled — FLAGGED.
 *   - Sequence: input pause (movement + menu) for the whole script,
 *     pre-delay, then the message; the camera cue (when present) cuts
 *     at sequence start and holds until done — the engine starts the
 *     message first and runs its op00 records during it (the exact
 *     op00 spline/second-vector semantics are undecoded) — FLAGGED.
 *     No enter/exit fade (op07 sub2's fade arm) — FLAGGED.
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

/* Add one examine object. gline = GLOBAL bank line id, or -1 for an
 * AREA-bank text chain (append records with em_examine_text). `cam` =
 * the op00 camera-cue eye, or NULL for no cue (the chase keeps the
 * camera). Returns the slot index or -1. */
int em_examine_add(const float pos[3], float yaw, float dist, float dy,
                   int gline, int delay, int cooldown, const float cam[3]);

/* Append one chained area-bank record (text for `dur` frames, then
 * `gap` blank frames) to instance `slot`. Returns 0 / -1. */
int em_examine_text(int slot, int dur, int gap, const char *text);

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
