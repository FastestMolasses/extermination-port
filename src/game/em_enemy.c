/* em_enemy.c — enemy actors (see em_enemy.h for the engine mapping and
 * the flagged fidelity deviations).
 *
 * THREE BRAINS share this module's slot pool (s62 condition decode of
 * the splat disassembly + the s68 creature-identity correction —
 * every trigger below is read off the instructions, not inferred;
 * the BUG brain is decoded structurally s76 with flagged port
 * magnitudes, see its block):
 *
 * THE PLACED CRAWLER / CRATE (FINDINGS "ENEMY AI ARCHITECTURE" §3,
 * func_001551B0 — the port's EM_ENEMY_KIND_CRATE; engine lifecycle
 * values kept):
 *
 *   0 INIT    HP(+0x34) = 1, base heading, the 4 diagonal probe
 *             directions, the floor probe -> on-surface +0x52 -> 4.
 *   4 IDLE    poll the +0x36 mailbox: ANY nonzero kills (HP = 1) -> 2,
 *             broadcasting the group alarm (+0x0A) to every live actor
 *             with a placed-crawler model byte {6,0x1C,0x1E,0x1F,0x50}
 *             and the on-surface flag — the WHOLE live list, no radius.
 *             Else own alarm set -> clear it, +0x2A = 6, -> 1.
 *             Else the disguise jitter. THE ENGINE HAS NO PROXIMITY
 *             TEST HERE — state 4 never reads the player position; the
 *             old ~10-u burst trigger and the 32-u wake were port
 *             inventions and are REMOVED.
 *   1 ATTACK  the suicide hop-run, and it is BLIND: no player reference
 *             anywhere in the engine's state 1. sub 0 STEER: +0x2A--;
 *             probe the 4 diagonals; >= 3 blocked (or both opposite
 *             pairs) -> hold, and at +0x2A == 0 -> back to 4 (the
 *             pending mailbox then kills it on the next IDLE tick —
 *             decoded deferral). Else rotate the heading +-0.0524 rad
 *             away from a blocked side, or RNG-perturb it (+-1/120 rad)
 *             when open; first launch arms the attack timer +0x2A = 180
 *             (0xB4; variant 6 instead uses 5x its height — n/a here).
 *             sub 1 HOP: integrate, 0.052/tick gravity. Timer expired
 *             or surface lost -> CLEAR +0x36 (damage taken mid-run is
 *             absorbed) -> 2. STATE 1 NEVER POLLS THE MAILBOX — the
 *             old IDLE+ATTACK poll widening is removed. (Port
 *             locomotion stand-in: repeated hops with re-steer on
 *             landing; the engine runs one long leap.)
 *   2 DEATH   engine: NEST-CHILD spawns (the s68 registry: BUGS or
 *             items — see "CRATE KIND" below) + gore FX + a MODEL
 *             REBIND to the burst-husk/gib models (library 0x22/0x29),
 *             and — when killed by DAMAGE (+0x36 nonzero, variants
 *             6/0x1E) — knockback: the hit vector in scratch
 *             D_700036E0 RNG-rotated (90/180/270 deg), velocity set
 *             from it, then a corpse slide that settles on the floor.
 *             Port: gameplay despawns immediately; on a LETHAL HIT the
 *             visual layer launches 3-5 GIB instances (the exported
 *             library burst set, assets/gibs/ — see "GIB LAYER"
 *             below) with exactly that knockback shape; the
 *             contact/suicide burst (mailbox empty, engine takes the
 *             no-knockback arm) and the missing-assets case keep the
 *             corpse placeholder: the frozen pose ALPHA-FADES out in
 *             place (white tint, 1 -> 0 over ENEMY_FADE_FRAMES
 *             through em_gfx_draw_skinned_tinted — the engine fades
 *             dead actors by walking the actor alpha down before
 *             freeing).
 *   3 FREE    slot inactive.
 *
 * THE BUG (s68 "CREATURE IDENTITY CORRECTION" — the port's
 * EM_ENEMY_KIND_BUG; full ledger in em_enemy.h "BUG KIND"). Decoded,
 * kept: HP 15 (variant A — func_00128390; variant B 30 and the
 * difficulty 30/50 column are recorded constants), the EVERY-TICK
 * +0x36 mailbox consumption (func_00128B80) with FLINCH below lethal
 * and DEATH at it (handler func_00129FC0), init/walk clip 1 (the
 * 90-f in-place WALK), flinch clip 0x1D, death clip 0x1B (EXPORTED
 * since s76 — the anim baker now handles the non-sentinel container,
 * so the corpse plays the real collapse; the frozen-pose fade is the
 * fallback only if it is ever absent). REAL MULTI-STATE
 * BRAIN (s76 — the two brains func_00128C10/func_0012A5D0 are decoded at
 * the STRUCTURAL level, FINDINGS "BUG BRAIN STATE MACHINES"; the
 * approach/lunge MAGNITUDES + the bite damage stay flagged port
 * constants — the move-helper bodies + the shared contact-damage value
 * are the s76 open items):
 *
 *   INIT      HP = 15, yaw toward the player (PORT stand-in: the
 *             engine copies the nest record's rot, unexported) ->
 *             ATTACK sub 0.
 *   sub 0     APPROACH (== pre-s76): home the yaw at BUG_TURN_RATE
 *             (PORT), walk BUG_WALK_SPEED (PORT) with the shared probe +
 *             floor follow to BUG_STANDOFF; there, inside the
 *             BUG_AIM_CONE facing gate -> WINDUP (sub 2). NO travelling
 *             lunge: the decode gives the bite clip + the contact box,
 *             not a travel speed, so position behaviour matches the old
 *             approach and the strike is the new, decoded part.
 *   sub 1     FLINCH: hold for the flinch clip's length (20-tick
 *             fallback without the asset), then sub 0. Set by
 *             enemy_tick's every-tick mailbox path (a hit interrupts
 *             any attack phase into flinch).
 *   sub 2     WINDUP: the bite lead-in, BUG_WINDUP_F ticks -> BITE.
 *   sub 3     BITE (clip 0x13, func_0012C490): IN PLACE; the contact is
 *             the shared melee resolver func_001B5360 — a BUG_CONTACT_R
 *             (=6, VERIFIED) sphere BUG_CONTACT_FWD (=10, VERIFIED)
 *             ahead of the bug vs the player. A HIT -> LATCH (sub 5),
 *             not a one-shot bite (s76, user-confirmed: the bug clings
 *             and the player shakes it off with clip 54).
 *   sub 4     RECOVER + cooldown (func_0012DD70), BUG_RECOVER_F -> sub 0.
 *   sub 5     LATCHED (s76): clings on the player at a per-bug bearing
 *             and drains (player-side player_shake_tick) until the
 *             player's shake-off (em_enemy_shake_off) throws it back to
 *             RECOVER with a knockback. The engine sets D_008102B0|=2 +
 *             the drain D_008104D4 + the anchor D_00810320; the cling
 *             geometry is a flagged PORT stand-in.
 *   DEATH     (state) gameplay slot frees immediately; the corpse plays
 *             the real DEATH clip 0x1B (s76) then holds + alpha-fades (no
 *             gibs — the husk set is the crate's; the bug's own gore
 *             chain is undecoded). Death sound: the shared 0x7D8 hurt-
 *             helper arm (PORT stand-in — func_00129FC0's audio undecoded).
 *
 * THE WORM / LEECH (FINDINGS §4, brain func_00153F10 + sub-machine
 * func_00154120, init func_00154040 — the port's EM_ENEMY_KIND_CRAWLER;
 * the kind-0xD creature the mode-2 generator pads emit — its ONLY
 * installer, s68: crate bursts never hatch it). It is
 * BORN ATTACKING — the engine brain has no idle state, no alarm read,
 * and no proximity gate; "target acquisition" is unconditional:
 *
 *   INIT      HP = 10 (func_00154040 — VESTIGIAL, see DEATH), yaw =
 *             atan2 toward the player
 *             mirror (D_00810350/58) -> ATTACK sub 0.
 *   sub 0     APPROACH: play the bound anim out (anim-gated by the
 *             0x1000 done bit in the engine; the bound id chain is
 *             unverified — the port maps it to the bank's 90-f emerge
 *             clip and HOLDS position: the loco clips are baked in
 *             place and the brain itself writes no position). The
 *             engine also runs the LATCH QUERY here — CORRECTED s66
 *             LIVE: func_0019AA80(slotA, slotB, 0x20) stages the
 *             worm's OWN neck->head rig segment (node-table slots
 *             +0x34/+0x40 = rig nodes 13 and 16, ~6 u apart) in spad
 *             0x70003190 and sweeps it against the PLAYER's
 *             hit-volume list (player +0x58, bone-anchored sphere
 *             records; the 0x20 is a FILTER-MASK channel, not a 32-u
 *             radius — func_001A7280) — latching the player at
 *             D_008104D4 = 5.0 when status == 1: UNTRANSLATED (no
 *             latch/shake-off system in the port; flagged).
 *   sub 1     STALK: +0x28 = 120 ticks (0x78), homing the yaw toward
 *             the player at 0.0698 rad/tick (0x3D8EFA35,
 *             func_001B12B0). Port locomotion stand-in: slides forward
 *             while homing (the engine's stalk root motion, if any, is
 *             the anim's — unexported).
 *   sub 2     WINDUP: anim out (the 45-f windup clip window); at the
 *             end SNAP the yaw to the player bearing (decoded) and
 *             play sound 0x431.
 *   sub 3     LUNGE: travel at the lunge clip's authored 21.27 u/s
 *             along the snapped yaw for the 120-f clip window. Each
 *             tick the engine resolves: the neck->head SEGMENT query
 *             (the same func_0019AA80 shape as sub 0) -> burst,
 *             latching D_008104D4 = 15.0 (the lunge hurts more); else
 *             func_0019A570 radius-6 contact -> burst (NO latch); else
 *             clip end -> state 3 DESPAWN (released — no burst, no
 *             gore). Port: the segment arm runs against a PLAYER
 *             CAPSULE stand-in for the unexported hit-volume list
 *             (worm_latch_segment below; the worm endpoints are the
 *             REAL palette nodes 13/16 when the leech asset is
 *             loaded), the radius-6 arm follows latch-free — engine
 *             order. Without rig data both arms fold into the
 *             radius-6 contact carrying the 15 (flagged fallback).
 *             Miss -> despawn.
 *   2 DEATH   burst: engine sound 0x434 + gore 0x80000052 + release.
 *             The worm is NOT SHOOTABLE — J2 CLOSED s66: both victim
 *             filters (func_00183AC0 / func_00183B80) reject model
 *             0x0D by name, and a live full-lifecycle memcheck saw
 *             ZERO +0x34/+0x36 accesses besides the release
 *             teardown's `sh zero, 0x36` (func_001AFC10), with two
 *             shots fired into it mid-stalk. HP=10 is vestigial init
 *             data; the old "unfound HP consumer" open item is CLOSED
 *             (there isn't one) and the port's shootable-worm mailbox
 *             poll is REMOVED. The only worm deaths are its own
 *             burst (lunge resolve) and the missed-lunge despawn; the
 *             gib knockback launch remains the DAMAGE-kill arm, i.e.
 *             crates only (engine variants 6/0x1E — s62).
 *
 * ANIMATION LAYER (FINDINGS "CRAWLER RESOLVED" section 4 — the leech
 * clip bank, 4 clips at 60 fps): a VISUAL layer driven BY the state
 * machine above. The worm's sub-0/2/3 windows are the CLIP LENGTHS at
 * rate 1.0 (the engine gates those subs on the anim-done bit 0x1000;
 * the port uses the same fixed counts with or without the asset, so
 * gameplay timing never depends on what loaded):
 *
 *   spawn/sub 0  clip 1 (emerge, 90 f) once — the approach window
 *   sub 1 STALK  clip 0 (crawl, 239 f, in-place) looped at the actual
 *                ground speed (21.27 u/s = 1.0x)
 *   sub 2        clip 2 (windup, 45 f) once
 *   sub 3        clip 3 (lunge, 120 f) once — the resolve window
 *   crate IDLE   no clips (1-node static mesh): the disguise jitter
 *   DEATH        no clip exists (engine rebinds gib MODELS instead) —
 *                frozen pose alpha-fades out (the per-draw tint path)
 *
 * Clip transitions crossfade over 0.15 s with the same linear palette
 * blend as the player path (em_game.c ANIM_BLEND_TIME — PROGRESS.md:
 * mid-blend live captures match no single clip).
 *
 * GIB LAYER (FINDINGS "GIB SET", decomp tools/export_props.py --gibs):
 * the burst-death visual. The engine rebinds the dead actor's model to
 * library entry 0x22 or 0x29 of chunk27/f01_id37.bin — the burst-husk
 * models — and knocks the corpse along the RNG-rotated hit vector.
 * The PICK is decoded (2026-06-11, func_001551B0 @0x156380): model
 * byte 6 -> husk A 0x22 (brown — the wooden crate of every exported
 * scene), else husk B 0x29 (grey-cyan; the 0x1C/0x1E/0x1F variants).
 * The exported set (one static 1-node EMDL per library entry, textures
 * from the office GS dump) also carries the husks' texture-paired
 * small chunk/shard meshes (0x1C/0x1D/0x1E share husk A's skin,
 * 0x26/0x27 husk B's, 0x28 = husk B at half size). On a lethal hit
 * the port spawns 3-5 instances from the MATCHING FAMILY (the husk
 * first — the engine's rebind corpse — then its chunks/shards
 * round-robin; GIB_FILES below), each launched with:
 *
 *   planar dir = the hit vector (attacker -> victim, port stand-in:
 *                player -> crawler) rotated by RNG in {90, 180, 270}
 *                deg — the DOCUMENTED choice set — plus a flagged
 *                +-30 deg port jitter so instances sharing a rotation
 *                separate;
 *   vertical   = launch pop + the engine's 0.052/tick gravity;
 *   landing    = the same floor query as the hop, then rest;
 *   exit       = after the ~3 s rest (180 ticks) the gib ALPHA-FADES
 *                in place over GIB_FADE_FRAMES (white tint, alpha
 *                1 -> 0 via em_gfx_draw_skinned_tinted — translucent
 *                draws disable the depth write, so a fading gib never
 *                occludes the scene) and frees. Same total lifetime
 *                as the old sink-despawn it replaces (180 + 30).
 *
 * Speeds/spin/jitter are port constants (flagged below); the RNG is a
 * tiny deterministic LCG so test runs and captures reproduce. Gib
 * instances are VISUAL ONLY: they draw through the same em_enemy_draw
 * chain contract as live crawlers (virtual indices >= the real slot
 * count, budgeted so the total never exceeds ENEMY_SLOT_MAX (16) —
 * em_game.c
 * sizes its render chain with it) and never touch gameplay state.
 *
 * EM_ENEMY_GIBDEMO=<frame>: debug hook — forces a lethal damage-death
 * on enemy 0 at that update tick, so EM_CAPTURE (frame 60) can
 * photograph the scatter without scripting a full kill run. (A DIRECT
 * kill since s66: a worm consumes no mailbox; on a crate the hook is
 * equivalent to the real damage path.)
 *
 * CRATE KIND (em_enemy.h "CRATE KIND"; FINDINGS "CRAWLER RESOLVED" +
 * the s26 office model-table carve): the engine's placed crawler IS the
 * disguised prop — the port spawns it as its own kind and now runs the
 * DECODED func_001551B0 machine (the state list at the top of this
 * header):
 *
 *   0 INIT    HP = 1 -> 4.
 *   4 IDLE    render the crate mesh with the PROCEDURAL jitter (the
 *             documented D_002468B0/B4/B8 x/z world-matrix perturbation
 *             — implemented as deterministic sines of the update tick:
 *             a slow chitter envelope gating a small x/z wiggle + yaw
 *             wobble; amplitudes/periods are flagged port constants;
 *             the engine runs the jitter in state 4 ONLY).
 *             Poll the +0x36 mailbox (HP 1: any hit is lethal, hit_dir
 *             = player -> crate) -> 2 + the GROUP-ALARM BROADCAST.
 *             Own alarm -> ATTACK: the crate HOPS AS THE CRATE (the
 *             engine's alarmed-crawler run — decoded, was untranslated)
 *             and suicide-bursts when the 180-tick attack timer runs
 *             out. DAMAGE is the only direct trigger — the engine has
 *             no proximity burst (the old ~10-u trigger is REMOVED).
 *   2 BURST   free the slot (no fade: the husk replaces it visually),
 *             hatch the NEST-GROUP BUGS at the crate position through
 *             the normal spawn path (s68: the engine's state-2 walks
 *             the registry group D_0024D820[area][base + link] and
 *             copies each 0x2C record into a child actor — pos +=
 *             parent, rot/param/behavior from the record; the office
 *             groups hold 2-3 bug records). The records are disc
 *             data, so the manifest carries the count (`bugs <n>`,
 *             default 2 — flagged) and the port stands in a small
 *             deterministic ring for the records' per-child offsets
 *             and the init yaw (flagged); then launch the husk gibs
 *             with the shared gib launcher (damage kills scatter
 *             along the hit vector; timer bursts along the facing).
 *             The pre-s68 WORM hatch is REMOVED (no nest anywhere
 *             installs func_00153F10 — generator pads only).
 *   3 FREE    slot inactive.
 *
 * GENERATOR KIND (em_enemy.h "GENERATOR"; FINDINGS "GENERATOR —
 * func_0015A2C0 RESOLVED", session 28): the engine's organic floor pad
 * that births worms while the player stands on it. Faithful pieces —
 * the decoded config footprints, count tables, 121-frame charge, the
 * 1800/3600/5400-frame delays, the 4-worm cap, worm-at-origin spawning
 * (the leech yaw-to-player applied at spawn, exactly like the crate
 * burst), per-tick trigger = player inside the config box (Y tolerance
 * +1.5), indestructibility (no mailbox, no hit sphere, excluded from
 * acquire/ray_test/alive), and the mode-1 breather's immediate
 * kind-0xE TENDRIL-FIELD pair (see "TENDRIL FIELD" below). Flagged
 * port stand-ins — the LCG replaces
 * the engine frame RNG for the mode draw and delay pick; the open-trap
 * player hit is a
 * one-shot mailbox 5 per box entry (engine: event-3 knockdown carrying
 * 5.0); the visual is an original placeholder mound scaled to the
 * decoded footprint with the +0x80 phase as a Y swell (the engine's
 * procedural VU morph — func_001E9580/001E9E60 per-instance buffers —
 * binds NO model-table entry, so there is nothing to export; sounds
 * 0x42F breathing / 0x430 worm emerge go through em_sfx and are
 * silent until the user's sfx.txt maps them).
 *
 * TENDRIL FIELD (em_enemy.h "TENDRIL FIELD"; FINDINGS "KIND-0xE
 * COMPANION RESOLVED" — brain func_001546C0, init func_00154740, tick
 * func_001549C0, render func_00154F00, trigger gate func_00154460):
 * the mode-1 pad's companion PAIR (func_0015A200(pad, 0xE, 0/1) at
 * generator init — the engine's only other dynamic generator child).
 * Each field actor owns 12 spike records and runs the decoded
 * SCAN -> DEPLOY -> HOLD -> RETRACT -> RESET machine:
 *
 *   SCAN     gameplay-frame gate (native: em_enemy_update only runs in
 *            gameplay frames) + player inside 3x the parent pad
 *            footprint, |dy| <= 3 + recY. Trigger: anchor = (player X,
 *            pad Y, player Z); 12 targets = anchor + polar(r, theta),
 *            theta uniform, r = 5.5 +- 2.0 u (pair idx 0) / 7.0 +-
 *            2.5 u (idx 1) — concentric rings; valid = target inside
 *            the 0.92x pad ellipse (the engine's atan2 + radius-at-
 *            angle formula == the normalized point-in-ellipse test);
 *            phase = rand 48..127, vel/ramp = 0, girth = the cycling
 *            {0xB4,0xDA,0xFF,0x180}/256 table (random start row);
 *            sound 0x42D if ANY target valid (engine range 300 —
 *            em_sfx has no positional attenuation, same note as
 *            0x42F); falls through to DEPLOY the same tick.
 *   DEPLOY   all 12 ramps += 37/tick clamp 300; 8-tick timer -> HOLD
 *            (ramps land on 296).
 *   HOLD     retract (timer 8) when the player leaves the Y band or
 *            moves dist^2 >= 4.0 (idx 0) / 16.0 (idx 1) from the
 *            anchor — the field stays up only while the player stands
 *            within 2 u / 4 u of where they triggered it.
 *   RETRACT  ramps -= 37/tick floor 0; 8 ticks -> RESET.
 *   RESET    one tick, back to SCAN (re-deploys indefinitely).
 *
 * Render tail (every tick while sub != 0): per VALID record the s16
 * bob integrator — phase += vel; parent breather phase > 0.5 (pad
 * opening) = violent thrash (vel -= 8/tick, kick +28..41 while phase
 * < 128) vs closed = gentle bob (vel -= 1/tick, kick +3..7 below 128,
 * vel halved at >= 8); floor clamp phase < 100 -> 100 with a fresh
 * 3..7 vel. Then the per-spike TRS: scale X/Z = girth/256, scale Y =
 * phase*ramp/65536, position = the record's world X/Z at pad Y — 12
 * re-posed draws of the ONE static spike mesh (assets/tendril.emdl,
 * the chunk03/f13_id15.bin export; load-if-present, else a logged
 * draw skip — the machine still runs), through the same virtual-slot
 * chain contract as the gibs. The field deals NO damage (the pad's
 * mailbox hit covers mode 1), has no HP/mailbox, and is excluded from
 * acquire/ray_test/alive exactly like its parent. The engine's render
 * tail TINT now applies (em_gfx_draw_skinned_tinted through the chain
 * tint contract): RGB blends the room tint toward the vivid
 * (6,92,1)/128 green as the parent pad's open phase rises, alpha =
 * ramp/300 (the deploy fade-in/out) — see TF_TINT_* below for the
 * room-tint TODO. Flagged port simplifications: the module LCG stands
 * in for the engine frame RNG (scatter/phase/girth/kicks); sound
 * 0x42D is UNMAPPED in the generated sfx.txt (soundmap pins it to
 * sfx/snd_0615.wav — noted in the registry, silent until mapped).
 *
 * EM_ENEMY_TEST=5 (tendril run — owned here like test 4; em_game.c
 * arms only 1..3): frame 0 places a link-1 pad (kind 1, the 30x30
 * footprint) at the player spawn and FORCES the runtime mode to 1
 * (the link-1 table draw is RNG 0/1; the test pins the breather
 * outcome — test-only, documented). A SYNTHETIC walker then
 * substitutes the player position THIS module sees (the run is
 * self-contained; the real player, camera and other modules are
 * untouched): it walks in from outside the 46.5-u start mark to the
 * pad center (the field pulses while it moves — each deploy's HOLD
 * breaks as the walker leaves the 2/4-u anchor radius), stands 50
 * ticks (steady HOLD: both fields sub 2, 12/12 valid targets, every
 * ramp at 296 = the 8-tick deploy, 24 spike draws when the mesh is
 * present), walks off (both fields RETRACT within the hold radii),
 * and exits the box (both fields rest at sub 0, all ramps 0).
 * PASS/FAIL line + quit, like tests 1..4.
 *
 * Manifest placement: em_game.c's scene_manifest_load parses the
 * `enemy generator x y z yaw [kind k] [link n]` lines (defaults
 * kind 1, link 2 for bare lines — port defaults; engine placements
 * always carry explicit values) and dispatches each to
 * em_enemy_add_generator at scene-load time, like every other enemy
 * kind.
 *
 * EM_ENEMY_TEST=4 (generator run — owned here; em_game.c only arms
 * values 1..3 and skips manifest generator lines while this test is
 * on, so the run is self-contained): frame 0 places a kind-2
 * generator 14 u ahead of the idle player (inside the 25-u box),
 * FORCES mode 2 (the link-2
 * table draw is RNG; the test pins the interesting outcome) and
 * divides the spawn delays by 60 (test acceleration — 30/60/90 s is
 * the shipped pacing). The script then asserts: no worm before the
 * 121-frame charge completes; each worm spawns AT the generator
 * origin; a 0x400A mailbox write 15 frames after worm 1 does NOT
 * kill it (J2 s66: the worm consumes nothing — the engine-true
 * non-consumption witness) while its OWN lunge lifecycle despawns it
 * within the resolve window; the generator keeps emitting; exactly 4
 * worms then EXHAUSTED (mode-2 sub 2), with a 240-frame silence
 * window proving no 5th spawn. PASS/FAIL line + quit, like tests 1..3.
 */
#include "game/em_enemy.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_model.h"
#include "game/em_frame.h"   /* em_frame_gfx + em_frame_request_quit
                              * (the EM_ENEMY_TEST=4 harness)          */
#include "game/em_sfx.h"

#define ENEMY_ASSET      "assets/enemy_crawler.emdl"
/* GLOBAL default crate disguise = the WOODEN shipping crate (the n0
 * leaf-table entry 0x0D carve — dark planks, metal corner straps,
 * stenciled freight markings; decomp FINDINGS s34: export_props.py
 * --crate --crate-dir extract/chunk06.n0, 2026-06-11 asset switch).
 * User-confirmed fidelity: the crate rooms use the large wooden boxes.
 * TABLE NUANCE (recorded honestly): the s28/s34 decode found the
 * CARDBOARD box in the n1 (office sub-state 1) leaf table — but no
 * sub-state-1 placement record spawns a crate (the captured office
 * scene places ZERO crawlers), so no shipped scene genuinely binds the
 * cardboard model and it ships nowhere; it stays available locally as
 * assets/enemy_crate_cardboard_n1.emdl, and a scene that ever proves
 * to bind it can carry it as <scene>/props/enemy_crate.emdl (the
 * scene-local probe below). */
#define CRATE_ASSET      "assets/enemy_crate.emdl"
/* The BUG hatchling (s68): GLOBAL creature slot 0x0F = variant A —
 * the port's only bound variant; slot 0x10 (variant B, the event-flag
 * 0x30 story swap) ships as assets/enemy_bug_infected.emdl but the
 * flag machinery is unmodeled, flagged in em_enemy.h "BUG KIND". */
#define BUG_ASSET        "assets/enemy_bug.emdl"
#define ENEMY_BONE_MAX   32
#define ENEMY_PI         3.14159265f

/* The crawler/gib SLOT pool — the original 16-slot budget (EM_ENEMY_MAX
 * grew into the render-chain DRAW capacity when the tendril fields
 * landed; the gameplay pools below are unchanged, so tests 1..4 and the
 * gib RNG stream stay byte-identical). */
#define ENEMY_SLOT_MAX   16

/* --- Engine constants (FINDINGS "ENEMY AI ARCHITECTURE" + the s62
 * condition decode of func_001551B0/func_00153F10/func_00154120 —
 * every value below is read off the disassembly) ----------------------- */
#define ENEMY_HP_CRATE   1        /* placed crawler init HP (+0x34 = 1)   */
#define ENEMY_HP_WORM    10       /* worm/leech init HP (func_00154040)
                                   * — VESTIGIAL (J2 s66 live: nothing
                                   * ever reads it; kept so the slot
                                   * mirrors the engine actor exactly)    */
#define ENEMY_TURN_RATE  0.0524f  /* +-3 deg/frame steer-away (0x3D56774F)*/
#define ENEMY_HOMING_RATE 0.0698f /* worm STALK homing, rad/tick
                                   * (0x3D8EFA35 -> func_001B12B0)        */
#define ENEMY_GRAVITY    0.052f   /* hop vertical integration, per tick
                                   * (0x3D54FDF4 = 0.051999)              */
#define ENEMY_CONTACT_R  6.0f     /* lunge-resolve second arm: radius-6
                                   * contact (func_0019A570(a, b, 6, 0) —
                                   * latch-FREE in the engine); also the
                                   * rig-less fallback fold of the
                                   * segment arm (then it carries the 15) */
/* LATCH SEGMENT (s66 live decode): the engine resolve's FIRST arm is
 * func_0019AA80(slotA, slotB, 0x20) — the worm's neck->head rig
 * segment (node-table slots +0x34/+0x40 = rig nodes 13/16) swept
 * against the PLAYER's hit-volume list (player +0x58: bone-anchored
 * sphere records gated by three filter bytes; mask 0x20 selects
 * filter channel 1 — func_001A7280). The port stages the segment from
 * the worm's OWN animated palette (translation columns of nodes
 * 13/16, world space after enemy_build_palette — one tick stale, the
 * pose the player SEES) and sweeps it against a PLAYER CAPSULE: the
 * volume list's radii/anchors are unexported, so the capsule is a
 * flagged stand-in sized from the player's known body numbers (wall
 * radius 4.5, ~17-u height). */
#define ENEMY_LATCH_NODE_A 13     /* neck — node-table slot +0x34 (s66)   */
#define ENEMY_LATCH_NODE_B 16     /* head — node-table slot +0x40 (s66)   */
#define ENEMY_ACTOR_SCALE 0.5f    /* func_00154040 -> actor +0x80: the
                                    * live leech is HALF authored size
                                    * (decoded; FINDINGS "CRAWLER
                                    * RESOLVED" §4)                      */
#define ENEMY_STALK_STANDOFF 10.0f /* stalk-slide stop distance (PORT
                                    * locomotion stand-in, flagged: the
                                    * engine's stalk root motion is its
                                    * anim's; the standoff keeps the
                                    * CONNECT on the lunge resolve)      */
#define PLAYER_HV_R      4.5f     /* capsule radius — the engine wall
                                   * radius (PORT stand-in, flagged)      */
#define PLAYER_HV_Y0     2.0f     /* capsule foot, above ground Y (PORT)  */
#define PLAYER_HV_Y1     15.0f    /* capsule head, above ground Y (PORT)  */
#define ENEMY_STEER_TICKS  6      /* +0x2A = 6 at the alarm wake          */
#define ENEMY_ATTACK_TICKS 180    /* +0x2A = 0xB4 at the hop launch: the
                                   * crate's suicide-run timer (variant 6
                                   * instead uses 5x its height — n/a)    */
#define ENEMY_APPROACH_F 90       /* worm sub 0 window: the engine gates
                                   * on its bound anim's end (id chain
                                   * unverified) — port mapping: the
                                   * bank's 90-f emerge clip              */
#define ENEMY_STALK_TICKS 120     /* worm sub 1: +0x28 = 0x78 (decoded)   */
#define ENEMY_WINDUP_F   45       /* worm sub 2 window = the 45-f windup
                                   * clip (anim-gated in the engine)      */
#define ENEMY_LUNGE_F    120      /* worm sub 3 window = the 120-f lunge
                                   * clip (anim-gated in the engine)      */
#define ENEMY_LATCH_LUNGE 15      /* D_008104D4 = 15.0 — the lunge-latch
                                   * magnitude (replaces the invented
                                   * 0x400A/10 contact code)              */
#define ENEMY_LATCH_TOUCH 5       /* D_008104D4 = 5.0 — the approach
                                   * latch; UNTRANSLATED (no latch/shake-
                                   * off system), recorded for fidelity   */
#define ENEMY_SFX_WINDUP 0x431u   /* leech windup -> lunge snap           */
#define ENEMY_SFX_BURST  0x434u   /* leech burst (func_00153F10 state 2)  */

/* --- Animation (the leech clip bank — FINDINGS "CRAWLER RESOLVED" §4,
 * clip ids = source container indices in the EMD3 clip table) --------- */
#define ENEMY_CLIP_CRAWL   0u     /* crawl/stalk loop, 239 f, in-place    */
#define ENEMY_CLIP_EMERGE  1u     /* spawn/emerge, 90 f, one-shot         */
#define ENEMY_CLIP_WINDUP  2u     /* lunge windup, 45 f, one-shot         */
#define ENEMY_CLIP_LUNGE   3u     /* lunge, 120 f, baked in-place; the
                                   * authored root travel = 21.27 u/s     */
#define ENEMY_LUNGE_SPEED  21.27f /* u/s at playback rate 1.0 (above)     */
#define ENEMY_ANIM_BLEND   0.15f  /* crossfade seconds (= em_game.c's
                                   * ANIM_BLEND_TIME player crossfade)    */

/* Anim-layer port tunings (visual only; flagged — not engine values) */
#define ENEMY_ATTACK_MIN   0.35f  /* rate floor while turning in place
                                   * (ground speed 0 must not freeze it)  */
#define ENEMY_FADE_FRAMES  30     /* DEATH placeholder: corpse alpha fade
                                   * 1 -> 0 (em_gfx_draw_skinned_tinted —
                                   * replaces the old sink-below-the-floor
                                   * stand-in from the no-per-draw-alpha
                                   * era)                                 */

/* --- Gib layer (see "GIB LAYER" in the file header) --------------------
 * Engine values: the rotation choice set and gravity. Everything else is
 * a flagged port constant (launch speeds are not exported from the disc). */
#define GIB_FAMILY_N     2        /* the engine's two husk families       */
#define GIB_FAM_A        0        /* model byte 6 -> husk 0x22 (wooden)   */
#define GIB_FAM_B        1        /* other variants -> husk 0x29          */
#define GIB_FAM_FILES    4        /* loadable models per family           */
#define GIB_BONE_MAX     4        /* exporter writes 1+1 palette slots    */
#define GIB_COUNT_MIN    3        /* instances per burst: 3..5            */
#define GIB_COUNT_SPAN   3
#define GIB_ROT_STEP     1.5707963f /* the documented 90-deg RNG steps    */
#define GIB_JITTER_DEG   30       /* PORT: +-30 deg spread inside a step  */
#define GIB_SPEED        0.28f    /* PORT: planar launch, units/frame     */
#define GIB_VY           0.45f    /* PORT: vertical pop (0.052 gravity)   */
#define GIB_SPIN_MAX     0.25f    /* PORT: yaw tumble, rad/frame          */
#define GIB_LAUNCH_LIFT  1.5f     /* PORT: spawn height above the feet    */
#define GIB_REST_FRAMES  180      /* ~3 s rest on the floor               */
#define GIB_FADE_FRAMES  30       /* then alpha 1 -> 0 over the last 30
                                   * frames (white tint, translucent
                                   * depth-write-off draw) and free       */

/* The exported burst set this module launches (decomp repo
 * tools/export_props.py --gibs; library entry in the name), split
 * into the engine's TWO HUSK FAMILIES. The pick is DECODED
 * (2026-06-11, func_001551B0 state 2 @0x156380 — closes the s24
 * "which husk binds to which variant" open item): the damage-kill
 * arm reads the crawler MODEL byte (+0x03) and rebinds the corpse
 * model (D_0028A56C library) to entry 0x22 when the byte is 6, else
 * 0x29. Byte 6 is EVERY crate in AREA02/11/13/18/20/22 — the WOODEN
 * crate — and husk A is its brown opened-crate base with the
 * texture-paired splinters 0x1C/0x1D/0x1E; the grey-cyan husk B
 * family (0x29 + chunks 0x26/0x27 + the half-size 0x28) belongs to
 * the 0x1C/0x1E/0x1F variants (AREA03/06/07/08 placements). The old
 * single mixed pool led with husk-B pieces — the wrong debris for
 * the wooden crate (user-reported; this split fixes it). Launch
 * order: the family HUSK first (it IS the engine's rebind corpse),
 * then its shards/chunks round-robin. Missing files shrink a
 * family; an empty family falls back to the corpse-fade
 * placeholder (no regression). */
static const char *const GIB_FILES[GIB_FAMILY_N][GIB_FAM_FILES] = {
    {   /* husk A family — brown crate tones (TBP 0x22F9) */
        "assets/gibs/gib_22.emdl",    /* husk A: opened 14x14 base  */
        "assets/gibs/gib_1c.emdl",    /* splinter A1                */
        "assets/gibs/gib_1d.emdl",    /* splinter A2                */
        "assets/gibs/gib_1e.emdl",    /* splinter A3                */
    },
    {   /* husk B family — grey-cyan (TBP 0x229B) */
        "assets/gibs/gib_29.emdl",    /* husk B: 14x14, 8 tall      */
        "assets/gibs/gib_26.emdl",    /* chunk 1                    */
        "assets/gibs/gib_27.emdl",    /* chunk 2                    */
        "assets/gibs/gib_28.emdl",    /* husk B at half size        */
    },
};

/* --- Crate kind (see "CRATE KIND" in the file header) -------------------
 * Engine values: HP 1, the damage-only burst trigger and the jitter
 * MECHANISM (x/z world-matrix perturbation). The jitter amplitudes and
 * periods are flagged port constants (the D_002468B0 tables are not
 * exported). The old ~10-u proximity trigger was a port invention —
 * REMOVED (the engine's state 4 never reads the player position). */
#define CRATE_BONE_MAX   4        /* exporter writes 1+1 palette slots    */
/* The office/AREA02 crate disguise (model id 0x0D) is a 14x14x14 box,
 * bbox X[-7,7] Z[-7,7] Y[0,14] — origin at the FLOOR, visual centre Y=7
 * (FINDINGS s28/s34; the shipped enemy_crate.emdl EMD3 header agrees).
 * The engine hits it as the FULL box collision hull (the movable-object
 * hull, any Y 0..14 — func_0019A570 mask bit0), NOT a low sphere, so a
 * shot anywhere on the box lands. The port ray-tests that box hull
 * (crate_ray_box / em_enemy_ray_test); the old Y=2 r=3.5 sphere covered
 * only the box's bottom ~40%, forcing the player to aim BELOW the visual
 * centre to connect (user-reported 2026-06-12). */
#define CRATE_BOX_HXZ    7.0f     /* X/Z half-extent of the 14^3 box      */
#define CRATE_BOX_TOP    14.0f    /* box top (above the floor origin)     */
#define CRATE_AIM_Y      7.0f     /* reticle / auto-aim point = box centre */
#define CRATE_HIT_R      8.0f     /* sphere radius (box hull is primary;
                                   * kept for any non-box fallback path)  */
#define CRATE_JIT_POS    0.08f    /* PORT: x/z wiggle amplitude, units    */
#define CRATE_JIT_YAW    0.02f    /* PORT: yaw wobble amplitude, rad      */
#define CRATE_BUGS_DEFAULT 0      /* nest-group fallback for a manifest
                                   * crate line WITHOUT `bugs <n>`: the
                                   * gore-only majority (12 of the office's
                                   * 17 are link -1 = no bugs, s68), so a
                                   * tag-less crate must NOT invent a nest.
                                   * The 5 real nests always carry an
                                   * explicit `bugs <n>`. (Was 2 — that
                                   * hatched bugs from every tag-less crate,
                                   * user-reported 2026-06-12.)            */
#define CRATE_BUG_RING   1.5f     /* PORT: child hatch-ring radius — a
                                   * stand-in for the nest records'
                                   * per-child pos offsets (unexported)   */

/* --- Bug kind (s68/s76 — see "THE BUG" in the file header) --------------
 * Engine values: HP, the every-tick mailbox consumption, the clip ids,
 * and (s76) the attack SHAPE — the brains' state machine is decoded
 * (FINDINGS "BUG BRAIN STATE MACHINES"): spawn-pose -> sense-gated
 * approach -> IN-PLACE bite (clip 0x13, func_0012C490) -> recover, with
 * the hurt/death already wired. The bite's contact is the shared melee
 * resolver func_001B5360: a radius-6 sphere ~10u ahead of the bug vs the
 * player (BUG_CONTACT_FWD/_R below are READ from that function). The
 * bite TIMERS and BUG_BITE_DMG stay FLAGGED PORT constants (the
 * per-attack timing and the shared contact-damage VALUE live in the
 * undecoded move-helper bodies + contact subsystem — s76 open). */
#define BUG_HP_A         15       /* func_00128390 variant A (slot 0x0F)  */
#define BUG_HP_B         30       /* variant B (slot 0x10) — recorded;
                                   * difficulty byte D_0081070A raises
                                   * the pair to 30/50 (unbound: the
                                   * port has no difficulty plumbing)     */
#define BUG_CLIP_WALK    1u       /* init clip: the 90-f in-place WALK    */
#define BUG_CLIP_DEATH   0x1Bu    /* func_00129FC0 death clip — EXPORTED
                                   * since s76 (the anim baker now handles
                                   * the non-sentinel container); the
                                   * corpse plays it. Falls back to the
                                   * frozen-pose fade only if ever absent. */
#define BUG_CLIP_FLINCH  0x1Du    /* func_00129FC0 flinch clip (exported) */
#define BUG_CLIP_BITE    0x13u    /* func_0012C490 bite/snap LUNGE clip
                                   * (19 dec — IS in the s68 bake list)   */
#define BUG_WALK_SPEED   0.16f    /* PORT: approach speed, units/frame    */
#define BUG_TURN_RATE    0.06f    /* PORT: homing yaw rate, rad/tick      */
#define BUG_STANDOFF     5.0f     /* PORT: approach stop distance — the
                                   * bug bites from here (the engine's
                                   * +10u/r6 box reaches the player at 5u:
                                   * |5-10| = 5 <= 6, see below)           */
/* s76 in-place bite cycle — magnitudes FLAGGED PORT unless VERIFIED. The
 * bug holds station at BUG_STANDOFF and strikes (no port-invented lunge
 * MOTION: the decode gives the bite clip + the contact box, NOT a travel
 * speed/distance — so the bug's POSITION behavior matches the pre-s76
 * approach, and the strike is the new, decoded part).                    */
#define BUG_AIM_CONE     0.5f     /* PORT: ~28 deg facing gate to commit  */
#define BUG_WINDUP_F     16       /* PORT: bite lead-in ticks             */
#define BUG_BITE_F       10       /* PORT: bite active/contact window     */
#define BUG_RECOVER_F    28       /* PORT: wind-down + cooldown ticks     */
#define BUG_CONTACT_FWD  10.0f    /* VERIFIED (func_001B5360): the attack
                                   * box is pushed +10u ahead of the bug  */
#define BUG_CONTACT_R    6.0f     /* VERIFIED (func_001B5360 -> _0019A570):
                                   * radius-6 contact sphere vs the player */
#define BUG_BITE_DMG     5        /* s76: func_001B5360 applies a per-
                                   * attack-class damage (jtbl_0026DEA0 on
                                   * entity+0x3 -> {2,3.5,4.2,5,6,6.5,8});
                                   * class 5 -> 5.0 is the candidate but
                                   * the bug's +0x3 is unpinned, so 5 stays
                                   * FLAGGED (also = the worm touch tier).  */
/* s76 LATCH / shake-off (user-confirmed: clip 54 = the player shaking the
 * bugs off): a connecting bite LATCHES the bug onto the player instead of
 * a one-shot bite — it clings and drains until the player shakes it off
 * (em_game's player_shake_tick -> em_enemy_shake_off). The engine sets
 * D_008102B0|=2 + the drain D_008104D4 + the attach anchor D_00810320;
 * the cling geometry below is a flagged PORT stand-in for that anchor.   */
#define BUG_CLING_R      1.3f     /* PORT: cling radius around the player  */
#define BUG_CLING_Y      1.0f     /* PORT: cling height on the body        */
#define BUG_SHAKE_PUSH   3.0f     /* PORT: knockback when shaken off       */
#define BUG_FLINCH_TICKS 20       /* flinch window fallback without the
                                   * clip (with it: the clip's length)    */
#define BUG_HIT_R        2.5f     /* PORT: bullet hit-sphere (the flat
                                   * ~3.7 x 1.9 x 8.9 authored body)      */
#define BUG_AIM_Y        1.0f     /* hit/aim center above the feet        */
#define BUG_WALK_MIN     0.35f    /* anim rate floor while standing
                                   * (PORT, = ENEMY_ATTACK_MIN's role)    */

/* --- Generator kind (see "GENERATOR KIND" in the file header) -----------
 * Engine values decoded from the boot ELF's .data this session (FINDINGS
 * "GENERATOR — func_0015A2C0 RESOLVED"); each table below is the decoded
 * CONTENT of the named engine datum. */
#define GEN_CHARGE_WORM   120.0f  /* mode-2 charge: spawn when +0x20+1
                                   * exceeds this = 121 in-box frames    */
#define GEN_CHARGE_OPEN   100.0f  /* mode-1 charge frames to OPEN        */
#define GEN_OPEN_HOLD     60.0f   /* mode-1 open hold (+0x20 = 60)       */
#define GEN_WORM_CAP      4       /* +0x2E limit (hardcoded slti 4)      */
#define GEN_BOX_Y         1.0f    /* config Y extent (1.0 in all 7 recs) */
#define GEN_BOX_Y_TOL     1.5f    /* func_001A8840 adds 1.5 to the Y test*/
#define GEN_TRAP_HIT      5       /* open-trap player damage (engine:
                                   * +0x22C = 5.0 with event 3; PORT:
                                   * one-shot mailbox write, flagged)    */
#define GEN_SFX_BREATH    0x42Fu  /* breathing, every 128 frames (open)  */
#define GEN_SFX_WORM      0x430u  /* leech init/emerge sound             */

/* D_00248120 — 7 config recs (engine kind 0..6): box half-extents X/Z
 * (field 1 = Y extent is 1.0 throughout; fields 3/4 zero). The same X/Z
 * doubled feed the engine's procedural pad geometry — the port scales
 * its placeholder mound by them. */
static const float GEN_CFG[7][2] = {
    {  5.0f,  5.0f }, { 15.0f, 15.0f }, { 25.0f, 25.0f }, { 10.0f, 15.0f },
    { 15.0f, 30.0f }, { 15.0f, 10.0f }, { 30.0f, 15.0f },
};

/* D_002481B0 (link 1) / D_002481D0 (link 2) — the mode draw: row =
 * frame RNG & 3, column = a global per-link counter & 7 (incremented
 * every draw). Byte = the runtime mode stored back into +0x56:
 * 0 inert, 1 breather/trap, 2 worm emitter. Link-1 placements can
 * never become worm emitters (table 1 holds only 0/1). */
static const uint8_t GEN_TBL[2][4][8] = {
    { { 0, 1, 0, 1, 0, 1, 0, 1 },
      { 1, 1, 0, 0, 0, 1, 0, 1 },
      { 0, 1, 0, 1, 0, 0, 0, 0 },
      { 1, 0, 1, 1, 0, 1, 0, 1 } },
    { { 0, 1, 2, 1, 0, 2, 0, 1 },
      { 1, 2, 1, 0, 2, 2, 0, 2 },
      { 0, 1, 0, 1, 2, 0, 2, 0 },
      { 2, 2, 1, 2, 0, 1, 0, 1 } },
};

/* D_002481F0 — inter-worm delay pool, frames (30/60/90 s at 60 Hz). */
static const float GEN_DELAY[3] = { 1800.0f, 3600.0f, 5400.0f };

/* Placeholder-mound visual (PORT, flagged): height of the unit mound
 * mesh and the phase-driven swell factor standing in for the engine's
 * +0x80 VU-morph blend. Original geometry, NOT disc data. */
#define GEN_PAD_HEIGHT    1.6f
#define GEN_PAD_SWELL     0.6f    /* Y scale grows to 1+this at phase 1 */
#define GEN_BONES         1

/* --- Tendril field (see "TENDRIL FIELD" in the file header) -------------
 * Engine values decoded in FINDINGS "KIND-0xE COMPANION RESOLVED"; the
 * only PORT items are the LCG (vs the engine frame RNG) and the pool
 * cap (the engine allocs from the global actor pool). */
#define TF_ASSET         "assets/tendril.emdl" /* chunk03/f13_id15.bin —
                                                * 96 verts, 1 node, spike
                                                * r~1.6 base, ~9.9 tall  */
#define TF_BONE_MAX      4
#define TF_SPIKES        12       /* records per field actor             */
#define EM_TENDRIL_MAX   8        /* field-actor pool = 4 pad pairs
                                   * (PORT cap; extra pads get a logged
                                   * skip, like an exhausted actor pool) */
#define TF_TRIG_MULT     3.0f     /* SCAN box: 3x the parent footprint   */
#define TF_TRIG_Y        3.0f     /* Y band: 3 + recY (GEN_BOX_Y)        */
#define TF_ELLIPSE       0.92f    /* validity ellipse vs pad half-extents*/
#define TF_RAMP_STEP     37       /* deploy/retract ramp step per tick   */
#define TF_RAMP_CAP      300      /* clamp (the 8-tick deploy lands 296) */
#define TF_TICKS         8        /* +0x28 deploy/retract countdown      */
#define TF_PHASE_FLOOR   100      /* render bob floor (fresh vel 3..7)   */
#define TF_SFX_TRIGGER   0x42Du   /* scan-success squelch, engine range
                                   * 300 (em_sfx: no positional
                                   * attenuation — same note as 0x42F).
                                   * UNMAPPED in the generated sfx.txt;
                                   * soundmap = sfx/snd_0615.wav (88 ms
                                   * squelch) — noted in the registry,
                                   * silent until the user maps it.      */
static const float TF_RING_BASE[2] = { 5.5f, 7.0f };  /* pair idx 0 / 1 */
static const float TF_RING_SPAN[2] = { 2.0f, 2.5f };  /* +- ring spread */
static const float TF_HOLD_R2[2]   = { 4.0f, 16.0f }; /* hold dist^2:
                                                        * 2 u / 4 u      */
/* D_0026D320 — the cycling girth table: scale-X/Z numerators / 256
 * (0.70 / 0.85 / 1.00 / 1.50), random start row, then sequential. */
static const int16_t TF_GIRTH[4] = { 0xB4, 0xDA, 0xFF, 0x180 };

/* Render-tail TINT (the engine's every-tick RGB blend, FINDINGS
 * "KIND-0xE COMPANION RESOLVED"):
 *
 *   rgb = (BASE + (1 - ph) * (ROOM - BASE)) / 128,  ph = parent +0x80
 *
 * BASE = the vivid green (6, 92, 1) the field reaches at full pad
 * open; ROOM = this room's rec from the engine's 22-rec room-tint
 * table D_00246800 (key = AREA<<8|ROOM, u8 c0..c3).
 * TODO(room-tint): D_00246800 is UNDECODED port-side — decode the
 * table in the decomp repo and key it per scene. Until then the port
 * uses the NEUTRAL rec (128, 128, 128): the office AREA02 rows are
 * (128,128,128,2)/(128,102,122,2), so neutral white is the right rest
 * blend for the captured scene (spikes sit room-colored while the pad
 * is closed and turn green as it opens).
 * ALPHA = ramp/300 — the deploy fade-in/out tied to the ramp (engine:
 * roomC.w/128 reached over the first 16 ramp units; the ramp/cap form
 * keeps the fade on the same deploy timeline without the undecoded
 * room alpha target — flagged with the TODO above). */
static const float TF_TINT_BASE[3] = {   6.0f, 92.0f,   1.0f };
static const float TF_TINT_ROOM[3] = { 128.0f, 128.0f, 128.0f };

/* --- Port placeholders (not exported from the disc; flagged) ----------- */
#define ENEMY_HOP_SPEED  0.32f    /* crate hop / worm stalk ground speed,
                                   * units/frame (engine: the crate run's
                                   * 11.0/1.4 velocity build and the
                                   * stalk's anim root motion — neither
                                   * maps to one exported number)         */
#define ENEMY_HOP_VY     0.42f    /* initial vertical velocity (~16-frame
                                   * airtime under the 0.052 gravity)     */
#define ENEMY_PROBE_LEN  6.0f     /* diagonal steer-probe length          */
#define ENEMY_PROBE_LIFT 1.0f     /* probe height above the feet          */
#define ENEMY_HIT_R      3.0f     /* bullet hit-sphere radius             */
#define ENEMY_AIM_Y      2.0f     /* aim/hit-sphere center above the feet */
#define ENEMY_FLOOR_UP   8.0f     /* floor-query window (em_game values)  */
#define ENEMY_FLOOR_DOWN 8.0f

typedef struct {
    int     active;       /* slot in use AND not yet FREE'd              */
    uint8_t kind;         /* EM_ENEMY_KIND_* (crawler / crate disguise)  */
    uint8_t seed;         /* spawn slot index: jitter phase offset       */
    uint8_t state;        /* actor +0x04 lifecycle (engine values)       */
    uint8_t sub;          /* attack sub-state (+0x05): crate 0 steer /
                           * 1 hop; worm 0 approach / 1 stalk / 2 windup
                           * / 3 lunge (the decoded func_00154120 subs)  */
    uint8_t alarm;        /* actor +0x0A group-alarm flag (crate only —
                           * the worm and bug brains never read it)      */
    uint8_t on_surface;   /* actor +0x52 on-surface flag — the engine's
                           * group-alarm broadcast only WAKES a crate when
                           * this is set, and it is 0 for every placed
                           * crate (live-read s76: drawbridge/office
                           * floors), so a destroyed crate wakes no
                           * neighbour. Set at INIT from a floor probe the
                           * port doesn't model → stays 0 (engine-true for
                           * all shipped scenes). Default 0 via memset.    */
    uint8_t children;     /* crate: nest-group bug count hatched at the
                           * burst (the s68 registry group size; the
                           * manifest `bugs <n>` channel)                */
    uint8_t variant;      /* crate: the placement MODEL byte (+0x03 —
                           * {6,0x1C,0x1E,0x1F,0x50}; manifest
                           * `variant <v>`, default 6 = every exported
                           * scene's crates). Picks the husk family on
                           * the damage-kill burst (decoded
                           * func_001551B0 @0x156380: 6 -> husk 0x22,
                           * else 0x29) and, engine-true, would gate
                           * the knockback arm (6/0x1E only — the port
                           * launches for both, flagged)                */
    uint8_t atk_armed;    /* crate: the 180-tick attack timer was armed
                           * at the first hop launch (port split of the
                           * engine's reused +0x2A — see ATTACK)         */
    int16_t hp;           /* actor +0x34                                 */
    int16_t mailbox;      /* actor +0x36 incoming-damage mailbox        */
    int     retreat;      /* actor +0x2A: steer budget (6 at the wake),
                           * then the crate's 180-tick suicide-run timer */
    int     t28;          /* actor +0x28: the worm's sub window counter
                           * (approach/stalk/windup/lunge ticks)         */
    float   pos[3];       /* actor +0xB0/B4/B8                           */
    float   yaw;          /* actor +0xC4 (heading; 0 = +Z, engine sense) */
    float   vy;           /* hop vertical velocity (+0x2C8)              */
    float   hop_y0;       /* launch height — the landing plane when the
                           * floor query finds nothing under the hop
                           * (e.g. outside the decoded grid floor)       */

    /* anim layer (VISUAL ONLY — never read by the state machine) */
    uint8_t aphase;       /* AnimPhase below                             */
    int     acur;         /* current clip index (into model.clips); -1 =
                           * no clip data, static base pose              */
    int     aprev;        /* fading-out clip index, -1 = no blend        */
    double  at;           /* current clip time, frames (60/s baked)      */
    double  aprev_t;      /* fading-out clip time, frames                */
    float   arate;        /* current clip frames-per-tick playback rate  */
    float   aprev_rate;   /* fading-out clip rate (keeps advancing, the
                           * player-path crossfade blends two LIVE clips)*/
    float   ablend;       /* crossfade weight of acur, 0..1              */
    float   speed;        /* actual XZ ground speed this tick, u/s       */
    int     fade;         /* DEATH placeholder: corpse-fade frames left
                           * (draws while > 0 even though the slot is
                           * inactive; tint alpha = fade/30)             */
    float   tint[4];      /* per-draw RGBA for the corpse fade (white,
                           * alpha walks 1 -> 0) — em_enemy_draw_tint    */

    /* lethal-hit record (engine: +0x36 nonzero + the +0x70 hit-source
     * position decide the knockback arm; port: the gib launch) */
    int     hit_lethal;   /* this death came from the damage mailbox     */
    float   hit_dir[2];   /* XZ hit vector, attacker -> victim, unit     */

    float   palette[ENEMY_BONE_MAX * 16];
} Enemy;

/* One loaded burst-set model (static 1-node EMDL; the exporter writes a
 * 1+1-slot palette, so bone_count is 2 with both slots identity). */
typedef struct {
    EmGfxMesh *mesh;
    EmModel    model;
    uint32_t   bone_count;
    float      base[GIB_BONE_MAX * 16];   /* frame-0 (identity) palette */
} GibModel;

/* One airborne/resting gib instance (visual only). */
typedef struct {
    int   active;
    int   fam;            /* husk family (GIB_FAM_A/B — see GIB_FILES)   */
    int   model;          /* index into s.gibm[fam]                      */
    float pos[3];
    float vel[3];         /* 0.052/tick gravity on [1]                   */
    float yaw, spin;      /* tumble (PORT visual)                        */
    float y0;             /* launch height = floor fallback (same rule
                           * as the hop's hop_y0)                        */
    int   age;            /* ticks since launch -> rest -> fade -> free  */
    float tint[4];        /* per-draw RGBA: white, alpha 1 while live /
                           * resting, 1 -> 0 over the fade window        */
    float palette[GIB_BONE_MAX * 16];
} Gib;

/* One placed generator pad (engine class 0x0D / func_0015A2C0 actor).
 * Engine actor offsets noted; generators live OUTSIDE the Enemy slot
 * array (hazard-list actors, not damage targets — file header). */
typedef struct {
    uint8_t cfg;        /* +0x54 placement kind 0..6 -> GEN_CFG row     */
    uint8_t link;       /* placement link 0/1/2 (the table selector)    */
    uint8_t mode;       /* +0x56 AFTER the init draw: the runtime mode  */
    uint8_t sub;        /* +0x05 sub-state                              */
    uint8_t in_box;     /* +0x0A player-inside-box, recomputed per tick */
    uint8_t open;       /* +0x0B breather OPEN flag                     */
    uint8_t trap_armed; /* PORT: one-shot trap hit edge per box entry   */
    int16_t spawned;    /* +0x2E worms emitted (capped at GEN_WORM_CAP) */
    float   timer;      /* +0x20 charge / hold / delay float            */
    float   phase;      /* +0x80 morph phase 0..1 (port: pad swell)     */
    float   pos[3];     /* +0xB0..B8                                    */
    float   yaw;        /* +0xC4 (placement; pads are all yaw 0)        */
    float   palette[GEN_BONES * 16];
} Gen;

/* One tendril spike record (the scratch +0x1C {X,Z} pair + the +0x7C
 * {phase, vel, ramp, girth, valid} block, stride 0xA — engine s16
 * widths kept so the integrator wraps identically). */
typedef struct {
    float   x, z;         /* world target (scattered at SCAN)            */
    int16_t phase;        /* +0x7C bob phase (the scale-Y numerator)     */
    int16_t vel;          /* +0x7E bob velocity                          */
    int16_t ramp;         /* +0x80 deploy ramp 0..296                    */
    int16_t girth;        /* +0x82 scale-X/Z numerator (/256)            */
    uint8_t valid;        /* +0x84 target inside the 0.92x pad ellipse   */
} TfSpike;

/* One tendril-field actor (one member of a mode-1 pad's pair). Engine
 * actor offsets noted; fields live outside every other pool (no HP, no
 * mailbox, no hit sphere — see the file header). */
typedef struct {
    uint8_t active;
    uint8_t pair;         /* +0x2E pair index 0/1 (ring + hold radius)   */
    uint8_t sub;          /* +0x05: 0 SCAN 1 DEPLOY 2 HOLD 3 RETRACT
                           * 4 RESET (engine sub-state values)           */
    uint8_t gcur;         /* girth-table cursor (random start row, then
                           * sequential — the engine's cycling seed)     */
    int     timer;        /* +0x28 deploy/retract countdown              */
    int     pad;          /* parent generator index (the +0x20 link)     */
    float   anchor[3];    /* scratch +0x10: (player X, pad Y, player Z)  */
    float   tint[4];      /* actor RGB mult +0x80..8C: room tint ->
                           * green by the parent open phase, alpha =
                           * ramp/300 (shared by the 12 spikes — the
                           * ramps move in lockstep)                     */
    TfSpike sp[TF_SPIKES];
    float   pal[TF_SPIKES][TF_BONE_MAX * 16];
} Tendril;

/* Anim-layer phases (port-side, NOT engine state values — the engine
 * picks clips inside func_00154040/func_00154120). */
enum {
    ANIM_EMERGE = 0,   /* clip 1 once (spawn)                       */
    ANIM_CRAWL  = 1,   /* clip 0 loop (idle slow / attack speed)    */
    ANIM_WINDUP = 2,   /* clip 2 once (close range)                 */
    ANIM_LUNGE  = 3    /* clip 3 once, the burst lands during it    */
};

static struct {
    /* shared mesh: the asset (preferred) or the runtime placeholder */
    int        mesh_tried;
    EmGfxMesh *mesh;
    EmModel    model;        /* loaded only on the asset path */
    int        has_model;
    uint32_t   bone_count;
    float      base[ENEMY_BONE_MAX * 16];  /* frame-0 pose (or identity) */

    /* resolved clip indices (-1 = the asset doesn't carry it); anim is
     * enabled only for a real multi-clip EMD3 asset (clip_crawl >= 0
     * and clip_count > 1 — an EMD2 fallback keeps the static pose) */
    int        anim_on;
    int        clip_crawl, clip_emerge, clip_windup, clip_lunge;
    float      blend_pal[ENEMY_BONE_MAX * 16]; /* crossfade scratch */

    /* bug hatchling mesh + clips (loaded when a bug or a crate is
     * placed — the crate burst needs it; worm-only runs untouched) */
    int        bug_tried;
    EmGfxMesh *bug_mesh;
    EmModel    bug_model;
    int        bug_has_model;
    uint32_t   bug_bones;
    float      bug_base[ENEMY_BONE_MAX * 16];
    int        bug_anim_on;
    int        bclip_walk, bclip_death, bclip_flinch, bclip_bite;

    /* crate disguise mesh (loaded only when a crate is placed, so
     * crawler-only runs keep byte-identical output) */
    int        crate_tried;
    EmGfxMesh *crate_mesh;
    EmModel    crate_model;
    int        crate_has_model;
    uint32_t   crate_bones;
    float      crate_base[CRATE_BONE_MAX * 16];

    Enemy      e[ENEMY_SLOT_MAX];
    int        n;

    int        player_hit;   /* player-side damage mailbox (+0x36 shape) */

    /* gib layer (visual only; see the file header) */
    int        gib_tried;
    GibModel   gibm[GIB_FAMILY_N][GIB_FAM_FILES];
    int        gibm_n[GIB_FAMILY_N]; /* loaded models per husk family
                                      * (0 = that family fades only)     */
    Gib        gib[ENEMY_SLOT_MAX];
    int        gib_tail;     /* virtual draw slots in use (compact top)  */
    int        gib_next;     /* round-robin cursor over a family's
                              * shard tail (the husk leads each burst)   */
    uint32_t   rng;          /* deterministic LCG state                  */
    int        frame;        /* update ticks (EM_ENEMY_GIBDEMO hook)     */
    int        demo;         /* parsed EM_ENEMY_GIBDEMO (-1 = off)       */

    /* generator pool (file header "GENERATOR KIND") */
    int        gen_tried;    /* placeholder pad mesh load attempted      */
    EmGfxMesh *gen_mesh;
    Gen        gen[EM_GENERATOR_MAX];
    int        gen_n;
    uint8_t    gen_cursor[2];/* D_008106EC/ED — global per-link column   */

    /* EM_ENEMY_TEST=4 harness (file header; -1 = off) */
    int        gt_on;
    int        gt_armed;     /* test pad placed (first update tick)      */
    int        gt_fail;      /* failed checkpoints                       */
    int        gt_gen;       /* generator index                          */
    int        gt_last_n;    /* s.n watermark for worm-spawn detection   */
    int        gt_worms;     /* worms seen                               */
    int        gt_kill_i;    /* witness watch: worm-1 slot (-1 done)     */
    int        gt_kill_f;    /* witness: frame to inject the mailbox
                              * write (which must NOT kill — J2 s66)     */
    int        gt_kill1_f;   /* frame the non-consumption witness passed
                              * (0 = not yet)                            */
    int        gt_post;      /* frames since exhaustion (silence window) */
    float      gt_delay_div; /* delay divisor (60 — test acceleration)   */

    /* tendril fields (file header "TENDRIL FIELD") */
    int        tf_tried;     /* spike mesh load attempted                */
    EmGfxMesh *tf_mesh;
    EmModel    tf_model;
    int        tf_has_model;
    uint32_t   tf_bones;
    float      tf_base[TF_BONE_MAX * 16];
    Tendril    tf[EM_TENDRIL_MAX];
    int        tf_n;

    /* EM_ENEMY_TEST=5 harness (file header) */
    int        tt_on;
    int        tt_armed;     /* pad placed + walker started              */
    int        tt_fail;      /* failed checkpoints                       */
    int        tt_pad;       /* test generator index (-1 = spawn failed) */
    int        tt_f[2];      /* the pair's field indices (-1 = missing)  */
    float      tt_w[3];      /* the synthetic walker                     */
    int        tt_stage;     /* 0 walk-in, 1 hold, 2 leave, 3 quiet,
                              * 99 done                                  */
    int        tt_stage_f;   /* frame the stage was entered              */
    int        tt_trigs;     /* SCAN->DEPLOY edges seen (field 0)        */
    int        tt_retract[2];/* RETRACT entries seen since stage 2       */
    uint8_t    tt_psub[2];   /* previous-tick sub (edge detection)       */
} s;

static void enemy_build_palette(Enemy *e);

void em_enemy_reset(void)
{
    /* Mesh/model survive a reset only through shutdown (mirrors
     * em_door_reset: boot resets once before adding). */
    memset(&s, 0, sizeof s);
    s.rng  = 0x2A5613u;   /* fixed LCG seed: deterministic runs/captures */
    s.demo = -1;
    const char *gd = getenv("EM_ENEMY_GIBDEMO");
    if (gd && *gd)
        s.demo = atoi(gd);
    /* EM_ENEMY_TEST=4 — the generator run is owned HERE (em_game.c only
     * arms values 1..3; see the file header). */
    const char *et = getenv("EM_ENEMY_TEST");
    if (et && et[0] == '4' && et[1] == '\0') {
        s.gt_on        = 1;
        s.gt_gen       = -1;
        s.gt_kill_i    = -1;
        s.gt_delay_div = 60.0f;
    }
    /* EM_ENEMY_TEST=5 — the tendril-field run is owned HERE too. */
    if (et && et[0] == '5' && et[1] == '\0') {
        s.tt_on   = 1;
        s.tt_pad  = -1;
        s.tt_f[0] = -1;
        s.tt_f[1] = -1;
    }
}

/* Deterministic LCG (Numerical-Recipes constants — our own tiny RNG,
 * NOT the engine's frame RNG at spad 0x70003B68; flagged port choice). */
static uint32_t gib_rng(void)
{
    s.rng = s.rng * 1664525u + 1013904223u;
    return s.rng >> 8;
}

/* ------------------------------------------------------------------ */
/* Shared mesh: asset, else the procedural placeholder                  */
/* ------------------------------------------------------------------ */

/* Append one axis-aligned box (per-face normals, untextured, bone 0) to
 * a 10-words-per-vertex buffer. PLACEHOLDER geometry — generated at
 * runtime, our own original vertices, NOT disc data. */
static void box_emit(float *verts, uint32_t *nv, uint32_t *idx, uint32_t *ni,
                     const float lo[3], const float hi[3])
{
    static const int face[6][4] = {
        /* +X */ {1, 3, 7, 5}, /* -X */ {4, 6, 2, 0},
        /* +Y */ {2, 6, 7, 3}, /* -Y */ {0, 1, 5, 4},
        /* +Z */ {5, 7, 6, 4}, /* -Z */ {0, 2, 3, 1}
    };
    static const float fnrm[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    const uint32_t no_tex = 0xFFFFFFFFu;

    for (int f = 0; f < 6; f++) {
        uint32_t base = *nv;
        for (int c = 0; c < 4; c++) {
            int    k = face[f][c];
            float *v = verts + (size_t)(*nv) * 10;
            v[0] = (k & 1) ? hi[0] : lo[0];
            v[1] = (k & 2) ? hi[1] : lo[1];
            v[2] = (k & 4) ? hi[2] : lo[2];
            v[3] = fnrm[f][0];
            v[4] = fnrm[f][1];
            v[5] = fnrm[f][2];
            v[6] = 0.0f;                       /* uv (untextured) */
            v[7] = 0.0f;
            memset(&v[8], 0, sizeof(float));   /* bone 0, no flags */
            memcpy(&v[9], &no_tex, sizeof no_tex);
            (*nv)++;
        }
        idx[(*ni)++] = base + 0;
        idx[(*ni)++] = base + 1;
        idx[(*ni)++] = base + 2;
        idx[(*ni)++] = base + 0;
        idx[(*ni)++] = base + 2;
        idx[(*ni)++] = base + 3;
    }
}

static void mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* Load the shared crawler mesh once: the EMDL asset when present (frame-0
 * pose of clip 0 — no enemy anim hookup yet, see em_enemy.h), else the
 * runtime PLACEHOLDER: a squat box body + a head box pointing +Z (the
 * facing convention), built through em_gfx_mesh_create. Returns 0 ok. */
static int enemy_mesh_get(EmGfx *gfx)
{
    if (s.mesh) return 0;
    if (s.mesh_tried) return -1;
    s.mesh_tried = 1;

    if (em_model_load(&s.model, ENEMY_ASSET) == 0) {
        if (s.model.bone_count > ENEMY_BONE_MAX) {
            fprintf(stderr, "enemy: %s: %u bones > %d\n", ENEMY_ASSET,
                    s.model.bone_count, ENEMY_BONE_MAX);
            em_model_free(&s.model);
            return -1;
        }
        s.mesh = em_gfx_mesh_create(gfx, s.model.verts, s.model.vert_count,
                                    s.model.indices, s.model.index_count,
                                    (const EmGfxTexDesc *)s.model.texs,
                                    s.model.tex_count, s.model.texels,
                                    s.model.flags);
        if (!s.mesh) {
            em_model_free(&s.model);
            return -1;
        }
        s.has_model  = 1;
        s.bone_count = s.model.bone_count;
        em_model_palette_at(&s.model, 0, 0.0, s.base); /* frame-0 pose */
        s.clip_crawl  = em_model_clip_index(&s.model, ENEMY_CLIP_CRAWL);
        s.clip_emerge = em_model_clip_index(&s.model, ENEMY_CLIP_EMERGE);
        s.clip_windup = em_model_clip_index(&s.model, ENEMY_CLIP_WINDUP);
        s.clip_lunge  = em_model_clip_index(&s.model, ENEMY_CLIP_LUNGE);
        s.anim_on = (s.clip_crawl >= 0 && s.model.clip_count > 1);
        printf("enemy model: %s — %u verts, %u tris, %u bones, %u clip(s)",
               ENEMY_ASSET, s.model.vert_count, s.model.index_count / 3,
               s.bone_count, s.model.clip_count);
        if (s.anim_on)
            printf(" — anim on (crawl #%d, emerge #%d, windup #%d, "
                   "lunge #%d)\n", s.clip_crawl, s.clip_emerge,
                   s.clip_windup, s.clip_lunge);
        else
            printf(" — static frame-0 pose (re-export the EMD3 clip "
                   "bank for animation)\n");
        return 0;
    }

    /* PLACEHOLDER crawler: body + head, ~4 units long, 2 high. */
    float    verts[48 * 10];
    uint32_t indices[72];
    uint32_t nv = 0, ni = 0;
    const float body_lo[3] = { -1.5f, 0.2f, -2.0f };
    const float body_hi[3] = {  1.5f, 2.2f,  2.0f };
    const float head_lo[3] = { -0.8f, 0.6f,  2.0f };
    const float head_hi[3] = {  0.8f, 1.8f,  3.2f };
    box_emit(verts, &nv, indices, &ni, body_lo, body_hi);
    box_emit(verts, &nv, indices, &ni, head_lo, head_hi);

    s.mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                NULL, 0, NULL, 0);
    if (!s.mesh) return -1;
    s.bone_count = 1;
    mat4_identity(s.base);
    printf("enemy model: no %s — PLACEHOLDER box crawler (%u verts, %u "
           "tris, runtime-generated). Export the real mesh with the decomp "
           "repo's tools/export_native.py\n", ENEMY_ASSET, nv, ni / 3);
    return 0;
}

/* Load the crate disguise mesh once (first CRATE spawn only): the
 * scene-local props/enemy_crate.emdl if the active scene carries one,
 * else the GLOBAL wooden-crate default (CRATE_ASSET — the n0 entry-0x0D
 * carve; see the table-nuance note at the define), else a PLACEHOLDER
 * box with the cardboard crate's 6x4x5 footprint (runtime-generated,
 * original vertices, NOT disc data). Returns 0 ok. */
static int crate_mesh_get(EmGfx *gfx)
{
    if (s.crate_mesh) return 0;
    if (s.crate_tried) return -1;
    s.crate_tried = 1;

    /* Per-scene crate probe (decomp FINDINGS s34 §3): under the EM_SCENE
     * shadow stage "assets/scene" IS the active scene directory, so a scene
     * may carry props/enemy_crate.emdl (props/ is a subdir, never slurped
     * by scene_load as level geometry); scenes without one — and the
     * default scene — fall back to the global CRATE_ASSET box, keeping
     * default behavior byte-identical (the existence check keeps the
     * loader's cannot-open noise out of default logs). Both crates are
     * 1-bone, so CRATE_BONE_MAX needs no change. */
    static const char SCENE_CRATE[] = "assets/scene/props/enemy_crate.emdl";
    const char *path = NULL;
    FILE *sc = fopen(SCENE_CRATE, "rb");
    if (sc) fclose(sc);
    if (sc && em_model_load(&s.crate_model, SCENE_CRATE) == 0)
        path = SCENE_CRATE;
    else if (em_model_load(&s.crate_model, CRATE_ASSET) == 0)
        path = CRATE_ASSET;
    if (path) {
        if (s.crate_model.bone_count > CRATE_BONE_MAX) {
            fprintf(stderr, "enemy: %s: %u bones > %d\n", path,
                    s.crate_model.bone_count, CRATE_BONE_MAX);
            em_model_free(&s.crate_model);
            return -1;
        }
        s.crate_mesh = em_gfx_mesh_create(gfx, s.crate_model.verts,
                                          s.crate_model.vert_count,
                                          s.crate_model.indices,
                                          s.crate_model.index_count,
                                          (const EmGfxTexDesc *)
                                          s.crate_model.texs,
                                          s.crate_model.tex_count,
                                          s.crate_model.texels,
                                          s.crate_model.flags);
        if (!s.crate_mesh) {
            em_model_free(&s.crate_model);
            return -1;
        }
        s.crate_has_model = 1;
        s.crate_bones     = s.crate_model.bone_count;
        em_model_palette_at(&s.crate_model, 0, 0.0, s.crate_base);
        printf("crate model: %s — %u verts, %u tris, %u texture(s)\n",
               path, s.crate_model.vert_count,
               s.crate_model.index_count / 3, s.crate_model.tex_count);
        return 0;
    }

    /* PLACEHOLDER crate: the office disguise box footprint. */
    float    verts[24 * 10];
    uint32_t indices[36];
    uint32_t nv = 0, ni = 0;
    const float lo[3] = { -3.0f, 0.0f, -2.5f };
    const float hi[3] = {  3.0f, 4.0f,  2.5f };
    box_emit(verts, &nv, indices, &ni, lo, hi);
    s.crate_mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                      NULL, 0, NULL, 0);
    if (!s.crate_mesh) return -1;
    s.crate_bones = 1;
    mat4_identity(s.crate_base);
    printf("crate model: no %s — PLACEHOLDER box (export with the decomp "
           "repo's tools/export_props.py --crate)\n", CRATE_ASSET);
    return 0;
}

/* Load the BUG hatchling mesh + clips once (first bug or crate spawn —
 * the crate burst hatches bugs inside em_enemy_update, without a gfx
 * handle, so the crate add preloads this): assets/enemy_bug.emdl =
 * the s68 variant-A export (global slot 0x0F + clip bank 0x11), else
 * a PLACEHOLDER flat box bug at the authored footprint (runtime-
 * generated, original vertices, NOT disc data). Clip resolution: walk
 * 1 (the decoded init clip), flinch 0x1D, death 0x1B — 0x1B is absent
 * from the current export (unbakeable container, s68) and resolves
 * -1, which the death path treats as "fade fallback". Returns 0 ok. */
static int bug_mesh_get(EmGfx *gfx)
{
    if (s.bug_mesh) return 0;
    if (s.bug_tried) return -1;
    s.bug_tried = 1;

    if (em_model_load(&s.bug_model, BUG_ASSET) == 0) {
        if (s.bug_model.bone_count > ENEMY_BONE_MAX) {
            fprintf(stderr, "enemy: %s: %u bones > %d\n", BUG_ASSET,
                    s.bug_model.bone_count, ENEMY_BONE_MAX);
            em_model_free(&s.bug_model);
            return -1;
        }
        s.bug_mesh = em_gfx_mesh_create(gfx, s.bug_model.verts,
                                        s.bug_model.vert_count,
                                        s.bug_model.indices,
                                        s.bug_model.index_count,
                                        (const EmGfxTexDesc *)
                                        s.bug_model.texs,
                                        s.bug_model.tex_count,
                                        s.bug_model.texels,
                                        s.bug_model.flags);
        if (!s.bug_mesh) {
            em_model_free(&s.bug_model);
            return -1;
        }
        s.bug_has_model = 1;
        s.bug_bones     = s.bug_model.bone_count;
        em_model_palette_at(&s.bug_model, 0, 0.0, s.bug_base);
        s.bclip_walk   = em_model_clip_index(&s.bug_model, BUG_CLIP_WALK);
        s.bclip_death  = em_model_clip_index(&s.bug_model, BUG_CLIP_DEATH);
        s.bclip_flinch = em_model_clip_index(&s.bug_model,
                                             BUG_CLIP_FLINCH);
        s.bclip_bite   = em_model_clip_index(&s.bug_model, BUG_CLIP_BITE);
        s.bug_anim_on  = (s.bclip_walk >= 0 && s.bug_model.clip_count > 1);
        printf("bug model: %s — %u verts, %u tris, %u bones, %u clip(s)"
               " — walk #%d, bite #%d, flinch #%d, death #%d%s\n",
               BUG_ASSET,
               s.bug_model.vert_count, s.bug_model.index_count / 3,
               s.bug_bones, s.bug_model.clip_count, s.bclip_walk,
               s.bclip_bite, s.bclip_flinch, s.bclip_death,
               s.bclip_death < 0 ? " (0x1B unexported — corpse-fade "
                                   "death, s68)" : "");
        return 0;
    }

    /* PLACEHOLDER bug: a flat low body + head at the authored
     * ~3.7 x 1.9 x 8.9 footprint. */
    float    verts[48 * 10];
    uint32_t indices[72];
    uint32_t nv = 0, ni = 0;
    const float body_lo[3] = { -1.85f, 0.1f, -4.4f };
    const float body_hi[3] = {  1.85f, 1.9f,  3.0f };
    const float head_lo[3] = { -0.9f,  0.3f,  3.0f };
    const float head_hi[3] = {  0.9f,  1.4f,  4.5f };
    box_emit(verts, &nv, indices, &ni, body_lo, body_hi);
    box_emit(verts, &nv, indices, &ni, head_lo, head_hi);
    s.bug_mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                    NULL, 0, NULL, 0);
    if (!s.bug_mesh) return -1;
    s.bug_bones    = 1;
    s.bclip_walk   = s.bclip_death = s.bclip_flinch = s.bclip_bite = -1;
    mat4_identity(s.bug_base);
    printf("bug model: no %s — PLACEHOLDER box bug (export with the "
           "decomp repo's tools/export_native.py, s68 recorded CLI)\n",
           BUG_ASSET);
    return 0;
}

/* Load the burst set once (first crawler spawn — the only entry point
 * with a gfx handle; em_enemy_update can't create GPU meshes). Both
 * husk families load front-compacted; missing files shrink a family
 * silently and an empty family = fade fallback for its bursts. */
static void gib_models_load(EmGfx *gfx)
{
    if (s.gib_tried) return;
    s.gib_tried = 1;

    for (int f = 0; f < GIB_FAMILY_N; f++) {
        for (int i = 0; i < GIB_FAM_FILES; i++) {
            GibModel *gm = &s.gibm[f][s.gibm_n[f]];
            if (em_model_load(&gm->model, GIB_FILES[f][i]) != 0)
                continue;
            if (gm->model.bone_count > GIB_BONE_MAX) {
                em_model_free(&gm->model);
                continue;
            }
            gm->mesh = em_gfx_mesh_create(gfx, gm->model.verts,
                                          gm->model.vert_count,
                                          gm->model.indices,
                                          gm->model.index_count,
                                          (const EmGfxTexDesc *)
                                          gm->model.texs,
                                          gm->model.tex_count,
                                          gm->model.texels,
                                          gm->model.flags);
            if (!gm->mesh) {
                em_model_free(&gm->model);
                continue;
            }
            gm->bone_count = gm->model.bone_count;
            em_model_palette_at(&gm->model, 0, 0.0, gm->base);
            s.gibm_n[f]++;
        }
    }
    if (s.gibm_n[GIB_FAM_A] > 0 || s.gibm_n[GIB_FAM_B] > 0)
        printf("enemy gibs: husk A %d/%d + husk B %d/%d burst-set "
               "models loaded from assets/gibs/ (the variant-keyed "
               "rebind families — func_001551B0 @0x156380)\n",
               s.gibm_n[GIB_FAM_A], GIB_FAM_FILES,
               s.gibm_n[GIB_FAM_B], GIB_FAM_FILES);
    else
        printf("enemy gibs: none of assets/gibs/ present — death keeps "
               "the corpse-fade placeholder (export with the decomp repo's "
               "tools/export_props.py --gibs)\n");
}

/* Slot setup shared by the public adds and the crate-burst worm spawn
 * (which runs inside em_enemy_update, without a gfx handle — every
 * mesh a burst needs is preloaded by em_enemy_add_kind). */
static int enemy_spawn(int kind, const float pos[3], float yaw)
{
    if (s.n >= ENEMY_SLOT_MAX) return -1;

    Enemy *e = &s.e[s.n];
    memset(e, 0, sizeof *e);
    e->active = 1;
    e->kind   = (uint8_t)kind;
    e->seed   = (uint8_t)s.n;
    e->state  = EM_ENEMY_INIT;
    e->pos[0] = pos[0];
    e->pos[1] = pos[1];
    e->pos[2] = pos[2];
    e->yaw    = yaw;
    /* Anim layer (the crate is a 1-node static mesh): a crawler spawn
     * plays the emerge clip once (clip 1), falling back to the crawl
     * loop if the asset lacks it; a bug spawn enters the decoded
     * init/walk clip 1 directly (s68). */
    e->acur  = -1;
    e->aprev = -1;
    if (kind == EM_ENEMY_KIND_CRAWLER && s.anim_on) {
        e->aphase = ANIM_EMERGE;
        e->acur   = s.clip_emerge >= 0 ? s.clip_emerge : s.clip_crawl;
        e->arate  = s.clip_emerge >= 0 ? 1.0f : ENEMY_ATTACK_MIN;
        e->ablend = 1.0f;
    } else if (kind == EM_ENEMY_KIND_BUG && s.bug_anim_on) {
        e->aphase = ANIM_CRAWL;          /* the walk loop phase        */
        e->acur   = s.bclip_walk;
        e->arate  = BUG_WALK_MIN;
        e->ablend = 1.0f;
    }
    if (kind == EM_ENEMY_KIND_CRATE) {
        e->children = CRATE_BUGS_DEFAULT;
        e->variant  = 6;   /* every exported scene's crate placements
                            * carry model byte 06 (the wooden crate —
                            * placements survey 2026-06-11)            */
    }
    /* Stage a valid pose immediately: the render chain may record this
     * instance's palette pointer before the first em_enemy_update. */
    enemy_build_palette(e);
    printf("enemy %d: %s at (%.1f, %.1f, %.1f) yaw %.3f\n", s.n,
           kind == EM_ENEMY_KIND_CRATE ? "crate"
           : kind == EM_ENEMY_KIND_BUG ? "bug" : "crawler",
           pos[0], pos[1], pos[2], yaw);
    return s.n++;
}

int em_enemy_add(EmGfx *gfx, const float pos[3], float yaw)
{
    if (s.n >= ENEMY_SLOT_MAX) return -1;
    if (enemy_mesh_get(gfx) != 0) return -1;
    gib_models_load(gfx);
    return enemy_spawn(EM_ENEMY_KIND_CRAWLER, pos, yaw);
}

int em_enemy_add_kind(EmGfx *gfx, int kind, const float pos[3], float yaw)
{
    if (kind == EM_ENEMY_KIND_CRAWLER)
        return em_enemy_add(gfx, pos, yaw);
    if (kind == EM_ENEMY_KIND_BUG) {
        if (s.n >= ENEMY_SLOT_MAX) return -1;
        if (bug_mesh_get(gfx) != 0) return -1;
        return enemy_spawn(EM_ENEMY_KIND_BUG, pos, yaw);
    }
    if (kind != EM_ENEMY_KIND_CRATE) return -1;
    return em_enemy_add_crate(gfx, pos, yaw, -1, -1);
}

int em_enemy_add_crate(EmGfx *gfx, const float pos[3], float yaw,
                       int bugs, int variant)
{
    if (s.n >= ENEMY_SLOT_MAX) return -1;
    if (crate_mesh_get(gfx) != 0) return -1;
    /* The burst will need the BUG mesh (the s68 nest children) + the
     * husk gibs; this is the only moment with a gfx handle, so preload
     * them now. (The worm preload is GONE — crates never hatch it.) */
    if (bug_mesh_get(gfx) != 0) return -1;
    gib_models_load(gfx);
    int i = enemy_spawn(EM_ENEMY_KIND_CRATE, pos, yaw);
    if (i >= 0 && bugs >= 0)
        s.e[i].children = (uint8_t)(bugs > ENEMY_SLOT_MAX
                                    ? ENEMY_SLOT_MAX : bugs);
    if (i >= 0 && variant >= 0)
        s.e[i].variant = (uint8_t)variant;
    return i;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Group-alarm broadcast (the placed crawler's IDLE damage path,
 * decoded): the engine walks the WHOLE live actor list D_00275BC0 —
 * no radius — and sets +0x0A on every actor with a placed-crawler
 * model byte {6, 0x1C, 0x1E, 0x1F, 0x50} and the on-surface flag
 * (+0x52). Natively: every live CRATE (the placed-crawler kind);
 * worms and bugs are NOT whitelisted (the bug models 0x0F/0x10 sit
 * outside the placed-crawler set) and never read the flag. */
static void enemy_alarm_broadcast(void)
{
    /* Decoded list-wide, no-radius wake (func_001551B0 state-4
     * broadcast) — but the engine reaches its `sb 1, +0x0A` wake ONLY
     * for a recipient with `+0x52 != 0` (on-surface). That flag is 0 on
     * every placed crate (live-read s76), so a destroyed crate wakes NO
     * neighbour — matching the original (user-reported 2026-06-12 that
     * the J1 "match" was wrong: breaking one crate must NOT move the
     * other). The mechanism is preserved against a future on-surface
     * scene; with on_surface defaulting to 0 it is inert, as in-game. */
    for (int i = 0; i < s.n; i++)
        if (s.e[i].active && s.e[i].kind == EM_ENEMY_KIND_CRATE &&
            s.e[i].on_surface)
            s.e[i].alarm = 1;
}

/* Consume the +0x36 mailbox — the CRATE's IDLE poll (the decoded
 * state-4 test is `+0x36 != 0`; HP 1 makes any nonzero value lethal)
 * and the BUG's every-tick poll (decoded func_00128B80 — HP 15, a
 * survivable hit returns 0 with the HP already debited and the
 * caller flinches on it). Low bits = amount (below the 0x2000 type
 * flag), matching the documented code layout. The WORM never reaches
 * this: its brain consumes nothing (J2 CLOSED s66 — the old
 * every-tick worm poll, the shootable stand-in, is REMOVED). The
 * subtractive shape is the canonical hurt helper func_00153B50,
 * whose death arm plays 0x7D8 (a FLAGGED stand-in for the bug: its
 * own handler func_00129FC0's audio is undecoded). */
static int enemy_mailbox_poll(Enemy *e, const float pp[3])
{
    if (e->mailbox == 0) return 0;
    int amount = e->mailbox & 0x1FFF;
    e->mailbox = 0;
    e->hp      = (int16_t)(e->hp - amount);
    if (e->hp > 0) return 0;
    /* 0x7D8 — engine func_00153B50 plays it positional at the dying
     * actor: play_sound(actor, 0x7D8, 0, 300.0) (radius read off the
     * call site's f12 = 0x43960000) */
    em_sfx_play_at(EM_SFX_ENEMY_DEATH, e->pos, 300.0f);
    /* Lethal: record the hit vector for the gib knockback. The engine
     * copies the attacker position into victim +0x70 (pair pass /
     * func_001B41F0); the port's only attacker is the player, so the
     * stand-in hit vector is player -> crawler in XZ. Degenerate
     * (same spot): knock straight back along the facing. */
    float hx = e->pos[0] - pp[0], hz = e->pos[2] - pp[2];
    float hl = sqrtf(hx * hx + hz * hz);
    if (hl > 1e-4f) {
        e->hit_dir[0] = hx / hl;
        e->hit_dir[1] = hz / hl;
    } else {
        e->hit_dir[0] = -sinf(e->yaw);
        e->hit_dir[1] = -cosf(e->yaw);
    }
    e->hit_lethal = 1;
    return 1;
}

/* Knee-height directional probe (func_0019AB20 stand-in over the static
 * sets): 1 = blocked by a non-walkable surface within `len`. */
static int enemy_probe(const EmCollision *coll, const Enemy *e,
                       float ang, float len)
{
    if (!coll || !coll->poly_count) return 0;
    float from[3] = { e->pos[0], e->pos[1] + ENEMY_PROBE_LIFT, e->pos[2] };
    float to[3]   = { from[0] + sinf(ang) * len, from[1],
                      from[2] + cosf(ang) * len };
    EmCollHit hit;
    if (!em_collision_segment_query(coll, from, to,
                                    EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                                    EM_COLL_ID_NONE, &hit))
        return 0;
    return hit.surf_class != EM_SURF_FLOOR &&
           hit.surf_class != EM_SURF_SLOPE;
}

/* Floor height under a point (the same vertical-query pattern AND query
 * id 0 as the player spine in em_game.c — the walkable grid floor
 * carries conditional attrs that a -1 id query skips). Falls back to
 * `fallback` (the launch height) when nothing is found (no collision
 * world, or the spot lies outside the decoded grid floor) so a failed
 * query can never ratchet the querier upward. Shared by the crawler's
 * hop and the gib landings. */
static float floor_at(const EmCollision *coll, const float pos[3],
                      float fallback)
{
    if (!coll || !coll->poly_count) return fallback;
    float from[3] = { pos[0], pos[1] + ENEMY_FLOOR_UP,   pos[2] };
    float down[3] = { pos[0], pos[1] - ENEMY_FLOOR_DOWN, pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.point[1];
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }
    return fallback;
}

static float enemy_floor(const EmCollision *coll, const Enemy *e)
{
    return floor_at(coll, e->pos, e->hop_y0);
}

static float wrap_pi(float a)
{
    while (a >  ENEMY_PI) a -= 2.0f * ENEMY_PI;
    while (a < -ENEMY_PI) a += 2.0f * ENEMY_PI;
    return a;
}

/* ------------------------------------------------------------------ */
/* Animation layer (visual only — see the file header)                  */
/* ------------------------------------------------------------------ */

/* Clip time for evaluation: every clip except the crawl LOOP is a
 * one-shot — clamp to the last baked frame so em_model_palette_at's
 * wrap (last blends into first) never plays a one-shot backwards. */
static double anim_eval_time(int clip, double t)
{
    const EmModelClip *c = &s.model.clips[clip];
    if (clip != s.clip_crawl && t > (double)(c->frame_count - 1))
        t = (double)(c->frame_count - 1);
    return t;
}

/* Bug-model variant of the same clamp: the WALK is the only loop;
 * flinch (and a future death clip) are one-shots. */
static double bug_eval_time(int clip, double t)
{
    const EmModelClip *c = &s.bug_model.clips[clip];
    if (clip != s.bclip_walk && t > (double)(c->frame_count - 1))
        t = (double)(c->frame_count - 1);
    return t;
}

/* Switch the current clip, starting a 0.15 s crossfade from the old
 * one (which keeps advancing at its own rate — the player path blends
 * two LIVE clips the same way). Same clip = just retune the rate (the
 * attack loop rescales every tick with the ground speed). */
static void enemy_anim_set(Enemy *e, int clip, float rate)
{
    if (clip < 0) return;
    if (clip == e->acur) {
        e->arate = rate;
        return;
    }
    e->aprev      = e->acur;
    e->aprev_t    = e->at;
    e->aprev_rate = e->arate;
    e->acur       = clip;
    e->at         = 0.0;
    e->arate      = rate;
    e->ablend     = e->aprev >= 0 ? 0.0f : 1.0f;
}

/* Pick this tick's clip + rate from the GAMEPLAY state (one-way: the
 * anim layer reads the state machine, never the reverse), then advance
 * the play heads and the crossfade weight. */
static void enemy_anim_update(Enemy *e, float dist)
{
    (void)dist;

    /* BUG layer (s68/s76): the walk loop while approaching/recovering
     * (rate floored so a standoff-parked bug keeps its leg cycle — the
     * clip is baked in place), the bite clip 0x13 during windup+lunge
     * (sub 2/3), the flinch one-shot during sub 1. DEATH keeps the
     * frozen pose for the corpse fade (the 0x1B death clip is unexported
     * — file header). */
    if (e->kind == EM_ENEMY_KIND_BUG) {
        if (!s.bug_anim_on) return;
        if (e->state != EM_ENEMY_ATTACK) return;
        if (e->sub == 1 && s.bclip_flinch >= 0) {
            enemy_anim_set(e, s.bclip_flinch, 1.0f);
        } else if ((e->sub == 2 || e->sub == 3) && s.bclip_bite >= 0) {
            enemy_anim_set(e, s.bclip_bite, 1.0f);
        } else if (e->sub != 1) {
            enemy_anim_set(e, s.bclip_walk,
                           e->speed > 0.5f ? 1.0f : BUG_WALK_MIN);
        }
        /* advance the play heads + crossfade (shared tail below) */
        e->at += (double)e->arate;
        if (e->aprev >= 0) {
            e->aprev_t += (double)e->aprev_rate;
            e->ablend  += (1.0f / 60.0f) / ENEMY_ANIM_BLEND;
            if (e->ablend >= 1.0f) {
                e->ablend = 1.0f;
                e->aprev  = -1;
            }
        }
        return;
    }

    if (!s.anim_on || e->kind != EM_ENEMY_KIND_CRAWLER) return;

    switch (e->state) {
    case EM_ENEMY_ATTACK:
        /* The worm's gameplay subs ARE the engine's anim windows (the
         * brain gates subs 0/2/3 on the anim-done bit 0x1000), so the
         * layer mirrors the sub directly. */
        switch (e->sub) {
        case 0:                          /* APPROACH = the emerge clip */
            break;                       /* set at spawn; plays out    */
        case 1: {
            /* STALK: the crawl loop at the ACTUAL ground speed; the
             * authored 21.27 u/s = rate 1.0. Floor it so turning in
             * place keeps writhing instead of freezing. */
            float rate = e->speed / ENEMY_LUNGE_SPEED;
            if (rate < ENEMY_ATTACK_MIN) rate = ENEMY_ATTACK_MIN;
            e->aphase = ANIM_CRAWL;
            enemy_anim_set(e, s.clip_crawl, rate);
            break;
        }
        case 2:
            if (e->aphase != ANIM_WINDUP && s.clip_windup >= 0) {
                e->aphase = ANIM_WINDUP;
                enemy_anim_set(e, s.clip_windup, 1.0f);
            }
            break;
        case 3:
            if (e->aphase != ANIM_LUNGE && s.clip_lunge >= 0) {
                e->aphase = ANIM_LUNGE;
                enemy_anim_set(e, s.clip_lunge, 1.0f);
            }
            break;
        }
        break;

    default:                    /* DEATH/FREE: pose frozen (fade only) */
        return;
    }

    /* advance the play heads (clips are baked at 60 fps = 1 frame per
     * 60 Hz tick at rate 1.0) and the 0.15 s crossfade */
    e->at += (double)e->arate;
    if (e->aprev >= 0) {
        e->aprev_t += (double)e->aprev_rate;
        e->ablend  += (1.0f / 60.0f) / ENEMY_ANIM_BLEND;
        if (e->ablend >= 1.0f) {
            e->ablend = 1.0f;
            e->aprev  = -1;
        }
    }
}

/* Build the instance's world palette: the anim-evaluated pose (or the
 * static base) composed with T(pos) * R_y(yaw).
 *
 * The placement composition is a LOCAL COPY of em_game.c's static
 * palette_apply_placement (same math, also copied by em_door.c) — not
 * shared because exporting it would touch em_game.h, which this module
 * doesn't own. Fold all three into a common helper when one moves.
 *
 * DEATH placeholder (flagged): a despawned-but-fading slot keeps its
 * frozen last pose — there is no death clip in the bank (the engine
 * REBINDS gib models instead); the exit is the per-draw alpha fade
 * (Enemy.tint via em_enemy_draw_tint), not a pose change, so the
 * palette is not rebuilt while the corpse fades. */
static void enemy_build_palette(Enemy *e)
{
    float yaw = e->yaw;
    float x   = e->pos[0];
    float y   = e->pos[1];
    float z   = e->pos[2];
    uint32_t bones = e->kind == EM_ENEMY_KIND_CRATE ? s.crate_bones
                   : e->kind == EM_ENEMY_KIND_BUG   ? s.bug_bones
                                                    : s.bone_count;

    /* CRATE IDLE jitter — the documented procedural disguise wiggle
     * (D_002468B0/B4/B8 perturb the world-matrix x/z translation; s23:
     * no skeletal clips exist for the 1-node rig). Deterministic pure
     * function of the update tick + the spawn slot, so runs and
     * captures reproduce: a slow chitter envelope gates a small x/z
     * wiggle and a yaw wobble (amplitudes/periods = flagged port
     * constants; the engine's table values are not exported). The
     * engine runs this block in STATE 4 ONLY — an alarmed crate hops
     * instead (state 1), so the jitter gates on IDLE. */
    if (e->kind == EM_ENEMY_KIND_CRATE && e->active &&
        e->state == EM_ENEMY_IDLE) {
        float t   = (float)s.frame;
        float ph  = (float)e->seed * 1.7f;
        float env = sinf(t * 0.037f + ph * 3.1f);
        if (env < 0.0f) env = 0.0f;          /* chitter ~half the time */
        x   += env * CRATE_JIT_POS * sinf(t * 0.83f + ph);
        z   += env * CRATE_JIT_POS * sinf(t * 0.67f + ph * 2.0f);
        yaw += env * CRATE_JIT_YAW * sinf(t * 0.49f + ph);
    }

    const float c = cosf(yaw), sn = sinf(yaw);

    if (e->kind == EM_ENEMY_KIND_CRATE) {
        memcpy(e->palette, s.crate_base, bones * 16 * sizeof(float));
    } else if (e->kind == EM_ENEMY_KIND_BUG) {
        /* the bug pose: walk/flinch evaluation against the BUG model
         * (no actor scale — decoded s68: the brains write no runtime
         * scale; the authored size IS the live size) */
        if (s.bug_anim_on && e->acur >= 0) {
            em_model_palette_at(&s.bug_model, (uint32_t)e->acur,
                                bug_eval_time(e->acur, e->at),
                                e->palette);
            if (e->aprev >= 0 && e->ablend < 1.0f) {
                em_model_palette_at(&s.bug_model, (uint32_t)e->aprev,
                                    bug_eval_time(e->aprev, e->aprev_t),
                                    s.blend_pal);
                uint32_t n = bones * 16;
                float    w = e->ablend;
                for (uint32_t i = 0; i < n; i++)
                    e->palette[i] = s.blend_pal[i] +
                                    (e->palette[i] - s.blend_pal[i]) * w;
            }
        } else {
            memcpy(e->palette, s.bug_base, bones * 16 * sizeof(float));
        }
    } else if (s.anim_on && e->acur >= 0) {
        em_model_palette_at(&s.model, (uint32_t)e->acur,
                            anim_eval_time(e->acur, e->at), e->palette);
        if (e->aprev >= 0 && e->ablend < 1.0f) {
            em_model_palette_at(&s.model, (uint32_t)e->aprev,
                                anim_eval_time(e->aprev, e->aprev_t),
                                s.blend_pal);
            uint32_t n = s.bone_count * 16;
            float    w = e->ablend;
            for (uint32_t i = 0; i < n; i++)
                e->palette[i] = s.blend_pal[i] +
                                (e->palette[i] - s.blend_pal[i]) * w;
        }
    } else {
        memcpy(e->palette, s.base, s.bone_count * 16 * sizeof(float));
    }

    /* WORM ACTOR SCALE — func_00154040 writes 0.5 to actor +0x80: the
     * in-game leech is HALF the authored size (~11 u long, FINDINGS
     * "CRAWLER RESOLVED"). The EMDL ships authored-size; scale the
     * whole posed palette about the model origin (closes the s62
     * "port applies actor scale" note that was never actually wired —
     * the authored-size whip arced the lunge 28 u over the player's
     * head, which is also why the latch segment could never connect). */
    if (e->kind == EM_ENEMY_KIND_CRAWLER && s.bone_count > 1) {
        for (uint32_t b = 0; b < bones; b++) {
            float *m = e->palette + b * 16;
            for (int k = 0; k < 3; k++) {
                m[k * 4 + 0] *= ENEMY_ACTOR_SCALE;
                m[k * 4 + 1] *= ENEMY_ACTOR_SCALE;
                m[k * 4 + 2] *= ENEMY_ACTOR_SCALE;
            }
            m[12] *= ENEMY_ACTOR_SCALE;
            m[13] *= ENEMY_ACTOR_SCALE;
            m[14] *= ENEMY_ACTOR_SCALE;
        }
    }

    for (uint32_t b = 0; b < bones; b++) {
        float *m = e->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float mx = m[col * 4 + 0], mz = m[col * 4 + 2];
            m[col * 4 + 0] =  c * mx + sn * mz;
            m[col * 4 + 2] = -sn * mx + c * mz;
        }
        m[12] += x;
        m[13] += y;
        m[14] += z;
    }
}

/* ------------------------------------------------------------------ */
/* Gib layer (visual only — see "GIB LAYER" in the file header)         */
/* ------------------------------------------------------------------ */

/* World palette of one gib: the model's identity base pose rotated by
 * the tumble yaw and translated to the instance position (the same
 * column rotation + translate composition as enemy_build_palette). */
static void gib_build_palette(Gib *g)
{
    const GibModel *gm = &s.gibm[g->fam][g->model];
    const float c = cosf(g->yaw), sn = sinf(g->yaw);

    memcpy(g->palette, gm->base, gm->bone_count * 16 * sizeof(float));
    for (uint32_t b = 0; b < gm->bone_count; b++) {
        float *m = g->palette + b * 16;
        for (int col = 0; col < 4; col++) {
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] =  c * x + sn * z;
            m[col * 4 + 2] = -sn * x + c * z;
        }
        m[12] += g->pos[0];
        m[13] += g->pos[1];
        m[14] += g->pos[2];
    }
}

/* Burst: launch 3-5 gib instances from a lethally-hit crawler with the
 * documented knockback shape (file header). The pieces come from ONE
 * husk family — the decoded variant-keyed rebind (GIB_FILES block):
 * a crate picks by its model byte (6 -> husk A, the wooden crate's
 * brown set; else husk B), any other kind (the debug worm demo —
 * worms have no engine rebind) keeps the husk-B set. The first
 * instance is the family HUSK itself (the engine's rebind corpse),
 * the rest round-robin its shards. Budgeted so the virtual draw
 * slots never push the crawler+gib total past ENEMY_SLOT_MAX (the
 * original budget; EM_ENEMY_MAX is now the chain reservation).
 * Returns the number launched (0 = corpse-fade placeholder). */
static int gib_burst(const Enemy *e)
{
    int fam = (e->kind == EM_ENEMY_KIND_CRATE && e->variant == 6)
              ? GIB_FAM_A : GIB_FAM_B;
    if (s.gibm_n[fam] == 0) return 0;

    int want   = GIB_COUNT_MIN + (int)(gib_rng() % GIB_COUNT_SPAN);
    int budget = ENEMY_SLOT_MAX - s.n; /* virtual slots we may occupy
                                        * (the original 16-slot budget —
                                        * keeps the gib RNG stream and
                                        * counts byte-identical)        */
    int spawned = 0;

    for (int k = 0; k < budget && spawned < want; k++) {
        Gib *g = &s.gib[k];
        if (g->active) continue;

        /* the documented rotation: hit vector turned by 90/180/270 deg
         * (RNG), plus the flagged +-30 deg port jitter */
        float ang = atan2f(e->hit_dir[0], e->hit_dir[1])
                  + GIB_ROT_STEP * (float)(1 + gib_rng() % 3)
                  + (float)((int)(gib_rng() % (2 * GIB_JITTER_DEG + 1))
                            - GIB_JITTER_DEG) * (ENEMY_PI / 180.0f);

        memset(g, 0, sizeof *g);
        g->active = 1;
        g->fam    = fam;
        /* the husk leads (instance 0 = the rebind corpse); shards
         * round-robin behind it (a 1-model family repeats the husk) */
        g->model  = (spawned == 0 || s.gibm_n[fam] == 1)
                    ? 0 : 1 + (int)(s.gib_next++ %
                                    (unsigned)(s.gibm_n[fam] - 1));
        g->pos[0] = e->pos[0];
        g->pos[1] = e->pos[1] + GIB_LAUNCH_LIFT;
        g->pos[2] = e->pos[2];
        g->vel[0] = sinf(ang) * GIB_SPEED;
        g->vel[1] = GIB_VY;
        g->vel[2] = cosf(ang) * GIB_SPEED;
        g->yaw    = ang;
        g->spin   = ((float)(gib_rng() % 2001) / 1000.0f - 1.0f)
                    * GIB_SPIN_MAX;
        g->y0     = e->pos[1];
        /* white tint, opaque — alpha 1.0 takes the renderer's exact
         * untinted path until the exit fade walks it down */
        g->tint[0] = g->tint[1] = g->tint[2] = g->tint[3] = 1.0f;
        gib_build_palette(g);
        if (k >= s.gib_tail) s.gib_tail = k + 1;
        spawned++;
    }
    return spawned;
}

/* Per-tick gib integration: arc under the 0.052 gravity, land on the
 * floor query, rest, then alpha-fade out and free (file header
 * timings; the fade replaces the old sink-despawn — same lifetime). */
static void gib_update(const EmCollision *coll)
{
    int tail = 0;
    for (int k = 0; k < s.gib_tail; k++) {
        Gib *g = &s.gib[k];
        if (!g->active) continue;
        g->age++;
        if (g->vel[0] != 0.0f || g->vel[1] != 0.0f || g->vel[2] != 0.0f) {
            g->pos[0] += g->vel[0];
            g->pos[2] += g->vel[2];
            g->vel[1] -= ENEMY_GRAVITY;
            g->pos[1] += g->vel[1];
            g->yaw    += g->spin;
            float fy = floor_at(coll, g->pos, g->y0);
            if (g->vel[1] < 0.0f && g->pos[1] <= fy) {   /* settle */
                g->pos[1] = fy;
                g->vel[0] = g->vel[1] = g->vel[2] = 0.0f;
                g->spin   = 0.0f;
            }
        } else if (g->age > GIB_REST_FRAMES) {           /* fade + free */
            g->tint[3] = (float)(GIB_REST_FRAMES + GIB_FADE_FRAMES
                                 - g->age) / (float)GIB_FADE_FRAMES;
            if (g->tint[3] < 0.0f) g->tint[3] = 0.0f;
            if (g->age > GIB_REST_FRAMES + GIB_FADE_FRAMES) {
                g->active = 0;
                continue;
            }
        }
        gib_build_palette(g);
        tail = k + 1;
    }
    s.gib_tail = tail;
}

/* ------------------------------------------------------------------ */
/* Generator pads (file header "GENERATOR KIND")                        */
/* ------------------------------------------------------------------ */

/* World palette of one pad: local scale (the decoded config footprint
 * in X/Z, the phase swell in Y — the PORT stand-in for the engine's
 * +0x80 VU-morph blend) * R_y(yaw), translated to the placement. */
static void gen_build_palette(Gen *g)
{
    const float ex = GEN_CFG[g->cfg][0];
    const float ez = GEN_CFG[g->cfg][1];
    const float ys = 1.0f + GEN_PAD_SWELL * g->phase;
    const float c  = cosf(g->yaw), sn = sinf(g->yaw);
    float *m = g->palette;

    memset(m, 0, sizeof g->palette);
    m[0]  =  c * ex;
    m[2]  = -sn * ex;
    m[5]  =  ys;
    m[8]  =  sn * ez;
    m[10] =  c * ez;
    m[15] =  1.0f;
    m[12] = g->pos[0];
    m[13] = g->pos[1];
    m[14] = g->pos[2];
}

/* PLACEHOLDER pad mesh (runtime-generated, our own original vertices,
 * NOT disc data — the engine's pad is procedural VU-morph geometry,
 * func_001E9580/001E9E60, and binds NO model-table entry, so there is
 * nothing to export; see the file header): a low three-tier mound on a
 * UNIT footprint (+-1), scaled per instance by the decoded config
 * extents in gen_build_palette. */
static int gen_mesh_get(EmGfx *gfx)
{
    if (s.gen_mesh) return 0;
    if (s.gen_tried) return -1;
    s.gen_tried = 1;

    float    verts[72 * 10];
    uint32_t indices[108];
    uint32_t nv = 0, ni = 0;
    const float h = GEN_PAD_HEIGHT;
    const float t0_lo[3] = { -1.00f, 0.0f,      -1.00f };
    const float t0_hi[3] = {  1.00f, h * 0.40f,  1.00f };
    const float t1_lo[3] = { -0.72f, h * 0.40f, -0.72f };
    const float t1_hi[3] = {  0.72f, h * 0.78f,  0.72f };
    const float t2_lo[3] = { -0.42f, h * 0.78f, -0.42f };
    const float t2_hi[3] = {  0.42f, h,          0.42f };
    box_emit(verts, &nv, indices, &ni, t0_lo, t0_hi);
    box_emit(verts, &nv, indices, &ni, t1_lo, t1_hi);
    box_emit(verts, &nv, indices, &ni, t2_lo, t2_hi);

    s.gen_mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                    NULL, 0, NULL, 0);
    if (!s.gen_mesh) return -1;
    printf("generator pad: PLACEHOLDER mound (%u verts, %u tris, "
           "runtime-generated — the engine pad is procedural VU-morph "
           "geometry, no model-table entry to export)\n", nv, ni / 3);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tendril field (file header "TENDRIL FIELD")                          */
/* ------------------------------------------------------------------ */

/* Load the shared spike mesh once (the static EMDL path, like the
 * gibs). ABSENT asset = a logged skip: the field machine still runs
 * (trigger/sound/state), only the 12 draws are skipped. */
static int tf_mesh_get(EmGfx *gfx)
{
    if (s.tf_mesh) return 0;
    if (s.tf_tried) return -1;
    s.tf_tried = 1;

    if (em_model_load(&s.tf_model, TF_ASSET) != 0) {
        printf("tendril field: no %s — the field runs, spikes are not "
               "drawn (export chunk03/f13_id15.bin as a static EMDL "
               "with the decomp repo's exporter)\n", TF_ASSET);
        return -1;
    }
    if (s.tf_model.bone_count > TF_BONE_MAX) {
        fprintf(stderr, "enemy: %s: %u bones > %d\n", TF_ASSET,
                s.tf_model.bone_count, TF_BONE_MAX);
        em_model_free(&s.tf_model);
        return -1;
    }
    s.tf_mesh = em_gfx_mesh_create(gfx, s.tf_model.verts,
                                   s.tf_model.vert_count,
                                   s.tf_model.indices,
                                   s.tf_model.index_count,
                                   (const EmGfxTexDesc *)s.tf_model.texs,
                                   s.tf_model.tex_count, s.tf_model.texels,
                                   s.tf_model.flags);
    if (!s.tf_mesh) {
        em_model_free(&s.tf_model);
        return -1;
    }
    s.tf_has_model = 1;
    s.tf_bones     = s.tf_model.bone_count;
    em_model_palette_at(&s.tf_model, 0, 0.0, s.tf_base);
    printf("tendril model: %s — %u verts, %u tris, %u texture(s)\n",
           TF_ASSET, s.tf_model.vert_count, s.tf_model.index_count / 3,
           s.tf_model.tex_count);
    return 0;
}

/* Attach a mode-1 pad's field PAIR (engine: func_0015A200(pad, 0xE, 0)
 * + (pad, 0xE, 1) at generator init — always a pair, exactly once).
 * Pool exhaustion skips with a log (the engine's full-actor-pool
 * NULL alloc — and unlike the worm path there is no retry). */
static void tf_attach(EmGfx *gfx, int pad)
{
    int first = s.tf_n;

    tf_mesh_get(gfx);            /* load-if-present (absent = logged) */
    for (int idx = 0; idx < 2; idx++) {
        if (s.tf_n >= EM_TENDRIL_MAX) {
            printf("tendril field: pool full (%d) — generator %d pair "
                   "member %d skipped\n", EM_TENDRIL_MAX, pad, idx);
            break;
        }
        Tendril *t = &s.tf[s.tf_n];
        memset(t, 0, sizeof *t);
        t->active = 1;
        t->pair   = (uint8_t)idx;
        t->pad    = pad;
        /* random start row of the cycling girth table (PORT: module
         * LCG stands in for the engine frame RNG, flagged) */
        t->gcur   = (uint8_t)(gib_rng() & 3u);
        s.tf_n++;
    }
    if (s.tf_n > first)
        printf("tendril field: %d field(s) attached to generator %d "
               "(rings %.1f+-%.1f / %.1f+-%.1f u, 12 spikes each)\n",
               s.tf_n - first, pad, TF_RING_BASE[0], TF_RING_SPAN[0],
               TF_RING_BASE[1], TF_RING_SPAN[1]);
}

/* SCAN gate (func_00154460): player inside 3x the parent pad footprint
 * and the (3 + recY) Y band. The engine's gameplay-frame gate (spad
 * 0x70003B8D == 0) holds natively because em_enemy_update only runs in
 * gameplay frames. */
static int tf_trigger(const Tendril *t, const float pp[3])
{
    const Gen *g = &s.gen[t->pad];
    return fabsf(pp[0] - g->pos[0]) <= TF_TRIG_MULT * GEN_CFG[g->cfg][0] &&
           fabsf(pp[2] - g->pos[2]) <= TF_TRIG_MULT * GEN_CFG[g->cfg][1] &&
           fabsf(pp[1] - g->pos[1]) <= TF_TRIG_Y + GEN_BOX_Y;
}

/* Scatter the 12 targets around the trigger anchor (the SCAN body):
 * polar ring per pair index, validity = inside the 0.92x pad ellipse
 * (the engine's func_001545B0 heading + radius-at-angle formula is the
 * same predicate as this normalized point-in-ellipse test). Plays the
 * 0x42D squelch when any target survives. */
static void tf_scatter(Tendril *t, const float pp[3])
{
    const Gen *g  = &s.gen[t->pad];
    float      ex = TF_ELLIPSE * GEN_CFG[g->cfg][0];
    float      ez = TF_ELLIPSE * GEN_CFG[g->cfg][1];
    int        any = 0;

    t->anchor[0] = pp[0];
    t->anchor[1] = g->pos[1];
    t->anchor[2] = pp[2];
    for (int i = 0; i < TF_SPIKES; i++) {
        TfSpike *sp = &t->sp[i];
        float th = (float)(gib_rng() % 65536u) *
                   (2.0f * ENEMY_PI / 65536.0f);
        float r  = TF_RING_BASE[t->pair] - TF_RING_SPAN[t->pair] +
                   (float)(gib_rng() % 65536u) / 65536.0f *
                   (2.0f * TF_RING_SPAN[t->pair]);
        sp->x = t->anchor[0] + sinf(th) * r;
        sp->z = t->anchor[2] + cosf(th) * r;
        {
            float nx = (sp->x - g->pos[0]) / ex;
            float nz = (sp->z - g->pos[2]) / ez;
            sp->valid = nx * nx + nz * nz <= 1.0f;
        }
        sp->phase = (int16_t)(48 + (int)(gib_rng() % 80u));
        sp->vel   = 0;
        sp->ramp  = 0;
        sp->girth = TF_GIRTH[t->gcur++ & 3];
        any      |= sp->valid;
    }
    if (any)
        /* 0x42D — engine play_sound(actor, id, 0, 300.0): positional
         * at the pad actor's placement (the spikes' parent origin) */
        em_sfx_play_at(TF_SFX_TRIGGER, g->pos, 300.0f);
}

/* World palette of one spike: the static base pose scaled (X/Z =
 * girth/256, Y = phase*ramp/65536 — the deploy ramp times the bob)
 * and translated to the record's target at pad height. Same column
 * composition as the gib palette (scale instead of tumble). */
static void tf_pal_build(Tendril *t, int i)
{
    const TfSpike *sp = &t->sp[i];
    float sx = (float)sp->girth / 256.0f;
    float sy = (float)sp->phase * (float)sp->ramp / 65536.0f;
    float *m = t->pal[i];

    if (!s.tf_has_model) return;        /* nothing will draw anyway */
    memcpy(m, s.tf_base, s.tf_bones * 16 * sizeof(float));
    for (uint32_t b = 0; b < s.tf_bones; b++) {
        float *bm = m + b * 16;
        for (int col = 0; col < 4; col++) {
            bm[col * 4 + 0] *= sx;
            bm[col * 4 + 1] *= sy;
            bm[col * 4 + 2] *= sx;      /* girth scales X and Z      */
        }
        bm[12] += sp->x;
        bm[13] += t->anchor[1];
        bm[14] += sp->z;
    }
}

/* Render-tail bob integrator (func_00154F00, s16 arithmetic kept):
 * runs once per tick while sub != 0, exactly the engine cadence (the
 * brain tail submits the 12 draws each tick). Also rebuilds the
 * field's per-draw TINT — the engine's every-tick RGB blend from the
 * room tint toward green by the parent open phase, plus the ramp
 * alpha (TF_TINT_* above; rides the chain via em_enemy_draw_tint into
 * em_gfx_draw_skinned_tinted). */
static void tf_animate(Tendril *t)
{
    const Gen *g    = &s.gen[t->pad];
    int        open = g->phase > 0.5f;  /* parent +0x80 breather phase */

    {
        float ph = g->phase;            /* 0 closed .. 1 fully open    */
        if (ph < 0.0f) ph = 0.0f;
        if (ph > 1.0f) ph = 1.0f;
        for (int c = 0; c < 3; c++)
            t->tint[c] = (TF_TINT_BASE[c] +
                          (1.0f - ph) *
                          (TF_TINT_ROOM[c] - TF_TINT_BASE[c])) / 128.0f;
        /* ramps move in lockstep (deploy/retract walk all 12), so
         * record 0 carries the shared alpha */
        t->tint[3] = (float)t->sp[0].ramp / (float)TF_RAMP_CAP;
    }

    for (int i = 0; i < TF_SPIKES; i++) {
        TfSpike *sp = &t->sp[i];
        if (!sp->valid) continue;
        sp->phase = (int16_t)(sp->phase + sp->vel);
        if (open) {                     /* violent thrash (pad open)   */
            sp->vel = (int16_t)(sp->vel - 8);
            if (sp->phase < 128)
                sp->vel = (int16_t)(sp->vel + 28 + (int)(gib_rng() % 14u));
        } else {                        /* gentle bob (pad closed)     */
            sp->vel = (int16_t)(sp->vel - 1);
            if (sp->phase < 128)
                sp->vel = (int16_t)(sp->vel + 3 + (int)(gib_rng() % 5u));
            if (sp->vel >= 8)
                sp->vel = (int16_t)(sp->vel / 2);
        }
        if (sp->phase < TF_PHASE_FLOOR) {
            sp->phase = TF_PHASE_FLOOR;
            sp->vel   = (int16_t)(3 + (int)(gib_rng() % 5u));
        }
        tf_pal_build(t, i);
    }
}

/* Per-tick field machine (func_001549C0 sub-states, engine values). */
static void tf_tick(Tendril *t, const float pp[3])
{
    switch (t->sub) {
    case 0:                             /* SCAN                        */
        if (!tf_trigger(t, pp))
            return;                     /* idle: no render tail        */
        tf_scatter(t, pp);
        t->timer = TF_TICKS;
        t->sub   = 1;
        /* FALLTHROUGH — the engine deploys on the trigger tick */
    case 1:                             /* DEPLOY                      */
        for (int i = 0; i < TF_SPIKES; i++) {
            t->sp[i].ramp = (int16_t)(t->sp[i].ramp + TF_RAMP_STEP);
            if (t->sp[i].ramp > TF_RAMP_CAP)
                t->sp[i].ramp = TF_RAMP_CAP;
        }
        if (--t->timer <= 0)
            t->sub = 2;
        break;
    case 2: {                           /* HOLD                        */
        const Gen *g  = &s.gen[t->pad];
        float      dx = pp[0] - t->anchor[0];
        float      dz = pp[2] - t->anchor[2];
        if (fabsf(pp[1] - g->pos[1]) > TF_TRIG_Y + GEN_BOX_Y ||
            dx * dx + dz * dz >= TF_HOLD_R2[t->pair]) {
            t->timer = TF_TICKS;
            t->sub   = 3;
        }
        break;
    }
    case 3:                             /* RETRACT                     */
        for (int i = 0; i < TF_SPIKES; i++) {
            t->sp[i].ramp = (int16_t)(t->sp[i].ramp - TF_RAMP_STEP);
            if (t->sp[i].ramp < 0)
                t->sp[i].ramp = 0;
        }
        if (--t->timer <= 0)
            t->sub = 4;
        break;
    default:                            /* 4 RESET -> rescan next tick */
        t->sub = 0;
        return;                         /* engine: no render at sub 0  */
    }
    tf_animate(t);     /* the brain tail renders whenever sub != 0 */
}

int em_enemy_add_generator(EmGfx *gfx, const float pos[3], float yaw,
                           int cfg, int link)
{
    if (s.gen_n >= EM_GENERATOR_MAX) return -1;
    if (cfg < 0 || cfg > 6 || link < 0 || link > 2) return -1;
    if (gen_mesh_get(gfx) != 0) return -1;
    /* A mode-2 pad's worms spawn later, inside em_enemy_update (no gfx
     * handle there) — preload the crawler mesh + gibs now, exactly the
     * crate rule. */
    if (enemy_mesh_get(gfx) != 0) return -1;
    gib_models_load(gfx);

    Gen *g = &s.gen[s.gen_n];
    memset(g, 0, sizeof *g);
    g->cfg    = (uint8_t)cfg;
    g->link   = (uint8_t)link;
    g->pos[0] = pos[0];
    g->pos[1] = pos[1];
    g->pos[2] = pos[2];
    g->yaw    = yaw;

    /* INIT mode draw (func_0015A2C0 state 0): link 1/2 pulls one byte
     * from the decoded count table — row = RNG & 3 (PORT: the module
     * LCG stands in for the engine frame RNG at spad 0x70003B68,
     * flagged), column = the global per-link cursor & 7 (D_008106EC/ED,
     * post-incremented) — and stores it as the runtime mode (+0x56).
     * link 0 (the office sub-state-0 set) stays mode 0: an inert pad. */
    if (link == 1 || link == 2) {
        int row = (int)(gib_rng() & 3u);
        int col = s.gen_cursor[link - 1] & 7;
        s.gen_cursor[link - 1]++;
        g->mode = GEN_TBL[link - 1][row][col];
        if (g->mode == 1)
            /* engine: mode 1 ALSO spawns an immediate pair of kind-0xE
             * TENDRIL FIELDS (func_0015A200(actor, 0xE, 0/1) -> brain
             * func_001546C0 — file header "TENDRIL FIELD"). */
            tf_attach(gfx, s.gen_n);
    }
    gen_build_palette(g);
    printf("enemy generator %d: at (%.1f, %.1f, %.1f) yaw %.3f, cfg %d "
           "(box %gx%g), link %d -> mode %d\n", s.gen_n,
           pos[0], pos[1], pos[2], yaw, cfg,
           GEN_CFG[cfg][0] * 2.0f, GEN_CFG[cfg][1] * 2.0f, link, g->mode);
    return s.gen_n++;
}

/* Per-tick generator behavior (func_0015A2C0 state 1; engine sub-state
 * values kept in g->sub). Worm spawns route through enemy_spawn, so
 * ENEMY_SLOT_MAX is the same wall the engine's full actor pool is:
 * a failed alloc does NOT consume the cap (func_0015A200 returns 0 ->
 * no +0x2E++) — the pad retries after the next delay. */
static void gen_tick(Gen *g, const float pp[3])
{
    /* Trigger: player inside the config box THIS tick. Engine: the
     * pair pass func_001A8BE0 -> func_001A8840 writes +0x0A during
     * frame close-out and the behavior consumes+clears it next tick;
     * natively computed in place (the one-frame phase is immaterial). */
    g->in_box = fabsf(pp[0] - g->pos[0]) <= GEN_CFG[g->cfg][0] &&
                fabsf(pp[2] - g->pos[2]) <= GEN_CFG[g->cfg][1] &&
                fabsf(pp[1] - g->pos[1]) <= GEN_BOX_Y + GEN_BOX_Y_TOL;

    switch (g->mode) {
    case 1:
        /* BREATHER/TRAP. sub 0 closed: the in-box charge drives the
         * morph phase; leaving resets it. */
        if (g->sub == 0) {
            g->phase = g->timer / GEN_CHARGE_OPEN;
            if (!g->in_box) {
                g->timer = 0.0f;
                break;
            }
            g->timer += 1.0f;
            if (g->timer >= GEN_CHARGE_OPEN) {
                g->sub   = 1;
                g->open  = 1;
                g->timer = GEN_OPEN_HOLD;
                g->phase = 1.0f;
            }
            break;
        }
        /* sub 1 OPEN: breathing sound every 128 frames (the engine
         * gates on the global frame counter), hold while the player
         * stays, decay to closed when they leave. The open pad hurts
         * the standing player — engine: func_001A8840 fires event 3
         * with magnitude 5.0; PORT: a one-shot mailbox write per box
         * entry (flagged stand-in). */
        if ((s.frame & 127) == 0)
            /* 0x42F at the pad actor — play_sound radius 300 (the
             * func_0015A2C0 site's f12 = 0x43960000) */
            em_sfx_play_at(GEN_SFX_BREATH, g->pos, 300.0f);
        g->phase = g->timer / GEN_OPEN_HOLD;
        if (g->in_box) {
            g->timer = GEN_OPEN_HOLD;
            if (!g->trap_armed) {
                s.player_hit = GEN_TRAP_HIT;
                g->trap_armed = 1;
            }
        } else {
            g->trap_armed = 0;
            g->timer -= 1.0f;
            if (g->timer <= 0.0f) {
                g->sub   = 0;
                g->open  = 0;
                g->timer = 0.0f;
                g->phase = 0.0f;
            }
        }
        break;

    case 2:
        /* WORM EMITTER. sub 0: charge needs CONSECUTIVE in-box frames
         * (engine: +0x20 += 1 while +0x0A, reset to 0 without it; the
         * spawn fires when +0x20+1 exceeds 120 = the 121st frame). */
        if (g->sub == 0) {
            if (!g->in_box) {
                g->timer = 0.0f;
                break;
            }
            g->timer += 1.0f;
            if (g->timer <= GEN_CHARGE_WORM)
                break;
            /* spawn ONE worm AT THE GENERATOR ORIGIN (func_0015A200
             * copies the parent +0xB0 verbatim — no offsets); the
             * engine zeroes the child yaw and the leech brain init
             * yaws it toward the player, so the port applies that yaw
             * at spawn (same rule as the crate burst). */
            {
                float dx   = pp[0] - g->pos[0];
                float dz   = pp[2] - g->pos[2];
                float wyaw = (fabsf(dx) + fabsf(dz) > 1e-4f)
                             ? atan2f(dx, dz) : g->yaw;
                int   wi   = enemy_spawn(EM_ENEMY_KIND_CRAWLER,
                                         g->pos, wyaw);
                if (wi >= 0) {
                    g->spawned++;     /* engine: +0x2E++ only on alloc */
                    /* 0x430 — the leech init (func_00154040) plays it
                     * at the spawned actor = the generator origin,
                     * play_sound radius 300 (f12 = 0x43960000) */
                    em_sfx_play_at(GEN_SFX_WORM, g->pos, 300.0f);
                }
            }
            if (g->spawned >= GEN_WORM_CAP) {
                g->sub = 2;           /* EXHAUSTED — permanent */
                printf("generator: exhausted (cap %d worms)\n",
                       GEN_WORM_CAP);
            } else {
                g->sub   = 1;
                g->timer = GEN_DELAY[gib_rng() % 3u];
                if (s.gt_on)
                    g->timer /= s.gt_delay_div;  /* test acceleration */
            }
            break;
        }
        if (g->sub == 1) {
            /* delay counts down WITHOUT needing the player */
            g->timer -= 1.0f;
            if (g->timer <= 0.0f) {
                g->sub   = 0;
                g->timer = 0.0f;
            }
        }
        /* sub 2: exhausted — still renders, never reacts again */
        break;

    default:
        /* mode 0: inert pad (the whole office sub-state-0 set) */
        break;
    }
    gen_build_palette(g);
}

/* --- EM_ENEMY_TEST=4 harness (file header) ------------------------- */

static void gt_check(int cond, const char *what)
{
    if (cond) return;
    s.gt_fail++;
    printf("generator test: CHECK FAILED — %s\n", what);
}

static void gen_test_finish(void)
{
    const Gen *g = s.gt_gen >= 0 ? &s.gen[s.gt_gen] : NULL;
    printf("generator test: %d worm(s) emitted (cap %d), pad mode %d "
           "sub %d spawned %d, worm-1 witness frame %d, %d live "
           "enem%s — %s\n", s.gt_worms, GEN_WORM_CAP,
           g ? g->mode : -1, g ? g->sub : -1, g ? g->spawned : -1,
           s.gt_kill1_f, em_enemy_alive(),
           em_enemy_alive() == 1 ? "y" : "ies",
           s.gt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    em_frame_request_quit();
}

static void gen_test_script(void)
{
    if (s.gt_gen < 0) {       /* spawn failed at arm time — reported */
        gen_test_finish();
        return;
    }
    Gen *g = &s.gen[s.gt_gen];

    /* worm-spawn watermark: slots are append-only, so every new index
     * past gt_last_n is a generator worm (no other spawner runs) */
    if (s.n > s.gt_last_n) {
        for (int i = s.gt_last_n; i < s.n; i++) {
            s.gt_worms++;
            float d = fabsf(s.e[i].pos[0] - g->pos[0]) +
                      fabsf(s.e[i].pos[1] - g->pos[1]) +
                      fabsf(s.e[i].pos[2] - g->pos[2]);
            gt_check(d < 0.01f, "worm emerged at the generator origin");
            gt_check(s.e[i].kind == EM_ENEMY_KIND_CRAWLER,
                     "the spawn is a worm");
            if (s.gt_worms == 1) {
                gt_check(s.frame >= 121,
                         "no worm before the 121-frame charge");
                s.gt_kill_i = i;            /* arm the J2 witness      */
                s.gt_kill_f = s.frame + 15; /* mid-approach: the write
                                             * must NOT kill (s66)     */
            } else if (s.gt_worms == 2) {
                gt_check(s.gt_kill1_f > 0,
                         "kept emitting after the worm-1 witness");
            }
            printf("generator test: worm %d at frame %d\n",
                   s.gt_worms, s.frame);
        }
        s.gt_last_n = s.n;
    }

    /* THE J2 WITNESS (s66): write the old lethal mailbox code into
     * worm 1 mid-approach and assert it does NOT die — the worm
     * consumes nothing (the engine's only +0x36 access is the release
     * teardown). Then watch its OWN lunge lifecycle end it: spawned
     * at the player's feet box, the lunge resolve bursts it (or the
     * miss despawns it) well inside approach+stalk+windup+lunge =
     * <= 375 ticks (+ margin). */
    if (s.gt_kill_i >= 0) {
        if (s.frame == s.gt_kill_f) {
            em_enemy_damage(s.gt_kill_i, 0x400A);
        } else if (s.frame == s.gt_kill_f + 5) {
            gt_check(s.e[s.gt_kill_i].active &&
                     s.e[s.gt_kill_i].state == EM_ENEMY_ATTACK &&
                     s.e[s.gt_kill_i].hp == ENEMY_HP_WORM,
                     "mailbox write did NOT kill worm 1 (J2 s66: the "
                     "worm consumes nothing)");
            s.gt_kill1_f = s.frame;
        } else if (s.frame > s.gt_kill_f + 5) {
            if (!s.e[s.gt_kill_i].active) {
                gt_check(s.e[s.gt_kill_i].state == EM_ENEMY_FREE &&
                         s.e[s.gt_kill_i].mailbox == 0,
                         "worm 1 released by its OWN lifecycle, +0x36 "
                         "teardown-cleared");
                s.gt_kill_i = -1;
            } else if (s.frame > s.gt_kill_f + 500) {
                gt_check(0, "worm 1 ended by its own lunge lifecycle "
                            "within the resolve window");
                s.gt_kill_i = -1;
            }
        }
    }

    /* exhaustion, then a 240-frame silence window (no 5th worm) */
    if (g->mode == 2 && g->sub == 2) {
        if (s.gt_post == 0)
            gt_check(s.gt_worms == GEN_WORM_CAP &&
                     g->spawned == GEN_WORM_CAP,
                     "exactly 4 worms at exhaustion");
        if (++s.gt_post == 240) {
            gt_check(s.gt_worms == GEN_WORM_CAP,
                     "no 5th worm after exhaustion");
            gen_test_finish();
        }
    } else if (s.frame > 2400) {
        gt_check(0, "generator reached the 4-worm cap by frame 2400");
        gen_test_finish();
    }
}

/* --- EM_ENEMY_TEST=5 harness (file header) ------------------------- */

#define TT_STEP 1.0f   /* synthetic walker speed, units per tick */

static void tt_check(int cond, const char *what)
{
    if (cond) return;
    s.tt_fail++;
    printf("tendril test: CHECK FAILED — %s\n", what);
}

static void tt_finish(void)
{
    const Tendril *a = s.tt_f[0] >= 0 ? &s.tf[s.tt_f[0]] : NULL;
    const Tendril *b = s.tt_f[1] >= 0 ? &s.tf[s.tt_f[1]] : NULL;
    printf("tendril test: pad %d, fields [%d,%d], %d trigger(s), "
           "retracts %d+%d, final sub %d/%d — %s\n",
           s.tt_pad, s.tt_f[0], s.tt_f[1], s.tt_trigs,
           s.tt_retract[0], s.tt_retract[1],
           a ? a->sub : -1, b ? b->sub : -1,
           s.tt_fail == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    s.tt_stage = 99;
    em_frame_request_quit();
}

/* Arm: the link-1 test pad at the player spawn, mode FORCED to 1, the
 * walker staged outside the 3x trigger box. First update tick only. */
static void tt_arm(const float player_pos[3])
{
    s.tt_armed = 1;
    float gp[3] = { player_pos[0], player_pos[1], player_pos[2] };
    s.tt_pad = em_enemy_add_generator(em_frame_gfx(), gp, 0.0f, 1, 1);
    if (s.tt_pad < 0) {
        tt_check(0, "generator spawn at arm time");
        tt_finish();
        return;
    }
    Gen *g = &s.gen[s.tt_pad];
    if (g->mode != 1) {
        /* TEST-ONLY override (documented in the file header): the
         * link-1 table draw is RNG 0/1 — force the breather outcome so
         * the run always exercises the field, and attach the pair the
         * skipped draw would have spawned. */
        g->mode = 1;
        tf_attach(em_frame_gfx(), s.tt_pad);
        printf("tendril test: link-1 mode draw FORCED to 1\n");
    }
    for (int i = 0; i < s.tf_n; i++)
        if (s.tf[i].pad == s.tt_pad && s.tf[i].pair < 2)
            s.tt_f[s.tf[i].pair] = i;
    tt_check(s.tt_f[0] >= 0 && s.tt_f[1] >= 0,
             "field pair attached to the mode-1 pad");
    if (s.tt_f[0] < 0 || s.tt_f[1] < 0) {
        tt_finish();
        return;
    }
    /* walker: outside the 3x box (kind 1: 3 * 15 = 45 u), walking +X */
    s.tt_w[0] = gp[0] - (TF_TRIG_MULT * GEN_CFG[1][0] + 1.5f);
    s.tt_w[1] = gp[1];
    s.tt_w[2] = gp[2];
    printf("tendril test: pad %d at (%.1f, %.1f, %.1f), walker from "
           "%.1f u out\n", s.tt_pad, gp[0], gp[1], gp[2],
           TF_TRIG_MULT * GEN_CFG[1][0] + 1.5f);
}

/* Advance the synthetic walker (runs BEFORE the field ticks, so the
 * machine sees this tick's position — the engine's own ordering: the
 * player moves in the frame phases before the actor-pool tick). */
static void tt_walker(void)
{
    const Gen *g = &s.gen[s.tt_pad];

    switch (s.tt_stage) {
    case 0:                            /* walk onto the pad center     */
        s.tt_w[0] += TT_STEP;
        if (s.tt_w[0] >= g->pos[0]) {
            s.tt_w[0]    = g->pos[0];
            s.tt_stage   = 1;
            s.tt_stage_f = s.frame;
        }
        break;
    case 2:                            /* leave: walk off in +Z        */
        s.tt_w[2] += TT_STEP;
        if (s.tt_w[2] - g->pos[2] >
            TF_TRIG_MULT * GEN_CFG[g->cfg][1] + 1.5f) {
            s.tt_stage   = 3;
            s.tt_stage_f = s.frame;
        }
        break;
    default:                           /* 1 stand still / 3 wait / 99  */
        break;
    }
}

/* Per-tick monitor + staged asserts (runs AFTER the field ticks). */
static void tt_script(void)
{
    Tendril *a = &s.tf[s.tt_f[0]];
    Tendril *b = &s.tf[s.tt_f[1]];

    /* edges: SCAN->DEPLOY triggers (field 0) + RETRACT entries */
    if (a->sub != 0 && a->sub != 4 &&
        (s.tt_psub[0] == 0 || s.tt_psub[0] == 4))
        s.tt_trigs++;
    for (int k = 0; k < 2; k++) {
        const Tendril *t = k == 0 ? a : b;
        if (s.tt_stage >= 2 && t->sub == 3 && s.tt_psub[k] != 3)
            s.tt_retract[k]++;
        s.tt_psub[k] = t->sub;
    }

    if (s.tt_stage == 1 && s.frame == s.tt_stage_f + 50) {
        /* steady-HOLD checkpoint: the walker has stood at the pad
         * center long enough for the last walking pulse to settle. */
        tt_check(a->sub == 2 && b->sub == 2,
                 "both fields in HOLD while standing on the anchor");
        int va = 0, vb = 0, ramp_ok = 1;
        for (int i = 0; i < TF_SPIKES; i++) {
            va += a->sp[i].valid;
            vb += b->sp[i].valid;
            ramp_ok &= a->sp[i].ramp == 8 * TF_RAMP_STEP &&
                       b->sp[i].ramp == 8 * TF_RAMP_STEP;
        }
        tt_check(va == TF_SPIKES && vb == TF_SPIKES,
                 "all 12 targets valid in both fields (anchor on the "
                 "pad)");
        tt_check(ramp_ok, "every ramp at 296 (the 8-tick x37 deploy)");
        tt_check(s.tt_trigs >= 2,
                 "field triggered while walking AND re-anchored after "
                 "the stop");
        /* spike draws through the chain contract (asset-dependent) */
        int drawn = 0;
        EmGfxMesh   *mm;
        const float *pl;
        uint32_t     bc;
        for (int i = s.n + s.gib_tail + s.gen_n; i < em_enemy_count();
             i++)
            drawn += em_enemy_draw(i, &mm, &pl, &bc);
        if (s.tf_mesh)
            tt_check(drawn == 2 * TF_SPIKES,
                     "24 spike draws at HOLD (both pair members)");
        printf("tendril test: HOLD — valid %d+%d, ramp %d, %d spike "
               "draw(s)%s\n", va, vb, a->sp[0].ramp, drawn,
               s.tf_mesh ? "" : " (no tendril.emdl — drawing skipped, "
                                "logged above)");
        s.tt_stage   = 2;
        s.tt_stage_f = s.frame;
    } else if (s.tt_stage == 2 && s.frame == s.tt_stage_f + 20) {
        /* both hold radii (2 u / 4 u) are behind the walker by now */
        tt_check(s.tt_retract[0] > 0 && s.tt_retract[1] > 0,
                 "both fields RETRACTED after leaving the hold radius");
    } else if (s.tt_stage == 3 && s.frame == s.tt_stage_f + 30) {
        int quiet = a->sub == 0 && b->sub == 0;
        int rest  = 1;
        for (int i = 0; i < TF_SPIKES; i++)
            rest &= a->sp[i].ramp == 0 && b->sp[i].ramp == 0;
        tt_check(quiet, "both fields rescanning (sub 0) outside the "
                        "trigger box");
        tt_check(rest, "all ramps retracted to 0");
        tt_finish();
    }
}

/* ------------------------------------------------------------------ */
/* State machine                                                        */
/* ------------------------------------------------------------------ */

/* Crate BURST (state 2, crate kind — s68 REBINDING): free the slot,
 * hatch the nest-group BUGS at the crate position through the normal
 * spawn path (the engine's state-2 walks the registry group's 0x2C
 * records and copies pos += parent / rot / param per child; the
 * office groups hold 2-3 bug records). The records are disc data, so
 * the count rides the manifest (`bugs <n>`, Enemy.children) and the
 * port stands in a small deterministic ring for the records' offsets
 * + the record rot (flagged — the bug INIT then yaws each child at
 * the player anyway, the port's flagged init). Then scatter the husk
 * gibs with the shared launcher. Children first: gib_burst budgets
 * its virtual draw slots against the LIVE instance count. The crate
 * never fades — the husk gibs replace it visually (no gibs loaded =
 * it just vanishes, matching the immediate gameplay despawn). A full
 * slot pool truncates the hatch exactly like the engine's exhausted
 * actor pool (func_0015A200 NULL alloc). */
static void crate_burst(Enemy *e, const float pp[3])
{
    e->state  = EM_ENEMY_FREE;
    e->active = 0;
    e->fade   = 0;

    /* timer/suicide bursts (no recorded hit vector): scatter the husk
     * along the facing — the engine's no-knockback arm */
    if (!e->hit_lethal) {
        e->hit_dir[0] = sinf(e->yaw);
        e->hit_dir[1] = cosf(e->yaw);
    }
    int hatched = 0;
    for (int k = 0; k < e->children; k++) {
        /* hatch ring (PORT stand-in for the record offsets): child k
         * at a fixed bearing around the crate, radius CRATE_BUG_RING */
        float a  = e->yaw + (float)k * (2.0f * ENEMY_PI /
                                        (float)e->children);
        float bp[3] = { e->pos[0] + sinf(a) * CRATE_BUG_RING,
                        e->pos[1],
                        e->pos[2] + cosf(a) * CRATE_BUG_RING };
        if (enemy_spawn(EM_ENEMY_KIND_BUG, bp, a) < 0) {
            printf("enemy: crate burst — slot pool full, %d/%d bug(s) "
                   "hatched\n", hatched, e->children);
            break;
        }
        hatched++;
    }
    int ng = gib_burst(e);
    printf("enemy: crate burst at (%.1f, %.1f, %.1f) — %d gib(s), "
           "%d bug(s) hatched\n", e->pos[0], e->pos[1], e->pos[2],
           ng, hatched);
    (void)pp;
}

/* BUG tick (s68 mailbox + s76 brain structure — see "THE BUG" in the
 * file header and FINDINGS "BUG BRAIN STATE MACHINES").
 *
 * The DECODED pieces: the EVERY-TICK mailbox consumption (func_00128B80
 * -> func_00129FC0: flinch below lethal, death at it; that lives in
 * enemy_tick, which sets sub=1 on a non-lethal hit and routes lethal to
 * DEATH) and the attack SHAPE — the two brains run a sense-gated
 * approach into a bite LUNGE (func_0012C490, clip 0x13) whose contact is
 * the shared melee resolver func_001B5360: a radius-6 sphere ~10u ahead
 * of the bug, tested vs the player and (on a hit) routed to the player
 * contact-damage latch. We mirror that with the same player-hit bridge
 * the worm uses (s.player_hit = 0x4000 | dmg).
 *
 * sub: 0 APPROACH, 1 FLINCH (set by enemy_tick's mailbox path — keep
 * this id), 2 WINDUP, 3 LUNGE, 4 RECOVER. The approach/lunge speeds and
 * timers and the bite damage are FLAGGED PORT constants (the move-helper
 * bodies + the contact-damage VALUE are the s76 open items); the
 * +10u/radius-6 contact box is read from func_001B5360. */
static void bug_attack_tick(const EmCollision *coll, Enemy *e,
                            const float pp[3])
{
    if (e->sub == 1) {                  /* FLINCH: hold the window     */
        if (--e->t28 <= 0)
            e->sub = 0;
        return;
    }

    /* Always face the player (the brains turn-toward every active tick
     * via func_001B12B0; the bug commits the bite only inside a cone). */
    float dx = pp[0] - e->pos[0];
    float dz = pp[2] - e->pos[2];
    float d2 = dx * dx + dz * dz;
    float want = (fabsf(dx) + fabsf(dz) > 1e-4f) ? atan2f(dx, dz)
                                                 : e->yaw;
    float diff = wrap_pi(want - e->yaw);
    if (diff >  BUG_TURN_RATE) diff =  BUG_TURN_RATE;
    if (diff < -BUG_TURN_RATE) diff = -BUG_TURN_RATE;
    e->yaw = wrap_pi(e->yaw + diff);

    switch (e->sub) {
    case 0:                             /* APPROACH (== pre-s76)       */
        if (d2 > BUG_STANDOFF * BUG_STANDOFF) {
            if (!enemy_probe(coll, e, e->yaw, BUG_WALK_SPEED + 0.5f)) {
                e->pos[0] += sinf(e->yaw) * BUG_WALK_SPEED;
                e->pos[2] += cosf(e->yaw) * BUG_WALK_SPEED;
                e->pos[1]  = floor_at(coll, e->pos, e->pos[1]);
            }
        } else if (fabsf(diff) < BUG_AIM_CONE) {
            e->sub       = 2;           /* at standoff + facing -> bite */
            e->t28       = BUG_WINDUP_F;
            e->atk_armed = 0;           /* one contact per bite         */
        }
        break;

    case 2:                             /* WINDUP (bite lead-in)       */
        if (--e->t28 <= 0) {
            e->sub = 3;
            e->t28 = BUG_BITE_F;
        }
        break;

    case 3:                             /* BITE active: contact window */
        /* func_001B5360: the attack box is the bug position pushed
         * +BUG_CONTACT_FWD ahead, radius-6 vs the player. At the ~5u
         * standoff |5-10| = 5 <= 6, so the forward box reaches the
         * player. NO position change. On a hit the bug LATCHES (s76) —
         * it does not bite-and-recover; it clings until shaken off. */
        if (!e->atk_armed) {
            float bx  = e->pos[0] + sinf(e->yaw) * BUG_CONTACT_FWD;
            float bz  = e->pos[2] + cosf(e->yaw) * BUG_CONTACT_FWD;
            float cdx = pp[0] - bx;
            float cdy = pp[1] - e->pos[1];
            float cdz = pp[2] - bz;
            if (cdx * cdx + cdy * cdy + cdz * cdz <=
                BUG_CONTACT_R * BUG_CONTACT_R) {
                e->atk_armed = 1;
                e->sub       = 5;        /* LATCH onto the player */
                break;
            }
        }
        if (--e->t28 <= 0) {
            e->sub = 4;
            e->t28 = BUG_RECOVER_F;
        }
        break;

    case 4:                             /* RECOVER + cooldown          */
        if (--e->t28 <= 0)
            e->sub = 0;
        break;

    case 5: {                           /* LATCHED: cling to the player */
        /* anchored on the player's body at a per-bug bearing (a swarm
         * spreads around them); held here until the player's shake-off
         * (em_enemy_shake_off) throws it back to RECOVER. The drain is
         * applied player-side while any bug is latched. */
        float a = (float)(e->seed & 7) * (ENEMY_PI * 0.25f);
        e->pos[0] = pp[0] + sinf(a) * BUG_CLING_R;
        e->pos[1] = pp[1] + BUG_CLING_Y;
        e->pos[2] = pp[2] + cosf(a) * BUG_CLING_R;
        e->yaw    = wrap_pi(a + ENEMY_PI);   /* face into the player */
        break;
    }

    default:                            /* any stray sub -> approach   */
        e->sub = 0;
        break;
    }
}

/* CRATE attack — the decoded func_001551B0 state 1: a BLIND suicide
 * hop-run (no player reference exists in the engine's state 1; heading
 * = probe-steered + RNG). The mailbox is NEVER polled here: damage
 * taken mid-run stays pending (kills on the next IDLE tick if the run
 * returns there) and is ABSORBED (+0x36 cleared) when the run ends in
 * the burst — both decoded off the disassembly. Port locomotion
 * stand-in (flagged): repeated hops with a re-steer on landing; the
 * engine launches one long leap. The attack timer +0x2A = 180 is armed
 * once at the first launch (the engine reuses +0x2A; the port splits
 * the field because of the repeated hops). */
static void crate_attack_tick(const EmCollision *coll, Enemy *e)
{
    if (e->atk_armed && --e->retreat < 0) {
        /* the 180-tick suicide-run timer expired (the engine also
         * bursts on probe result 4 = surface lost — the port floor
         * query falls back instead of failing, so the timer is the
         * port's only burst arm): absorb pending damage and burst. */
        e->mailbox    = 0;          /* decoded: sh zero, 0x36 pre-burst */
        e->hit_lethal = 0;
        e->state      = EM_ENEMY_DEATH;
        return;
    }
    if (e->sub == 0) {
        /* STEER: probe the 4 diagonals and turn AWAY from blocked
         * sides at +-0.0524 rad (decoded); when fully boxed in, hold
         * and fall back to IDLE once the steer budget (+0x2A = 6 at
         * the wake) is spent. NO player seek (decoded). */
        int bl[4];   /* the 4 diagonals: +-45, +-135 deg off heading */
        int blocked = 0;
        static const float diag[4] = { 0.7854f, -0.7854f,
                                       2.3562f, -2.3562f };
        for (int k = 0; k < 4; k++) {
            bl[k] = enemy_probe(coll, e, e->yaw + diag[k],
                                ENEMY_PROBE_LEN);
            blocked += bl[k];
        }
        if (blocked >= 3 || (bl[0] && bl[3]) || (bl[1] && bl[2])) {
            /* >= 3 blocked or both opposite pairs (decoded gate) */
            if (!e->atk_armed && --e->retreat <= 0)
                e->state = EM_ENEMY_IDLE;   /* pending +0x36 kills on
                                             * the next IDLE tick     */
            return;
        }
        if (bl[0] != bl[1]) {
            /* a blocked front diagonal: rotate away (decoded rate) */
            e->yaw = wrap_pi(e->yaw + (bl[0] ? -ENEMY_TURN_RATE
                                             :  ENEMY_TURN_RATE));
        } else {
            /* open: the engine RNG-perturbs the velocity components
             * by (rand/2^31 - 0.5)/60 ~= +-1/120 rad on a unit
             * heading — same magnitude here, port LCG (flagged) */
            e->yaw = wrap_pi(e->yaw +
                             ((float)(gib_rng() % 65536u) / 65536.0f
                              - 0.5f) / 60.0f);
        }
        /* launch the hop; first launch arms the 180-tick run timer */
        e->vy     = ENEMY_HOP_VY;
        e->hop_y0 = e->pos[1];
        if (!e->atk_armed) {
            e->atk_armed = 1;
            e->retreat   = ENEMY_ATTACK_TICKS;   /* +0x2A = 0xB4 */
        }
        e->sub = 1;
    } else {
        /* HOP: forward integrate unless a wall blocks the step;
         * vertical under the engine's 0.052/tick gravity. */
        if (!enemy_probe(coll, e, e->yaw, ENEMY_HOP_SPEED + 0.5f)) {
            e->pos[0] += sinf(e->yaw) * ENEMY_HOP_SPEED;
            e->pos[2] += cosf(e->yaw) * ENEMY_HOP_SPEED;
        }
        e->vy     -= ENEMY_GRAVITY;
        e->pos[1] += e->vy;
        float floor_y = enemy_floor(coll, e);
        if (e->vy < 0.0f && e->pos[1] <= floor_y) {
            e->pos[1] = floor_y;
            e->vy     = 0.0f;
            e->sub    = 0;
        }
    }
}

/* THE LATCH QUERY — the engine's func_0019AA80(slotA, slotB, 0x20)
 * resolve arm (s66 live; the constants block above). Returns 1 when
 * the worm's neck->head rig segment crosses the player's hit volume.
 * Worm segment: world translations of palette nodes 13/16 (the REAL
 * animated pose, one tick stale) — needs the leech asset's 24-node
 * rig; without it (placeholder mesh / static base) returns -1 so the
 * caller folds the arm into the radius-6 contact (flagged fallback).
 * Player volume: capsule stand-in (PLAYER_HV_*) for the unexported
 * +0x58 sphere list — segment-vs-segment distance vs the radius. */
static int worm_latch_segment(const Enemy *e, const float pp[3])
{
    if (s.bone_count <= (uint32_t)ENEMY_LATCH_NODE_B)
        return -1;                     /* no rig data: caller folds   */
    const float *ma = e->palette + ENEMY_LATCH_NODE_A * 16;
    const float *mb = e->palette + ENEMY_LATCH_NODE_B * 16;
    /* segment 1 = worm neck->head; segment 2 = player capsule axis */
    float p1[3] = { ma[12], ma[13], ma[14] };
    float d1[3] = { mb[12] - ma[12], mb[13] - ma[13], mb[14] - ma[14] };
    float p2[3] = { pp[0], pp[1] + PLAYER_HV_Y0, pp[2] };
    float d2[3] = { 0.0f, PLAYER_HV_Y1 - PLAYER_HV_Y0, 0.0f };
    /* closest point pair of two segments (standard clamped solve) */
    float r[3] = { p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2] };
    float a = d1[0]*d1[0] + d1[1]*d1[1] + d1[2]*d1[2];
    float eL = d2[0]*d2[0] + d2[1]*d2[1] + d2[2]*d2[2];
    float f = d2[0]*r[0] + d2[1]*r[1] + d2[2]*r[2];
    float t = 0.0f, u = 0.0f;
    if (a > 1e-9f) {
        float c2 = d1[0]*r[0] + d1[1]*r[1] + d1[2]*r[2];
        float b  = d1[0]*d2[0] + d1[1]*d2[1] + d1[2]*d2[2];
        float den = a * eL - b * b;
        if (den > 1e-9f) t = (b * f - c2 * eL) / den;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        u = eL > 1e-9f ? (b * t + f) / eL : 0.0f;
        if (u < 0.0f) { u = 0.0f; t = -c2 / a; }
        else if (u > 1.0f) { u = 1.0f; t = (b - c2) / a; }
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    } else {
        u = eL > 1e-9f ? f / eL : 0.0f;
        if (u < 0.0f) u = 0.0f;
        if (u > 1.0f) u = 1.0f;
    }
    float q1[3] = { p1[0] + d1[0]*t, p1[1] + d1[1]*t, p1[2] + d1[2]*t };
    float q2[3] = { p2[0] + d2[0]*u, p2[1] + d2[1]*u, p2[2] + d2[2]*u };
    float dx = q1[0] - q2[0], dy = q1[1] - q2[1], dz = q1[2] - q2[2];
    return dx*dx + dy*dy + dz*dz <= PLAYER_HV_R * PLAYER_HV_R;
}

/* WORM attack — the decoded func_00154120 sub-machine (see the file
 * header). The brain consumes NO damage: the old shootable-worm
 * mailbox poll is removed (J2 CLOSED s66). */
static void worm_attack_tick(const EmCollision *coll, Enemy *e,
                             const float pp[3])
{
    float dx = pp[0] - e->pos[0];
    float dz = pp[2] - e->pos[2];

    switch (e->sub) {
    case 0:                       /* APPROACH (anim window; in place) */
        if (--e->t28 <= 0) {
            e->sub = 1;
            e->t28 = ENEMY_STALK_TICKS;       /* +0x28 = 0x78 decoded */
        }
        break;

    case 1: {                     /* STALK: homing + port locomotion */
        float want = atan2f(dx, dz);
        float diff = wrap_pi(want - e->yaw);
        float step = diff;
        if (step >  ENEMY_HOMING_RATE) step =  ENEMY_HOMING_RATE;
        if (step < -ENEMY_HOMING_RATE) step = -ENEMY_HOMING_RATE;
        e->yaw = wrap_pi(e->yaw + step);      /* 0.0698 rad/t decoded */
        /* the forward slide is a PORT locomotion stand-in (the
         * engine's stalk root motion is its anim's — unexported); it
         * STOPS at a standoff so the stalk cannot shove the worm into
         * trivial radius-6 contact — CONNECTING is the lunge
         * resolve's job (the engine's segment arm). Without the
         * standoff the overshooting slide parked the worm inside 6 u
         * and the first lunge tick burst latch-FREE off the still-
         * coiled windup pose. Flagged port constant. */
        if (dx * dx + dz * dz >
                ENEMY_STALK_STANDOFF * ENEMY_STALK_STANDOFF &&
            !enemy_probe(coll, e, e->yaw, ENEMY_HOP_SPEED + 0.5f)) {
            e->pos[0] += sinf(e->yaw) * ENEMY_HOP_SPEED;
            e->pos[2] += cosf(e->yaw) * ENEMY_HOP_SPEED;
            e->pos[1]  = floor_at(coll, e->pos, e->pos[1]);
        }
        if (--e->t28 <= 0) {
            e->sub = 2;
            e->t28 = ENEMY_WINDUP_F;
        }
        break;
    }

    case 2:                       /* WINDUP (anim window; in place) */
        if (--e->t28 <= 0) {
            /* decoded: snap the yaw to the player bearing + sound
             * 0x431, then lunge */
            if (fabsf(dx) + fabsf(dz) > 1e-4f)
                e->yaw = atan2f(dx, dz);
            em_sfx_play_at(ENEMY_SFX_WINDUP, e->pos, 300.0f);
            e->sub = 3;
            e->t28 = ENEMY_LUNGE_F;
        }
        break;

    case 3: {                     /* LUNGE: resolve window */
        /* travel at the clip's authored root speed (21.27 u/s) */
        float step = ENEMY_LUNGE_SPEED / 60.0f;
        if (!enemy_probe(coll, e, e->yaw, step + 0.5f)) {
            e->pos[0] += sinf(e->yaw) * step;
            e->pos[2] += cosf(e->yaw) * step;
            e->pos[1]  = floor_at(coll, e->pos, e->pos[1]);
        }
        /* CONNECT — the engine's two resolve arms in order (s66):
         * 1. the neck->head SEGMENT query (worm_latch_segment above)
         *    -> burst + the decoded lunge latch D_008104D4 = 15.0
         *    through the player mailbox bridge (0x4000 = the bridge's
         *    health route);
         * 2. else the radius-6 contact (func_0019A570) -> burst with
         *    NO latch (the engine writes no damage on this arm).
         * Rig-less fallback (worm_latch_segment -1): both arms fold
         * into the radius-6 contact CARRYING the 15 (flagged — the
         * pre-s66 port behavior, kept so asset-less runs still hurt). */
        int seg = worm_latch_segment(e, pp);
        float ddx = pp[0] - e->pos[0];
        float ddy = pp[1] - e->pos[1];
        float ddz = pp[2] - e->pos[2];
        int touch = ddx * ddx + ddy * ddy + ddz * ddz <=
                    ENEMY_CONTACT_R * ENEMY_CONTACT_R;
        if (seg == 1 || (seg < 0 && touch)) {
            s.player_hit  = 0x4000 | ENEMY_LATCH_LUNGE;
            e->hit_lethal = 0;
            e->state      = EM_ENEMY_DEATH;   /* burst (suicide path) */
            em_sfx_play_at(ENEMY_SFX_BURST, e->pos, 300.0f);
            break;
        }
        if (seg == 0 && touch) {
            /* the latch-free contact burst (engine arm 2) */
            e->hit_lethal = 0;
            e->state      = EM_ENEMY_DEATH;
            em_sfx_play_at(ENEMY_SFX_BURST, e->pos, 300.0f);
            break;
        }
        if (--e->t28 <= 0) {
            /* missed: the engine releases the actor (state 3) — no
             * burst, no gore, no corpse. The release teardown is the
             * worm's ONLY +0x36 access (func_001AFC10 `sh zero,
             * 0x36` — s66): clear any unconsumed write with it. */
            e->state   = EM_ENEMY_FREE;
            e->active  = 0;
            e->fade    = 0;
            e->mailbox = 0;
        }
        break;
    }
    }
}

static void enemy_tick(const EmCollision *coll, Enemy *e,
                       const float pp[3])
{
    float dx = pp[0] - e->pos[0];
    float dz = pp[2] - e->pos[2];

    switch (e->state) {
    case EM_ENEMY_INIT:
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            /* placed crawler: HP = 1, dormant (the engine also builds
             * the probe vectors and resolves the nest registry) */
            e->hp    = ENEMY_HP_CRATE;
            e->state = EM_ENEMY_IDLE;
        } else if (e->kind == EM_ENEMY_KIND_BUG) {
            /* bug INIT (s68): variant-A HP; yaw toward the player is
             * the PORT stand-in for the nest record's rot (flagged) */
            e->hp = BUG_HP_A;
            if (fabsf(dx) + fabsf(dz) > 1e-4f)
                e->yaw = atan2f(dx, dz);
            e->sub   = 0;
            e->state = EM_ENEMY_ATTACK;
        } else {
            /* worm init func_00154040: HP = 10, yaw toward the player,
             * BORN ATTACKING (the brain has no idle state) */
            e->hp = ENEMY_HP_WORM;
            if (fabsf(dx) + fabsf(dz) > 1e-4f)
                e->yaw = atan2f(dx, dz);
            e->sub   = 0;
            e->t28   = ENEMY_APPROACH_F;
            e->state = EM_ENEMY_ATTACK;
        }
        break;

    case EM_ENEMY_IDLE:
        /* crate only — worms never idle. Decoded state 4: the mailbox
         * is the ONLY direct trigger (no proximity test exists); a
         * kill broadcasts the group alarm to the placed-crawler kind;
         * the own alarm flag is the only other wake. */
        if (enemy_mailbox_poll(e, pp)) {
            enemy_alarm_broadcast();    /* decoded: list-wide, no radius */
            e->state = EM_ENEMY_DEATH;
            break;
        }
        if (e->alarm) {
            e->alarm     = 0;
            e->retreat   = ENEMY_STEER_TICKS;   /* +0x2A = 6 */
            e->atk_armed = 0;
            e->sub       = 0;
            e->state     = EM_ENEMY_ATTACK;
        }
        break;

    case EM_ENEMY_ATTACK:
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            /* decoded: state 1 never polls +0x36 (damage defers) */
            crate_attack_tick(coll, e);
        } else if (e->kind == EM_ENEMY_KIND_BUG) {
            /* the bug consumes the mailbox EVERY tick (decoded
             * func_00128B80): lethal -> death, nonlethal -> flinch
             * (the 0x1D window), else the minimal walk brain */
            int had = e->mailbox != 0;
            if (enemy_mailbox_poll(e, pp)) {
                e->state = EM_ENEMY_DEATH;
                break;
            }
            if (had) {
                e->sub = 1;
                e->t28 = (s.bclip_flinch >= 0)
                         ? (int)s.bug_model.clips[s.bclip_flinch]
                               .frame_count
                         : BUG_FLINCH_TICKS;
            }
            bug_attack_tick(coll, e, pp);
        } else {
            /* the worm consumes NOTHING (J2 CLOSED s66): no mailbox
             * poll — a write just sits until the slot frees, exactly
             * the engine's teardown-only `sh zero, 0x36` */
            worm_attack_tick(coll, e, pp);
        }
        break;

    case EM_ENEMY_DEATH:
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            crate_burst(e, pp);   /* husk gibs + the bugs (file header) */
            break;
        }
        if (e->kind == EM_ENEMY_KIND_BUG) {
            /* bug death (s68 + s76): the gameplay slot frees immediately
             * (alive/hit-tests off) while the corpse plays the real DEATH
             * clip 0x1B (func_00129FC0) and alpha-fades. The clip is now
             * exported (s76 anim-baker fix unblocked the non-sentinel
             * container) so the bug collapses for real, then holds the
             * last frame and fades; if it is ever missing, the frozen-
             * pose fade stands in (pre-s76 fallback). No gibs — the husk
             * burst set is the crate's; the bug's gore chain is undecoded. */
            e->state   = EM_ENEMY_FREE;
            e->active  = 0;
            e->mailbox = 0;
            if (s.bclip_death >= 0) {
                e->acur  = s.bclip_death;   /* collapse once, then hold */
                e->aprev = -1;
                e->at    = 0.0;
                e->arate = 1.0f;
                e->fade  = (int)s.bug_model.clips[s.bclip_death]
                               .frame_count + ENEMY_FADE_FRAMES;
            } else {
                e->fade  = ENEMY_FADE_FRAMES;   /* frozen-pose fallback */
            }
            e->tint[0] = e->tint[1] = e->tint[2] = e->tint[3] = 1.0f;
            break;
        }
        /* Engine sub-machine: nest-child spawns, gore sounds/FX pairs,
         * a gib-model rebind and a knockback corpse-slide. The GAMEPLAY
         * slot frees immediately (alive/state/hit-tests identical to
         * the pre-gib build). Visuals: a LETHAL HIT launches the gib
         * burst (the engine's damage-kill knockback arm — see "GIB
         * LAYER"); the contact/suicide burst (mailbox empty: the
         * engine's no-knockback arm) and a missing assets/gibs/ keep
         * the corpse placeholder — the frozen pose alpha-fades out
         * (white tint; the update loop walks the alpha down). */
        e->state   = EM_ENEMY_FREE;
        e->active  = 0;
        e->mailbox = 0;     /* the release teardown clear (s66)       */
        e->fade    = (e->hit_lethal && gib_burst(e) > 0)
                     ? 0 : ENEMY_FADE_FRAMES;
        e->tint[0] = e->tint[1] = e->tint[2] = e->tint[3] = 1.0f;
        break;

    default:
        break;
    }
}

void em_enemy_update(const EmCollision *coll, const float player_pos[3])
{
    if (!player_pos) return;
    /* EM_ENEMY_TEST=5 arm + synthetic walker (file header): the test
     * substitutes its own deterministic walker for the player position
     * THIS MODULE sees, so the run is self-contained — the real
     * player, camera and every other module are untouched. */
    const float *pp = player_pos;
    if (s.tt_on) {
        if (!s.tt_armed)
            tt_arm(player_pos);
        if (s.tt_pad >= 0 && s.tt_stage != 99) {
            tt_walker();
            pp = s.tt_w;
        }
    }
    /* EM_ENEMY_TEST=4 arm — first update tick (needs player_pos; see
     * the file header). Manifest generators are placed at scene-load
     * time by em_game.c's parser, not here. */
    if (s.gt_on && !s.gt_armed) {
        s.gt_armed = 1;
        /* kind-2 pad (25-u box) 14 u ahead of the idle player:
         * the player stands inside the box without moving; worms
         * emerge a safe steer-and-hop away. Mode is PINNED to 2
         * after the add's RNG table draw (deterministic anyway,
         * but the table also holds 0/1 outcomes). */
        float gp[3] = { player_pos[0], player_pos[1],
                        player_pos[2] + 14.0f };
        s.gt_gen = em_enemy_add_generator(em_frame_gfx(), gp,
                                          0.0f, 2, 2);
        if (s.gt_gen >= 0) {
            Gen *g = &s.gen[s.gt_gen];
            g->mode    = 2;
            g->sub     = 0;
            g->timer   = 0.0f;
            g->spawned = 0;
            printf("generator test: pad %d at (%.1f, %.1f, %.1f), "
                   "FORCED mode 2, delays /%g\n", s.gt_gen,
                   gp[0], gp[1], gp[2], s.gt_delay_div);
        } else {
            gt_check(0, "generator spawn at arm time");
        }
        s.gt_last_n = s.n;
    }
    /* EM_ENEMY_GIBDEMO debug hook: lethal damage-death on enemy 0 at
     * the requested tick (file header). Crates and bugs take the REAL
     * mailbox path; a worm consumes no mailbox (J2 s66), so the hook
     * forces its death DIRECTLY — a debug bypass with no engine
     * equivalent, kept only so gib captures stay scriptable. */
    if (s.demo >= 0 && s.frame == s.demo && s.n > 0 && s.e[0].active) {
        Enemy *de = &s.e[0];
        if (de->kind == EM_ENEMY_KIND_CRATE ||
            de->kind == EM_ENEMY_KIND_BUG) {
            /* both consume the mailbox — the REAL damage path (the
             * 0xFFF amount out-kills any bug HP variant) */
            em_enemy_damage(0, 0x4FFF);
        } else {
            float hx = de->pos[0] - pp[0], hz = de->pos[2] - pp[2];
            float hl = sqrtf(hx * hx + hz * hz);
            de->hit_dir[0] = hl > 1e-4f ? hx / hl : -sinf(de->yaw);
            de->hit_dir[1] = hl > 1e-4f ? hz / hl : -cosf(de->yaw);
            de->hit_lethal = 1;
            de->state      = EM_ENEMY_DEATH;
            em_sfx_play_at(EM_SFX_ENEMY_DEATH, de->pos, 300.0f);
        }
    }
    s.frame++;

    for (int i = 0; i < s.n; i++) {
        Enemy *e = &s.e[i];
        if (!e->active) {
            /* Corpse fade. Most kinds hold the frozen death-tick pose and
             * only walk the tint alpha 1 -> 0. A BUG corpse instead plays
             * its real DEATH clip (acur set to s.bclip_death at death,
             * s76): advance + rebuild the pose so the collapse animates at
             * full alpha, then it holds the last frame (bug_eval_time
             * clamps non-walk clips) and the tail fades. */
            if (e->fade > 0) {
                e->fade--;
                if (e->kind == EM_ENEMY_KIND_BUG && s.bug_anim_on &&
                    s.bclip_death >= 0 && e->acur == s.bclip_death) {
                    e->at += (double)e->arate;
                    enemy_build_palette(e);
                    e->tint[3] = e->fade >= ENEMY_FADE_FRAMES ? 1.0f
                               : (float)e->fade / (float)ENEMY_FADE_FRAMES;
                } else {
                    e->tint[3] = (float)e->fade / (float)ENEMY_FADE_FRAMES;
                }
            }
            continue;
        }
        float px = e->pos[0], pz = e->pos[2];
        enemy_tick(coll, e, pp);
        /* actual ground speed this tick — drives the attack-loop rate */
        float mx = e->pos[0] - px, mz = e->pos[2] - pz;
        e->speed = sqrtf(mx * mx + mz * mz) * 60.0f;
        if (e->active) {
            float dx = pp[0] - e->pos[0];
            float dz = pp[2] - e->pos[2];
            enemy_anim_update(e, sqrtf(dx * dx + dz * dz));
        }
        enemy_build_palette(e);   /* died this tick: freeze the pose
                                   * the corpse fade starts from */
    }
    gib_update(coll);
    /* GENERATOR pads (engine: hazard-list actors in the same pool
     * tick). Worms they spawn appear after this frame's enemy loop —
     * first updated next tick, the engine's own one-frame latency. */
    for (int i = 0; i < s.gen_n; i++)
        gen_tick(&s.gen[i], pp);
    /* TENDRIL FIELDS tick after their parent pads (the engine's pool
     * order: the pair is alloc'd after the generator). */
    for (int i = 0; i < s.tf_n; i++)
        if (s.tf[i].active)
            tf_tick(&s.tf[i], pp);
    if (s.gt_on)
        gen_test_script();
    if (s.tt_on && s.tt_pad >= 0 && s.tt_f[0] >= 0 && s.tt_f[1] >= 0 &&
        s.tt_stage != 99)
        tt_script();
}

/* ------------------------------------------------------------------ */
/* Damage + hitscan support                                             */
/* ------------------------------------------------------------------ */

void em_enemy_damage(int i, int16_t code)
{
    if (i < 0 || i >= s.n || !s.e[i].active) return;
    s.e[i].mailbox = code;
}

int em_enemy_player_hit_take(void)
{
    int code = s.player_hit;
    s.player_hit = 0;
    return code;
}

/* s76 LATCH / shake-off bridge (em_game's player_shake_tick): how many
 * bugs are currently clinging to the player (sub 5), and the shake-off
 * that throws them all back with a knockback + RECOVER cooldown. */
int em_enemy_latched_count(void)
{
    int n = 0;
    for (int i = 0; i < s.n; i++)
        if (s.e[i].active && s.e[i].kind == EM_ENEMY_KIND_BUG &&
            s.e[i].state == EM_ENEMY_ATTACK && s.e[i].sub == 5)
            n++;
    return n;
}

void em_enemy_shake_off(void)
{
    for (int i = 0; i < s.n; i++) {
        Enemy *e = &s.e[i];
        if (e->active && e->kind == EM_ENEMY_KIND_BUG &&
            e->state == EM_ENEMY_ATTACK && e->sub == 5) {
            e->pos[0] -= sinf(e->yaw) * BUG_SHAKE_PUSH;  /* yaw faces the
                                          * player, so -dir = away */
            e->pos[2] -= cosf(e->yaw) * BUG_SHAKE_PUSH;
            e->pos[1]  = e->pos[1] - BUG_CLING_Y;        /* back to the floor */
            e->sub     = 4;              /* RECOVER cooldown before re-approach */
            e->t28     = BUG_RECOVER_F;
        }
    }
}

/* THE VICTIM FILTER — the engine's MODEL-keyed switch (J2 CLOSED s66:
 * func_00183AC0 for bullets/melee and func_00183B80 for the
 * targetable gate BOTH reject model 0x0D by name; the worm's class
 * byte IS 2 — the model exclusion is doing the work, deliberately).
 * Port mapping: the CRATE (model 0x06 family, victim while +0x9F ==
 * 0 — `active` covers it) and the BUG (global models 0x0F/0x10 —
 * mailbox-shootable, s68) are victims; the WORM is rejected — rays
 * pass through, auto-aim never locks, melee whiffs. */
static int enemy_victim(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ||
           e->kind == EM_ENEMY_KIND_BUG;
}

/* Per-kind hit-sphere parameters (crawler values unchanged — tests 1/2
 * and the gib demo stay byte-identical). */
static float kind_aim_y(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ? CRATE_AIM_Y
         : e->kind == EM_ENEMY_KIND_BUG   ? BUG_AIM_Y : ENEMY_AIM_Y;
}

static float kind_hit_r(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ? CRATE_HIT_R
         : e->kind == EM_ENEMY_KIND_BUG   ? BUG_HIT_R : ENEMY_HIT_R;
}

int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3])
{
    int   best    = -1;
    float best_d  = max_dist;
    float fx = sinf(yaw), fz = cosf(yaw);

    for (int i = 0; i < s.n; i++) {
        const Enemy *e = &s.e[i];
        if (!e->active || !enemy_victim(e)) continue;  /* model filter */
        float dx = e->pos[0] - from[0];
        float dz = e->pos[2] - from[2];
        float d  = sqrtf(dx * dx + dz * dz);
        if (d > best_d || d < 1e-4f) continue;
        if ((dx * fx + dz * fz) / d < cone_cos) continue;
        best   = i;
        best_d = d;
    }
    if (best >= 0 && aim_out) {
        aim_out[0] = s.e[best].pos[0];
        aim_out[1] = s.e[best].pos[1] + kind_aim_y(&s.e[best]);
        aim_out[2] = s.e[best].pos[2];
    }
    return best;
}

/* func_00199220 candidate gate (em_enemy.h): live slot, VICTIM by
 * model, HP left. The engine chain is status != 0 -> func_00183B80
 * targetable -> HP +0x34 != 0; the port's `active` covers the first
 * (death frees the slot immediately) and enemy_victim IS the 183B80
 * model switch (worms excluded — J2 s66: the auto-aim lock never
 * fills on a worm). */
int em_enemy_targetable(int i)
{
    return i >= 0 && i < s.n && s.e[i].active &&
           enemy_victim(&s.e[i]) && s.e[i].hp > 0;
}

/* func_00183C40 class-keyed aim point (em_enemy.h): the hit-sphere
 * center — pos + the per-kind aim height, the exact center
 * em_enemy_ray_test intersects against. */
void em_enemy_aim_point(int i, float out[3])
{
    const Enemy *e = &s.e[i];
    out[0] = e->pos[0];
    out[1] = e->pos[1] + kind_aim_y(e);
    out[2] = e->pos[2];
}

/* Ray (from->to) vs a CRATE's box collision hull, in the crate's local
 * frame: AABB X/Z in [-CRATE_BOX_HXZ, +CRATE_BOX_HXZ], Y in [0,
 * CRATE_BOX_TOP], rotated by the crate yaw about its floor origin. The
 * engine hits the whole box hull (movable-object hull, not a sphere), so
 * a shot lands anywhere on the 14^3 box — at its visual centre, not only
 * the low band a sphere covered. Slab test; returns 1 + the entry
 * parameter t in [0,1] on a hit. */
static int crate_ray_box(const Enemy *e, const float from[3],
                         const float to[3], float *t_out)
{
    const float cs = cosf(-e->yaw), sn = sinf(-e->yaw);
    float px = from[0] - e->pos[0], pz = from[2] - e->pos[2];
    float qx = to[0]   - e->pos[0], qz = to[2]   - e->pos[2];
    float o[3] = { px * cs - pz * sn, from[1] - e->pos[1], px * sn + pz * cs };
    float q[3] = { qx * cs - qz * sn, to[1]   - e->pos[1], qx * sn + qz * cs };
    float d[3] = { q[0] - o[0], q[1] - o[1], q[2] - o[2] };
    const float lo[3] = { -CRATE_BOX_HXZ, 0.0f,          -CRATE_BOX_HXZ };
    const float hi[3] = {  CRATE_BOX_HXZ, CRATE_BOX_TOP,  CRATE_BOX_HXZ };
    float tmin = 0.0f, tmax = 1.0f;
    for (int a = 0; a < 3; a++) {
        if (fabsf(d[a]) < 1e-9f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return 0;   /* parallel + outside */
        } else {
            float inv = 1.0f / d[a];
            float t1 = (lo[a] - o[a]) * inv, t2 = (hi[a] - o[a]) * inv;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return 0;
        }
    }
    *t_out = tmin;
    return 1;
}

int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3])
{
    int   best   = -1;
    float best_t = 2.0f;
    float d[3]   = { to[0] - from[0], to[1] - from[1], to[2] - from[2] };
    float dd     = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    if (dd < 1e-9f) return -1;

    for (int i = 0; i < s.n; i++) {
        const Enemy *e = &s.e[i];
        if (!e->active || !enemy_victim(e)) continue;  /* model filter:
                                   * the bullet ray passes THROUGH a
                                   * worm (func_00183AC0 rejects model
                                   * 0x0D — J2 s66) and resolves the
                                   * world behind it instead */
        float t;
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            /* the engine's full box hull (s76) — a shot lands anywhere
             * on the 14^3 box, no aim-low */
            if (!crate_ray_box(e, from, to, &t))
                continue;
        } else {
            float r    = kind_hit_r(e);
            float c[3] = { e->pos[0], e->pos[1] + kind_aim_y(e), e->pos[2] };
            float m[3] = { from[0] - c[0], from[1] - c[1], from[2] - c[2] };
            float b    = m[0] * d[0] + m[1] * d[1] + m[2] * d[2];
            float cc   = m[0] * m[0] + m[1] * m[1] + m[2] * m[2]
                       - r * r;
            float disc = b * b - dd * cc;
            if (disc < 0.0f) continue;
            t = (-b - sqrtf(disc)) / dd;     /* entry point */
            if (cc <= 0.0f) t = 0.0f;        /* starts inside */
        }
        if (t < 0.0f || t > 1.0f || t >= best_t) continue;
        best   = i;
        best_t = t;
    }
    if (best >= 0 && hit_out) {
        hit_out[0] = from[0] + d[0] * best_t;
        hit_out[1] = from[1] + d[1] * best_t;
        hit_out[2] = from[2] + d[2] * best_t;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Draw + introspection accessors                                       */
/* ------------------------------------------------------------------ */

/* Draw-slot count: the real instances plus the gib layer's virtual
 * slots plus the generator pads, clamped to EM_ENEMY_MAX — em_game.c
 * reserves exactly that many render-chain entries, so pads past the
 * budget (e.g. the fully-crated office floor) simply don't draw until
 * slots free up (documented port limitation; gameplay unaffected —
 * the office set is link-0 inert anyway). */
int em_enemy_count(void)
{
    int n = s.n + s.gib_tail + s.gen_n + s.tf_n * TF_SPIKES;
    return n > EM_ENEMY_MAX ? EM_ENEMY_MAX : n;
}

int em_enemy_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count)
{
    if (i < 0) return 0;
    if (i >= s.n) {                        /* virtual gib/pad/spike slot */
        int k = i - s.n;
        if (k >= s.gib_tail) {
            int gi = k - s.gib_tail;
            if (gi >= s.gen_n) {           /* tendril spike */
                int ti = gi - s.gen_n;
                int f  = ti / TF_SPIKES;
                int sp = ti % TF_SPIKES;
                if (f >= s.tf_n || !s.tf_mesh) return 0;
                const Tendril *t = &s.tf[f];
                /* the engine draws only VALID records while the field
                 * machine is out of SCAN; a zero ramp is a zero-height
                 * spike — skipped instead of submitting degenerate
                 * geometry */
                if (!t->active || t->sub == 0 || !t->sp[sp].valid ||
                    t->sp[sp].ramp <= 0)
                    return 0;
                *mesh       = s.tf_mesh;
                *palette    = t->pal[sp];
                *bone_count = s.tf_bones;
                return 1;
            }
            if (!s.gen_mesh) return 0;     /* generator pad */
            *mesh       = s.gen_mesh;
            *palette    = s.gen[gi].palette;
            *bone_count = GEN_BONES;
            return 1;
        }
        if (!s.gib[k].active) return 0;
        const GibModel *gm = &s.gibm[s.gib[k].fam][s.gib[k].model];
        *mesh       = gm->mesh;
        *palette    = s.gib[k].palette;
        *bone_count = gm->bone_count;
        return 1;
    }
    if (!s.e[i].active && s.e[i].fade <= 0) return 0;  /* fade visual */
    if (s.e[i].kind == EM_ENEMY_KIND_CRATE) {
        if (!s.crate_mesh) return 0;
        *mesh       = s.crate_mesh;
        *palette    = s.e[i].palette;
        *bone_count = s.crate_bones;
        return 1;
    }
    if (s.e[i].kind == EM_ENEMY_KIND_BUG) {
        if (!s.bug_mesh) return 0;
        *mesh       = s.bug_mesh;
        *palette    = s.e[i].palette;
        *bone_count = s.bug_bones;
        return 1;
    }
    if (!s.mesh) return 0;
    *mesh       = s.mesh;
    *palette    = s.e[i].palette;
    *bone_count = s.bone_count;
    return 1;
}

/* Per-draw RGBA tint for slot `i` (same index mapping as em_enemy_draw;
 * pointer contract identical to the palette: recorded at chain-build
 * time, the VALUES read at flush are this frame's — em_enemy_update
 * runs in between). NULL = untinted (em_gfx_draw_skinned — live
 * crawlers, crates and generator pads keep the exact pre-tint draw).
 * Non-NULL consumers:
 *   - tendril spikes: room tint -> (6,92,1)/128 green by the parent
 *     pad's open phase, alpha = ramp/300 (the deploy fade);
 *   - gibs: white at alpha 1.0 (the renderer's opaque path —
 *     bit-identical to untinted) until the exit fade walks it to 0;
 *   - the no-gib corpse fade: white, alpha = fade/30. */
const float *em_enemy_draw_tint(int i)
{
    if (i < 0) return NULL;
    if (i >= s.n) {                        /* virtual gib/pad/spike slot */
        int k = i - s.n;
        if (k >= s.gib_tail) {
            int gi = k - s.gib_tail;
            if (gi >= s.gen_n) {           /* tendril spike */
                int f = (gi - s.gen_n) / TF_SPIKES;
                return f < s.tf_n ? s.tf[f].tint : NULL;
            }
            return NULL;                   /* generator pad: untinted */
        }
        return s.gib[k].tint;              /* gib rest/fade */
    }
    if (!s.e[i].active && s.e[i].fade > 0)
        return s.e[i].tint;                /* corpse fade-out */
    return NULL;                           /* live crawler/crate */
}

int em_enemy_alive(void)
{
    int n = 0;
    for (int i = 0; i < s.n; i++)
        if (s.e[i].active) n++;
    return n;
}

int em_enemy_state(int i)
{
    return (i >= 0 && i < s.n) ? s.e[i].state : -1;
}

int em_enemy_kind(int i)
{
    return (i >= 0 && i < s.n) ? s.e[i].kind : -1;
}

int em_enemy_hp(int i)
{
    return (i >= 0 && i < s.n) ? s.e[i].hp : 0;
}

void em_enemy_pos(int i, float out[3])
{
    if (i < 0 || i >= s.n) return;
    out[0] = s.e[i].pos[0];
    out[1] = s.e[i].pos[1];
    out[2] = s.e[i].pos[2];
}

int em_enemy_generator_count(void)
{
    return s.gen_n;
}

int em_enemy_generator_mode(int i)
{
    return (i >= 0 && i < s.gen_n) ? s.gen[i].mode : -1;
}

int em_enemy_generator_spawned(int i)
{
    return (i >= 0 && i < s.gen_n) ? s.gen[i].spawned : -1;
}

void em_enemy_shutdown(EmGfx *gfx)
{
    if (s.mesh) {
        em_gfx_mesh_destroy(gfx, s.mesh);
        if (s.has_model)
            em_model_free(&s.model);
    }
    if (s.crate_mesh) {
        em_gfx_mesh_destroy(gfx, s.crate_mesh);
        if (s.crate_has_model)
            em_model_free(&s.crate_model);
    }
    if (s.bug_mesh) {
        em_gfx_mesh_destroy(gfx, s.bug_mesh);
        if (s.bug_has_model)
            em_model_free(&s.bug_model);
    }
    for (int f = 0; f < GIB_FAMILY_N; f++)
        for (int i = 0; i < s.gibm_n[f]; i++) {
            em_gfx_mesh_destroy(gfx, s.gibm[f][i].mesh);
            em_model_free(&s.gibm[f][i].model);
        }
    if (s.gen_mesh)
        em_gfx_mesh_destroy(gfx, s.gen_mesh);
    if (s.tf_mesh) {
        em_gfx_mesh_destroy(gfx, s.tf_mesh);
        if (s.tf_has_model)
            em_model_free(&s.tf_model);
    }
    memset(&s, 0, sizeof s);
}
