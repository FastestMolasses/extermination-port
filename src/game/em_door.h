/* em_door.h — interactive doors: the port's first interactive objects.
 *
 * Native translation of the engine's class-5 double-door actor (decomp
 * repo Extermination/docs/FINDINGS.md "FIRST INTERACTIVE OBJECTS"):
 *
 *   func_001BC350  door behavior (per-frame state machine on the actor's
 *                  sub-state byte +0x05) -> em_door_update()
 *   func_00184BA0  player-side USE SCAN — run by its callers
 *                  (func_00160220/func_001612D0/func_0016DE40) ONLY on
 *                  the USE-button press edge. CONFIRMED 2026-07-31: all
 *                  three recovered callers gate the call on
 *                  `D_00810E74 & *(u16*)0x70003B76`. That D_00810E74 is
 *                  specifically the "cur & ~prev" EDGE mask, and that
 *                  the config mask defaults to 0x0040 = CROSS, are DATA
 *                  readings — neither is stated by any recovered
 *                  function. Doors have NO walk-into trigger: the
 *                  0x2D prefix in func_00183EF0 is
 *                  `if (player+0x1F0 == 0x2D) { if (kind != 7) return 0;
 *                  ... }`, i.e. it EXCLUDES class-5 outright.
 *                  Per-candidate test = the func_00183EF0 SELECTOR-0 /
 *                  KIND-5 branch (citation tightened 2026-07-31 to match
 *                  em_door.c's constants block, which had already been
 *                  corrected: func_00183EF0 switches on the candidate's
 *                  class SELECTOR byte +0x08, and its `case 5:` is an
 *                  unrelated test — planar dist^2 <= 196, |dy| <= 4,
 *                  accept, no facing window. The door test is inside
 *                  `case 0:` guarded by the KIND nibble
 *                  `*(u8*)(cand+2) & ~0xE0` being 5. Calling it "the
 *                  CLASS-5 branch" sends the reader to the wrong arm.
 *                  Re-read in full 2026-07-31; NEARMISS, so its logic is
 *                  authoritative): for model bytes 3 / 0x15 the distance
 *                  is measured from the DOORWAY CENTER
 *                    `(door.x - 5*cos(door_yaw), door.z + 5*sin(door_yaw))`
 *                  (the placement origin is the HINGE corner; the same
 *                  5-u lateral term the kickoff uses), then
 *                    dist  <= desc[0]   and   |dy| <= desc[1]
 *                  where desc = *(float**)(obj+0x30). The 10 / 8 values
 *                  are the DATA in D_002755F0, not code. Then the side
 *                  test (bearing-vs-door-yaw <= pi/2 picks which of
 *                  player_yaw / player_yaw+pi is compared), and finally
 *                  `|ang| <= 0.7853982f` = pi/4 of through-door. No LOS
 *                  and no auto-ring return-2 anywhere in the
 *                  selector-0/kind-5 path. ENUMERATION CORRECTED
 *                  2026-07-31 (audit): func_00183EF0 has THREE
 *                  func_0019A910 raycast sites, not the two this used to
 *                  list — the 0x2D/kind-7 prefix, the selector-0/kind-4
 *                  sub-0x2C case, AND the selector-3/4 branch (gated on
 *                  `(cand+2 & 0xF) == 7 || cand+0x10 == func_00219550`).
 *                  None of them is on the door path; `return 2` really
 *                  does occur only in the 0x2D/kind-7 prefix
 *                  -> the trigger scan inside em_door_update()
 *   func_001BC300  per-frame articulation + publish/draw (CONFIRMED
 *                  against the byte-matched func_001BC300 /
 *                  func_001C68C0, audit 2026-07-31): it runs
 *                  func_001C68C0 =
 *                    `build_trs_matrix(o+0xD0, o+0xB0, o+0xC0, o+0x60);
 *                     func_001C9940(o+0x110, *(u8*)(o+0xC), o+0xD0);`
 *                  — placement transform from the actor's
 *                  pos(+0xB0)/orientation(+0xC0)/SCALE(+0x60), then the
 *                  bone palette through func_001C9940 keyed on
 *                  the actor byte +0x0C (CORRECTED 2026-07-31: this
 *                  comment used to say "the model byte", which in this
 *                  header means +0x03 — the lock/family byte. That call
 *                  does NOT read +0x03. SCOPED the same date: calling
 *                  func_001C9940 "the skeleton evaluator" is an
 *                  inference — it is an undecompiled stub. What the
 *                  byte-matched func_001C68C0 settles is the call shape
 *                  and the +0x0C key, nothing more). func_001BC300 then
 *                  calls func_001B1B30(self, x, 10.0f + y, z) — the
 *                  actor's spatial anchor 10 u above the placement
 *                  origin — and its own +0x4C method
 *                  -> em_door_palette build + the draw accessors
 *
 * Door instances come from the SCENE MANIFEST's doors section (one line
 * per placed door, written by the decomp repo's export_props.py --doors /
 * export_level.py --office0-doors, destination-annotated by
 * export_level.py --door-goto):
 *
 *   door <file.emdl> <x> <y> <z> <yaw> <trigger_radius>
 *       [goto <scene-dir> <sx> <sy> <sz> <syaw>]
 *
 * The OPTIONAL goto tail is the per-area DOOR DESTINATION. The
 * dest-table SHAPE is CONFIRMED in the byte-matched func_001BC150:
 * `rec = D_0024E140[D_00810700] + (id & 0x7F) * 4`, then bit 7 of the
 * id short selects the inter-area branch (rec[0] area, rec[1] entry,
 * rec[2] ? rec[3] : 0xFF sub) vs the room-move branch, which reads
 * `rec[*(u16*)(self+0x2E)]` — the side latch INDEXES the record.
 * The arrival spawn record shape is confirmed in func_001B07C0
 * (NEARMISS 99.04%, so its body is authoritative but not byte-exact —
 * CORRECTED 2026-07-31, this used to say "byte-matched"): 0x30-byte
 * records at D_0024D650[area][slot] + D_00810702 * 0x30, pos at
 * +0/+4/+8, yaw at +0xC, walk-out flag at +0x14. Those are the record
 * SHAPES; the concrete area/entry NUMBERS the exporter writes into a
 * goto tail come from reading the D_0024E140 / D_0024D650 tables as
 * DATA — no recovered function states them. A goto door's
 * transition COMMIT switches the ACTIVE SCENE at full black instead of
 * the same-scene re-place: em_game consumes em_door_goto_pending() and
 * runs em_game_scene_switch() + the spawn placement while the screen is
 * black (the engine's B8==1 area-change shape, minus the audio fade;
 * BGM and the sfx registry persist). <scene-dir> with no '/' is the
 * SIBLING directory name of the current scene dir. 2026-06-11 (scoped
 * 2026-07-31: everything in this paragraph is the exporter's read of
 * the D_0024E140 / D_0024D650 tables as DATA — the record SHAPES are
 * decoded from func_001BC150 / func_001B07C0, the area and entry
 * NUMBERS below are not stated by any recovered function): the
 * shipped links are the real in-table inter-area destinations —
 * AREA01 sub 0 (the drawbridge room, chunk05.n0) is exported as
 * scene_drawbridge, so the office west door (door id 1|0x80 -> AREA01
 * entry 5), office0's m15 door (id 0|0x80 -> AREA01 entry 3) and the
 * drawbridge room's doors 1/3 back into the two office scenes all
 * carry their authentic dest records; the old SYNTHETIC west<->m15
 * wiring is gone. One flagged synthetic remains: the office VENT (the
 * engine's office0 access; its in-room mechanism is overlay-scripted,
 * not a placement object — the manifest carries a flagged vent door
 * line at the suite-corridor position whose goto lands on office0's
 * real spawn entry 5, beside the engine's own class-0x0B trigger
 * record at (43, 3.5, -147)). EM_DOOR_TEST ignores goto tails (it
 * asserts the same-scene re-place geometry).
 *
 * plus one OPTIONAL global sound line (generated by the decomp repo's
 * tools/gen_sfx_registry.py next to the sfx registry; em_door scans
 * scene.txt for it itself — em_game's parser skips unknown keywords):
 *
 *   doorsfx <front-id> <back-id>
 *
 * the D_0024DB80 sound pair the engine patches into the open script.
 * CONFIRMED 2026-07-31 in the byte-matched func_001BBD60, which is
 * exactly
 *   `a1[6] = *(u16*)(D_0024DB80 + ((*(short*)(door+0x56) & 0xFF00) >> 8)
 *                                 * 4 + *(u16*)(door+0x2E) * 2);`
 * i.e. row = link >> 8 at a 4-byte (2 x u16) stride, entry = the side
 * latch, patched into script record word 6. The concrete ids (office
 * links 0x02xx -> 0x3FD front / 0x3FE back) are DATA read out of
 * D_0024DB80, not something the function states. One global line is a
 * FLAGGED simplification — per-door
 * pairs need the manifest door lines to carry the placement LINK
 * halfword (export_props.py owns them). Without the line the legacy
 * placeholder OPEN id fires (em_sfx.h). No close id has been found:
 * func_001BBD20, the last candidate for one, is byte-matched and reads
 * the SAME D_0024DB80 row off the same +0x56 link (its caller picks the
 * entry) before `func_001FBD50(obj, id, a2, 300.0f)` — a positional cue
 * at radius 300, from the open table. TIGHTENED 2026-07-31: that makes
 * "there is no separate close sound" well supported, but nothing in the
 * recovered set shows func_001BBD20 being invoked at close time, so
 * "the door close is silent in the engine" stays an OBSERVATION rather
 * than a decoded fact. The port is silent on close either way.
 *
 * The EMDL is the door's own articulated model in DOOR-LOCAL space
 * (bone 0 = the door panel, bone 1 = the lock fixture; frame 0 = the
 * captured closed pose). Door files live under <scene>/doors/ so the
 * static scene loader (em_game.c scene_load, which slurps every .emdl in
 * the scene dir) never double-draws them.
 *
 * DOOR TRANSIT — the FULL captured sequence (FINDINGS.md "AREA
 * TRANSITION LIFECYCLE", s22 room-move timeline), natively:
 *
 *   1. use-arm (+0x0B = 4) -> kickoff (func_001BBE40, byte-matched;
 *      re-read 2026-07-31): its own gate is the BIT test
 *      `if (door[0xB] & 4)`, not an equality. It latches the player's
 *      side into +0x2E from |norm(bearing - door_yaw)| <= pi/2, patches
 *      the shared script records, snaps the player yaw through the
 *      door, MOVE-TOs the staging point, queues the script and pumps it
 *      (func_001BA1A0 + func_001BA1F0). PROVENANCE CORRECTED
 *      2026-07-31: func_001BBE40 does NOT itself set either lock — it
 *      writes no lock global at all. The MOVEMENT lock is already up
 *      before it runs (the use scan func_00184BA0 writes spad
 *      0x70003B8D = 3 on the frame it arms the door), and the rest of
 *      the scripted-mode state comes from the queued script's op07
 *      sub0. The port sets both locks at the kickoff because that is
 *      the frame the port's scan and arm coincide.
 *      Kickoff walks the player to the STAGING point = doorway CENTER
 *      (hinge + 5 u along the panel) + 5*n on his own side — both s22
 *      captures reproduce exactly ((104, -247.2) / (62, -225.5)). The
 *      engine SNAPs there in one frame; the port drives the same point
 *      through the scripted MOVE-TO walk (em_door_transit_active ->
 *      em_game player_move), per the walk-to of func_00182F90.
 *   2. OPEN phase (sub 3) — the door script D_0024DE40 (FINDINGS.md
 *      "DOOR SCRIPTS DECODED" s23), run on walk-to arrival: op07 sub0
 *      enters scripted mode, op0D sub5 fires a CHASE-CAMERA cue
 *      (-20.0, hold 0x78 — NOT ported: em_door does not own the camera,
 *      FLAGGED), then the player faces the door (the kickoff yaw snap)
 *      and plays anim 0x45 (front) / 0x43 (back) at rate 1.0 through
 *      em_game's scripted-anim mailbox (op 0x0A sub 0) while the door
 *      clip + sound run (op 0x0B sub 6; captured clip window 77..97
 *      vsyncs); the script waits 90 (front) / 70 (back) frames (op 0x02
 *      STOP) — the 0x42B40000 / 0x428C0000 literals func_001BBE40
 *      stores into the wait record — -> commit (sub 4,
 *      func_001BC240 -> func_001BC150). func_001BC150 is byte-matched
 *      and branches on the door id's bit 7: CLEAR = same-area room move
 *      -> func_001AEDE0(4, 0), fade only, B8 = 2; SET = inter-area ->
 *      func_001B0C00(4), fade + audio fades, B8 = 1. So room moves
 *      genuinely do NOT fade audio. (The "64-frame" ramp length for
 *      speed 4 is a capture, not something func_001AEDE0 states.
 *      TIGHTENED 2026-07-31 (audit): the byte-matched func_001AEDE0
 *      writes FOUR fields, not the two this used to list —
 *      D_0028A9A0 = 3, D_0028A9A6 = speed (4), D_0028A9A3 = 3,
 *      D_0028A9A2 = mode (0). Only D_0028A9A0 != 0 matters to the port:
 *      that is the word func_001AE7E0 gates the menu on.)
 *   3. while black: the player is RE-PLACED at the spawn point behind
 *      the door (door - 5*n on the far side, exit yaw); consumed by
 *      em_game through em_door_warp_pending(). CORRECTED 2026-07-31:
 *      this used to add "the spawn-table records flank their door at
 *      +-5", which the port's own cited numbers contradict — office
 *      recs 2/3 at (104, -245) / (104, -259) against centre
 *      (104, -252.2) are +7.2 / -6.8, i.e. ~+-7. The 5.0 is the
 *      KICKOFF staging literal out of func_001BBE40, a different
 *      quantity; the port reuses it to SYNTHESISE a far-side re-place
 *      for non-goto doors, so that stand-in lands ~2 u shy of a real
 *      record. Goto doors carry the real decoded record and are
 *      unaffected. The door SNAPS SHUT:
 *      func_001BC290 (byte-matched) sees the request byte B8 clear at
 *      the re-place and calls anim_clip_init(self, 0, 0, 0), resetting
 *      the clip to the captured closed pose in one frame — it never
 *      plays the clip backwards. A 64-frame fade-in starts. The
 *      re-place ALSO starts the ARRIVAL WALK-OUT (below).
 *   4. ARRIVAL WALK-OUT (the engine's player state 5/1, dispatcher
 *      func_0015B610 -> handler func_00183250). PROVENANCE, split
 *      honestly by the 2026-07-31 audit:
 *        CONFIRMED — func_001B07C0 (NEARMISS 99.04%, so its LOGIC is
 *          authoritative but not its scheduling — provenance fixed
 *          2026-07-31; this line still said "byte-matched", contradicting
 *          the dest-table paragraph above that had already corrected it)
 *          copies the spawn
 *          record's +0x14 byte to player+0x0E and, when its own arg is
 *          nonzero, a +0x0E of 1 writes player state/sub/phase =
 *          5 / 1 / 0. The whole phase machine below is read out of the
 *          byte-matched func_00183250 (re-derived instruction by
 *          instruction this audit — that file is an `asm`/.word body, so
 *          there is no readable C to skim; every constant below was
 *          taken out of its literal stream and every branch target
 *          recomputed). The state-5 sub-1 -> func_00183250 link is
 *          likewise re-read in the byte-matched func_0015B610, whose
 *          sub-byte ladder is `lbu a0,0x5(s0)` then beql against
 *          4/3/2/1/0 dispatching func_001834E0 / func_00183440 /
 *          func_001833F0 / func_00183250 / func_00183240 — sub 1 is
 *          func_00183250.
 *        NOT CONFIRMED — that the door re-place passes 1. That caller is
 *          the in-game frame machine func_001AE040, still undecompiled;
 *          the one recovered call site (src/anim_frame_top_a.c, the
 *          0x001ACA20 attract machine) passes 0, which arms nothing. So
 *          "every arrival walks out" is an OBSERVED port behaviour, not
 *          a source-derived fact.
 *      Its sub-machine (+0x06 phases):
 *        phase 0 (1 frame): request the locomotion clip at locIdx 2
 *                 (mode-1 anim table D_00248AB0[1][family*4+2]: family 0
 *                 unarmed -> id 2 = RUN; armed families -> the scripted
 *                 walks 0x4D/0x4E), ramp +0x38 = 0.3 u/tick
 *                 (D_00248870[2]), timer 0x32
 *        phase 1 (timer 0x32): clip plays, NO translation (the mover
 *                 func_00178B90 is not called) — hidden under the
 *                 fade-in for its first ~64 frames
 *        phase 2 (timer 0x1E): mover runs at 0.3 u/tick along the spawn
 *                 yaw
 *        phase 3 (timer 0x1E): mover runs, THEN the ramp decays
 *                 0.011362791/frame (0x3C3A2E8C exactly — CORRECTED
 *                 2026-07-31, the old 0.0113636 was a hand-rounded
 *                 1/88) to 0 on the 27th decrement, and the base idle
 *                 is requested (blend 12.0) on EVERY frame from the
 *                 crossing onward, not once — the player decelerates to
 *                 a stop ~13.1 u out from the spawn (9.0 u in phase 2
 *                 plus ~4.1 u here; the "~12.8" this line used to state
 *                 was already corrected in em_door.c's constant block
 *                 and is fixed here to match)
 *        exit: player state 1/0, phase 0, action +0x1F0 = 0, spad 3B8D
 *                 cleared.
 *      The walk-out is UNINTERRUPTIBLE: state 5 never reads the stick.
 *      TIMER SHAPE (corrected 2026-07-31, re-derived from the
 *      byte-matched func_00183250 — it is stored as `asm`/.word, so
 *      this was read out of the instruction stream, not from readable
 *      C): each phase does `lh v1,0x28(s0); addiu v0,v1,-1;
 *      bnez v1,<exit>; sh v0,0x28(s0)` — read, decrement and store
 *      every frame, advance the phase on the frame the READ value is 0,
 *      and do NOT call the mover (func_00178B90) on that hand-over
 *      frame. Each phase therefore costs its count PLUS one frame:
 *      51 + 31 + 31 = 113 frames of phases 1-3, 60 of them moving.
 *      PORT DEVIATION (flagged 2026-07-31): the engine ALSO spends one
 *      whole frame in phase 0 (its body just arms the clip/ramp/timer
 *      and returns), so the engine's true cost from the re-place is
 *      114 frames. em_door.c's walkout_start folds phase 0 into the
 *      re-place frame and enters at phase 1, so the port runs 113 —
 *      one frame short. Not worth a behaviour change on its own; do
 *      not "fix" 113 to 114 without also moving walkout_start.
 *
 * THE TWO LOCKS (decoded 2026-06-11 — they are SEPARATE systems):
 *
 *   MOVEMENT lock — the player actor's STATE machine. From the use-arm
 *   the script/transit owns the player: the use scan func_00184BA0
 *   writes spad 0x70003B8D = 3 on the frame it arms a target, and the
 *   frame dispatcher (src/anim_frame_top_a.c, state 4 sub 1) reads
 *   `if (*(u8*)0x70003B8D == 0) func_001AE5E0(); else func_001AE6B0();`
 *   — so ANY nonzero value routes the frame to the scripted variant.
 *   ("never runs free movement" is the port's reading of that split,
 *   not something func_001AE6B0's own body states.) On arrival state
 *   5/1 (the walk-out above) ignores the stick until its phases
 *   complete. Natively: em_door_movement_locked() — kickoff until the
 *   walk-out ends.
 *
 *   MENU lock — the frame poll func_001AE7E0 (the gate that returns 2
 *   = "open the status screen" on Triangle/Start; the 2-means-status
 *   reading is the port's, the function only returns the code). Its
 *   return-0 refusals, in the exact order the recovered function tests
 *   them (CONFIRMED 2026-07-31, NEARMISS 99.10%):
 *     - a transition request is pending (D_008106B8 != 0, then
 *       D_008106B9 != 0),
 *     - [interleaved non-refusals: D_008106CE -> 3; D_008106C5 or
 *       D_008106B0 -> 2],
 *     - the FADE machine is not idle (D_0028A9A0 != 0; func_001AEDE0
 *       sets it to 3 to arm the transit fade-out, so 3 = fade-out),
 *     - scripted mode is active (spad 0x70003B8D != 0),
 *     - [then D_00810E74 & 0x100 or D_00810E50 != 4 -> 1],
 *     - D_008106B3 != 0 (added 2026-07-31 — the old list omitted it).
 *   func_001AFCF0 CLEARS spad 3B8D (and memsets the whole 0x48-byte
 *   D_008106B0 block, which is where B8/B9 live). Its BODY is
 *   byte-matched and settles both of those writes. DOWNGRADED
 *   2026-07-31 (audit): "On ARRIVAL" is NOT settled — the only
 *   recovered call site is src/anim_frame_top_a.c's state-4 sub-0
 *   ENTER-PLAY fanout (alongside func_001B07C0(0), func_001AEE10(4,0)
 *   and the rest of the mode-enter chain), not a door arrival. Nothing
 *   recovered shows it running at the door re-place. The port assumes
 *   it does, so the only menu gate left is the fade-in:
 *   Triangle/Start works again the moment the fade-in completes
 *   (~frame 64 of the 113-frame walk-out — "about halfway through",
 *   while movement is still locked). Natively:
 *   em_door_menu_locked() — kickoff until the fade-in completes.
 *   em_hud gates its open toggle on it (the func_001AE7E0 stand-in).
 *
 * SLIDERS (the variant brain func_001BB860). CORRECTED 2026-07-31
 * (audit): this line used to read "sliding doors do NOT run the m03
 * transit", which the recovered brain flatly contradicts and which the
 * FIDELITY note further down already argued against. src/func_001BB860.c
 * sub-state 3 is `func_001BC150(self); self[5]++;` — the SAME transition
 * commit func_001BC240 makes for the hinged family. What sliders do NOT
 * run is the m03 SCRIPT: no player gesture anim, no 90/70 op02 wait, a
 * 6.0-u staging offset instead of 5.0, and a native panel slide
 * (func_001BB400) in place of the hinged clip. The transition itself is
 * identical.
 *
 * FAMILY CORRECTED 2026-07-31. This block used to be headed "m17/m09".
 * The model bytes (+0x03) the recovered slider code actually tests are
 *   func_001BB560: 0x08, 0x16          (side-latch inversion)
 *   func_001BB860: 0x16, 0x17, 0x3E    (lock gate)
 *   func_001BB400: 0x08, 0x16 single-leaf; 0x3D, 0x3E wide travel
 * i.e. the slider family is {0x08, 0x16, 0x17, 0x3D, 0x3E}, disjoint
 * from the hinged {0x03, 0x15} that func_00183EF0 and func_001BC350
 * test. 0x09 appears NOWHERE in the recovered door code — it survives
 * only as the port exporter's filename convention (FLAGGED). The old
 * `0x09 || 0x17` classifier sent m08/m16/m3D/m3E placements through the
 * HINGED brain; em_door.c's door_model_get was fixed for this on the
 * same date.
 *
 * The trigger is the same +0x0B bit-2 use-arm — the CROSS-edge use scan
 * above (the s56 "walk-into, no button" reading is OVERTURNED) — and the
 * trigger sub func_001BB560 then snaps the player yaw through the door,
 * latches the side into +0x2E from the bearing test and, for model bytes
 * 0x08 / 0x16 ONLY, INVERTS that latch (`latch = 1 - latch`; CONFIRMED
 * in the byte-matched func_001BB560, added to this header 2026-07-31 —
 * the inversion hits the latch alone, the yaw snap above it is written
 * from the UNINVERTED test). It then stages the player at
 * door_pos - 6.0 * (sin, cos)(snapped yaw) (func_00182F90 instant
 * translate; 6.0 — not the m03 5.0) and queues the OPEN script
 * D_0024D900: scripted-
 * mode enter (input lock, NO fade), chase-camera cue, ONE positional
 * door sound, the NATIVE SLIDE func_001BB400 (DECODED: the two panels
 * part symmetrically at 0.2 u/frame — single-leaf placements, flags2
 * 8/0x16, move one panel only — until the leading panel passes 9.0 u,
 * i.e. 46 frames; the wide flags2 0x3D/0x3E pair runs to 13.0 u = 66
 * frames. The EMDL's baked 46-frame clip is exactly the 9.0-u case),
 * then op01-sub8 = a
 * scripted player WALK-THROUGH (walk clip). The absence of a player
 * door-gesture anim in the slider script is an OBSERVATION, on two
 * legs, neither of them recovered C: the script data D_0024D900 carries
 * no op0A player-anim record (unlike the hinged D_0024DE40, whose 0x45/
 * 0x43 ids func_001BBE40 does patch in code), and the user's own PCSX2
 * run shows the panels parting with no gesture. Scoped 2026-07-31.
 * Lock-gated placements (flags2 0x16/0x17/0x3E vs the
 * D_00810841 unlock bits) run a LOCKED script (camera + VO, no motion)
 * — not in the port (no lock bitmask, flagged). See em_door.c "SLIDER
 * VARIANT BRAIN" for the full decode + port mapping (heading reference
 * fixed 2026-07-31 — the old "(m17/m09)" title no longer exists).
 *
 * FIDELITY NOTES (remaining port deviations, each flagged in em_door.c):
 *  - TRIGGER IS NOT A DEVIATION (s58): the engine arms ALL doors on the
 *    CROSS press edge (use scan callers gate on D_00810E74 &
 *    spad-0x70003B76 = 0x0040); the port's in->pressed CROSS gate is
 *    the engine behavior for hinged doors AND sliders. The earlier
 *    "walk-into via action-state 0x2D" contract (s17) is OVERTURNED —
 *    0x2D guards only the class-7 scan prefix and EXCLUDES doors.
 *    (The old 2-u auto ring and LOS pocket exemption are GONE —
 *    2026-06-11: the class-5 use-scan branch has neither.)
 *  - The engine SNAPs the player to the staging point; the port walks
 *    the same point through the scripted MOVE-TO.
 *  - The hinged/slider model flags ride the exporter's
 *    doors/door_mXX.emdl filename, not a manifest field.
 *  - Every transit is treated as the INTRA-AREA room move (request mode
 *    B8 == 2): same-scene re-place, no audio fade, actors/overlay kept.
 *    The inter-area path (B8 == 1: audio fade + overlay/asset/actor-pool
 *    reload) needs the native area loader — goto doors approximate it
 *    with the scene switch (em_door_goto_pending). A goto SLIDER reuses
 *    that same commit after its walk-through (the native slider
 *    commit/fade interleave is unread — flagged).
 *  - A goto-less SLIDER is a PURE PORT INVENTION (audit 2026-07-31): it
 *    ends its script with the player free on the far side, stays
 *    parted, and re-closes by reversing the clip once the player leaves
 *    the scan radius. func_001BB860 has no such state — its sub 2 goes
 *    straight to sub 3 = func_001BC150, so in the engine EVERY slider
 *    that opens commits a transition. Doing this properly needs slider
 *    dest records in the manifest.
 *
 * COLLISION: the engine gives placed objects collision through per-uid
 * AABB records in the area state blob (movable-hull set, mask bit 0) —
 * NOT through the static world, and the office EMCL bake confirms both
 * doorways are open in the static sets. em_door_probe() is the native
 * movable-hull stand-in: a segment-vs-AABB test against every door that
 * is not fully open, consumed by the player move probe (em_game.c).
 * (OBSERVED, not decoded: nothing in the recovered door code clears or
 * restores a collision word, so "a fully open door stops blocking" is a
 * port rule. It matters less than it used to now that the close is a
 * one-frame snap.) The
 * camera solver keeps the engine's own mask 6 (static only), so the
 * camera sees through doors exactly like the original.
 *
 * THE LOCKED SEQUENCE (engine subs 1/2 — ported 2026-06-11; FINDINGS
 * "DOOR SCRIPTS DECODED" s23 script D_0024DEC0 + the s45 clip-motion
 * verdicts + the s53/s56 locked-look camera):
 *
 *   GATE  func_001BC350 sub-state 0, model byte +0x03 == 0x15 ("security
 *         door"): `D_00810841[D_00810700] & (1 << *(short*)(self+0x34))`
 *         — bit SET opens normally (func_001BBE40 mode 0 -> sub 3), bit
 *         CLEAR runs the LOCKED TRY (mode 1 -> sub 1); any other model
 *         byte skips the gate entirely and always takes mode 0. The
 *         slider brain func_001BB860 runs the same gate for model bytes
 *         0x16 / 0x17 / 0x3E. D_00810841 is BSS: every
 *         lock-gated door starts LOCKED until a game event (door
 *         panel / keycard script) sets its bit. Natively the manifest
 *         door line carries the decoded gate as a `locked` token
 *         (export_level.py --door-locked) and em_door_unlock() is the
 *         bit-set event. Slider lock gates (flags2 0x16/0x17/0x3E,
 *         script D_0024DA40 = camera + VO only) have no exported
 *         placement; a `locked` slider runs the hinged refusal minus
 *         anim/clip/rattle (FLAGGED approximation, unexercised).
 *
 *   SCRIPT D_0024DEC0 (queued by func_001BBE40 mode 1, after the same
 *   side-latch/yaw-snap/staging walk as the open kickoff):
 *     op07 sub2   scripted-mode enter (both locks; it also runs
 *                 func_001AEB60(4). DOWNGRADED 2026-07-31 (audit):
 *                 calling that "the fade-arm variant ... a fade-IN arm"
 *                 overstates the recovered C. The byte-matched
 *                 func_001AEB60 is `if (D_0028A8D0 == 1) return;
 *                 D_0028A8D0 = 3; D_0028A8D2 = speed; D_0028A8D4 = 0;`
 *                 — a DIFFERENT global block from the transit fade
 *                 machine D_0028A9A0 that func_001AEDE0 drives, and
 *                 nothing recovered says which direction its mode-3
 *                 means. Not ported either way, so no behaviour rests
 *                 on it.)
 *     op09        func_001BBBF0 locked-look camera CUT. CONFIRMED
 *                 2026-07-31 (NEARMISS, logic authoritative):
 *                   target = (door.x - 8*cos(door_yaw),
 *                             door.y + 10,
 *                             door.z + 8*sin(door_yaw))
 *                   eye    = (target.x - 13*sin(D_00810374),
 *                             door.y + 12,      <- door.y, NOT target.y
 *                             target.z - 13*cos(D_00810374))
 *                 The +-8 lateral term is the same sign convention as
 *                 the doorway-centre 5-u term, i.e. the HANDLE side;
 *                 D_00810374 is a global yaw the port stands in for
 *                 with the kickoff snap yaw (em_game consumes
 *                 em_door_locked_look)
 *     op0A sub0   player anim 0x46 front / 0x44 back rate 1.0 — the ids
 *                 are DECODED (func_001BBE40 mode 1 stores them), but
 *                 the read of them as TRY-THE-HANDLE-AND-FAIL gestures
 *                 (200 f, limbs only, exact return to rest; peak node
 *                 deviation 5.4/6.6 u at f64-68) is a MEASUREMENT off
 *                 the clip bake, not anything the code states
 *     op0B sub0   DOOR clip engine id 3 front / 1 back — the lock-
 *                 fixture jiggle (s30: 200 f, panel still, fixture
 *                 rattle peaks ~f60-110, settles to rest), no sound
 *     op02        wait 60 frames
 *     op17 sub0   positional sound 0x3F2 — the LOCKED RATTLE (fires
 *                 exactly as the fixture motion peaks)
 *     op09        func_001BBAE0 locked "VO" — DOWNGRADED 2026-07-31.
 *                 What the BYTE-MATCHED func_001BBAE0 actually does:
 *                 it is a one-shot issue + ready-poll on the 4-word
 *                 request block at 0x002821B0. On its latch byte st[4]
 *                 == 0 it writes D_002821B0 = 2 (request kind),
 *                 D_002821B4 = 1 (busy), derives
 *                 `kind = *(char*)(obj+0x56) & 0x3F` and maps it
 *                 0->0x80000006 1->0x80000000 2->0x80000002
 *                 3->0x80000008 4->0x8000000A 5->0x80000004 into
 *                 D_002821B8, clears D_002821BC and latches st[4] = 1;
 *                 kind >= 6 returns 1 WITHOUT writing the selector,
 *                 WITHOUT clearing D_002821BC and WITHOUT latching
 *                 st[4] — but the kind/status words D_002821B0 = 2 and
 *                 D_002821B4 = 1 are stored ABOVE the switch and have
 *                 already landed, so the block is left armed-but-
 *                 unaddressed (CORRECTED 2026-07-31, audit: this line
 *                 used to say "returns 1 having issued NOTHING", which
 *                 the byte-matched src/func_001BBAE0.c contradicts, and
 *                 which already disagreed with em_door.c's own
 *                 DOOR_RADIO_LINE block). Then it polls, returning 1
 *                 once D_002821B4 reaches 2.
 *                 So "link bits 0-5 select a message" is supported (as
 *                 a 6-entry map, which mwcc lowered to jtbl_0026E1A0).
 *                 "TEXT-ONLY", "global message line" and "every shipped
 *                 line's voice-cue field is -1, so there is NO audio"
 *                 are NOT: none of that is in this function, and
 *                 whatever services D_002821B0 is not recovered. Treat
 *                 the silence as OBSERVED, not decoded.
 *                 The optional scene.txt `lockedvo <id-hex>` line (the
 *                 gen_sfx_registry.py decode emits it only if a real
 *                 cue ever resolves) plays through em_sfx here;
 *                 absent = silence (the radio text machine is not
 *                 ported — flagged)
 *     op0B sub1   wait door clip end (200 f — dominates the VO wait)
 *   then sub 1 queues D_0024DBC0 = op07 sub4 EXIT: restore camera +
 *   control (-> sub 2 -> re-arm CLOSED). The door never opens, no
 *   fade, no warp; the player is left standing at the staging point
 *   (the engine snapped him there; the port walked him).
 */
#ifndef EM_DOOR_H
#define EM_DOOR_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Instance-list capacity (the office has 2; the render chain in
 * em_game.c sizes its draw slots with this). */
#define EM_DOOR_MAX 8

/* Door sub-states — the ENGINE's values for the actor byte +0x05
 * (jtbl_0026E1C0). 1/2 = the LOCKED sequence (model 0x15 security
 * doors vs the unlock bitmask D_00810841 — ported 2026-06-11, see
 * "THE LOCKED SEQUENCE" in the header comment). */
enum {
    EM_DOOR_CLOSED     = 0,
    EM_DOOR_LOCKED_TRY = 1, /* locked-try script D_0024DEC0 running
                             * (engine sub 1: clip pump + script) */
    EM_DOOR_LOCKED_END = 2, /* finish script D_0024DBC0 ran (engine sub
                             * 2: restore + re-arm next frame) */
    EM_DOOR_OPENING    = 3,
    EM_DOOR_OPEN       = 4, /* one-frame transition COMMIT (func_001BC240
                             * -> func_001BC150) */
    EM_DOOR_CLOSING    = 5  /* transition pending (engine sub 5,
                             * func_001BC290): clip keeps advancing
                             * forward through the fade-out; at the
                             * re-place the request byte clears and the
                             * clip SNAPS to the closed pose + re-arms */
};

/* Reset the instance list (boot / scene reload). Does not free GPU
 * resources — pair with em_door_shutdown for that. */
void em_door_reset(void);

/* Add one door instance from a manifest line. `file` is relative to
 * `scene_dir` (e.g. "doors/door_m03.emdl"); the model is loaded and
 * uploaded once per distinct file. Returns 0 on success. */
int em_door_add(EmGfx *gfx, const char *scene_dir, const char *file,
                const float pos[3], float yaw, float radius);

/* Attach a manifest goto tail to door `i` (the last em_door_add):
 * `target` is the destination scene dir (sibling name or path), `spawn`/
 * `spawn_yaw` the decoded arrival spawn record. Returns 0 on success. */
int em_door_set_goto(int i, const char *target, const float spawn[3],
                     float spawn_yaw);

/* Mark door `i` LOCK-GATED (the manifest `locked` token — the decoded
 * model-0x15 / flags2 gate vs the BSS unlock bitmask D_00810841, which
 * is all-zero at boot: the door starts LOCKED). Returns 0 on success. */
int em_door_set_locked(int i);

/* The unlock event — the native D_00810841[area] |= 1 << door_id (the
 * engine's door-panel / keycard scripts write it). A locked door
 * em_door_unlock'ed opens normally on the next use-arm. */
void em_door_unlock(int i);

/* 1 while door `i` still refuses (lock-gated and not yet unlocked). */
int em_door_is_locked(int i);

/* LOCKED-LOOK camera feed (script D_0024DEC0 record 2 = op 0x09 ->
 * func_001BBBF0): returns 1 while a locked-try script holds the
 * locked-look placement (arrival .. finish), with the door's placement
 * pos/yaw for the handle-side math and the kickoff SNAP yaw (the
 * through-door axis latched by door_transit_kickoff — the s71
 * deterministic stand-in for the engine's script-camera yaw global
 * D_00810374, which the chase camera never writes). em_game.c's camera
 * consumes it (the cut on the rising edge, the op07-sub4 restore on
 * the falling one). out_snap_yaw may be NULL. */
int em_door_locked_look(float out_door_pos[3], float *out_door_yaw,
                        float *out_snap_yaw);

/* Locked-rattle play count (op 0x17 sub 0, sound 0x3F2) — test/debug
 * introspection (EM_LOCKED_TEST asserts exactly one per refusal). */
int em_door_rattles(void);

/* One-shot SCENE-SWITCH request — the goto-door analog of
 * em_door_warp_pending(): returns 1 exactly once, at fade-out completion
 * (screen fully black), with the target scene dir copied into `dir` and
 * the arrival spawn pose. em_game.c consumes it (em_game_scene_switch +
 * player/camera placement while black); the fade-in and the input
 * unlock keep running across the reload (the transit-wide lock survives
 * em_door_scene_clear). */
int em_door_goto_pending(char *dir, unsigned dir_size, float out_pos[3],
                         float *out_yaw);

/* 0x1AE040 state 4's re-place of a room move (S12b), called by the
 * 001B07C0(1) adapter after the player is placed: `walkout` is 001B07C0's
 * player state 5/1/0 (the spawn record's +0x14 byte == 1), run by the
 * legacy walk-out along `exit_yaw` (the record heading). A door sequence in
 * flight ends here: the script teardown (em_game_anim_cancel), the menu
 * unlock armed for the fade-in end, and the movement lock released unless
 * the walk-out holds it. */
void em_door_room_move_arrival(int walkout, float exit_yaw);

/* Test instrumentation (EM_ROOM_MOVE_TEST, S12b): put the first closed door
 * bound to an original room move into the state its open script ends in
 * (sub 4, side latch `side`, both locks held), so that its next tick runs
 * the real 001BC240/001BC150 commit. Returns the door index, or -1. */
int em_door_room_move_request_test(int side);

/* Scene switch teardown: free every door instance + model/mesh like
 * em_door_shutdown, but PRESERVE the transit-wide state (both locks,
 * the armed fade-in unlock, and the arrival walk-out — the switch
 * happens mid-transit, while black, and the walk-out runs in the NEW
 * scene exactly like the engine's player state surviving the area
 * load). The per-scene doorsfx pair is cleared (the new scene.txt is
 * scanned by its own first em_door_add). */
void em_door_scene_clear(EmGfx *gfx);

/* Per-frame update: trigger scan (the func_00184BA0 use scan — runs on
 * the CROSS press edge in the frame input block, against this player
 * position/facing) and every door's state machine + articulation
 * palette. `coll` is unused by the scan (class-5 doors do no LOS query
 * — decoded 2026-06-11; kept for signature stability, may be NULL).
 * Call once per gameplay frame, at the world-services slot
 * (func_001AFD70). */
void em_door_update(const EmCollision *coll, const float player_pos[3],
                    float player_yaw, const EmFrameInput *in);

/* Active door-transit MOVE-TO (func_001BBE40's player walk-to; the
 * engine's gameplay-frame selector 3 "door transit" variant). While it
 * returns 1, the player WALKS to `out_target` (the staging point) with
 * yaw snapped to `out_yaw` through the normal locomotion path,
 * collision-free — this is how the engine carries the player across the
 * statically sealed doorway boundary planes (the engine snaps; the port
 * walks the same scripted move). Consumed by em_game.c player_move. */
int em_door_transit_active(float out_target[3], float *out_yaw);

/* THE TWO TRANSIT LOCKS (the decoded split — see "THE TWO LOCKS" in the
 * header comment; the old single em_door_input_locked() is GONE):
 *
 * MOVEMENT lock — nonzero from the transit kickoff until the arrival
 * WALK-OUT completes (player state 5/1, func_00183250 phases): player
 * movement + actions are ignored (player_move and the weapon machine
 * read neutral input) and the camera auto-orient is suppressed.
 * em_game.c consumes it every frame; tests query it directly. */
int em_door_movement_locked(void);

/* MENU lock — nonzero from the transit kickoff until the FADE-IN
 * completes (the func_001AE7E0 gate: pending request / fade machine
 * D_0028A9A0 != 0 / scripted spad 3B8D; on arrival 3B8D is already
 * cleared at the re-place, so the fade-in is the last gate). em_hud
 * gates the Triangle/Start status-screen toggle on it — the menu
 * opens mid-walk-out, exactly like the engine. */
int em_door_menu_locked(void);

/* ARRIVAL WALK-OUT drive (player state 5/1, func_00183250): returns 1
 * while the walk-out runs, with the walk direction (the spawn record's
 * exit yaw) and THIS frame's commanded translation speed in units/sec
 * (phase 1: 0 — the clip plays in place; phase 2: 18 u/s = the engine's
 * 0.3 u/tick locIdx-2 speed; phase 3: the decaying ramp). em_game.c
 * player_move consumes it INSTEAD of stick input (uninterruptible),
 * driving the locomotion clip from the speed. Phases advance in
 * em_door_update; the state survives a goto scene switch
 * (em_door_scene_clear), exactly like the engine's player state. */
int em_door_walkout_active(float *out_yaw, float *out_speed);

/* One-shot RE-PLACE request: returns 1 exactly once, at fade-out
 * completion (screen fully black), with the spawn point behind the door
 * (door -+ 5*normal, far side) and the exit yaw — the documented
 * spawn-table re-place. em_game.c consumes it (sets the player position
 * + yaw and re-seats the camera while black). */
int em_door_warp_pending(float out_pos[3], float *out_yaw);

/* Draw accessors for the render chain. */
int  em_door_count(void);
void em_door_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);

/* Movable-hull segment probe (collision-set bit 0 stand-in): nearest
 * blocking-door AABB hit on [from, to], or 0. A fully OPEN door does not
 * block (the engine clears the object's collision state word). */
int em_door_probe(const float from[3], const float to[3], EmCollHit *hit);

/* Introspection (debug / self-tests). */
int  em_door_state(int i);
void em_door_pos(int i, float out[3]);

/* Destroy GPU meshes + free models. */
void em_door_shutdown(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif /* EM_DOOR_H */
