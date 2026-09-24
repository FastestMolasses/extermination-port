/* em_enemy.c — enemy actors (see em_enemy.h for the engine mapping and
 * the flagged fidelity deviations).
 *
 * THREE BRAINS share this module's slot pool (s62 condition decode of
 * the splat disassembly + the s68 creature-identity correction —
 * every trigger below is read off the instructions, not inferred.
 * DOWNGRADED 2026-07-31: the BUG brain is NOT "decoded structurally".
 * Only its damage chain (func_00128B80 -> func_00129FC0) and its init
 * HP (func_00128390) come from recovered C; the approach/bite/latch
 * shape this file runs is a PORT construction, see its block):
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
 *             Else the disguise jitter — decoded as a rattle (sound
 *             0x19C @300) + a random 0..255-tick wait + a 4-frame
 *             shudder, and gated on the crate still owing its nest group
 *             a child (see CRATE_JIT_* below). THE ENGINE HAS NO
 *             PROXIMITY TEST HERE — state 4 never reads the player
 *             position; the old ~10-u burst trigger and the 32-u wake
 *             were port inventions and are REMOVED.
 *   1 ATTACK  the suicide hop-run, and it is BLIND: no player reference
 *             anywhere in the engine's state 1. sub 0 STEER: +0x2A--;
 *             probe the 4 diagonals; >= 3 blocked (or both opposite
 *             pairs) -> hold, and at +0x2A == 0 -> back to 4 (the
 *             pending mailbox then kills it on the next IDLE tick —
 *             decoded deferral). Else rotate the heading +-0.0524 rad
 *             away from a blocked side, or RNG-perturb it when open
 *             (three euler deltas: +-1/120 rad on X and Z, +-1/100 on
 *             Y); first launch arms the attack timer +0x2A — 0xB4 = 180
 *             for every variant EXCEPT model byte 6, which instead gets
 *             60 * its world Y / 12 (the port's default crate IS byte 6,
 *             so this branch is live here — the old "n/a" was wrong).
 *             sub 1 HOP: a 0x1E = 30-frame arc (Y only), then on the
 *             LAST arc frame the horizontal velocity is built as
 *             11.0 * the chosen euler delta (+0x2CC = 11 * -euler.x,
 *             +0x2C4 = 11 * euler.z, and both eulers are then divided
 *             by 1.4), and the ballistic phase integrates under
 *             0.052/tick gravity. CORRECTED 2026-07-31 — the arc is
 *             CONDITIONAL: sub 0 seeds +0x28 = 0x1E, but the
 *             nothing-blocked branch (the RNG-perturb one) overwrites
 *             it with 0, so a crawler that launches with all four
 *             corners clear skips the arc, jumps straight to the
 *             ballistic leg AND never reaches the 11.0 velocity build —
 *             it carries whatever +0x2CC/+0x2C4 already held. Only a
 *             steer-away launch (1 or 2 corners blocked) gets the arc
 *             and the 11.0 speed. Probe returns 4 (STATIC WORLD hit) or the timer
 *             runs out -> CLEAR +0x36 (damage taken mid-run is
 *             absorbed) -> 2. STATE 1 NEVER POLLS THE MAILBOX — the
 *             old IDLE+ATTACK poll widening is removed. (Port
 *             locomotion stand-in: repeated hops with re-steer on
 *             landing; the engine runs one long leap.)
 *   2 DEATH   engine sub 0: NEST-CHILD spawns (the s68 registry: BUGS
 *             or items — see "CRATE KIND" below), the variant's burst
 *             sound (0x19D / 0x19E) and its two gore FX, a 6-tick
 *             recover timer, and — ONLY when killed by DAMAGE (+0x36
 *             nonzero) and only for variants 6/0x1E — a MODEL REBIND to
 *             the burst-husk models (library 0x22 / 0x29) plus a random
 *             QUARTER-TURN of the corpse: an identity matrix in the
 *             scratch D_700036E0 rotated by an RNG draw of 0..3 * 90
 *             deg, multiplied into the actor transform with the render
 *             position preserved. No velocity is written and no corpse
 *             slide runs — the old "knockback along the RNG-rotated hit
 *             vector" reading is retracted. Every other variant (and any
 *             timer/suicide burst) drops straight to state 3. Sub 1 then
 *             re-probes the four corners with mask 6 and frees once
 *             fewer than three are blocked — but that whole loop is
 *             gated on +0x52 != 0, which is 0 for a placed crate, so the
 *             rebound husk of a shot crate simply persists.
 *             Port: gameplay despawns immediately; on a LETHAL HIT the
 *             visual layer launches 3-5 GIB instances (the exported
 *             library burst set, assets/gibs/ — see "GIB LAYER"
 *             below), a PORT scatter that borrows the engine's
 *             quarter-turn draw as a bearing; the contact/suicide burst
 *             (mailbox empty, engine skips the rebind entirely) and the
 *             missing-assets case keep the
 *             corpse placeholder: the frozen pose ALPHA-FADES out in
 *             place (white tint, 1 -> 0 over ENEMY_FADE_FRAMES
 *             through em_gfx_draw_skinned_tinted — the engine fades
 *             dead actors by walking the actor alpha down before
 *             freeing).
 *   3 FREE    slot inactive.
 *
 * THE BUG (s68 "CREATURE IDENTITY CORRECTION" — the port's
 * EM_ENEMY_KIND_BUG; full ledger in em_enemy.h "BUG KIND"). Decoded,
 * kept: HP 15 (variant A — func_00128390, BYTE-MATCHED: it returns
 * 15/30 while D_0081070A == 0 and 30/50 otherwise, selected by its
 * second argument), the EVERY-TICK +0x36 mailbox consumption, and the
 * clip set. CORRECTED 2026-07-31 — the mailbox path is a TWO-STAGE
 * chain, and neither stage is what the old note said:
 *   func_00128B80 (recovered) is only the ROUTER. `if (+0x36 == 0 &&
 *   D_0081080F == 0) return 0;` else it forces actor[0]=3, actor[4]=2
 *   (the reaction STATE) and clears actor[5]/[6]/[7], calls
 *   func_0012E070(ctrl), and returns 1. It never compares HP and never
 *   distinguishes flinch from death. (D_0081080F is a global instakill
 *   flag: when set it copies +0x34 into +0x36 so the next stage always
 *   runs the actor out of HP.)
 *   func_00129FC0 (BYTE-MATCHED) is the reaction driver, and IT does
 *   the arithmetic: case 0 subtracts the LOW BYTE of +0x36 from +0x34,
 *   picks the knockdown clip 0x1B or the flinch clip 0x1D by the damage
 *   word's 0x2000 bit / the controller state, and fires the death event
 *   0x8000000C when HP hits 0; the corpse's own collapse clip is 0x20
 *   (case 3, with sound 0x1B7) before the free in case 4.
 * Clips therefore: init/walk 1 (the 90-f in-place WALK), flinch 0x1D,
 * KNOCKDOWN 0x1B (untranslated), COLLAPSE 0x20 (BUG_CLIP_DEATH).
 * The brain SHAPE below is a PORT construction. CORRECTED 2026-07-31 —
 * the old note said "func_00128C10 / func_0012A5D0 are still stubs".
 * THAT IS FALSE: both are now recovered (NEARMISS 97.97% / 99.72%) and
 * neither runs anything like the port's approach-standoff-bite cycle:
 *   func_00128C10 is the NPC per-frame brain (state e[4], sub e[5],
 *     creature kind e[0xD] 0..9, kinds 4/9 remapped to 3/8). Its live
 *     state 1 is a SENSE + WANDER machine, not a pursuit: sub 0 stands
 *     on clip 1 and wakes when func_001B13F0(player, self, 100.0f)
 *     reports the player inside 100 u (kind < 4; kinds >= 4 use 20/40 u
 *     by the b+0xE4>>8 near flag); sub 1 holds the alert clip while the
 *     player stays inside 150 u (90 idle ticks outside it fall back to
 *     sub 0) and commits at 24 u (10 u near) by picking a RANDOM heading,
 *     not the player bearing; sub 2 turns to that heading at
 *     0.34906587 rad/tick and hands to sub 3 with walk speed 0.8 and anim
 *     rate 2.6; sub 3 walks on clip 6 turning at 0.06981317 rad/tick for a
 *     table-drawn count, then turns +-0.69813174 rad and repeats; sub 8 is
 *     the knockdown delegate. It reads the mailbox through func_00128B80
 *     every tick, which is the one piece the port does reproduce.
 *   func_0012A5D0 is a different actor family's main tick (its own
 *     14-way movement table, of which func_0012C490 — the clip
 *     0x13/0x14/0x15 LEAP chain with body hops and sfx 0x1AE — is one
 *     entry). Nothing ties it to the crate-hatched bug.
 * WHICH of those (if either) is the crate hatchling is NOT settled: the
 * bug is identified by asset slot 0x0F/0x10, and nothing recovered pins
 * the hatchling's actor +0x03 / +0x0D bytes. So the numbers above are
 * recorded for a later pass and deliberately NOT wired in here — the
 * port keeps its own shape rather than guess an identity:
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
 *   sub 3     BITE — PORT SHAPE, corrected 2026-07-31. Clip 0x13 IS in
 *             func_0012C490, but nothing else in the old note survives
 *             the decompilation: that function is a 9-state machine on
 *             actor[6] whose state 2 plays clip 0x13 together with a
 *             BODY HOP func_00128830(actor, 0, 0, -2.5) and sfx 0x1AE,
 *             then chains clips 0x14 (hop 0,2.5,0) and 0x15 (hop
 *             0,1.5,3) into a spin phase — it is a LEAP sequence, not an
 *             in-place bite, and it contains NO contact test at all.
 *             func_001B5360 is the shared GROUND-SNAP probe (see the
 *             CORRECTION block at BUG_CONTACT_FWD), not a melee
 *             resolver, so BUG_CONTACT_R / BUG_CONTACT_FWD are PORT
 *             constants — the old "(=6, VERIFIED)" / "(=10, VERIFIED)"
 *             tags were wrong. The port keeps its in-place strike; the
 *             engine's real leap chain is UNTRANSLATED. A HIT -> LATCH
 *             (sub 5) is an OBSERVED behaviour (2026-06-12 play), not a
 *             decoded one.
 *   sub 4     RECOVER + cooldown, BUG_RECOVER_F -> sub 0. DOWNGRADED
 *             2026-07-31: the old citation "(func_0012DD70)" cannot
 *             support anything — func_0012DD70 ships in the decomp as a
 *             hand-written asm-void body with no recovered C and no
 *             semantics note, so nothing is known about what it does.
 *             This sub is a PORT construction.
 *   sub 5     LATCHED (LIVE s76): clings on the player at a per-bug
 *             bearing while em_game's player_struggle_tick drains health
 *             + rises infection; when the player WINS the CROSS-mash
 *             struggle, em_enemy_shake_off throws it off AND KILLS it
 *             (-> EM_ENEMY_DEATH). DOWNGRADED 2026-07-31: the old
 *             "the engine's func_001EFE00 throw broadcast" citation does
 *             not hold — func_001EFE00 has no recovered C either (asm-
 *             void), and the call sites we CAN read use it as a generic
 *             id-keyed event fire/query. The whole shake-off arm is a
 *             PORT construction on a play observation; see the longer
 *             note at em_enemy_shake_off. Cling geometry is a flagged
 *             PORT stand-in.
 *   DEATH     (state) gameplay slot frees immediately; the corpse plays
 *             the real DEATH clip 0x1B (s76) then holds + alpha-fades (no
 *             gibs — the husk set is the crate's; the bug's own gore
 *             chain is undecoded). Death sound CORRECTED 2026-07-31:
 *             func_00129FC0's audio IS decoded — 0x1B1 on every reaction
 *             entry (case 0), 0x1B7 with the collapse clip (case 3),
 *             0x1B5 at the free (case 4). The port now plays 0x1B1 on
 *             any hit and 0x1B7 on death; the shared 0x7D8 stand-in is
 *             gone. See the BUG_SFX_* block.
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
 *             engine also runs the LATCH QUERY here:
 *             func_0019AA80(a, b, 0x20) stages two points in spad
 *             0x70003190/+0x10 and hands `0x20 & 0xFFFF` to
 *             func_001A7280 — so 0x20 is a FILTER-MASK channel, not a
 *             32-u radius (CONFIRMED off the recovered func_0019AA80).
 *             DOWNGRADED 2026-07-31 — the ENDPOINTS: func_00154120
 *             reads both out of the GLOBAL D_00275B40, as
 *             `*(char **)(D_00275B40 + 0x34) + 0xC0` and
 *             `*(char **)(D_00275B40 + 0x40) + 0xC0`, NOT out of the
 *             worm actor it was handed. "The worm's own neck->head rig
 *             nodes 13/16 swept against the player's +0x58 sphere
 *             list" is the s66 LIVE reading and stays OBSERVED, not
 *             source-derived. What IS in the recovered C: on a hit,
 *             and only while D_008102B0[0] == 1, it sets
 *             D_008102BF[0] = 2, D_008104D4 = 5.0, D_008102B0[0] |= 2
 *             and stages the attach vector into D_00810320 —
 *             UNTRANSLATED (no latch/shake-off system in the port).
 *   sub 1     STALK: +0x28 = 120 ticks (0x78), homing the yaw toward
 *             the player at 0.0698 rad/tick (0x3D8EFA35,
 *             func_001B12B0). Port locomotion stand-in: slides forward
 *             while homing (the engine's stalk root motion, if any, is
 *             the anim's — unexported).
 *   sub 2     WINDUP: anim out (the 45-f windup clip window); at the
 *             end SNAP the yaw to the player bearing and play sound
 *             0x431. CONFIRMED 2026-07-31 off func_00154120 case 2:
 *             on the anim-done bit it plays clip 3, bumps the sub,
 *             writes +0xC4 = func_001B1240(pos, D_00810350[0],
 *             D_00810358[0]) and calls
 *             func_001FBD50(a, 0x431, 0, 300.0f).
 *   sub 3     LUNGE: travel at the lunge clip's authored 21.27 u/s
 *             along the snapped yaw for the 120-f clip window. Each
 *             tick the engine resolves: the neck->head SEGMENT query
 *             (the same func_0019AA80 shape as sub 0) -> burst,
 *             latching D_008104D4 = 15.0 (the lunge hurts more — again
 *             only while D_008102B0[0] == 1); else the second arm
 *             func_0019A570(from, to, 6, 0) -> burst (NO latch, no
 *             damage written); else clip end -> state 3 DESPAWN
 *             (released — no burst, no gore). NOTE the "6" there is
 *             func_0019A570's channel MASK (bit1 area hulls + class-4
 *             actors, bit2 static world), NOT a radius — see the
 *             CORRECTION at ENEMY_CONTACT_R. Port: the segment arm
 *             runs against a PLAYER CAPSULE stand-in for the
 *             unexported hit-volume list (worm_latch_segment below,
 *             staged from the worm's own palette nodes 13/16 — a
 *             flagged PORT choice, see the DOWNGRADE at
 *             ENEMY_LATCH_NODE_A), then the port's own 6-unit contact
 *             sphere follows latch-free — engine order. Without rig
 *             data both arms fold into that sphere carrying the 15
 *             (flagged fallback). Miss -> despawn.
 *   2 DEATH   burst: engine sound 0x434 + gore 0x80000052 + release.
 *             The worm is NOT SHOOTABLE — J2 CLOSED s66: both victim
 *             filters (func_00183AC0 / func_00183B80) reject model
 *             0x0D by name, and a live full-lifecycle memcheck saw
 *             ZERO +0x34/+0x36 accesses besides the release
 *             teardown's +0x36 = 0 store (func_001AFC10), with two
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
 * models — and gives it a random quarter-turn (0/90/180/270 deg). It
 * does NOT knock the corpse anywhere: the decompiled state-2 arm writes
 * no velocity, so the scatter below is a PORT visual that reuses the
 * quarter-turn draw as a launch bearing.
 * The PICK is decoded (func_001551B0 state 2 sub 0): model byte 6 ->
 * husk A 0x22 (brown — the wooden crate of every exported scene), else
 * husk B 0x29 (grey-cyan). CORRECTED 2026-07-31 — the ENTRY condition,
 * which the old note left out: the rebind arm runs only when
 *     +0x36 != 0  &&  (model byte == 6 || model byte == 0x1E)
 * so husk B is reachable by variant 0x1E ALONE. Variants 0x1C / 0x1F /
 * 0x50 take the `state = 3` branch and never rebind a corpse at all,
 * and no variant rebinds on a timer/suicide burst (empty mailbox). The
 * port launches its gib scatter for every variant — a flagged PORT
 * visual, not the engine's rebind.
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
 * header), re-verified against the recovered C 2026-07-31:
 *
 *   0 INIT    HP = 1 -> 4.
 *   4 IDLE    render the crate mesh with the PROCEDURAL jitter (the
 *             D_002468B0/B4/B8 x/z + yaw world-matrix perturbation). The
 *             CYCLE is decoded (CONFIRMED 2026-07-31 against
 *             func_001551B0 state 4): rattle sound 0x19C, a random 0..255-tick
 *             wait, then a 4-frame shudder on the odd counter values
 *             below 9 and a clean restore at 0 — and the whole thing is
 *             gated on the crate still owing its nest group a child, so
 *             a gore-only crate never moves. Amplitudes stay flagged
 *             port constants; the engine runs the jitter in state 4
 *             ONLY. (The old "deterministic sines" envelope was a port
 *             invention and is REPLACED.)
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
 * THE AREA-11 DRUMS (00156620, model byte 0x18): retired from this module
 * in census L25. They run on their original owner (em_drum_original over
 * the roster nodes, em_area11_boxes.c), and the scene manifest's
 * `enemy egg` lines no longer place a copy; no other scene places one.
 * egg_explode (below) stays: the door-husk partner's burst uses it.
 *
 * GENERATOR KIND (em_enemy.h "GENERATOR"; FINDINGS "GENERATOR —
 * func_0015A2C0 RESOLVED", session 28): the engine's organic floor pad
 * that births worms while the player stands on it. Faithful pieces
 * (all CONFIRMED 2026-07-31 against the recovered func_0015A2C0, except
 * the table CONTENTS, which are disc .data — the function shows only
 * their shape) — the config footprints, count tables, 121-frame charge,
 * the 1800/3600/5400-frame delays, the 4-worm cap, worm-at-origin spawning
 * (the leech yaw-to-player applied at spawn, exactly like the crate
 * burst), per-tick trigger = player inside the config box (Y tolerance
 * +1.5), indestructibility (no mailbox, no hit sphere, excluded from
 * acquire/ray_test/alive), and the mode-1 breather's immediate
 * kind-0xE TENDRIL-FIELD pair (see "TENDRIL FIELD" below). Flagged
 * port stand-ins — the LCG replaces
 * the engine frame RNG for the mode draw and delay pick; the open-trap
 * player hit is a
 * one-shot mailbox 5 per box entry (engine, func_001A8840: 5.0f into
 * the PLAYER's +0x22C plus player state byte = 3, gated on the pad's
 * open flag +0x0B); the visual is an original placeholder mound scaled to the
 * decoded footprint with the +0x80 phase as a Y swell (the engine's
 * procedural VU morph — func_001E9580/001E9E60 per-instance buffers —
 * binds NO model-table entry, so there is nothing to export; CONFIRMED
 * 2026-07-31: neither recovered function calls a model-bind helper, they
 * fill/emit a vertex record directly; sounds
 * 0x42F breathing / 0x430 worm emerge go through em_sfx and are
 * silent until the user's sfx.txt maps them).
 *
 * TENDRIL FIELD (em_enemy.h "TENDRIL FIELD"; FINDINGS "KIND-0xE
 * COMPANION RESOLVED" — brain func_001546C0, init func_00154740, tick
 * func_001549C0, render func_00154F00, trigger gate func_00154460):
 * the mode-1 pad's companion PAIR (func_0015A200(pad, 0xE, 0/1) at
 * generator init — the engine's only other dynamic generator child;
 * CONFIRMED off func_0015A2C0 state 0, which fires that pair for either
 * link table whenever the drawn mode is 1, and off func_0015A200, which
 * copies the pad origin verbatim and hands the child the parent's
 * config kind at +0x0D and the pair index at +0x2E).
 * Each field actor owns 12 spike records and runs the decoded
 * SCAN -> DEPLOY -> HOLD -> RETRACT -> RESET machine, re-verified
 * sub-state by sub-state against func_001549C0 on 2026-07-31:
 *
 *   SCAN     gameplay-frame gate (native: em_enemy_update only runs in
 *            gameplay frames) + player inside 3x the parent pad
 *            footprint, |dy| <= 3 + recY. Trigger: anchor = (player X,
 *            pad Y, player Z); 12 targets = anchor + polar(r, theta),
 *            theta uniform, r = 5.5 + 2.0*rand01 u (pair idx 0) /
 *            7.0 + 2.5*rand01 u (idx 1) — CORRECTED 2026-07-31 off
 *            func_001549C0 case 0: the draw is one-sided (BASE is the
 *            inner edge), so the rings really are concentric —
 *            [5.5, 7.5) and [7.0, 9.5); valid = target inside
 *            the 0.92x pad ellipse (the engine's atan2 + radius-at-
 *            angle formula == the normalized point-in-ellipse test);
 *            phase = rand 48..127, vel/ramp = 0, girth = the cycling
 *            {0xB4,0xDA,0xFF,0x180}/256 table (random start row);
 *            sound 0x42D if ANY target valid (engine range 300 —
 *            em_sfx has no positional attenuation, same note as
 *            0x42F). CORRECTED 2026-07-31: it does NOT deploy on the
 *            trigger tick — func_001549C0 case 0 ends in a plain
 *            `break`, so only the render tail runs that tick (the tail
 *            fires whenever the sub-state is nonzero, and case 0 has
 *            just made it 1).
 *   DEPLOY   all 12 ramps += 37/tick clamp 300 starting the tick AFTER
 *            the scan; 8-tick timer -> HOLD (ramps land on 296).
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
/* The AREA-11 DOOR-HUSK PAIR (deferred records 7/8 @(387,231.8,290.3) —
 * FINDINGS "Door-position creature/husk set-piece"; INVESTIGATION_first_
 * level_area11 §5.2; INVESTIGATION_area11_director §5). Both meshes are
 * SCENE-LOCAL AREA-11 content (carved + verified: model 0x1A = the
 * scripted creature, model 0x29 = the shootable burst-husk-B), so they
 * live under <scene>/props/ and never load for a scene that lacks them.
 * The scene loader supplies the active directory explicitly: New Game
 * and room transitions can change it without main.c's EM_SCENE fixture. */
static char enemy_scene_dir[512] = "assets/scene";

void em_enemy_set_scene_directory(const char *directory)
{
    snprintf(enemy_scene_dir, sizeof enemy_scene_dir, "%s", directory);
}
#define ENEMY_BONE_MAX   32
#define ENEMY_PI         3.14159265f

/* The crawler/gib SLOT pool — the original 16-slot budget (EM_ENEMY_MAX
 * grew into the render-chain DRAW capacity when the tendril fields
 * landed; the gameplay pools below are unchanged, so tests 1..4 and the
 * gib RNG stream stay byte-identical). */
#define ENEMY_SLOT_MAX   16

/* --- Engine constants. CONFIRMED 2026-07-31 (audit) against the
 * recovered C, one function named per value — re-check them there, not
 * against a disassembly listing:
 *   func_001551B0 (NEARMISS) — the placed-crawler brain
 *   func_00154120 (NEARMISS) — the leech sub-machine
 *   func_00154040 (BYTE-MATCHED) — the leech init
 * NOTE func_00153F10, the leech's outer brain, is NOT readable C in the
 * decomp (it ships as an all-`.word` asm body), so nothing below rests
 * on it. --------------------------------------------------------------- */
#define ENEMY_HP_CRATE   1        /* placed crawler init HP: func_001551B0
                                   * state 0 `*(short *)(a+0x34) = 1`     */
#define ENEMY_HP_WORM    10       /* worm/leech init HP: func_00154040
                                   * `*(short *)(a+0x34) = 0xA`
                                   * — VESTIGIAL (J2 s66 live: nothing
                                   * ever reads it; kept so the slot
                                   * mirrors the engine actor exactly)    */
#define ENEMY_TURN_RATE  0.0524f  /* +-3 deg/frame steer-away: the
                                   * func_001551B0 state-1 sub-0 tilt
                                   * constant 0.052359875f (0x3D56774F)   */
#define ENEMY_HOMING_RATE 0.0698f /* worm STALK homing, rad/tick: the
                                   * func_00154120 case-1 literal
                                   * 0.0698131695f fed to func_001B12B0   */
#define ENEMY_GRAVITY    0.052f   /* hop vertical integration, per tick:
                                   * func_001551B0 state-1 sub-1 ballistic
                                   * leg `+0x2C8 -= 0.052f`
                                   *
                                   * RE-VERIFIED 2026-07-31, DO NOT "fix" this
                                   * to the shared 0.04f/-4.0f pair. The crate
                                   * hop is NOT the shared gravity tick: the
                                   * whole of func_001551B0 contains zero calls
                                   * to func_00179880/func_001796C0, and its
                                   * own decrement really is 0.052f with no
                                   * terminal clamp. A gap-triage pass called
                                   * this "port-invented" and it is not.
                                   *
                                   * Two look-alikes to avoid: func_001551B0
                                   * also carries 0.052359875f (= pi/60, a
                                   * 3-degree angle) fed to the matrix
                                   * rotators func_00102B08/func_00102A60 —
                                   * a rotation, not an acceleration; and the
                                   * PLAYER falls by the shared -0.04f with a
                                   * -4.0f terminal (func_00179880). Three
                                   * similar-looking constants, three
                                   * different jobs.                       */
#define ENEMY_CONTACT_R  6.0f     /* PORT (flagged — was mis-read as an
                                   * engine value): the lunge-resolve second
                                   * arm calls func_0019A570(from, to, 6, 0),
                                   * and the DECODE of that function (its own
                                   * body: `flags = arg2 & 0xFF` then three
                                   * independent `flags & 1 / & 2 / & 4`
                                   * channel tests) shows arg2 is a COLLISION
                                   * CHANNEL MASK, not a radius — bit0 = the
                                   * entity list (func_001A6440, arg3 = the
                                   * 16-bit exclusion id), bit1 = the area
                                   * hulls + published class-4 actors
                                   * (func_001A0B10/func_0019F730), bit2 =
                                   * the static world collision tree
                                   * (func_0019D330/func_0019C830). The two
                                   * point args are a SEGMENT (from/to staged
                                   * at 0x70003190/0x700031A0), so there is no
                                   * sphere and no "6 units" anywhere. The
                                   * port keeps a 6-unit contact sphere as its
                                   * OWN stand-in for the unexported volume —
                                   * the number is now a port choice, not a
                                   * decoded one. Also the rig-less fallback
                                   * fold of the segment arm (then it carries
                                   * the 15).                              */
/* LATCH SEGMENT. The engine resolve's FIRST arm is
 * func_0019AA80(a, b, 0x20) — CONFIRMED 2026-07-31 as a SEGMENT query by
 * the recovered func_0019AA80: it stages the two point args at
 * 0x70003190/+0x10 and passes `arg2 & 0xFFFF` to func_001A7280, so
 * 0x20 is a filter MASK, never a radius.
 * DOWNGRADED (2026-07-31): the endpoints. func_00154120 reads both from
 * the GLOBAL D_00275B40 — `*(char **)(D_00275B40 + 0x34) + 0xC0` and
 * `*(char **)(D_00275B40 + 0x40) + 0xC0` — not from the worm actor it
 * was handed. "Rig nodes 13/16, swept against the player's +0x58
 * bone-anchored sphere list" is the s66 LIVE reading and stays
 * OBSERVED, not source-derived. The port stages the segment from
 * the worm's OWN animated palette (translation columns of nodes
 * 13/16, world space after enemy_build_palette — one tick stale, the
 * pose the player SEES) and sweeps it against a PLAYER CAPSULE: the
 * volume list's radii/anchors are unexported, so the capsule is a
 * flagged stand-in sized from the player's known body numbers (wall
 * radius 4.5, ~17-u height). */
#define ENEMY_LATCH_NODE_A 13     /* neck — node-table slot +0x34 (s66)   */
#define ENEMY_LATCH_NODE_B 16     /* head — node-table slot +0x40 (s66)   */
/* DOWNGRADED 2026-07-31 — this was recorded as "func_00154040 writes 0.5
 * to actor +0x80, so the live leech is HALF authored size". The
 * decompilation does not support that reading:
 *   - func_00154040 (BYTE-MATCHED) writes FOUR floats, +0x80 = 0.5,
 *     +0x84 = 0.875, +0x88 = 1.0, +0x8C = 1.0 — not a uniform scale;
 *   - the actor pool allocator func_001AFA90 (BYTE-MATCHED) initialises
 *     BOTH quads +0x60..+0x6C and +0x80..+0x8C to (1,1,1,1);
 *   - func_001549C0 (the tendril render tail) writes the room-tint GREEN
 *     into +0x84 as `(92 + k * (room.g - 92)) / 128` — i.e. +0x80..+0x8C
 *     is the actor's RGBA MULTIPLIER, exactly as this file already
 *     documents it in the Tendril struct.
 * So func_00154040 gives the leech a cool (0.5, 0.875, 1.0, 1.0) TINT,
 * and the decomp supplies NO actor scale at all. The 0.5 below stays
 * because the port's lunge geometry was tuned around it (authored size
 * arced the lunge over the player), but it is now a FLAGGED PORT
 * constant, not a decoded one. The decoded tint is recorded and
 * UNTRANSLATED (live crawlers still draw untinted). */
#define ENEMY_ACTOR_SCALE 0.5f    /* PORT (flagged): render scale — the
                                   * DOWNGRADE above was re-verified
                                   * 2026-07-31 (func_00154040 writes a
                                   * 4-float RGBA tint at +0x80, no scale) */
#define ENEMY_STALK_STANDOFF 10.0f /* stalk-slide stop distance (PORT
                                    * locomotion stand-in, flagged: the
                                    * engine's stalk root motion is its
                                    * anim's; the standoff keeps the
                                    * CONNECT on the lunge resolve)      */
#define PLAYER_HV_R      4.5f     /* capsule radius — the engine wall
                                   * radius (PORT stand-in, flagged)      */
#define PLAYER_HV_Y0     2.0f     /* capsule foot, above ground Y (PORT)  */
#define PLAYER_HV_Y1     15.0f    /* capsule head, above ground Y (PORT)  */
#define ENEMY_STEER_TICKS  6      /* +0x2A = 6 at the alarm wake —
                                   * CONFIRMED 2026-07-31 off
                                   * func_001551B0 state 4's `+0x0A`
                                   * branch (also state 2 sub 1's re-arm) */
#define ENEMY_ATTACK_TICKS 180    /* +0x2A = 0xB4 at the hop launch — the
                                   * suicide-run timer for every variant
                                   * EXCEPT model byte 6 (see below).
                                   * CONFIRMED 2026-07-31                 */
/* DECODED (func_001551B0 state 1 sub 0, the run-timer arm) — CONFIRMED
 * 2026-07-31 against the recovered C: model byte 6
 * — the wooden crate, i.e. the port's DEFAULT variant — does NOT get the
 * 0xB4 constant. It gets `(short)(60.0f * actor+0xB4 / 12.0f)`, i.e. FIVE
 * TIMES THE ACTOR'S WORLD Y at launch. The old "5x its height — n/a here"
 * note was wrong twice over: it is the world Y (not a body height), and it
 * applies to exactly the variant the port ships. A crate placed low to the
 * world origin therefore gets a very short run; a high placement runs long. */
#define ENEMY_ATTACK_Y_NUM 60.0f
#define ENEMY_ATTACK_Y_DEN 12.0f
#define ENEMY_APPROACH_F 90       /* worm sub 0 window: the engine gates
                                   * on its bound anim's end (id chain
                                   * unverified) — port mapping: the
                                   * bank's 90-f emerge clip              */
#define ENEMY_STALK_TICKS 120     /* worm sub 1 window. CONFIRMED
                                   * 2026-07-31: func_00154120 case 0
                                   * writes `*(short *)(a+0x28) = 0x78`
                                   * when the sub-0 anim reports done, and
                                   * case 1 advances at t == 0            */
#define ENEMY_WINDUP_F   45       /* worm sub 2 window = the 45-f windup
                                   * clip. The engine gates sub 2 on the
                                   * anim-done bit (func_00154120 case 2
                                   * `if (arg1[2] & 0x1000)`), so the
                                   * FRAME COUNT is the port's mapping    */
#define ENEMY_LUNGE_F    120      /* worm sub 3 window = the 120-f lunge
                                   * clip; anim-gated the same way
                                   * (func_00154120 case 3's third arm)   */
#define ENEMY_LATCH_LUNGE 15      /* D_008104D4 = 15.0. CONFIRMED
                                   * 2026-07-31: func_00154120 case 3
                                   * stores 0x41700000 there on the
                                   * segment hit, gated on
                                   * D_008102B0[0] == 1                   */
#define ENEMY_LATCH_TOUCH 5       /* D_008104D4 = 5.0. CONFIRMED
                                   * 2026-07-31: func_00154120 case 0
                                   * stores 0x40A00000 there, same gate;
                                   * UNTRANSLATED (no latch/shake-off
                                   * system), recorded for fidelity       */
#define ENEMY_SFX_WINDUP 0x431u   /* leech windup -> lunge snap. CONFIRMED
                                   * 2026-07-31: func_00154120 case 2
                                   * `func_001FBD50(a, 0x431, 0, 300.0f)` */
#define ENEMY_SFX_BURST  0x434u   /* leech burst. DOWNGRADED 2026-07-31:
                                   * its only citation is func_00153F10,
                                   * which has NO readable C in the decomp
                                   * (all-`.word` asm body). Treat as
                                   * OBSERVED, not source-derived         */

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
 * into the engine's TWO HUSK FAMILIES. The pick is DECODED and
 * re-verified 2026-07-31
 * (func_001551B0 state 2 sub 0 — closes the s24
 * "which husk binds to which variant" open item): the damage-kill
 * arm reads the crawler MODEL byte (+0x03) and rebinds the corpse
 * model (D_0028A56C library) to entry 0x22 when the byte is 6, else
 * 0x29. Byte 6 is EVERY crate in AREA02/11/13/18/20/22 — the WOODEN
 * crate — and husk A is its brown opened-crate base with the
 * texture-paired splinters 0x1C/0x1D/0x1E; the grey-cyan husk B
 * family (0x29 + chunks 0x26/0x27 + the half-size 0x28) is the
 * `else`. CORRECTED 2026-07-31: that `else` is reached by variant
 * 0x1E ONLY — the whole arm is gated on
 *     +0x36 != 0 && (byte == 6 || byte == 0x1E)
 * and 0x1C / 0x1F / 0x50 drop straight to state 3 with no corpse at
 * all. The old note credited husk B to "the 0x1C/0x1E/0x1F variants",
 * which the recovered C does not support; the port still scatters
 * husk-B pieces for those variants as a flagged PORT visual. The old
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
/* DISGUISE JITTER — the CYCLE is DECODED (func_001551B0 state 4, the
 * `+0x0E & 1` block) and CONFIRMED 2026-07-31 line-for-line against the
 * recovered C; only the table VALUES stay unexported:
 *
 *   - the whole block is gated on flag bit 0 of actor +0x0E, and INIT
 *     CLEARS that bit when the crate's nest group has no un-triggered
 *     record left (state 0 walks the group and counts them). A gore-only
 *     crate therefore sits PERFECTLY STILL — the wiggle is the nest tell,
 *     not a universal prop idle. Port equivalent: `children > 0`.
 *   - counter +0x238 starts at -1. On every tick with counter < 0 the
 *     engine plays sound 0x19C positionally at range 300
 *     (func_001FBD50(self, 0x19C, 0, 300.0f)), then redraws the counter
 *     as rand 0..255 and the table ROW +0x23C as rand 0..6.
 *   - while the counter runs down it does NOTHING until it reaches 8.
 *     Only on ODD counter values below 9 (7, 5, 3, 1 — four frames,
 *     alternating with clean ones) does it apply one wiggle step: a yaw
 *     rotation from D_002468B0 and x/z render-position offsets from the
 *     parallel D_002468B4/D_002468B8, all indexed [row][(counter-1)>>1],
 *     i.e. 7 rows x 4 columns of 3 floats. At counter == 0 the base world
 *     matrix is restored, so the burst always settles clean.
 *
 * So the idle is: a rattle sound, a random wait of up to ~4 s, a 4-frame
 * shudder, repeat — NOT the continuous sine envelope the port used to run
 * (that was invented). The amplitudes below and the 4-step column shape
 * remain FLAGGED PORT constants (the D_002468B0/B4/B8 rows are not
 * exported); the LCG stands in for the engine frame RNG, as elsewhere. */
#define CRATE_JIT_POS    0.08f    /* PORT: x/z wiggle amplitude, units    */
#define CRATE_JIT_YAW    0.02f    /* PORT: yaw wobble amplitude, rad      */
#define CRATE_JIT_WAIT   256      /* counter draw 0..255 (decoded span)   */
#define CRATE_JIT_ROWS   7        /* table row draw 0..6 (decoded span)   */
#define CRATE_SFX_IDLE   0x19Cu   /* idle rattle at each cycle start      */
/* PORT: the per-step magnitude shape standing in for one D_002468B0 row
 * (4 columns, applied in the decoded order col 3, 2, 1, 0 as the counter
 * walks 7 -> 1). Flagged — the engine rows are not exported. */
static const float CRATE_JIT_COL[4] = { 0.30f, 0.65f, 1.00f, 0.55f };

/* BURST audio/FX — DECODED (func_001551B0 state 2 sub 0, the per-model
 * arm), all five rows CONFIRMED 2026-07-31 against the recovered C.
 * Each placed-crawler variant plays one positional sound through
 * func_001FC580 plus two gore effects through func_001EFD90(code, pos,
 * rot):
 *      byte 6    -> 0x19D, FX 0x8000000A + 0x80000015
 *      byte 0x1E -> 0x19D, FX 0x80000031 + 0x80000015
 *      byte 0x1C -> 0x19E, FX 0x8000000B + 0x80000014
 *      byte 0x50 -> 0x19E, FX 0x8000000B + 0x80000014
 *      byte 0x1F -> 0x19E, FX 0x80000032 + 0x80000014
 * The port plays the sound (em_sfx resolves it if the user's sfx.txt maps
 * it, silent otherwise) and RECORDS the FX ids — the gore particle chain
 * lives outside this module, so the visible burst stays the gib scatter
 * (flagged, as for the husk partner). The generic 0x7D8 hurt-helper death
 * sound the port used to play for a crate is REMOVED: state 4 hands
 * straight to state 2 without touching the shared hurt helper, so the
 * engine never plays 0x7D8 for a placed crawler. */
#define CRATE_SFX_BURST_A 0x19Du  /* model byte 6 / 0x1E                  */
#define CRATE_SFX_BURST_B 0x19Eu  /* model byte 0x1C / 0x50 / 0x1F        */
#define CRATE_FX_BURST_6  0x8000000Au /* recorded; gore chain unported    */
#define CRATE_FX_BURST_1C 0x8000000Bu
#define CRATE_FX_BURST_1E 0x80000031u
#define CRATE_FX_BURST_1F 0x80000032u
#define CRATE_FX_GORE_A   0x80000015u /* byte 6/0x1E second effect        */
#define CRATE_FX_GORE_B   0x80000014u /* byte 0x1C/0x50/0x1F second       */
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

/* --- The burst billboard + debris (egg_explode, which the door-husk
 * partner's burst reuses; it was the retired drum kind's explosion) -------
 * The model-0x18 drum does NOT alpha-fade on death — it EXPLODES: an
 * expanding fireball/flash billboard (engine FX 0x80000013 -> child
 * 0x8000006E + flash layer 0x8000001C) at the drum pos with Y+7, plus
 * flung debris chunks. PURELY VISUAL — no radius/blast damage, no chain
 * (LIVE: player stayed at 100.0 HP). The port reuses the gib system for
 * the debris and an in-module expanding-billboard "flash gib" for the
 * fireball (the truly-additive .emtx billboard path lives in em_weapon.c
 * / the gfx beam queue, out of this module's reach — FLAGGED: the flash
 * here is an alpha-blended tinted quad through the enemy draw chain, not
 * the engine's additive 0x8000006E sprite). */
#define EGG_BLAST_LIFT   7.0f     /* fireball origin Y+7 (LIVE: FX param
                                   * block pos was drum XZ, Y raised +7.0) */
#define EGG_DEBRIS_MIN   5        /* flung chunks: 5..8 (spec ~5-8)        */
#define EGG_DEBRIS_SPAN  4
#define EGG_FLASH_FRAMES 14       /* fireball life ~12-16 ticks            */
#define EGG_FLASH_R0     6.0f     /* start radius (units)                  */
#define EGG_FLASH_R1     20.0f    /* peak radius (the 0x80000013 scale
                                   * table tops at 10/15/20 u — INVEST.)   */
#define EGG_FLASH_RGB_R  1.0f     /* bright orange-yellow fireball tint    */
#define EGG_FLASH_RGB_G  0.65f
#define EGG_FLASH_RGB_B  0.18f

/* --- AREA-11 DOOR-HUSK PAIR (see "DOOR-HUSK PAIR" in the file header /
 * em_enemy.h kinds 4/5) ------------------------------------------------
 * Records 7/8 @(387,231.8,290.3), the staged "first-monster moment" by
 * the room-move door. The pair is SELF-CONTAINED (its own timer/proximity
 * state machine — func_00825900 / func_001B0FD0, states 0..4 + the live
 * 0x64 scripted timer), reading NO story flag: it is NOT driven by the
 * D_00810813 director (INVESTIGATION_area11_director §5 — static negative).
 * At the OPENING both are STAGED-INERT: present/visible but not aggroing,
 * not moving, not attacking — the opening must stay enemy-free (owner
 * requirement + §5.2 verdict ZERO ACTIVE ENEMIES). Port discipline mirrors
 * the crate/egg INIT->IDLE: spawn -> hold an inert IDLE forever.
 *
 * Both are STATIC posed meshes here (no anim layer is wired — the engine's
 * scripted creature clips and the husk's animation are undecoded; the
 * meshes carve a single rest pose). The creature is NOT shootable; the
 * husk partner IS (HP 1, mailbox poll, burst on death).                  */
#define HUSK_CREATURE_BONE_MAX 32  /* multi-node rig — generous cap; the
                                    * actual bone_count is read from the
                                    * carved mesh and clamped to this      */
#define HUSK_PARTNER_BONE_MAX  32
#define HUSK_CREATURE_AIM_Y    4.0f /* unused (creature not shootable) —
                                    * kept for parity with the other kinds */
#define HUSK_CREATURE_HIT_R    4.0f
/* The shootable husk partner (ov 0x827490, model 0x29): HP 1 — any +0x36
 * write is lethal — and on death it BURSTS with gore FX 0x80000045 (the
 * engine's burst particle). The port reuses the husk-B gib/explosion path
 * (grey-cyan family, the same 0x29 husk-B set the crate variant!=6 rebind
 * uses); FX 0x80000045 is the engine's exact particle id — NOT directly
 * reproducible here (the additive particle subsystem lives outside this
 * module), so it is FLAGGED and the visible burst is the reused gib
 * scatter. The engine actor is partner-linked (puid 0x50) with taken-bit
 * persistence; the port models only the shootable/burst arm — the link
 * and across-visit persistence are FLAGGED (the port re-spawns on every
 * scene load, like the egg/crate). */
#define HUSK_PARTNER_HP        1     /* +0x34 = 1 (HP-1 shootable husk)    */
#define HUSK_PARTNER_AIM_Y     6.0f  /* PORT: reticle/auto-aim center —
                                      * mid-height of the 0x29 husk-B body
                                      * (14x14, 8 tall — FINDINGS s24); the
                                      * engine target volume is unexported  */
#define HUSK_PARTNER_HIT_R     7.0f  /* PORT: bullet hit-sphere ~ the husk
                                      * body half-extent (the engine
                                      * func_001B1D20 volume is unexported) */
#define HUSK_PARTNER_FX_BURST  0x80000045u /* gore burst particle (engine
                                      * id; FLAGGED — reused gib scatter)  */

/* --- Bug kind (s68/s76 — see "THE BUG" in the file header) --------------
 * DOWNGRADED 2026-07-31. The old header here read "and (s76) the attack
 * SHAPE — the brains' state machine is decoded ... spawn-pose ->
 * sense-gated approach -> IN-PLACE bite (clip 0x13, func_0012C490) ->
 * recover". No recovered function says that:
 *   - func_0012C490 (BYTE-MATCHED) is a 9-state LEAP chain — clip 0x13
 *     with a body hop func_00128830(actor, 0, 0, -2.5) and sfx 0x1AE,
 *     then clips 0x14 (hop 0,2.5,0) and 0x15 (hop 0,1.5,3) — and it is
 *     one entry in func_0012A5D0's movement table, with no contact test
 *     anywhere in it;
 *   - func_00128C10 (recovered since, NEARMISS) is a sense + WANDER
 *     machine that turns to RANDOM headings, not a pursuit-and-bite (the
 *     full read-out is in the file header's BUG block);
 *   - func_001B5360, once credited with the bite box, is the shared
 *     GROUND-SNAP probe (correction block below).
 * What IS engine-derived here: the init HP (func_00128390), the
 * every-tick mailbox consumption (func_00128B80 -> func_00129FC0), the
 * clip ids and the reaction audio. The approach/bite/latch SHAPE, its
 * timers, the contact box and BUG_BITE_DMG are all PORT choices. */
#define BUG_HP_A         15       /* func_00128390 variant A (slot 0x0F)  */
#define BUG_HP_B         30       /* variant B (slot 0x10) — recorded;
                                   * difficulty byte D_0081070A raises
                                   * the pair to 30/50 (unbound: the
                                   * port has no difficulty plumbing)     */
#define BUG_CLIP_WALK    1u       /* init clip: the 90-f in-place WALK    */
/* CORRECTED (func_00129FC0, BYTE-MATCHED — read the reaction driver, not
 * the old summary). The clip ids split THREE ways, and 0x1B is NOT the
 * death clip:
 *   case 0 (reaction entry) subtracts the damage and then picks
 *          func_001287F0(actor, ctrl, 0x1B, 0.0f)  -> the KNOCKDOWN anim
 *          when the damage word has bit 0x2000, or ctrl[0xFB] bit 7, or
 *          ctrl+0xE4 is 0 / 0x400 / 0x500 — a HEAVY-HIT gate that has
 *          nothing to do with lethality (it fires on survivable hits
 *          too), and hands to reaction state 5;
 *          else func_001287F0(..., 0x1D, 0.0f) -> the FLINCH anim.
 *          Either way, HP <= 0 additionally fires the death event
 *          func_001EFD90(0x8000000C, pos, rot).
 *   case 1 waits for that anim to finish; if HP ran out it stages the
 *          fall direction and picks reaction state 2 (func_0012D580,
 *          undecoded) or 3.
 *   case 3 is the COLLAPSE: func_001287F0(..., 0x20, 4.0f) +
 *          func_001FBD50(actor, 0x1B7, 0, 300.0f).
 *   case 4 then frees the actor (sound 0x1B5).
 * So the corpse's own animation is 0x20; 0x1B is the knockdown reaction.
 * The port asks for 0x20 first and falls back to 0x1B, then to the
 * frozen-pose fade, so a bake that lacks either still degrades cleanly. */
#define BUG_CLIP_DEATH   0x20u    /* func_00129FC0 case 3 collapse clip   */
#define BUG_CLIP_KNOCKDN 0x1Bu    /* func_00129FC0 case 0 knockdown clip
                                   * (heavy-hit reaction, survivable —
                                   * UNTRANSLATED: the port has no
                                   * knockdown branch, it flinches)       */
#define BUG_CLIP_FLINCH  0x1Du    /* func_00129FC0 case 0 flinch clip     */
/* BUG AUDIO — DECODED (func_00129FC0, BYTE-MATCHED); all three ids
 * CONFIRMED 2026-07-31 against the recovered C. The old note that
 * "func_00129FC0's audio is undecoded" is false: the recovered driver
 * plays three distinct sounds, none of them the generic 0x7D8 the port
 * was using for a bug.
 *   case 0 (EVERY reaction entry, survivable or lethal):
 *          func_001FBD50(actor, 0x1B1, 0, 300.0f) — the hurt grunt. It
 *          is unconditional; the separate 0x4000 flag on the damage word
 *          only gates the VOICE event 0x80000027 (with a 60-frame
 *          cooldown at +0x28), which is a different channel.
 *   case 3 (COLLAPSE, alongside clip 0x20):
 *          func_001FBD50(actor, 0x1B7, 0, 300.0f).
 *   case 4 (the free): func_001FC580(actor, 0x1B5).
 * The port collapses the reaction chain into one mailbox poll, so it
 * plays 0x1B1 on any hit and 0x1B7 on the lethal one. 0x1B5 is recorded;
 * the port frees in the same tick as the collapse, so firing both there
 * would double up — UNTRANSLATED, flagged. */
#define BUG_SFX_HURT     0x1B1u   /* func_00129FC0 case 0, range 300      */
#define BUG_SFX_COLLAPSE 0x1B7u   /* func_00129FC0 case 3, range 300      */
#define BUG_SFX_FREE     0x1B5u   /* func_00129FC0 case 4 — UNTRANSLATED  */
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
/* CORRECTION (func_001B5360 + func_0019A570 both recovered), re-verified
 * 2026-07-31: the three
 * constants below were recorded as "VERIFIED" off func_001B5360, and that
 * reading does not survive the decompilation. func_001B5360 is not a melee
 * contact resolver at all — it is the shared GROUND-SNAP helper. It copies
 * the actor's transform block (+0xB0) into the scratch vectors, raises the
 * start point by +10.0 in Y (up, not forward), drops the end point to
 * start.y - 30.0 (- 200.0 for model byte 4), traces that VERTICAL segment
 * with func_0019A570(from, to, 6, 0), and on a hit blends +0xB0 toward the
 * hit surface with a weight chosen by a 13-entry jump table on the actor's
 * MODEL byte +0x03: {2.0, 3.5, 4.2 (default), 5.0, 6.0, 6.5, 8.0}. Those
 * are BLEND WEIGHTS handed to func_001F9100/func_001F9180 together with the
 * hit point and the surface record — not damage amounts. And the "6" is
 * func_0019A570's channel MASK (see ENEMY_CONTACT_R above), not a radius.
 * The call sites agree: func_001B5360 is called from ~17 brain functions
 * including the bug's own HURT/DEATH handler func_00129FC0, which would
 * never run an attack box.
 * So: the bug's real bite volume and its damage value are NOT decoded
 * (re-verified 2026-07-31 — func_001B5360's jump table really does hand
 * those numbers to func_001F9100/func_001F9180 as blend weights). The
 * numbers below keep their behaviour (they are a reasonable reach for the
 * authored body) but they are PORT constants, flagged like the timers. */
#define BUG_CONTACT_FWD  10.0f    /* PORT: contact box pushed ahead of the
                                   * bug (was mis-attributed to _001B5360) */
#define BUG_CONTACT_R    6.0f     /* PORT: contact sphere radius (ditto)   */
#define BUG_BITE_DMG     5        /* PORT: bite damage — the jtbl_0026DEA0
                                   * "damage tiers" turned out to be
                                   * ground-snap blend weights (above), so
                                   * this is a plain port choice           */
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
 * The four control constants below are CONFIRMED 2026-07-31 against the
 * recovered func_0015A2C0 (NEARMISS) state 1; the Y tolerance against
 * func_001A8840 (NEARMISS). The TABLE CONTENTS (GEN_CFG / GEN_TBL /
 * GEN_DELAY) are disc .data read out separately — func_0015A2C0 only
 * shows their SHAPE (5 floats per config row, [row&3][cursor&7] byte
 * lookup, a 3-entry delay pool), which does match. */
#define GEN_CHARGE_WORM   120.0f  /* mode-2 charge: func_0015A2C0 mode 2
                                   * sub 0 `f = +0x20 + 1; if (!(f <=
                                   * 120.0f)) spawn` = 121 in-box frames */
#define GEN_CHARGE_OPEN   100.0f  /* mode-1 sub 0: `if (!(f < 100.0f))`  */
#define GEN_OPEN_HOLD     60.0f   /* mode-1 open hold (+0x20 = 60.0f)    */
#define GEN_WORM_CAP      4       /* mode-2 `if ((int)+0x2E >= 4) sub=2` */
#define GEN_BOX_Y         1.0f    /* config Y extent (1.0 in all 7 recs) */
#define GEN_BOX_Y_TOL     1.5f    /* func_001A8840's Y test really is
                                   * `|dy| <= 1.5f + bounds[1]`          */
#define GEN_TRAP_HIT      5       /* open-trap player damage. Tightened
                                   * 2026-07-31 to name its source: the
                                   * pair pass func_001A8840 (recovered).
                                   * Inside the pad box, with the pad's
                                   * OPEN flag +0x0B set, D_00810707 != 1
                                   * and the player's status byte == 1, it
                                   * writes 5.0f (0x40A00000) into the
                                   * PLAYER's +0x22C and forces the player
                                   * state byte to 3 — a knockdown state,
                                   * not an "event 3" as the old note put
                                   * it. PORT: one-shot mailbox write per
                                   * box entry (flagged — no player
                                   * state-3 channel here)                */
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

/* PAD GEOMETRY — what the engine actually builds (func_001E9580, BYTE-
 * MATCHED, called from the generator init func_0015A2C0). Every line of
 * this block was re-read against the recovered C 2026-07-31 and is
 * CONFIRMED, including the (i/3 - 1) span algebra:
 *   - the pad owns an 0xA060-byte per-instance record: a 0x60 header,
 *     then a vertex lattice at +0x60 (stride 0x10, row stride 0x200), a
 *     UV lattice at +0x4060 and two int lattices at +0x8060/+0x9060.
 *   - init fills an 8 x 8 patch. FOOTPRINT (this confirms the port's
 *     scaling choice): func_0015A2C0 passes the config row's X and Z
 *     half-extents DOUBLED, so the cell spacing is the full config box.
 *     Vertex i/j lands at origin + halfExtent * (i/3 - 1) — a span of
 *     [-1, +4/3] half-extents, i.e. the box plus a third of it hanging
 *     off the +X/+Z side (an off-by-one in the original: 8 samples
 *     divided by 6).
 *   - REST SHAPE: every vertex sits at the pad's own Y. Vertices in the
 *     dome interior (weight t = (3 - hypot(i-3, j-3)) / 4 above 0.1) get
 *     one small random lift of 0.15 + 0.05 * rand/2^31 — so the pad is
 *     essentially a FLAT membrane with a ~0.15..0.20 unit stipple in the
 *     middle, not a raised mound. The dome only appears once the sim
 *     runs: t (saturated to 1.0 above 0.15, else doubled, clamped at 0)
 *     is stored per vertex at +0x6C as the morph weight, and the physics
 *     params are +0x40 = 0.005 / +0x44 = 0.445 / +0x48 = 50.0 /
 *     +0x4C = -9.0 / +0x50 = -1.7 (per-area overrides exist). UVs are
 *     (i/8, j/8).
 * The port keeps the placeholder mound: the REST lattice is decoded but
 * the OPEN shape is the undecoded VU/soft-body step, and a flat patch
 * would simply hide the pad's breathing with nothing decoded to replace
 * it. Height and swell below stay flagged PORT constants; the footprint
 * scaling in gen_build_palette is CONFIRMED against the engine
 * (func_0015A2C0 doubles the config row before handing it to
 * func_001E9580, whose lattice then spans that full box).
 * Original geometry, NOT disc data. */
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
/* CONFIRMED 2026-07-31 against the recovered gate func_00154460 (byte-
 * matched: `3.0f * recX` / `3.0f * recZ` / `3.0f + recY`) and the
 * validity predicate func_001545B0 (byte-matched: the 0.92 semi-axes and
 * the radius-at-angle form r^2 = a^2 b^2 / (a^2 sin^2 + b^2 cos^2), which
 * is algebraically the normalised point-in-ellipse test tf_scatter runs). */
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
/* CORRECTED 2026-07-31 (func_001549C0 case 0, the SCAN scatter loop):
 * the draw is ONE-SIDED, not centred. The recovered C reads
 *     radius = 5.5f + 2.0f * (rand() * 1/2^31)     when +0x2E == 0
 *     radius = 7.0f + 2.5f * (rand() * 1/2^31)     otherwise
 * so the ring spans [5.5, 7.5) for pair 0 and [7.0, 9.5) for pair 1 —
 * BASE is the inner edge and SPAN the outward spread. The port used to
 * build `BASE - SPAN + rand01 * 2 * SPAN`, i.e. [3.5, 7.5) / [4.5, 9.5),
 * which pulled a third of every field's spikes inside the engine's inner
 * radius and stacked both rings on top of each other instead of leaving
 * them concentric. */
static const float TF_RING_BASE[2] = { 5.5f, 7.0f };  /* pair idx 0 / 1 */
static const float TF_RING_SPAN[2] = { 2.0f, 2.5f };  /* OUTWARD spread */
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
 * open; ROOM = this room's rec keyed by AREA<<8|ROOM.
 * ROOM TINT. CONFIRMED 2026-07-31, with one honest caveat about WHICH
 * table. func_001E9580 (byte-matched) carries the RGB set below inline,
 * switched on exactly that key ((D_00810700 << 8) | D_00810701), and the
 * whole table was re-read off it this session — every row here matches.
 * The tendril field, though, does not call func_001E9580: func_00154740
 * (byte-matched) walks a SEPARATE 8-byte-record data table D_00246800
 * (key u32 + R/G/B/A bytes, at most 0x16 = 22 records) for the same key.
 * That the two agree is INFERRED, not read — the strongest evidence is
 * that func_001E9580's switch has exactly 22 distinct keys. The values
 * themselves are disc data and unexported either way.
 * The func_001E9580 set, key -> (R, G, B):
 *      0x0000, 0x0200                  -> (128, 128, 128)
 *      0x0001, 0x1001, 0x0002, 0x0101,
 *      0x0601, 0x0202, 0x0600          -> (128, 102, 122)
 *      0x0100, 0x0D00                  -> (128, 110, 128)
 *      0x0401                          -> (128,  88,  88)
 *      0x0700                          -> (125,  96, 128)
 *      0x0702, 0x0703                  -> (127, 104, 128)
 *      0x0803                          -> ( 97,  95, 128)
 *      0x1300                          -> ( 95,  80, 128)
 *      0x0300, 0x0400, 0x1000, 0x1301,
 *      0x1400, and anything unlisted   -> (128, 110, 128)
 * The port has no area/room identity to key on (scenes are manifests, not
 * area+room ids), so TF_TINT_ROOM stays the AREA02 room-0 row 0x0200 =
 * (128, 128, 128) — which is the correct rec for the captured office
 * scene, not a neutral guess any more. A future scene-identity channel can
 * key straight into the table above.
 * ALPHA = ramp/300 — the deploy fade-in/out tied to the ramp (engine:
 * roomC.w/128 reached over the first 16 ramp units; the ramp/cap form
 * keeps the fade on the same deploy timeline without the undecoded
 * room alpha target — flagged with the TODO above). */
static const float TF_TINT_BASE[3] = {   6.0f, 92.0f,   1.0f };
/* DECODED row 0x0200 (AREA02, room 0) — see the table above. */
static const float TF_TINT_ROOM[3] = { 128.0f, 128.0f, 128.0f };

/* --- Port placeholders (not exported from the disc; flagged) ----------- */
#define ENEMY_HOP_SPEED  0.32f    /* crate hop / worm stalk ground speed,
                                   * units/frame. PORT: the engine's own
                                   * numbers are 11.0 * the steer euler
                                   * (= 11 * 0.052359875 ~ 0.576 u/frame,
                                   * built on the last arc frame, with the
                                   * eulers then divided by 1.4) and, for
                                   * the stalk, the anim's root motion —
                                   * neither maps to one exported number
                                   * for the port's repeated-hop stand-in */
#define ENEMY_HOP_VY     0.42f    /* initial vertical velocity (~16-frame
                                   * airtime under the 0.052 gravity)     */
/* DECODED (func_001551B0 state 0, the +0x2D0..+0x2EC corner table),
 * re-verified 2026-07-31 including the r*sqrt2 algebra: the
 * four steer probes sit at the corners of the crawler's own SQUARE
 * footprint. INIT rotates the vector (r, 0, r) by the spawn world matrix
 * and lays the corners out 90 deg apart around the origin, with
 *      r = 4.5961943  for model byte 6 and 0x1E   (= 3.25 * sqrt2)
 *      r = 2.1213202  for model byte 0x1C/0x50/0x1F (= 1.5 * sqrt2)
 * so the corner radius is r * sqrt2 = 6.5 units for the wooden crate and
 * 3.0 for the small variants. CORRECTED 2026-07-31 — the column's SIGN:
 * the steer probe starts at pos.y - 1.0 and carries the delta vector
 * (_, -2.0, _), so it spans [pos.y - 3, pos.y - 1] and points DOWN, not
 * up (the old "[pos.y - 1, pos.y + 1] ... walks the delta -2.0 upward"
 * reading had it backwards). The INIT floor probe next to it is the same
 * shape one unit lower: start pos.y - 2.0, delta -3.0. So the four steer
 * probes are DOWNWARD ledge/prop soundings under the crawler's corners,
 * and a corner counts blocked only on func_0019AB20 return 2 (area hulls
 * / published class-4 actors) with the solid flag at 0x700031D4 set —
 * NOT on the static world, which is return 4.
 * PORT deviations, flagged: the port re-derives the corners from the LIVE
 * heading every tick (the engine snapshots them at INIT and never moves
 * them again, so an engine crawler probes the same four world columns for
 * its whole life), and it casts a horizontal ray at ENEMY_PROBE_LIFT
 * rather than a vertical column — the port's collision helper has no
 * column query. The reach and the lift are the decoded ones. */
#define ENEMY_PROBE_LEN  6.5f     /* corner radius, model byte 6/0x1E     */
#define ENEMY_PROBE_LEN_SMALL 3.0f /* corner radius, byte 0x1C/0x50/0x1F  */
#define ENEMY_PROBE_LIFT 1.0f     /* probe band half-height above/below   */
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
                           * / 3 lunge — the func_00154120 sub values,
                           * CONFIRMED 2026-07-31                       */
    uint8_t alarm;        /* actor +0x0A group-alarm flag (crate only —
                           * the worm and bug brains never read it)      */
    uint8_t on_surface;   /* actor +0x52. The probe shape below is
                           * CONFIRMED 2026-07-31 off func_001551B0 state
                           * 0. The engine's group-alarm
                           * broadcast only WAKES a crate when this is set,
                           * and it is 0 for every placed crate (live-read
                           * s76), so a destroyed crate wakes no neighbour.
                           * The DECODE explains why, and inverts the old
                           * "on-surface" reading: INIT (func_001551B0 state
                           * 0) fires ONE probe down the actor's own column,
                           * from pos.y - 2.0 with the delta (_, -3.0, _)
                           * — i.e. straight DOWN — and writes
                           *      +0x52 = 0 when the probe returns 4
                           *      +0x52 = 1 otherwise
                           * Return 4 is func_0019AB20's STATIC-WORLD channel
                           * (func_0019C830, the level collision tree);
                           * return 2 is the area hulls / class-4 actors and
                           * 0 is a clean miss. So +0x52 == 0 means "resting
                           * on level geometry" — the normal case for every
                           * placement — and +0x52 == 1 means the crawler is
                           * standing on a prop or on nothing. The alarm and
                           * the state-2 recovery loop are gated on the
                           * UNUSUAL value. func_001AFA90 (byte-matched pool
                           * alloc) zeroes +0x52 at allocation, so a spawned
                           * child starts here too. Kept 0 port-side: the
                           * name is historical.                            */
    int16_t jit_t;        /* actor +0x238 disguise-jitter counter (-1 =
                           * redraw + rattle this tick; see CRATE_JIT_*)   */
    uint8_t jit_row;      /* actor +0x23C jitter table row, 0..6           */
    uint8_t children;     /* crate: nest-group bug count hatched at the
                           * burst (the s68 registry group size; the
                           * manifest `bugs <n>` channel)                */
    uint8_t variant;      /* crate: the placement MODEL byte (+0x03 —
                           * {6,0x1C,0x1E,0x1F,0x50}; manifest
                           * `variant <v>`, default 6 = every exported
                           * scene's crates). Picks the husk family on
                           * the damage-kill burst (decoded
                           * func_001551B0 state 2 sub 0: 6 -> husk 0x22,
                           * else 0x29 — and CORRECTED 2026-07-31, the
                           * whole arm needs +0x36 != 0 AND byte 6 or
                           * 0x1E, so 0x1C/0x1F/0x50 never rebind at
                           * all). It also gates the engine's
                           * post-death arm (6/0x1E only) and, on byte
                           * 6, swaps the suicide-run timer for the
                           * world-Y formula — see crate_attack_tick
                           * and crate_burst. The port launches gibs
                           * for both families (flagged).              */
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

/* One airborne/resting gib instance (visual only). Also stands in for the
 * egg/drum EXPLOSION fireball: a `flash` gib is stationary, draws the
 * procedural billboard mesh (s.flash_mesh), expands its `scale` from
 * EGG_FLASH_R0 -> R1 and fades over EGG_FLASH_FRAMES (the in-module
 * approximation of the engine's additive 0x80000013->0x8000006E sprite —
 * see "DEATH = EXPLOSION" in the egg constants block). */
typedef struct {
    int   active;
    int   flash;          /* 1 = fireball billboard (not a debris chunk) */
    int   fam;            /* husk family (GIB_FAM_A/B — see GIB_FILES)   */
    int   model;          /* index into s.gibm[fam]                      */
    float pos[3];
    float vel[3];         /* 0.052/tick gravity on [1]                   */
    float yaw, spin;      /* tumble (PORT visual)                        */
    float scale;          /* uniform scale (1 for debris; the expanding
                           * radius for a flash billboard)               */
    float y0;             /* launch height = floor fallback (same rule
                           * as the hop's hop_y0)                        */
    int   age;            /* ticks since launch -> rest -> fade -> free  */
    int   life;           /* flash: total ticks before free (EGG_FLASH_
                           * FRAMES); debris ignore it                   */
    float tint[4];        /* per-draw RGBA: white, alpha 1 while live /
                           * resting, 1 -> 0 over the fade window (the
                           * flash carries a bright fireball RGB)        */
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

    /* AREA-11 door-husk pair meshes (scene-local — loaded only when a
     * husk_creature / husk_partner line is placed, so every other scene
     * keeps byte-identical output). Static posed meshes, no clips. */
    int        husk_c_tried;
    EmGfxMesh *husk_c_mesh;
    EmModel    husk_c_model;
    int        husk_c_has_model;
    uint32_t   husk_c_bones;
    float      husk_c_base[HUSK_CREATURE_BONE_MAX * 16];

    int        husk_p_tried;
    EmGfxMesh *husk_p_mesh;
    EmModel    husk_p_model;
    int        husk_p_has_model;
    uint32_t   husk_p_bones;
    float      husk_p_base[HUSK_PARTNER_BONE_MAX * 16];

    Enemy      e[ENEMY_SLOT_MAX];
    int        n;

    int        player_hit;   /* player-side damage mailbox (+0x36 shape) */

    /* gib layer (visual only; see the file header) */
    int        gib_tried;
    GibModel   gibm[GIB_FAMILY_N][GIB_FAM_FILES];
    int        gibm_n[GIB_FAMILY_N]; /* loaded models per husk family
                                      * (0 = that family fades only)     */
    /* egg/drum EXPLOSION fireball billboard (procedural quad — original
     * vertices, NOT disc data; the in-module stand-in for the additive
     * 0x80000013->0x8000006E sprite, "DEATH = EXPLOSION"). Built lazily
     * on the first egg/drum spawn; flash gibs draw it via the gib path. */
    int        flash_tried;
    EmGfxMesh *flash_mesh;
    float      flash_base[GIB_BONE_MAX * 16];   /* unit-quad identity pose */
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

    /* Last player position this module saw (em_enemy_update), for the
     * target-range test outside the update tick. */
    float      pp_last[3];
    int        pp_seen;
} s;

static void enemy_build_palette(Enemy *e);
static int  flash_mesh_get(EmGfx *gfx);   /* egg/drum fireball billboard */

void em_enemy_reset(void)
{
    /* Mesh/model survive a reset only through shutdown (mirrors
     * em_door_reset: boot resets once before adding). */
    memset(&s, 0, sizeof s);
    s.rng  = 0x2A5613u;   /* fixed LCG seed: deterministic runs/captures */
    s.demo = -1;   /* EM_ENEMY_GIBDEMO / EM_ENEMY_TEST legacy runs retired 2026-09-23 */
    const char *et = NULL;
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

    /* An area may override the shared crate model. Resolve that override
     * from the active scene, including normal New Game and room changes. */
    char scene_crate[sizeof enemy_scene_dir + 32];
    snprintf(scene_crate, sizeof scene_crate, "%s/props/enemy_crate.emdl",
             enemy_scene_dir);
    const char *path = NULL;
    FILE *sc = fopen(scene_crate, "rb");
    if (sc) fclose(sc);
    if (sc && em_model_load(&s.crate_model, scene_crate) == 0)
        path = scene_crate;
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

/* Load the AREA-11 door-husk CREATURE mesh once (first husk_creature
 * spawn): <scene>/props/area_husk_creature.emdl (model 0x1A — the
 * scripted creature carve, verified). No placeholder fallback: the
 * husk pair is AREA-11-specific scripted content with no procedural
 * stand-in, so a missing mesh skips the actor (logged) rather than
 * inventing a box. Static posed mesh — the scripted creature clips are
 * undecoded (frame-0 rest pose only). Returns 0 ok. */
static int husk_creature_mesh_get(EmGfx *gfx)
{
    if (s.husk_c_mesh) return 0;
    if (s.husk_c_tried) return -1;
    s.husk_c_tried = 1;
    char path[sizeof enemy_scene_dir + 40];
    snprintf(path, sizeof path, "%s/props/area_husk_creature.emdl", enemy_scene_dir);

    if (em_model_load(&s.husk_c_model, path) != 0) {
        fprintf(stderr, "enemy: %s not found — door-husk CREATURE "
                "skipped (AREA-11 scripted content, no placeholder)\n",
                path);
        return -1;
    }
    if (s.husk_c_model.bone_count > HUSK_CREATURE_BONE_MAX) {
        fprintf(stderr, "enemy: %s: %u bones > %d\n", path,
                s.husk_c_model.bone_count, HUSK_CREATURE_BONE_MAX);
        em_model_free(&s.husk_c_model);
        return -1;
    }
    s.husk_c_mesh = em_gfx_mesh_create(gfx, s.husk_c_model.verts,
                                       s.husk_c_model.vert_count,
                                       s.husk_c_model.indices,
                                       s.husk_c_model.index_count,
                                       (const EmGfxTexDesc *)
                                       s.husk_c_model.texs,
                                       s.husk_c_model.tex_count,
                                       s.husk_c_model.texels,
                                       s.husk_c_model.flags);
    if (!s.husk_c_mesh) {
        em_model_free(&s.husk_c_model);
        return -1;
    }
    s.husk_c_has_model = 1;
    s.husk_c_bones     = s.husk_c_model.bone_count;
    em_model_palette_at(&s.husk_c_model, 0, 0.0, s.husk_c_base);
    printf("husk creature model: %s — %u verts, %u tris, %u texture(s)\n",
           path, s.husk_c_model.vert_count,
           s.husk_c_model.index_count / 3, s.husk_c_model.tex_count);
    return 0;
}

/* Load the AREA-11 door-husk PARTNER mesh once (first husk_partner
 * spawn): <scene>/props/area_husk_partner.emdl (model 0x29 burst-husk-B,
 * verified). Same no-placeholder policy as the creature. Returns 0 ok. */
static int husk_partner_mesh_get(EmGfx *gfx)
{
    if (s.husk_p_mesh) return 0;
    if (s.husk_p_tried) return -1;
    s.husk_p_tried = 1;
    char path[sizeof enemy_scene_dir + 40];
    snprintf(path, sizeof path, "%s/props/area_husk_partner.emdl", enemy_scene_dir);

    if (em_model_load(&s.husk_p_model, path) != 0) {
        fprintf(stderr, "enemy: %s not found — door-husk PARTNER "
                "skipped (AREA-11 scripted content, no placeholder)\n",
                path);
        return -1;
    }
    if (s.husk_p_model.bone_count > HUSK_PARTNER_BONE_MAX) {
        fprintf(stderr, "enemy: %s: %u bones > %d\n", path,
                s.husk_p_model.bone_count, HUSK_PARTNER_BONE_MAX);
        em_model_free(&s.husk_p_model);
        return -1;
    }
    s.husk_p_mesh = em_gfx_mesh_create(gfx, s.husk_p_model.verts,
                                       s.husk_p_model.vert_count,
                                       s.husk_p_model.indices,
                                       s.husk_p_model.index_count,
                                       (const EmGfxTexDesc *)
                                       s.husk_p_model.texs,
                                       s.husk_p_model.tex_count,
                                       s.husk_p_model.texels,
                                       s.husk_p_model.flags);
    if (!s.husk_p_mesh) {
        em_model_free(&s.husk_p_model);
        return -1;
    }
    s.husk_p_has_model = 1;
    s.husk_p_bones     = s.husk_p_model.bone_count;
    em_model_palette_at(&s.husk_p_model, 0, 0.0, s.husk_p_base);
    printf("husk partner model: %s — %u verts, %u tris, %u texture(s)\n",
           path, s.husk_p_model.vert_count,
           s.husk_p_model.index_count / 3, s.husk_p_model.tex_count);
    return 0;
}

/* Load the BUG hatchling mesh + clips once (first bug or crate spawn —
 * the crate burst hatches bugs inside em_enemy_update, without a gfx
 * handle, so the crate add preloads this): assets/enemy_bug.emdl =
 * the s68 variant-A export (global slot 0x0F + clip bank 0x11), else
 * a PLACEHOLDER flat box bug at the authored footprint (runtime-
 * generated, original vertices, NOT disc data). Clip resolution: walk 1
 * (the init clip func_00128C10's live sub 0 stands on), flinch 0x1D and
 * COLLAPSE 0x20 with knockdown 0x1B as the fallback — the split
 * CORRECTED off func_00129FC0 (case 0 picks 0x1B/0x1D, case 3 plays
 * 0x20); both death candidates are absent from the current export
 * (unbakeable container, s68) and resolve -1, which the death path
 * treats as "fade fallback". Returns 0 ok. */
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
        /* CORRECTED (func_00129FC0 — see BUG_CLIP_DEATH): the corpse's
         * own animation is the case-3 COLLAPSE clip 0x20; the old 0x1B
         * is the case-0 KNOCKDOWN reaction. Ask for 0x20, fall back to
         * 0x1B, then to the frozen-pose fade. */
        s.bclip_death  = em_model_clip_index(&s.bug_model, BUG_CLIP_DEATH);
        if (s.bclip_death < 0)
            s.bclip_death = em_model_clip_index(&s.bug_model,
                                                BUG_CLIP_KNOCKDN);
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
               s.bclip_death < 0 ? " (neither collapse 0x20 nor knockdown "
                                   "0x1B exported — corpse-fade death)"
                                 : "");
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
     * loop if the asset lacks it; a bug spawn enters init/walk clip 1
     * directly — the clip func_00128C10's live sub 0 stands on
     * (NEARMISS; the bug's own identity is NOT pinned to that brain, see
     * the file header's BUG block, so treat this as the best available
     * mapping rather than a settled decode). */
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
           : kind == EM_ENEMY_KIND_BUG ? "bug"
           : kind == EM_ENEMY_KIND_HUSK_CREATURE ? "husk_creature (staged-inert)"
           : kind == EM_ENEMY_KIND_HUSK_PARTNER ? "husk_partner (staged-inert, shootable)"
           : "crawler",
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
    if (kind == EM_ENEMY_KIND_HUSK_CREATURE) {
        /* AREA-11 door-husk scripted creature (ov 0x825940, model 0x1A):
         * a STAGED-INERT scripted actor — spawns and holds an inert idle
         * (no aggro, no movement, no attack) so the opening stays
         * enemy-free. NOT shootable. The engine spawns one child via
         * func_001AFA90 (param 0x7A) — a SEPARATE undecoded mesh, FLAGGED:
         * the port does NOT invent it (em_enemy.h kind 4). */
        if (s.n >= ENEMY_SLOT_MAX) return -1;
        if (husk_creature_mesh_get(gfx) != 0) return -1;
        return enemy_spawn(EM_ENEMY_KIND_HUSK_CREATURE, pos, yaw);
    }
    if (kind == EM_ENEMY_KIND_HUSK_PARTNER) {
        /* AREA-11 door-husk SHOOTABLE husk (ov 0x827490, model 0x29):
         * HP 1, shootable, STAGED-INERT (no aggro/movement/attack). On
         * its damage mailbox it bursts with FX 0x80000045 (flagged) —
         * reuse the husk-B gib/explosion path, so preload the gib set +
         * fireball billboard now while a gfx handle is held (the burst
         * runs in em_enemy_update without one). */
        if (s.n >= ENEMY_SLOT_MAX) return -1;
        if (husk_partner_mesh_get(gfx) != 0) return -1;
        gib_models_load(gfx);     /* husk-B debris (idempotent)          */
        flash_mesh_get(gfx);      /* burst flash quad (logged if absent) */
        return enemy_spawn(EM_ENEMY_KIND_HUSK_PARTNER, pos, yaw);
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
 * decoded; the whitelist and the +0x52 gate CONFIRMED 2026-07-31 against
 * func_001551B0 state 4): the engine walks the WHOLE live actor list D_00275BC0 —
 * no radius — and sets +0x0A on every actor with a placed-crawler
 * model byte {6, 0x1C, 0x1E, 0x1F, 0x50} and the on-surface flag
 * (+0x52). Natively: every live CRATE (the placed-crawler kind);
 * worms and bugs are NOT whitelisted (the bug models 0x0F/0x10 sit
 * outside the placed-crawler set) and never read the flag. */
static void enemy_alarm_broadcast(void)
{
    /* Decoded list-wide, no-radius wake (func_001551B0 state-4
     * broadcast, CONFIRMED 2026-07-31) — but the engine reaches its +0x0A = 1 byte store (the wake) ONLY
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

/* Consume the +0x36 mailbox — the CRATE's IDLE poll (decoded
 * func_001551B0 state 4: the test is `+0x36 != 0`; HP 1 makes any
 * nonzero value lethal — see the CORRECTION inside the function,
 * 2026-07-31) and the BUG's every-tick poll.
 *
 * AMOUNT = the LOW BYTE of +0x36 — CORRECTED (was `& 0x1FFF`). The
 * bug's reaction driver func_00129FC0 (BYTE-MATCHED) reads the amount
 * as `dec = *(unsigned char *)(arg0 + 0x36)` and does
 * `+0x34 -= dec`, so bits 0x0100..0xFF00 are FLAGS, not amount:
 * 0x4000 = play the hurt voice (arming a 60-frame cooldown at +0x28),
 * 0x2000 = force the knockdown branch. (The other canonical hurt
 * helper, func_00153B50, subtracts the whole halfword — but that one
 * drives a different actor family; the bug's own handler is the byte
 * form, and every port kind but the bug is HP 1, where the mask is
 * moot.) The 0x2000 knockdown flag itself is UNTRANSLATED here.
 *
 * The WORM never reaches this: its brain consumes nothing (J2 CLOSED
 * s66 — the old every-tick worm poll, the shootable stand-in, is
 * REMOVED). The subtractive shape is the canonical hurt helper
 * func_00153B50 (BYTE-MATCHED), whose death arm plays 0x7D8 and whose
 * survive arm plays 0x7D4. CORRECTED 2026-07-31: the BUG no longer
 * borrows those — its own driver func_00129FC0 plays 0x1B1 on every
 * reaction and 0x1B7 at the collapse, and the port now does too. 0x7D8
 * remains only for the husk partner (its audio really is undecoded). */
static int enemy_mailbox_poll(Enemy *e, const float pp[3])
{
    if (e->mailbox == 0) return 0;
    /* CORRECTED 2026-07-31 (audit): the HP-1 PROP brains do NO arithmetic
     * at all. func_001551B0 state 4 (the placed crawler) and
     * func_00156620 state 1 (the drum) both open with a bare
     *     if (*(short *)(actor + 0x36) != 0) { ...burst... }
     * and never look at +0x34 or decompose the word — so a damage word
     * whose LOW BYTE happens to be 0 (a pure flag code such as 0x2000 /
     * 0x4000) still destroys the prop. Subtracting the byte first, as
     * this poll used to do for every kind, left such a prop alive with
     * HP 1. Only the BUG runs the subtractive path, and that one really
     * is the byte: func_00129FC0 case 0 does
     *     dec = *(unsigned char *)(actor + 0x36);  +0x34 -= dec;
     * (bits 0x0100..0xFF00 are flags: 0x4000 = hurt voice, 0x2000 =
     * force the knockdown branch). */
    const int prop = e->kind == EM_ENEMY_KIND_CRATE ||
                     e->kind == EM_ENEMY_KIND_HUSK_PARTNER;
    int amount = e->mailbox & 0xFF;    /* func_00129FC0: byte load @0x36 */
    e->mailbox = 0;
    if (prop)
        e->hp = 0;                     /* any nonzero word is lethal    */
    else
        e->hp = (int16_t)(e->hp - amount);
    /* DECODED (func_00129FC0 case 0), CONFIRMED 2026-07-31: the bug's
     * reaction entry plays 0x1B1 unconditionally, on survivable hits as well as lethal ones —
     * the port had no hurt sound at all here. */
    if (e->kind == EM_ENEMY_KIND_BUG)
        em_sfx_play_at(BUG_SFX_HURT, e->pos, 300.0f);
    if (e->hp > 0) return 0;
    if (e->kind == EM_ENEMY_KIND_CRATE) {
        /* the placed crawler plays NOTHING here: func_001551B0 state 4
         * hands straight to state 2 without touching the shared hurt
         * helper, and state 2 sub 0 is what plays the variant's burst
         * sound (0x19D / 0x19E — crate_burst below). The generic 0x7D8
         * this arm used to play for a crate was never in the engine's
         * path. */
    } else if (e->kind == EM_ENEMY_KIND_BUG) {
        /* CORRECTED 2026-07-31: the bug does NOT use the generic 0x7D8
         * hurt-helper death. Its own driver func_00129FC0 (BYTE-MATCHED)
         * plays 0x1B7 with the collapse clip 0x20 in case 3 — see the
         * BUG_SFX_* block. (0x1B5, the case-4 free, is UNTRANSLATED: the
         * port frees in the same tick.) */
        em_sfx_play_at(BUG_SFX_COLLAPSE, e->pos, 300.0f);
    } else {
        /* 0x7D8 — engine func_00153B50 plays it positional at the dying
         * actor: play_sound(actor, 0x7D8, 0, 300.0) (radius read off the
         * call site's f12 = 0x43960000). This arm is now only the HUSK
         * PARTNER, whose own death audio is undecoded — flagged
         * stand-in. */
        em_sfx_play_at(EM_SFX_ENEMY_DEATH, e->pos, 300.0f);
    }
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
                   : e->kind == EM_ENEMY_KIND_HUSK_CREATURE ? s.husk_c_bones
                   : e->kind == EM_ENEMY_KIND_HUSK_PARTNER  ? s.husk_p_bones
                                                    : s.bone_count;

    /* CRATE IDLE jitter — the DECODED cycle (func_001551B0 state 4,
     * CONFIRMED 2026-07-31; the
     * full derivation sits at CRATE_JIT_* in the constants block). The
     * counter is stepped once per tick by enemy_tick; here we only render
     * the step it selected. A wiggle frame is an ODD counter value below
     * 9, and the table column is (counter - 1) >> 1 — so the burst runs
     * columns 3, 2, 1, 0 on counters 7, 5, 3, 1 with a clean frame
     * between each, and counter 0 restores the base pose. Amplitudes and
     * the column shape are flagged port constants; the row (0..6) picks a
     * bearing so different crates shudder differently. The engine runs
     * this in STATE 4 ONLY — an alarmed crate hops instead. */
    if (e->kind == EM_ENEMY_KIND_CRATE && e->active &&
        e->state == EM_ENEMY_IDLE &&
        e->jit_t > 0 && e->jit_t < 9 && (e->jit_t & 1)) {
        int   col = (e->jit_t - 1) >> 1;              /* decoded index   */
        float amp = CRATE_JIT_COL[col];
        float ang = (float)e->jit_row * 0.8975979f;   /* PORT: row -> a
                                                       * bearing around
                                                       * the crate       */
        float sgn = (e->jit_row & 1) ? -1.0f : 1.0f;
        x   += sgn * amp * CRATE_JIT_POS * sinf(ang);
        z   += sgn * amp * CRATE_JIT_POS * cosf(ang);
        yaw += sgn * amp * CRATE_JIT_YAW;
    }

    /* DRUM IDLE: NOTHING. The old "procedural wobble" here was a PORT
     * INVENTION and is REMOVED (2026-07-31 fidelity audit). The
     * recovered func_00156620 state 1 (armed idle) is exactly:
     *
     *     if (+0x36) { ...burst... }
     *     (*(actor+0x4C))(actor);                 // render hook
     *     v = player(D_00810350) - pos; d2 = |v|^2;
     *     if (d2 <= 50*50) { actor[1] = 1; func_001B1D20(actor); }
     *     else               func_001B17A0(actor);
     *
     * — no matrix touch, no phase, no jitter counter. The +0x74 field
     * and the D_00246A00 / D_00246A10 tables the old comment cited as
     * "wobble amplitudes" are read in STATE 2 phase 0 (after the
     * explosion) as the flung-debris AIM ANGLE, SPEED (+0x38) and PITCH
     * (+0x78) — they have nothing to do with an idle. An engine drum is
     * a perfectly still prop. */

    const float c = cosf(yaw), sn = sinf(yaw);

    if (e->kind == EM_ENEMY_KIND_CRATE) {
        memcpy(e->palette, s.crate_base, bones * 16 * sizeof(float));
    } else if (e->kind == EM_ENEMY_KIND_HUSK_CREATURE) {
        /* door-husk scripted creature: static rest pose (no anim layer —
         * the scripted clips are undecoded). No idle jitter: the engine
         * actor is a staged scripted creature, not a wiggling prop. */
        memcpy(e->palette, s.husk_c_base, bones * 16 * sizeof(float));
    } else if (e->kind == EM_ENEMY_KIND_HUSK_PARTNER) {
        /* door-husk shootable husk: static rest pose (no anim layer). */
        memcpy(e->palette, s.husk_p_base, bones * 16 * sizeof(float));
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

    /* WORM RENDER SCALE — a FLAGGED PORT constant (see the DOWNGRADE at
     * ENEMY_ACTOR_SCALE: actor +0x80..+0x8C is the RGBA multiplier, not
     * a scale, so the decomp gives no scale here). Kept because the
     * port's lunge geometry is tuned around it — the authored-size whip
     * arced the lunge over the player's head and the latch segment could
     * never connect. */
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

/* World palette of one gib: the model's identity base pose scaled by
 * g->scale, rotated by the tumble yaw and translated to the instance
 * position (the same column rotation + translate composition as
 * enemy_build_palette). Debris gibs run scale 1; the egg/drum FLASH gib
 * uses the expanding billboard mesh and ramps the scale (the fireball). */
static void gib_build_palette(Gib *g)
{
    const float *base = g->flash ? s.flash_base
                                 : s.gibm[g->fam][g->model].base;
    const uint32_t bones = g->flash ? 1u
                                    : s.gibm[g->fam][g->model].bone_count;
    const float c  = cosf(g->yaw), sn = sinf(g->yaw);
    const float sc = g->scale != 0.0f ? g->scale : 1.0f;

    memcpy(g->palette, base, bones * 16 * sizeof(float));
    for (uint32_t b = 0; b < bones; b++) {
        float *m = g->palette + b * 16;
        for (int col = 0; col < 3; col++) {   /* basis cols only (not xlate) */
            float x = m[col * 4 + 0], z = m[col * 4 + 2];
            m[col * 4 + 0] = (c * x + sn * z) * sc;
            m[col * 4 + 2] = (-sn * x + c * z) * sc;
            m[col * 4 + 1] *= sc;
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

        /* The engine's quarter-turn draw, corrected against the decode
         * (func_001551B0 state 2 sub 0, damage-kill arm): it draws
         * r = 0..3 and rotates by r * 90 deg — case 0 is a real outcome
         * (no rotation), so the set is {0, 90, 180, 270}, four ways, not
         * three. What it rotates is the CORPSE's own world matrix: the
         * engine builds an identity in the scratch matrix D_700036E0,
         * turns it by the draw, multiplies the actor transform by it and
         * restores the render position — a random quarter-turn of the
         * husk. It sets NO velocity and runs NO corpse slide (the old
         * "knockback along the RNG-rotated hit vector" reading does not
         * survive the decompilation). The port keeps using the draw as a
         * scatter bearing off the hit vector, plus the flagged +-30 deg
         * jitter — a PORT visual, now flagged as such. */
        float ang = atan2f(e->hit_dir[0], e->hit_dir[1])
                  + GIB_ROT_STEP * (float)(gib_rng() % 4)
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
        g->scale  = 1.0f;
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

/* Build the FLASH billboard mesh once (the fireball stand-in): a unit
 * camera-agnostic double-sided quad in the XY plane, centered on the
 * origin (gib_build_palette scales it to the blast radius and the gib
 * pos puts it at the drum). Original vertices, NOT disc data — the same
 * runtime-geometry contract as the placeholder boxes. Returns 0 on
 * success, -1 once tried-and-failed (no asset, no retry). NOTE: this is
 * an axial quad, not a true camera-facing billboard (the enemy draw
 * chain has no per-draw camera-orientation hook); FLAGGED — the engine's
 * 0x8000006E sprite is a real additive billboard, see "DEATH = EXPLOSION".
 */
static int flash_mesh_get(EmGfx *gfx)
{
    if (s.flash_mesh) return 0;
    if (s.flash_tried) return -1;
    s.flash_tried = 1;

    /* a unit cube (+-0.5) of glowing geometry — box_emit gives a
     * watertight 6-face box (24 verts / 36 indices); reading as a bright
     * volume from any angle is a fair fireball stand-in (no texture: the
     * fireball color comes entirely from the per-draw RGBA tint) */
    static const float lo[3] = { -0.5f, -0.5f, -0.5f };
    static const float hi[3] = {  0.5f,  0.5f,  0.5f };
    float    verts[24 * 10];
    uint32_t indices[36];
    uint32_t nv = 0, ni = 0;
    box_emit(verts, &nv, indices, &ni, lo, hi);

    s.flash_mesh = em_gfx_mesh_create(gfx, verts, nv, indices, ni,
                                      NULL, 0, NULL, 0);
    if (!s.flash_mesh) return -1;
    mat4_identity(s.flash_base);
    printf("enemy egg/drum: explosion fireball = runtime billboard quad "
           "(in-module stand-in for the engine's additive 0x8000006E "
           "sprite — em_weapon.c's .emtx beam queue is out of reach)\n");
    return 0;
}

/* Egg/drum EXPLODE (func_00156620 model-0x18 death — INVESTIGATION
 * "DEATH = EXPLOSION"): the drum vanishes behind a fireball + debris
 * spray. PURELY VISUAL — no damage, no chain (the caller applies none).
 * Spawns ONE expanding fireball flash gib at (x, y+7, z) and 5-8 debris
 * chunks flung RADIALLY (omnidirectional — a drum bursts every way, not
 * along a single hit vector). Reuses the gib pool + gib_update 1:1; the
 * debris come from the grey-cyan husk-B set (the metal/industrial family
 * — FLAGGED: no drum-specific shard set is exported, the crate's husk-B
 * stands in). Returns the number of gib slots claimed (flash + debris). */
static int egg_explode(const Enemy *e)
{
    int budget = ENEMY_SLOT_MAX - s.n;
    int claimed = 0;

    /* (1) the fireball FLASH — one expanding billboard at the blast
     * origin (drum XZ, Y+7). Needs the billboard mesh; if it didn't
     * build, the flash is skipped and only the debris fly. */
    if (s.flash_mesh) {
        for (int k = 0; k < budget; k++) {
            Gib *g = &s.gib[k];
            if (g->active) continue;
            memset(g, 0, sizeof *g);
            g->active = 1;
            g->flash  = 1;
            g->pos[0] = e->pos[0];
            g->pos[1] = e->pos[1] + EGG_BLAST_LIFT;
            g->pos[2] = e->pos[2];
            g->scale  = EGG_FLASH_R0;
            g->life   = EGG_FLASH_FRAMES;
            g->y0     = e->pos[1];
            g->tint[0] = EGG_FLASH_RGB_R;
            g->tint[1] = EGG_FLASH_RGB_G;
            g->tint[2] = EGG_FLASH_RGB_B;
            g->tint[3] = 1.0f;
            gib_build_palette(g);
            if (k >= s.gib_tail) s.gib_tail = k + 1;
            claimed++;
            break;
        }
    }

    /* (2) DEBRIS — 5-8 metal chunks flung radially under gravity. Uses
     * the husk-B (grey-cyan/metal) family; if no gib models loaded, the
     * debris are skipped (flash only). */
    int want = EGG_DEBRIS_MIN + (int)(gib_rng() % EGG_DEBRIS_SPAN);
    if (s.gibm_n[GIB_FAM_B] > 0) {
        int flung = 0;
        for (int k = 0; k < budget && flung < want; k++) {
            Gib *g = &s.gib[k];
            if (g->active) continue;

            /* radial spray: an even fan around the drum + RNG jitter,
             * so chunks scatter every direction (a drum, not a directed
             * crate-corpse knockback) */
            float ang = ((float)flung / (float)want) * (2.0f * ENEMY_PI)
                      + (float)((int)(gib_rng() % (2 * GIB_JITTER_DEG + 1))
                                - GIB_JITTER_DEG) * (ENEMY_PI / 180.0f);

            memset(g, 0, sizeof *g);
            g->active = 1;
            g->fam    = GIB_FAM_B;
            g->model  = (s.gibm_n[GIB_FAM_B] == 1)
                        ? 0 : (int)(s.gib_next++ %
                                    (unsigned)s.gibm_n[GIB_FAM_B]);
            g->pos[0] = e->pos[0];
            g->pos[1] = e->pos[1] + GIB_LAUNCH_LIFT;
            g->pos[2] = e->pos[2];
            g->vel[0] = sinf(ang) * GIB_SPEED;
            g->vel[1] = GIB_VY;
            g->vel[2] = cosf(ang) * GIB_SPEED;
            g->yaw    = ang;
            g->spin   = ((float)(gib_rng() % 2001) / 1000.0f - 1.0f)
                        * GIB_SPIN_MAX;
            g->scale  = 1.0f;
            g->y0     = e->pos[1];
            g->tint[0] = g->tint[1] = g->tint[2] = g->tint[3] = 1.0f;
            gib_build_palette(g);
            if (k >= s.gib_tail) s.gib_tail = k + 1;
            flung++;
            claimed++;
        }
    }
    return claimed;
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
        if (g->flash) {
            /* the egg/drum fireball: expand R0 -> R1 and fade the alpha
             * out over its life, then free. Stationary (no ballistic
             * integration). The in-module additive-sprite stand-in. */
            float t = (float)g->age / (float)g->life;
            if (t > 1.0f) t = 1.0f;
            g->scale   = EGG_FLASH_R0 + (EGG_FLASH_R1 - EGG_FLASH_R0) * t;
            g->tint[3] = 1.0f - t;                 /* fade as it expands */
            if (g->age >= g->life) {
                g->active = 0;
                continue;
            }
            gib_build_palette(g);
            tail = k + 1;
            continue;
        }
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
 * NOT disc data — CONFIRMED 2026-07-31: the engine's pad is a procedural
 * 8x8 membrane lattice,
 * func_001E9580 init + func_001E9E60 render, and binds NO model-table
 * entry, so there is nothing to export; the decoded lattice and why the
 * mound stays are documented at GEN_PAD_HEIGHT): a low three-tier mound
 * on a UNIT footprint (+-1), scaled per instance by the config extents in
 * gen_build_palette — a scaling the engine's own spacing confirms. */
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
        /* CORRECTED: BASE + SPAN * rand01 — the engine's one-sided draw
         * (func_001549C0 case 0), NOT BASE +- SPAN. See TF_RING_BASE. */
        float r  = TF_RING_BASE[t->pair] +
                   (float)(gib_rng() % 65536u) / 65536.0f *
                   TF_RING_SPAN[t->pair];
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
        /* CORRECTED 2026-07-31 (audit): the engine does NOT ramp on the
         * trigger tick. func_001549C0 case 0 ends in a plain `break` —
         * it only scatters, arms +0x28 = 8 and bumps the sub-state — and
         * the shared tail then renders, because `if (s7[5] != 0)
         * func_00154F00(s7)` already sees the new sub. The first
         * ramp step lands on the NEXT tick, in case 1. The old
         * fallthrough deployed one tick early. */
        break;
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

    /* INIT mode draw (func_0015A2C0 state 0), CONFIRMED 2026-07-31:
     * link 1/2 pulls one byte
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
         * the standing player — engine (func_001A8840, recovered): with
         * the pad's OPEN flag +0x0B set it writes 5.0f into the PLAYER's
         * +0x22C and forces the player state byte to 3. PORT: a one-shot
         * mailbox write per box entry (flagged stand-in). */
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

    /* DECODED (func_001551B0 state 2 sub 0), CONFIRMED 2026-07-31: the
     * burst opens with the
     * variant's positional sound — 0x19D for model byte 6 / 0x1E, 0x19E
     * for 0x1C / 0x50 / 0x1F — played through func_001FC580, whose own
     * range argument is 300.0. The paired gore effects (CRATE_FX_* in the
     * constants block) are recorded but not reproduced: the particle
     * chain lives outside this module, so the gibs below remain the
     * visible burst (flagged). */
    em_sfx_play_at((e->variant == 6 || e->variant == 0x1E)
                   ? CRATE_SFX_BURST_A : CRATE_SFX_BURST_B,
                   e->pos, 300.0f);

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
 * The DECODED piece is the mailbox chain only: func_00128B80 routes ANY
 * nonzero +0x36 into the reaction state without looking at HP, and
 * func_00129FC0 case 0 then debits the low byte of the mailbox from
 * +0x34 and picks flinch / knockdown / death. The port folds that into
 * enemy_tick (sub=1 on a survivable hit, DEATH when HP runs out).
 *
 * EVERYTHING BELOW IS A PORT CONSTRUCTION (corrected 2026-07-31). The
 * old note claimed the bite was func_0012C490 clip 0x13 resolved by "the
 * shared melee resolver func_001B5360, a radius-6 sphere ~10 u ahead".
 * Both halves are refuted by the recovered C: func_001B5360 is the
 * shared GROUND-SNAP probe (vertical, +10 Y up / 30 down, mask 6 — see
 * the CORRECTION block at BUG_CONTACT_FWD), and func_0012C490 is a
 * 9-state LEAP chain (clip 0x13 with a body hop and sfx 0x1AE, then
 * clips 0x14/0x15 and a spin) that contains no contact test.
 * CORRECTED AGAIN 2026-07-31: the follow-up "the two real brains
 * func_00128C10 / func_0012A5D0 are still undecompiled stubs" is ALSO
 * false — both are recovered now (NEARMISS). func_00128C10's live state
 * is a sense-then-WANDER machine (wake at 100 u, lose interest past
 * 150 u after 90 ticks, commit at 24 u by turning to a RANDOM heading at
 * 0.34906587 rad/tick, then walk at speed 0.8 / turn 0.06981317 for a
 * table-drawn count) and func_0012A5D0 belongs to a different actor
 * family. Neither is pinned to the crate hatchling, so nothing from them
 * is wired in — see the file header's BUG block for the full read-out.
 * So the approach/bite shape, its timers, the contact box and
 * BUG_BITE_DMG remain PORT choices, flagged at their definitions.
 *
 * sub: 0 APPROACH, 1 FLINCH (set by enemy_tick's mailbox path — keep
 * this id), 2 WINDUP, 3 BITE, 4 RECOVER, 5 LATCHED. */
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
        /* PORT contact box (NOT func_001B5360 — that one is the shared
         * vertical GROUND-SNAP probe, see the CORRECTION at
         * BUG_CONTACT_FWD): the bug position pushed +BUG_CONTACT_FWD
         * ahead, BUG_CONTACT_R sphere vs the player. At the ~5u standoff
         * |5-10| = 5 <= 6, so the forward box reaches the player. NO
         * position change. On a hit the bug LATCHES — an OBSERVED
         * behaviour (2026-06-12 play; provenance re-checked 2026-07-31),
         * not a decoded one: it does not
         * bite-and-recover, it clings until shaken off. */
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

/* CRATE attack — the decoded func_001551B0 state 1 (re-verified
 * 2026-07-31): a BLIND suicide
 * hop-run (no player reference exists in the engine's state 1; heading
 * = probe-steered + RNG). The mailbox is NEVER polled here: damage
 * taken mid-run stays pending (kills on the next IDLE tick if the run
 * returns there) and is ABSORBED (+0x36 cleared) when the run ends in
 * the burst — both CONFIRMED 2026-07-31 in func_001551B0: state 1 reads
 * only +0x2A/+0x28/+0x03, and its sub-1 exit writes `+0x36 = 0` before
 * setting state 2. Port locomotion
 * stand-in (flagged): repeated hops with a re-steer on landing; the
 * engine launches one long leap. The attack timer +0x2A = 180 is armed
 * once at the first launch (the engine reuses +0x2A; the port splits
 * the field because of the repeated hops). */
static void crate_attack_tick(const EmCollision *coll, Enemy *e)
{
    if (e->atk_armed && --e->retreat < 0) {
        /* the suicide-run timer expired. (The engine's other burst arm
         * is the in-flight probe returning 4 — func_0019AB20's STATIC
         * WORLD channel, i.e. the leap struck level geometry, NOT the
         * "surface lost" this comment used to claim. The port's floor
         * query falls back instead of failing, so the timer is the
         * port's only burst arm.) Absorb pending damage and burst. */
        e->mailbox    = 0;          /* decoded, CONFIRMED 2026-07-31:
                                     * func_001551B0 state 1 sub 1 clears
                                     * +0x36 before handing to state 2   */
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
        /* decoded corner radius: 6.5 for the wooden crate (model byte 6)
         * and its 0x1E sibling, 3.0 for the small variants */
        float reach = (e->variant == 6 || e->variant == 0x1E)
                      ? ENEMY_PROBE_LEN : ENEMY_PROBE_LEN_SMALL;
        for (int k = 0; k < 4; k++) {
            bl[k] = enemy_probe(coll, e, e->yaw + diag[k], reach);
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
        /* launch the hop; the first launch arms the run timer. DECODED
         * split: model byte 6 (the wooden crate — the port's default
         * variant) gets 60 * world-Y / 12, every other variant gets the
         * 0xB4 = 180 constant. The old blanket 180 was wrong for exactly
         * the crate the port ships. */
        e->vy     = ENEMY_HOP_VY;
        e->hop_y0 = e->pos[1];
        if (!e->atk_armed) {
            e->atk_armed = 1;
            if (e->variant == 6) {
                /* the engine converts to int and stores into the s16
                 * +0x2A — keep the same truncation */
                float rt = ENEMY_ATTACK_Y_NUM * e->pos[1] /
                           ENEMY_ATTACK_Y_DEN;
                if (rt >  32767.0f) rt =  32767.0f;
                if (rt < -32768.0f) rt = -32768.0f;
                e->retreat = (int)(int16_t)(int)rt;
            } else {
                e->retreat = ENEMY_ATTACK_TICKS;   /* +0x2A = 0xB4 */
            }
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

/* THE LATCH QUERY — the port's stand-in for the engine's
 * func_0019AA80(a, b, 0x20) resolve arm. Returns 1 when the staged
 * segment crosses the player's hit volume.
 * PROVENANCE (DOWNGRADED 2026-07-31, see ENEMY_LATCH_NODE_A): only the
 * SHAPE is source-derived — func_0019AA80 stages two points and hands a
 * masked id to func_001A7280. The endpoints func_00154120 actually
 * passes come from the global D_00275B40 (+0x34 / +0x40, each +0xC0),
 * NOT from the worm's rig; "neck->head nodes 13/16" is the s66 LIVE
 * reading. The port stages the segment from the worm's OWN animated
 * palette nodes 13/16 (one tick stale) — a flagged PORT choice. Needs
 * the leech asset's rig; without it (placeholder mesh / static base)
 * returns -1 so the caller folds the arm into the port's own contact
 * sphere (flagged fallback).
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
 * header; sub windows, homing rate, snap+0x431 and the two resolve arms
 * all CONFIRMED 2026-07-31 against the recovered C). The brain consumes
 * NO damage: the old shootable-worm
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
            e->t28 = ENEMY_STALK_TICKS;       /* +0x28 = 0x78 — decoded,
                                              * func_00154120 case 0    */
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
         * trivial contact-sphere range — CONNECTING is the lunge
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
            /* decoded (func_00154120 case 2, CONFIRMED 2026-07-31):
             * snap the yaw to the player bearing + sound 0x431, then
             * lunge */
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
        /* CONNECT — the engine's two resolve arms, in func_00154120
         * case 3's order:
         * 1. the func_0019AA80 SEGMENT query (worm_latch_segment above)
         *    -> burst + the lunge latch D_008104D4 = 15.0 (CONFIRMED
         *    2026-07-31)
         *    through the player mailbox bridge (0x4000 = the bridge's
         *    health route);
         * 2. else func_0019A570(from, to, 6, 0) -> burst with NO latch
         *    (the engine writes no damage on this arm). The 6 is that
         *    function's channel MASK, not a radius — the port's
         *    ENEMY_CONTACT_R sphere is its own stand-in, see the
         *    CORRECTION there.
         * Rig-less fallback (worm_latch_segment -1): both arms fold
         * into the port's contact sphere CARRYING the 15 (flagged — the
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
             * worm's ONLY +0x36 access (func_001AFC10's +0x36 = 0
             * halfword store — s66): clear any unconsumed write with it. */
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
             * the probe corner table and resolves the nest registry).
             * The jitter counter is armed to -1 exactly as INIT does
             * (+0x238 = -1, +0x23C = 0), so the first IDLE tick rattles
             * and draws the first wait — but only for a crate that still
             * owes its nest group a child (the +0x0E bit-0 gate INIT
             * clears when the group is spent — CONFIRMED 2026-07-31). */
            e->hp      = ENEMY_HP_CRATE;
            e->jit_t   = -1;
            e->jit_row = 0;
            e->state   = EM_ENEMY_IDLE;
        } else if (e->kind == EM_ENEMY_KIND_HUSK_CREATURE) {
            /* door-husk scripted creature (ov 0x825940): STAGED-INERT at
             * the opening. The engine actor runs its own timer/proximity
             * machine (states 0..4 + the 0x64 scripted timer), but the
             * opening beat keeps it inert — so the port stages it directly
             * to a held IDLE: NO HP (not shootable), NO player reference,
             * NO aggro, NO movement, NO attack. It just stands by the
             * door. (The scripted-timer wake schedule is undecoded —
             * FLAGGED; the port holds the inert opening state.) */
            e->hp    = 0;
            e->state = EM_ENEMY_IDLE;
        } else if (e->kind == EM_ENEMY_KIND_HUSK_PARTNER) {
            /* door-husk shootable husk (ov 0x827490): HP 1, STAGED-INERT.
             * It only reacts to being shot (poll +0x36 in IDLE) — no
             * aggro/movement/attack at the opening. */
            e->hp    = HUSK_PARTNER_HP;
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
        if (e->kind == EM_ENEMY_KIND_HUSK_CREATURE) {
            /* door-husk scripted creature: STAGED-INERT — it does NOTHING
             * at the opening. NOT shootable (no mailbox poll), NO aggro,
             * NO proximity test, NO movement. The engine's own scripted
             * timer/proximity wake schedule (func_00825900 / func_001B0FD0,
             * states 0..4 / 0x64) is undecoded — FLAGGED; the port holds
             * the inert opening state so the opening stays enemy-free
             * (INVESTIGATION_first_level_area11 §5.2). */
            break;
        }
        if (e->kind == EM_ENEMY_KIND_HUSK_PARTNER) {
            /* door-husk shootable husk IDLE: poll +0x36 ONLY — HP 1 makes
             * any nonzero hit lethal -> burst. NO alarm broadcast / wake
             * (it is not in the placed-crawler whitelist), NO proximity,
             * NO player reference: STAGED-INERT, only reacts to being
             * shot. (Same passive-destructible shape as the egg, minus the
             * idle wobble — the husk is a scripted-set actor, not a
             * wiggling prop.) */
            if (enemy_mailbox_poll(e, pp))
                e->state = EM_ENEMY_DEATH;
            break;
        }
        /* crate only below — worms never idle. Decoded state 4: the
         * mailbox is the ONLY direct trigger (no proximity test exists);
         * a kill broadcasts the group alarm to the placed-crawler kind;
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
            break;
        }
        /* DISGUISE JITTER counter (decoded — CRATE_JIT_* block). Runs
         * only while the crate still owes its nest group a child: INIT
         * clears the enabling flag bit when the group has no untriggered
         * record left, so a gore-only crate never moves at all. Counter
         * expired -> rattle (sound 0x19C at range 300) and redraw the
         * wait and the table row; otherwise just walk it down. The
         * render step reads it in enemy_build_palette. */
        if (e->children > 0) {
            if (e->jit_t < 0) {
                em_sfx_play_at(CRATE_SFX_IDLE, e->pos, 300.0f);
                e->jit_t   = (int16_t)(gib_rng() % (uint32_t)CRATE_JIT_WAIT);
                e->jit_row = (uint8_t)(gib_rng() % (uint32_t)CRATE_JIT_ROWS);
            } else {
                e->jit_t--;
            }
        }
        break;

    case EM_ENEMY_ATTACK:
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            /* decoded, CONFIRMED 2026-07-31: func_001551B0 state 1
             * never reads +0x36 (damage defers) */
            crate_attack_tick(coll, e);
        } else if (e->kind == EM_ENEMY_KIND_BUG) {
            /* the bug consumes the mailbox EVERY tick. DECODED, with
             * the split corrected: func_00128B80 only ROUTES any nonzero
             * +0x36 into the reaction state (no HP compare lives there);
             * func_00129FC0 case 0 is what debits the low byte from
             * +0x34 and picks flinch clip 0x1D vs the death event
             * 0x8000000C. The port collapses both into this poll. */
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
             * the engine's teardown-only +0x36 = 0 store */
            worm_attack_tick(coll, e, pp);
        }
        break;

    case EM_ENEMY_DEATH:
        if (e->kind == EM_ENEMY_KIND_CRATE) {
            crate_burst(e, pp);   /* husk gibs + the bugs (file header) */
            break;
        }
        if (e->kind == EM_ENEMY_KIND_HUSK_PARTNER) {
            /* door-husk shootable husk DEATH (ov 0x827490, model 0x29):
             * the engine bursts with gore FX 0x80000045 (HUSK_PARTNER_FX_
             * BURST — the exact particle id, FLAGGED: not reproducible in
             * this module). The port reuses the husk-B gib/explosion path
             * (egg_explode launches the grey-cyan husk-B family + a flash
             * billboard — the SAME 0x29 burst-husk-B set this very husk's
             * model is). The death sound already played on the lethal poll
             * (enemy_mailbox_poll's generic-death branch). Free the slot
             * with no corpse fade — the burst FX hides it. */
            e->state   = EM_ENEMY_FREE;
            e->active  = 0;
            e->mailbox = 0;
            e->fade    = 0;
            egg_explode(e);          /* husk-B gib scatter + flash (stand-in
                                      * for FX 0x80000045) */
            break;
        }
        if (e->kind == EM_ENEMY_KIND_BUG) {
            /* bug death: the gameplay slot frees immediately
             * (alive/hit-tests off) while the corpse plays the DEATH
             * clip and alpha-fades. CORRECTED — the collapse clip is
             * 0x20 (func_00129FC0 case 3, played with sound 0x1B7 just
             * before the case-4 free), NOT 0x1B; 0x1B is the case-0
             * KNOCKDOWN reaction and survivable hits play it too. The
             * loader asks for 0x20 first and falls back to 0x1B, then to
             * the frozen-pose fade. No gibs — the husk burst set is the
             * crate's; the bug's gore chain is undecoded. */
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
    s.pp_last[0] = player_pos[0];
    s.pp_last[1] = player_pos[1];
    s.pp_last[2] = player_pos[2];
    s.pp_seen    = 1;
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
    /* OBSERVED 2026-06-12 (play capture), not source-derived — the
     * behaviour below is kept, its provenance is DOWNGRADED 2026-07-31:
     * the shake-off throws every clinging bug off AND KILLS it, rather
     * than detaching it to re-approach.
     * The old citation ("the engine's func_001EFE00 throw broadcast")
     * does not hold up. func_001EFE00 is still an asm-void in the decomp
     * (no readable C at all), and every call site we CAN read uses it as
     * a generic event fire/query keyed by an id — func_00129FC0 calls it
     * as func_001EFE00(0x80000027, actor) to raise the hurt voice and as
     * func_001EFE00(0x8000000F, actor) as a predicate. There is no
     * evidence in the recovered code of a "throw broadcast", and no
     * latch/shake-off state machine has been decompiled at all, so this
     * whole arm stays a PORT construction on top of a play observation. */
    for (int i = 0; i < s.n; i++) {
        Enemy *e = &s.e[i];
        if (e->active && e->kind == EM_ENEMY_KIND_BUG &&
            e->state == EM_ENEMY_ATTACK && e->sub == 5) {
            e->pos[0]    -= sinf(e->yaw) * BUG_SHAKE_PUSH;  /* yaw faces the
                                          * player, so -dir = away */
            e->pos[2]    -= cosf(e->yaw) * BUG_SHAKE_PUSH;
            e->pos[1]    -= BUG_CLING_Y;                 /* back to the floor */
            e->mailbox    = 0;
            e->hit_lethal = 1;
            e->state      = EM_ENEMY_DEATH;              /* thrown off -> dies */
        }
    }
}

/* THE VICTIM FILTER — CONFIRMED 2026-07-31 by decoding func_00183B80
 * (it ships as an asm-void `.word` leaf, so it had never actually been
 * read; the whole body is 47 instructions and decodes cleanly). It
 * returns, for actor `a`:
 *     class = byte[a+2] & ~0xE0;  if (class != 2)      -> 0
 *     model = byte[a+3];
 *       0x0D, 0x0E, 0x0F, 0x13                          -> 0
 *       0x06                                            -> byte[a+0x9F] == 0
 *       0x12                                            -> byte[a+0x0D] == 1
 *       anything else (0x18, 0x29, 0x1C/0x1E/0x1F/0x50) -> 1
 * That CONFIRMS the two readings this port depends on: 0x0D (the leech)
 * is rejected by name — J2 stays closed — and the placed crate 0x06 is a
 * victim exactly while +0x9F is 0. It also confirms two the port asserts
 * elsewhere: 0x0E (the tendril field) is rejected, matching the field's
 * exclusion from acquire/ray_test/alive, and the drum 0x18 falls to the
 * accepting default, so it is shootable.
 * ONE OPEN ITEM, left honest rather than guessed: 0x0F is in the REJECT
 * set. The port calls the bug "global models 0x0F/0x10", but those are
 * asset/creature-table slots (em_enemy.h "BUG KIND"), and nothing we have
 * recovered pins the bug actor's +0x03 TYPE byte — the crate's nest
 * records copy +0x03 straight out of disc data (func_001551B0 state 2),
 * so the two numbering spaces need not coincide. The bug therefore stays
 * a victim here (it is mailbox-shootable in every capture); if its type
 * byte is ever shown to be 0x0F this filter has to drop it. */
static int enemy_victim(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ||
           e->kind == EM_ENEMY_KIND_BUG   ||
           e->kind == EM_ENEMY_KIND_HUSK_PARTNER; /* model 0x29: the HP-1
                                               * shootable husk (ov 0x827490
                                               * polls +0x36). The husk
                                               * CREATURE (model 0x1A) is the
                                               * scripted actor and is NOT a
                                               * victim — it is excluded. */
}

/* CORRECTED 2026-07-31 — there is NO per-kind targeting-range gate.
 * This used to exclude the drum (the retired drum kind) beyond 50 units on
 * the reading that func_00156620's 50-unit test published it to "the
 * target list". It does not (docs/CRATES_DRUMS_ORIGINAL.md). func_001B1D20 feeds the
 * CONTACT pass (func_001A9000 -> func_001A8F40), the auto-aim walks a
 * different list entirely (func_00199220 over D_00275B8C), and outside
 * 50 u func_001B17A0 publishes the drum anyway whenever it is visible.
 * The engine's own range limit on acquisition is func_00199220's 260
 * units, which lives on the weapon side, not here — so this gate is now
 * unconditional and the drum is shootable at any range, as in-game.
 * Kept as a hook (and to leave every call site untouched) rather than
 * unpicked, since a later scene-visibility channel is the honest place
 * for the forced-publish override. */
static int enemy_in_target_range(const Enemy *e, const float ref[3])
{
    (void)e;
    (void)ref;
    return 1;
}

/* Per-kind hit-sphere parameters (crawler values unchanged — tests 1/2
 * and the gib demo stay byte-identical). */
static float kind_aim_y(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ? CRATE_AIM_Y
         : e->kind == EM_ENEMY_KIND_BUG   ? BUG_AIM_Y
         : e->kind == EM_ENEMY_KIND_HUSK_PARTNER ? HUSK_PARTNER_AIM_Y
                                          : ENEMY_AIM_Y;
}

static float kind_hit_r(const Enemy *e)
{
    return e->kind == EM_ENEMY_KIND_CRATE ? CRATE_HIT_R
         : e->kind == EM_ENEMY_KIND_BUG   ? BUG_HIT_R
         : e->kind == EM_ENEMY_KIND_HUSK_PARTNER ? HUSK_PARTNER_HIT_R
                                          : ENEMY_HIT_R;
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
        if (!enemy_in_target_range(e, from)) continue; /* drum 50-u gate */
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
 * fills on a worm). CORRECTED 2026-07-31: the drum's extra 50-u gate is
 * gone — func_00156620's 50-u branch is a contact-pass visibility
 * override, not a targeting gate, and func_00199220's
 * own range limit is 260 u on the weapon side. */
int em_enemy_targetable(int i)
{
    return i >= 0 && i < s.n && s.e[i].active &&
           enemy_victim(&s.e[i]) && s.e[i].hp > 0 &&
           enemy_in_target_range(&s.e[i], s.pp_seen ? s.pp_last : NULL);
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
        if (!enemy_in_target_range(e, from)) continue; /* drum 50-u gate */
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
        if (s.gib[k].flash) {              /* egg/drum fireball billboard */
            if (!s.flash_mesh) return 0;
            *mesh       = s.flash_mesh;
            *palette    = s.gib[k].palette;
            *bone_count = 1;
            return 1;
        }
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
    if (s.e[i].kind == EM_ENEMY_KIND_HUSK_CREATURE) {
        if (!s.husk_c_mesh) return 0;
        *mesh       = s.husk_c_mesh;
        *palette    = s.e[i].palette;
        *bone_count = s.husk_c_bones;
        return 1;
    }
    if (s.e[i].kind == EM_ENEMY_KIND_HUSK_PARTNER) {
        if (!s.husk_p_mesh) return 0;
        *mesh       = s.husk_p_mesh;
        *palette    = s.e[i].palette;
        *bone_count = s.husk_p_bones;
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
    if (s.husk_c_mesh) {
        em_gfx_mesh_destroy(gfx, s.husk_c_mesh);
        if (s.husk_c_has_model)
            em_model_free(&s.husk_c_model);
    }
    if (s.husk_p_mesh) {
        em_gfx_mesh_destroy(gfx, s.husk_p_mesh);
        if (s.husk_p_has_model)
            em_model_free(&s.husk_p_model);
    }
    for (int f = 0; f < GIB_FAMILY_N; f++)
        for (int i = 0; i < s.gibm_n[f]; i++) {
            em_gfx_mesh_destroy(gfx, s.gibm[f][i].mesh);
            em_model_free(&s.gibm[f][i].model);
        }
    if (s.flash_mesh)
        em_gfx_mesh_destroy(gfx, s.flash_mesh);
    if (s.gen_mesh)
        em_gfx_mesh_destroy(gfx, s.gen_mesh);
    if (s.tf_mesh) {
        em_gfx_mesh_destroy(gfx, s.tf_mesh);
        if (s.tf_has_model)
            em_model_free(&s.tf_model);
    }
    memset(&s, 0, sizeof s);
}
