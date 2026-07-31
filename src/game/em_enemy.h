/* em_enemy.h — placed crawler enemies: the port's first hostile actors.
 *
 * Native translation of the engine's most common placed enemy (decomp
 * repo Extermination/docs/FINDINGS.md "ENEMY AI ARCHITECTURE", session
 * 22, plus the s62 CONDITION DECODE of the three behavior functions —
 * the wake/burst/damage triggers below are read off the disassembly):
 *
 *   func_001551B0  the PLACED CRAWLER / disguised crate (lifecycle byte
 *                  actor +0x04, engine values kept: 0 INIT -> 4 IDLE ->
 *                  1 ATTACK -> 2 DEATH -> 3 FREE). The port's
 *                  EM_ENEMY_KIND_CRATE.
 *   func_00153F10  the WORM/LEECH brain (+ sub-machine func_00154120,
 *                  init func_00154040) — the kind-0xD creature the
 *                  mode-2 generator pads emit. Its ONLY installer in
 *                  the whole image is the generator helper
 *                  func_0015A200 (s68 exhaustive pointer scan): crate
 *                  bursts NEVER hatch it. The port's
 *                  EM_ENEMY_KIND_CRAWLER. Born attacking: approach ->
 *                  stalk (120 t, homing 0.0698 rad/t) -> windup ->
 *                  lunge resolve -> burst or despawn.
 *   func_00128C10 / func_0012A5D0  the two BUG brains (s68 "CREATURE
 *                  IDENTITY CORRECTION") — the 15-node insectoid
 *                  hatchling the NEST REGISTRY spawns from crate
 *                  bursts (and the game's ubiquitous room spawn,
 *                  200+ deferred-spawn records across 14 areas). The
 *                  port's EM_ENEMY_KIND_BUG (see "BUG KIND" below).
 *                  Shared INIT func_00128AB0 binds GLOBAL creature
 *                  slot 0x0F (variant A) / 0x10 (variant B when event
 *                  flag 0x30 == 0xFF) + clip bank 0x11; HP
 *                  func_00128390 = 15/30 (30/50 on difficulty byte
 *                  D_0081070A); mailbox consumer func_00128B80
 *                  polled EVERY tick, hurt/death handler
 *                  func_00129FC0 (death/flinch clips 0x1B/0x1D/0x20);
 *                  init clip 1 = the 90-frame in-place WALK. The brains
 *                  are DECODED STRUCTURALLY (s76, FINDINGS "BUG BRAIN
 *                  STATE MACHINES"): a 5-state top dispatch + a per-pose
 *                  / 14-case move-helper jtbl; the bite is
 *                  func_0012C490, clip 0x13. RETRACTED: func_001B5360
 *                  was recorded here as the "shared contact resolver
 *                  (radius-6 box +10u ahead)" and it is nothing of the
 *                  sort — the recovered C shows a GROUND-SNAP probe (up
 *                  +10 in Y, down 30, or 200 for model byte 4) traced
 *                  with func_0019A570(from, to, 6, 0), where 6 is that
 *                  function's collision CHANNEL MASK, and blended into
 *                  the actor transform with a per-model-byte weight.
 *                  The bug's real bite volume and damage are NOT
 *                  decoded. The port runs the real APPROACH -> BITE ->
 *                  RECOVER shape; the move-helper constants, the contact
 *                  box AND the damage value are flagged port magnitudes.
 *   actor +0x34    HIT POINTS, s16 (crate init = 1: any damage kills;
 *                  bug init = 15 variant A / 30 variant B
 *                  (func_00128390; 30/50 on the difficulty byte) —
 *                  genuinely consumed, the bug is shootable;
 *                  worm init = 10 — func_00154040 — but VESTIGIAL:
 *                  s66 live memchecks saw NOTHING ever read a worm's
 *                  +0x34, and the only +0x36 access in its whole life
 *                  is the release teardown's `sh zero, 0x36`
 *                  (func_001AFC10). Worms are NOT shootable.)
 *   actor +0x36    INCOMING-DAMAGE MAILBOX, s16 — attackers write it
 *                  (low bits = amount, high bits = weapon-type flags,
 *                  e.g. 0x4000), the behavior polls + clears it in its
 *                  own tick. There is NO central HP system. The CRATE
 *                  consumes it (IDLE poll), the BUG consumes it EVERY
 *                  tick (func_00128B80 -> flinch/death handler
 *                  func_00129FC0); the worm brain never reads it.
 *                                                 -> em_enemy_damage()
 *   victim filter  BOTH the bullet/laser victim filter (func_00183AC0)
 *                  and the second/targetable filter (func_00183B80)
 *                  switch on the MODEL byte and reject 0x0D (worm) —
 *                  the class byte IS 2; the model exclusion is doing
 *                  the work, deliberately (s66, static + live). The
 *                  crate family (model 0x06) is a victim while +0x9F
 *                  == 0, and the BUG is a victim (mailbox-shootable —
 *                  s68). The port mirrors this in em_enemy_acquire /
 *                  em_enemy_ray_test / em_enemy_targetable: bullets,
 *                  auto-aim and melee all pass straight through a
 *                  worm but land on crates and bugs. Worms are
 *                  dodged, not shot — their only deaths are their own
 *                  burst/despawn.
 *   actor +0x0A    GROUP-ALARM flag: a damage-KILLED idle crawler walks
 *                  the whole live actor list (no radius), but the
 *                  engine's wake write (`sb 1, +0x0A`) is reached ONLY
 *                  for a recipient whose +0x52 is set — and that is 0 on
 *                  every placed crate (live-read s76), so a destroyed
 *                  crate wakes NO neighbour (user-reported 2026-06-12;
 *                  the old "wakes every crate" was wrong).
 *                  Worms are not whitelisted and never read it.
 *   actor +0x52    NOT "on-surface" — the sense is the other way round.
 *                  INIT probes the crawler's own column (pos.y+1 down to
 *                  pos.y-2) and writes 0 when the probe returns 4, which
 *                  is func_0019AB20's STATIC-WORLD channel, else 1. So 0
 *                  = resting on level geometry (every placement) and 1 =
 *                  standing on a prop or on nothing. The alarm wake and
 *                  the state-2 recovery loop are both gated on the
 *                  unusual value. func_001AFA90 zeroes it at allocation.
 *   +0x2D0..0x2EC  4 precomputed diagonal probe CORNERS (INIT, absolute
 *                  world positions that never move again): the vector
 *                  (r,0,r) turned by the spawn matrix and laid out 90 deg
 *                  apart, r = 4.5961943 for model byte 6/0x1E and
 *                  2.1213202 for 0x1C/0x50/0x1F — corner radius 6.5 and
 *                  3.0 units. The steer phase probes each as a vertical
 *                  2-unit column with func_0019AB20 and turns the heading
 *                  +-3 deg/frame (0x3D56774F ~= 0.0524 rad) away from
 *                  blocked sides.
 *   hop physics    velocity integration with vertical gravity
 *                  0.052/tick (state 1 sub 1).
 *
 * FIDELITY NOTES (engine-true conditions + remaining flagged items —
 * details at each site in em_enemy.c):
 *  - WAKE (decoded; corrected s76): the placed crawler IDLE wakes only
 *    via the group alarm or dies to damage — func_001551B0 contains NO
 *    player-distance test (no func_0019AA80/func_0019A570 call, no
 *    player-position global); the old port-invented 32-u distance wake
 *    is REMOVED. AND the group-alarm wake is itself gated on the
 *    recipient's +0x52, which is 0 on every placed crate BY
 *    CONSTRUCTION (see +0x52 above: 0 is what the INIT column probe
 *    writes when the crawler is resting on level geometry)
 *    — so in practice a placed crate NEVER wakes from a neighbour's
 *    death and just sits as a destructible box until damaged (s76,
 *    user-confirmed). A lone undamaged placement sits forever. The
 *    free-roaming attacker is the WORM (no idle state, needs no wake).
 *  - DAMAGE WINDOW (decoded; J2 CLOSED s66 live): state 4 polls +0x36
 *    every tick; state 1 NEVER polls it — the only +0x36 access in
 *    the whole attack run is `sh zero, 0x36` on the burst transition.
 *    So damage written mid-run DEFERS (kills on the next IDLE tick if
 *    the run returns there via the boxed-in path) and is ABSORBED if
 *    the run ends in the suicide burst. The port implements exactly
 *    this for the crate. The WORM consumes NOTHING: a full-lifecycle
 *    live memcheck (spawn -> latch burst, two shots fired into it
 *    mid-stalk) saw zero +0x34/+0x36 accesses besides the teardown
 *    clear — combined with the model-0xD victim-filter rejection
 *    (file header) the old "unfound HP consumer" open item is CLOSED:
 *    there isn't one. HP=10 is vestigial init data. The port's former
 *    every-tick worm mailbox poll (the shootable stand-in) is REMOVED.
 *  - LUNGE DAMAGE (decoded s62, resolve test corrected s66): the old
 *    0x400A/amount-10 write was an invention (that code is the
 *    swipe-pass ENEMY-victim value). The engine worm damages the
 *    player only by LATCHING: D_008102BF = 2, D_008104D4 = 5.0 on an
 *    approach touch / 15.0 on the lunge connect, player status bit
 *    |= 2. The port posts the decoded 15 through its player-mailbox
 *    bridge on the lunge connect (health route); the 5.0 approach
 *    latch and the latch/shake-off mechanic are UNTRANSLATED
 *    (flagged). The lunge connect test (s66 live): func_0019AA80(a,
 *    b, 0x20) is a SEGMENT-vs-PLAYER-HIT-VOLUME query, not a 32-u
 *    radius — slots +0x34/+0x40 of the worm's OWN node table are rig
 *    nodes 13 (neck) and 16 (head), staged as a segment in spad
 *    0x70003190 and swept against the player's hit-volume list
 *    (player +0x58: bone-anchored sphere records, filter mask 0x20 —
 *    func_001A7280). That shape is CONFIRMED by the recovered
 *    func_0019AA80: it stages the two point args as a segment at
 *    0x70003190/+0x10 and hands arg2 & 0xFFFF straight to
 *    func_001A7280. The port runs it: the worm's neck->head rig segment
 *    (palette nodes 13/16 when the leech asset is loaded) against a
 *    PLAYER CAPSULE stand-in for the unexported volume list (flagged
 *    constants in em_enemy.c). The engine's second, latch-free arm is
 *    func_0019A570 — ALSO a segment trace, not a sphere: its own body
 *    reads arg2 as a 3-bit collision CHANNEL MASK (bit0 entity list,
 *    bit1 area hulls + class-4 actors, bit2 static world) and arg3 as a
 *    16-bit exclusion id, so the "radius 6" recorded here was a
 *    misreading of the mask value 6. The port's 6-unit contact sphere
 *    is now a flagged PORT stand-in. Without rig data (placeholder
 *    mesh) both arms fold into that contact carrying the latch.
 *  - DEATH: the engine's state 2 spawns nest children, gore FX, a
 *    MODEL REBIND to the gib models (library entries 0x22/0x29 — the
 *    leech clip bank has NO death clip; FINDINGS "CRAWLER RESOLVED").
 *    The husk PICK is decoded (2026-06-11, func_001551B0 @0x156380 —
 *    closes the s24 open item): the damage-kill arm keys on the
 *    crawler MODEL byte — 6 (the wooden crate, every exported
 *    scene's placements) -> husk 0x22 + its brown splinter family;
 *    any other variant -> husk 0x29 + the grey-cyan chunk family.
 *    The port's burst launches from the matching family, husk first
 *    (em_enemy.c GIB_FILES). RETRACTED: the engine adds NO knockback
 *    corpse-slide. The recovered state-2 arm builds an identity matrix
 *    in the scratch D_700036E0, turns it by an RNG draw of 0..3
 *    quarter-turns (so the set is {0, 90, 180, 270}, four ways) and
 *    multiplies it into the corpse's own transform with the render
 *    position preserved — a random re-orientation of the husk, and no
 *    velocity anywhere. It also fires only for model bytes 6/0x1E, and
 *    only on a damage kill. The gameplay slot still
 *    despawns immediately; VISUALLY a lethal hit launches 3-5 gib
 *    instances from the exported burst set (assets/gibs/gib_*.emdl,
 *    decomp tools/export_props.py --gibs) — a PORT scatter that reuses
 *    the quarter-turn draw as a bearing: arc under the 0.052 gravity,
 *    settle on the
 *    floor query, rest ~3 s, then ALPHA-FADE out over the last 30
 *    frames (white tint, alpha 1 -> 0 through the chain's per-draw
 *    tint: em_gfx_draw_skinned_tinted, the engine's faded-actor
 *    ZMSK=1 state; replaces the old sink-despawn stand-in from the
 *    no-per-draw-alpha era, same total lifetime) — drawn through the
 *    same chain contract as live crawlers (virtual indices in
 *    em_enemy_count/em_enemy_draw/em_enemy_draw_tint, budgeted to
 *    EM_ENEMY_MAX). Nest children and the gore particle FX remain
 *    untranslated. Missing gib assets, and the contact/suicide burst
 *    (where the engine skips the rebind entirely), keep the VISUAL-ONLY
 *    corpse
 *    placeholder — the frozen pose now fades out the same way
 *    instead of sinking (flagged in em_enemy.c). Debug:
 *    EM_ENEMY_GIBDEMO=<frame> forces a lethal damage-death on enemy 0
 *    at that tick so EM_CAPTURE can photograph the scatter (a DIRECT
 *    debug kill since the worm consumes no mailbox; on a crate it is
 *    equivalent to the real damage path).
 *  - SPEED/RANGES: hop forward speed, hop airtime and the per-model
 *    hit-sphere radius are not exported from the disc; the port
 *    constants are flagged in em_enemy.c. The lunge travels at the
 *    lunge clip's authored 21.27 u/s root speed (engine data); the
 *    connect test is the decoded segment-vs-player arm + the port's
 *    6-unit contact stand-in (see LUNGE DAMAGE above).
 *
 * MESH: assets/enemy_crawler.emdl (EMD2/EMD3 via the em_model API,
 * disc-derived, generated locally, git-ignored) when present; otherwise
 * a PLACEHOLDER box-ish crawler built procedurally at runtime (original
 * vertices, NOT disc data) through em_gfx_mesh_create.
 *
 * ANIMATION: with the EMD3 multi-clip asset (the leech clip bank, 4
 * clips: 0 crawl loop / 1 emerge / 2 windup / 3 lunge — FINDINGS
 * "CRAWLER RESOLVED" section 4) the state machine drives a visual-only
 * anim layer with 0.15 s crossfades: emerge on spawn, the crawl loop
 * slow in IDLE and ground-speed-scaled in ATTACK (the loco clips are
 * baked in place; root motion = the entity's own hop integration), and
 * windup -> lunge at close range under the suicide burst. An EMD2 or
 * single-clip asset (and the placeholder mesh) keeps the old static
 * frame-0 pose. The layer never feeds back into the gameplay state
 * machine, so spawn/wake/lunge/death TIMING is identical either way.
 *
 * CRATE KIND (FINDINGS "CRAWLER RESOLVED" sections 2/3 + the s26
 * office-table carve): in the engine the placed crawler IS a disguised
 * prop — its IDLE state renders the per-area model-table entry bound by
 * the placement param (office AREA02: entry 0x0D = a plain 6x4x5
 * cardboard box, three 64x64 PSMT4 skins; NOT the AREA11 14u beveled
 * crate — per-area skins), wiggling in place with a PROCEDURAL jitter
 * (float tables D_002468B0/B4/B8 perturb the world-matrix x/z
 * translation at actor +0x100/+0x108; there are no skeletal clips for
 * the 1-node rig). The port models that disguise as its own spawn KIND:
 *
 *   - `enemy crate <x> <y> <z> <yaw>` manifest lines (or
 *     em_enemy_add_kind with EM_ENEMY_KIND_CRATE) place one;
 *   - IDLE renders assets/enemy_crate.emdl (decomp repo
 *     tools/export_props.py --crate; placeholder box when absent) with
 *     the DECODED jitter CYCLE — rattle sound 0x19C at range 300, a
 *     random 0..255-tick wait, then a four-frame shudder on the odd
 *     counter values below 9 (table column (counter-1)>>1, row redrawn
 *     0..6 each cycle) and a clean matrix restore at 0. The whole block
 *     is gated on the crate still owing its nest group an untriggered
 *     record, so a gore-only crate sits PERFECTLY STILL. The
 *     amplitudes/periods are flagged port constants — the D_002468B0/
 *     B4/B8 table values are not exported;
 *   - HP 1: DAMAGE (the +0x36 mailbox — bullet or knife) BURSTS it,
 *     broadcasting the group alarm; the engine has NO proximity
 *     trigger (decoded — the old ~10-u port trigger is removed). Husk
 *     gibs fly through the shared gib launcher and the nest-group
 *     BUGS hatch at the crate position (s68 REBINDING APPLIED — the
 *     "CREATURE IDENTITY CORRECTION": the engine's state-2 nest
 *     children are the registry records of D_0024D820[area] from base
 *     index D_0024A850[area] + the crate's link; office links 0-4
 *     carry 2/2/3/2/2 bug records, AREA03/06 nests carry ITEM records
 *     (func_0015AFA0 — untranslated here, em_pickup's job when a
 *     scene exports one), and NO nest anywhere installs the worm.
 *     The registry itself is disc data the port cannot read, so the
 *     manifest carries the group size: `enemy crate x y z yaw
 *     [bugs <n>]` — bare lines fall back to 2, the office's modal
 *     group (flagged port fallback; n = 0 models the 12 office
 *     link -1 gore-only crates once the exporter emits nest links).
 *     The pre-s68 worm hatch is REMOVED — the worm stays exclusive
 *     to the mode-2 generator pads (already engine-true);
 *   - the group alarm sends the crate on the engine's blind suicide
 *     hop-run AS THE CRATE (decoded func_001551B0 state 1: probe-
 *     steered + RNG heading, no player seek, then the burst hatches the
 *     same nest group). RUN TIMER, corrected: 0xB4 = 180 ticks is the
 *     constant for every variant EXCEPT model byte 6, which instead
 *     gets 60 * its world Y / 12 — and byte 6 IS the port's default
 *     crate, so that branch is the live one here. Damage during the run
 *     defers/absorbs exactly as decoded (see em_enemy.c).
 *
 * BUG KIND (s68 "CREATURE IDENTITY CORRECTION" — the crate hatchling
 * and the game's ubiquitous enemy): the 15-node insectoid, GLOBAL
 * creature slot 0x0F (variant A, grey-blue chitin) / 0x10 (variant B
 * red flesh — the story swap at event flag 0x30, UNMODELED port-side:
 * the port always loads variant A) + the 36-container clip bank 0x11.
 * Decoded, kept: HP 15 (A; B = 30, difficulty 30/50 — recorded), the
 * EVERY-TICK mailbox consumption (func_00128B80) with flinch on a
 * nonlethal hit and death on a lethal one (handler func_00129FC0),
 * init/walk clip 1 (the 90-frame in-place WALK), flinch clip 0x1D,
 * death clip 0x1B — 0x1B sits in a non-sentinel-header container the
 * anim decoder cannot bake yet (s68), so the current export lacks it
 * and the death falls back to the corpse alpha-fade (the code
 * requests the engine id and wires itself when a future export
 * carries it). FLAGGED-MINIMAL BRAIN: the two real brains
 * (func_00128C10 simple / func_0012A5D0 full, a 14-case sub jtbl)
 * are uncharacterized beyond INIT/damage (s68 open item), so the
 * port runs idle/walk/approach only — home toward the player at a
 * flagged port turn rate, walk a flagged port speed to a flagged
 * standoff, and deal NO damage (the engine bugs' attack moves —
 * hop/lunge clips 7/8/10/12/13/17/19 — are undecoded; a bug that
 * reaches the player just crowds it). Placement-pose codes (param
 * 4/5/9: floor/wall/ceiling attach probes, func_00129780) are
 * untranslated — every port bug hatches floor-posed. MESH:
 * assets/enemy_bug.emdl (decomp tools/export_native.py, recorded CLI
 * in FINDINGS s68; assets/enemy_bug_infected.emdl = variant B,
 * shipped but unbound); placeholder flat box bug when absent.
 * Instances: hatched by crate bursts, or placed directly by manifest
 * `enemy bug <x> <y> <z> <yaw>` lines (port convenience standing in
 * for the registry's ordinary room-spawn records).
 *
 * EGG KIND (FINDINGS s78 §5 + INVESTIGATION_area11_egg_vs_barrel.md —
 * brain func_00156620; the AREA-11 opening's 2 destructible fixtures):
 * RE-IDENTIFIED 2026-06-17 as an industrial metal DRUM/canister
 * destructible (115 placements, 10 areas), model byte 0x18 / kind 0x46,
 * HP 1 — NOT an organic egg, NOT a hatcher and NOT a crate variant.
 * Passive set-dressing that EXPLODES when shot (the kind keeps the name
 * EM_ENEMY_KIND_EGG until a later rename pass; only its DEATH behaviour
 * is the drum's). No children, no husk rebind, no attack, no movement,
 * no proximity aggro, no alarm. Decoded, kept: HP 1 (any +0x36 kills),
 * the damage-only EXPLOSION (a fireball/blast billboard FX 0x80000013
 * -> 0x8000006E + flash 0x8000001C + flung debris + explosion sound
 * 0x1A1, then self-free — INVESTIGATION "DEATH = EXPLOSION", live-
 * confirmed; PURELY VISUAL, no radius damage, no chain), and the idle
 * wobble MECHANISM (a visual-only world-matrix perturbation — the
 * engine's amplitude tables D_00246A00/D_00246A10 are not exported, so
 * the amplitudes/periods are flagged port constants). It is shootable
 * (model 0x18 is in the victim whitelist — em_enemy_acquire/ray_test/
 * targetable). MESH: assets/enemy_egg.emdl (the area-11 model-table
 * entry 0x0E carve — decomp tools/export_props.py --egg; capped-cylinder
 * drum mesh), placeholder upright box when absent. Instances: manifest
 * `enemy egg <x> <y> <z> <yaw>` lines, dispatched here (em_enemy_add_kind
 * with EM_ENEMY_KIND_EGG). Full ledger in em_enemy.c "THE EGG / DRUM".
 *
 * GENERATOR (FINDINGS "GENERATOR — func_0015A2C0 RESOLVED", session
 * 28): the engine's most-placed creature behavior (class 0x0D, model 3,
 * 129 placements) — an organic infected-growth FLOOR PAD that charges
 * while the player stands on it and births worms. Decoded machine
 * (boot-ELF .data tables read locally; constants below are the decoded
 * values, recorded in the decomp FINDINGS like every other datum):
 *
 *   placement      kind(+0x54) 0..6 -> config rec D_00248120 (box
 *                  half-extents X/Z, Y = 1.0); link(+0x56) 0/1/2.
 *   INIT           link 1/2 draws ONE BYTE from count table D_002481B0
 *                  / D_002481D0 (row = frame RNG & 3, column = global
 *                  per-link counter & 7, post-incremented) and stores
 *                  it back into +0x56 as the RUNTIME MODE: 0 = inert
 *                  pad, 1 = breather/trap (+ an immediate pair of
 *                  kind-0xE TENDRIL FIELDS — func_001546C0, see
 *                  "TENDRIL FIELD" below),
 *                  2 = WORM EMITTER. link 0 = mode locked 0 (inert).
 *   trigger        +0x0A = "player inside my box THIS frame" — the
 *                  pair pass func_001A8BE0 -> func_001A8840 box-tests
 *                  the player against the config extents (Y tolerance
 *                  +1.5); the behavior consumes and clears it each
 *                  tick. NOT the group alarm the crawlers use.
 *   mode 2         sub 0: 121 CONSECUTIVE in-box frames (+0x20 charge,
 *                  reset on exit) -> spawn ONE kind-0xD worm AT THE
 *                  GENERATOR ORIGIN (func_0015A200 copies +0xB0
 *                  verbatim; the leech brain then yaws it toward the
 *                  player) -> +0x2E++; >= 4 -> sub 2 EXHAUSTED forever;
 *                  else sub 1 = delay D_002481F0[RNG%3] frames
 *                  ({1800, 3600, 5400} = 30/60/90 s), counted down
 *                  WITHOUT needing the player, then sub 0 again.
 *   mode 1         sub 0: 100-frame in-box charge driving the morph
 *                  phase +0x80 -> OPEN (+0x0B=1, 60-frame hold,
 *                  refreshed while the player stays): breathing sound
 *                  0x42F every 128 frames, particle fountain
 *                  func_0015A750, and the box pass HURTS the standing
 *                  player (event 3, magnitude 5.0).
 *   destructible?  NO — the behavior never reads +0x34/+0x36, the
 *                  laser/bullet victim filter func_00183AC0 rejects
 *                  class 0x0D, and the pair-pass model whitelist skips
 *                  model 3. The only way it stops is the 4-worm cap.
 *   visual         NOT a model-table entry: func_001E9580/001E9E60
 *                  build a PROCEDURAL VU-morphed pad into a private
 *                  0xA060-byte buffer (pool D_00275C1C, slot = the
 *                  placement uid), blending a rest shape by the +0x80
 *                  phase. Nothing to export — the port uses an ORIGINAL
 *                  placeholder mound scaled to the decoded footprint,
 *                  with the phase as a swell (flagged stand-in until
 *                  the morph pipeline is reimplemented).
 *
 * PORT FIDELITY (deviations flagged in em_enemy.c): the mode draw and
 * delay pick use the module's deterministic LCG, not the engine frame
 * RNG; the second box pass (generators waking nearby D_00275BB0-list
 * actors) is untranslated; the open-trap
 * hit is a one-shot 5-damage player-mailbox write per entry (the
 * engine's event-3 knockdown path is untranslated); worms spawned past
 * the 16 crawler slots fail the alloc exactly like the engine's full
 * actor pool (the cap is NOT consumed — the engine retries after the
 * delay).
 * Generators live OUTSIDE the crawler slot array (the engine keeps
 * them on the HAZARD list, not the damage-target list): they are not
 * shootable, not acquirable, not counted by em_enemy_alive, and they
 * draw through the same chain budget only when free slots remain.
 *
 * TENDRIL FIELD (FINDINGS "KIND-0xE COMPANION RESOLVED", session 33 —
 * brain func_001546C0, init func_00154740, tick func_001549C0, render
 * func_00154F00, trigger gate func_00154460): the kind-0xE PAIR a
 * mode-1 (breather) pad emits at init is a STATIONARY AREA-EFFECT
 * actor — a field of 12 tapering organic spikes that erupt out of the
 * infested floor around the PLAYER whenever the player lingers near
 * the pad. It deals NO damage (the pad's box pass does that), has no
 * HP, no mailbox, no hit sphere, never moves and never publishes to
 * the target lists: not shootable, not acquirable, not counted by
 * em_enemy_alive. Decoded machine (sub-state +0x05):
 *
 *   spawn       a mode-1 generator attaches the pair (idx 0/1 —
 *               concentric rings) at pad init, parent-linked; nothing
 *               kills them but the scene reset.
 *   0 SCAN      trigger = player inside 3x the parent pad footprint
 *               (|dy| <= 3 + recY). On trigger: anchor = (player X,
 *               pad Y, player Z); scatter 12 targets on the ring
 *               r = 5.5 +- 2.0 u (idx 0) / 7.0 +- 2.5 u (idx 1)
 *               around the player, REJECTING points outside the
 *               0.92x pad ellipse; per record phase = rand 48..127,
 *               ramp 0, girth from the cycling {180,218,255,384}/256
 *               table; sound 0x42D if ANY target survived; falls
 *               through to DEPLOY the same tick.
 *   1 DEPLOY    all 12 ramps += 37/tick clamp 300, 8 ticks (-> 296).
 *   2 HOLD      while the player stays within 2 u (idx 0) / 4 u
 *               (idx 1) of the trigger anchor and inside the Y band;
 *               leaving -> RETRACT.
 *   3 RETRACT   ramps -= 37/tick floor 0, 8 ticks.
 *   4 RESET     back to SCAN (the field re-deploys indefinitely).
 *
 *   visual      12 re-posed draws of ONE static spike mesh
 *               (assets/tendril.emdl = chunk03/f13_id15.bin, 96 verts,
 *               1 node — load-if-present, else the field runs with a
 *               logged draw skip); per spike scale X/Z = girth/256,
 *               scale Y = phase*ramp/65536 with the engine's s16 bob
 *               integrator (gentle bob while the pad is closed,
 *               violent thrash while the parent breather phase > 0.5:
 *               gravity 1 vs 8/tick, kicks 3..7 vs 28..41 below phase
 *               128, vel halved at >= 8 when closed, floor clamp at
 *               phase 100 with a fresh 3..7 vel). The spikes draw
 *               TINTED (em_gfx_draw_skinned_tinted via the chain's
 *               em_enemy_draw_tint contract): RGB blends the room
 *               tint toward the vivid (6,92,1)/128 green as the
 *               parent pad's open phase rises, alpha = ramp/300 (the
 *               deploy fade-in/out; translucent draws disable the
 *               depth write — the engine's GS ZMSK=1 faded-actor
 *               state). The per-room tint table D_00246800 is
 *               undecoded port-side: the rest blend uses the NEUTRAL
 *               (128,128,128) rec (TODO flagged in em_enemy.c).
 *
 * EM_ENEMY_TEST=5 (tendril run, owned by em_enemy.c like test 4):
 * places a link-1 pad at the player spawn with the mode draw FORCED
 * to 1 (the link-1 table draw is RNG 0/1; the test pins the breather
 * outcome — documented test-only override), then drives a SYNTHETIC
 * deterministic walker (substituting the player position THIS module
 * sees, so the run is self-contained) onto the pad: asserts the pair
 * attached, the field triggers while walking in, reaches steady HOLD
 * with all 12 targets valid and every ramp at 296 (= the 8-tick
 * deploy), 24 spike draws when the mesh is present, RETRACTS once the
 * walker leaves the hold radius, and rests at sub 0 / ramp 0 outside
 * the trigger box. PASS/FAIL line + quit.
 *
 * Instances come from the SCENE MANIFEST: `enemy crawler <x> <y> <z>
 * <yaw>` / `enemy crate <x> <y> <z> <yaw> [bugs <n>] [variant <v>]`
 * / `enemy bug <x> <y> <z> <yaw>` lines (parsed by em_game.c next to
 * the door lines), plus `enemy generator <x> <y> <z> <yaw> [kind <k>]
 * [link <n>]` lines — all parsed natively by em_game.c's
 * scene_manifest_load and dispatched here (em_enemy_add_kind /
 * em_enemy_add_crate / em_enemy_add_generator) at scene-load time.
 * No enemy lines = this module never loads, updates or draws anything,
 * keeping default-run frame output byte-identical.
 *
 * EM_ENEMY_TEST=4 (generator run) is owned by this module (em_game.c
 * only arms 1..3, and skips manifest generator lines while this test is
 * on so the run stays self-contained): spawns one forced-mode-2
 * generator 14 u ahead of
 * the idle player (inside the kind-2 box), with the 1800/3600/5400
 * delays divided by 60 (test-only acceleration, flagged); asserts the
 * 121-frame charge, worm-at-origin spawns, that a mid-stalk mailbox
 * write does NOT kill worm 1 (s66: worms consume nothing — the write
 * lands and dies with the slot) while its OWN lunge lifecycle ends it,
 * continued emission to the 4-worm cap, and silence after exhaustion.
 */
#ifndef EM_ENEMY_H
#define EM_ENEMY_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Render-chain draw-slot capacity (em_game.c sizes its chain with
 * this): the 16 crawler/gib slots (the internal slot pool, unchanged)
 * + the generator pool + the tendril-field spike instances (8 fields
 * x 12 spikes). em_enemy_count() never exceeds it; em_enemy_draw()
 * returns 0 for the unused tail, so em_game's chain build skips it. */
#define EM_ENEMY_MAX (16 + 12 + 8 * 12)

/* Lifecycle states — the ENGINE's values for the actor byte +0x04. */
enum {
    EM_ENEMY_INIT   = 0,
    EM_ENEMY_ATTACK = 1,
    EM_ENEMY_DEATH  = 2,   /* burst/death sequence (engine sub-machine) */
    EM_ENEMY_FREE   = 3,   /* released (func_001AFC10); slot inactive   */
    EM_ENEMY_IDLE   = 4    /* dormant on the nest                       */
};

/* Spawn kinds — the decoded engine brains:
 * CRAWLER = the worm/leech (func_00153F10/func_00154120, the kind-0xD
 * runtime creature; born attacking — the engine never places one, so a
 * manifest `enemy crawler` line is a port convenience), CRATE = the
 * placed crawler func_001551B0 (the disguised prop; bursts into gibs +
 * its nest group's BUGS on DAMAGE or at the end of its alarm-driven
 * suicide run — s68), BUG = the nest hatchling (func_00128C10/
 * func_0012A5D0, decoded structurally s76: approach -> in-place bite ->
 * recover with flagged port magnitudes — "BUG KIND" above), EGG = the
 * AREA-11 egg/growth fixture func_00156620 (a stationary destructible
 * decor PROP that bursts when shot — no children, no attack, no movement;
 * "EGG KIND" above). */
enum {
    EM_ENEMY_KIND_CRAWLER = 0,   /* the worm/leech creature            */
    EM_ENEMY_KIND_CRATE   = 1,   /* the placed crawler (disguise)      */
    EM_ENEMY_KIND_BUG     = 2,   /* the nest hatchling (s68)           */
    EM_ENEMY_KIND_EGG     = 3,   /* the AREA-11 metal DRUM fixture
                                  * (func_00156620 — FINDINGS s78 §5 +
                                  * INVESTIGATION: a STATIONARY industrial
                                  * destructible DRUM, model 0x18 / kind
                                  * 0x46, HP 1. Set-dressing that EXPLODES
                                  * when shot (fireball + debris + sound
                                  * 0x1A1, purely visual); NO children, NO
                                  * husk rebind, NO attack, NO movement, NO
                                  * proximity aggro, NO blast damage. NOT a
                                  * crate variant — its own kind. Name kept
                                  * ..._EGG pending a later rename pass —
                                  * see "EGG KIND" above / em_enemy.c)   */
    EM_ENEMY_KIND_HUSK_CREATURE = 4, /* AREA-11 door-husk scripted creature
                                  * (deferred record 7, ov 0x00825940 /
                                  * func_00825900, model 0x1A — FINDINGS
                                  * "Door-position creature/husk set-piece"
                                  * + INVESTIGATION_first_level_area11 §5.2
                                  * + INVESTIGATION_area11_director §5).
                                  * The staged "first-monster moment" actor
                                  * by the room-move door at (387,231.8,290.3).
                                  * SELF-CONTAINED: its own timer/proximity
                                  * state machine (states 0..4, the live
                                  * 0x64 scripted timer), reads NO story flag
                                  * — NOT driven by the D_00810813 director
                                  * (director §5 static negative). At the
                                  * OPENING it is STAGED-INERT: present and
                                  * visible but not aggroing (the opening
                                  * must stay enemy-free, owner-required).
                                  * NOT shootable (it is the scripted actor,
                                  * not the husk). Engine spawns one child
                                  * through func_001AFA90(0x7A) — and that
                                  * function is now byte-matched, so the
                                  * param is settled: it is an actor CLASS
                                  * byte, not a mesh id. func_001AFA90 masks
                                  * it with ~0xE0, so 0x7A means class 0x1A
                                  * with the two high flag bits (0x60) set,
                                  * stores the whole byte at child +0x02,
                                  * pops a 0x2F0-byte actor off the free
                                  * list, gives it identity scales, a zero
                                  * position and +0x94 = -1, and links it
                                  * into the active list. It binds NO model
                                  * at all — whatever the child renders comes
                                  * from its own init, which is undecoded.
                                  * FLAGGED, not invented.
                                  * MESH: <scene>/props/area_husk_creature
                                  * .emdl. See "DOOR-HUSK PAIR" in em_enemy.c */
    EM_ENEMY_KIND_HUSK_PARTNER  = 5  /* AREA-11 door-husk SHOOTABLE husk
                                  * (deferred record 8, ov 0x00827490 /
                                  * func_00827490, model 0x29 burst-husk-B —
                                  * same docs as HUSK_CREATURE). HP 1,
                                  * shootable: polls its +0x36 mailbox and on
                                  * death BURSTS with gore FX 0x80000045
                                  * (flagged — not directly reproducible;
                                  * the port reuses the gib/explosion FX
                                  * path). Partner-linked (engine puid 0x50,
                                  * taken-bit persistence — the port models
                                  * the SHOOTABLE/BURST arm; the link/persist
                                  * is flagged, see em_enemy.c). At the
                                  * OPENING it too is STAGED-INERT (no aggro,
                                  * no attack, no movement — it just stands
                                  * shootable). MESH: <scene>/props/
                                  * area_husk_partner.emdl */
};

/* Reset the instance list (boot / scene reload). Does not free GPU
 * resources — pair with em_enemy_shutdown for that. */
void em_enemy_reset(void);

/* Spawn one crawler at `pos` facing `yaw` (a manifest line or a test
 * script). Loads the shared mesh on first use (asset, else the runtime
 * placeholder). Returns the instance index, or -1. */
int em_enemy_add(EmGfx *gfx, const float pos[3], float yaw);

/* Kind-aware spawn (manifest dispatch): EM_ENEMY_KIND_CRAWLER is
 * em_enemy_add; EM_ENEMY_KIND_BUG places one bug; EM_ENEMY_KIND_CRATE
 * places the disguised crate with the default nest-group size (and
 * preloads the bug + gib assets its burst will need);
 * EM_ENEMY_KIND_HUSK_CREATURE / EM_ENEMY_KIND_HUSK_PARTNER place the
 * AREA-11 door-husk pair, both STAGED-INERT at the opening (the
 * creature is a scripted idle actor; the partner is HP-1 shootable —
 * see "DOOR-HUSK PAIR" in em_enemy.c). The husk meshes are scene-local
 * (<scene>/props/area_husk_creature.emdl / area_husk_partner.emdl). */
int em_enemy_add_kind(EmGfx *gfx, int kind, const float pos[3], float yaw);

/* Crate spawn with an explicit nest-group size (`enemy crate ... bugs
 * <n>` manifest lines — the channel for the decoded registry counts,
 * which are disc data the port cannot read itself). `bugs` < 0 = the
 * default group (2, the office modal group — flagged fallback); 0 =
 * a gore-only crate (the office link -1 majority). `variant` = the
 * placement MODEL byte (`variant <v>`; < 0 = the default 6 — every
 * exported scene's crates): it picks the burst's HUSK FAMILY exactly
 * like the engine's damage-kill rebind (func_001551B0 @0x156380 —
 * byte 6 -> library husk 0x22, the wooden crate's brown set; any
 * other crawler variant -> husk 0x29, grey-cyan). */
int em_enemy_add_crate(EmGfx *gfx, const float pos[3], float yaw,
                       int bugs, int variant);

/* Place one GENERATOR pad (engine class 0x0D / func_0015A2C0 — see
 * "GENERATOR" above; em_game.c's manifest parser dispatches
 * `enemy generator x y z yaw [kind k] [link n]` lines here, defaults
 * kind 1 / link 2 for bare lines). `cfg` = the engine kind 0..6 (the
 * D_00248120 footprint row); `link` = the placement link 0/1/2 (0 =
 * inert pad, 1/2 = draw the runtime mode from the decoded count table
 * at init).
 * Generators occupy their own pool (EM_GENERATOR_MAX), NOT crawler
 * slots; the worms a mode-2 pad emits go through the normal crawler
 * spawn path and DO consume slots. Returns the generator index or -1.
 * (12 = the 8-pad office floor + headroom for the EM_ENEMY_TEST=5 pad
 * even on a fully-padded scene.) */
#define EM_GENERATOR_MAX 12
int em_enemy_add_generator(EmGfx *gfx, const float pos[3], float yaw,
                           int cfg, int link);

/* Generator introspection (debug / self-tests). */
int em_enemy_generator_count(void);
int em_enemy_generator_mode(int i);     /* runtime +0x56 mode, or -1   */
int em_enemy_generator_spawned(int i);  /* worms emitted (+0x2E), or -1 */

/* Per-frame update: every enemy's state machine (crate: idle/alarm
 * wake + the blind suicide run; worm: approach/stalk/windup/lunge;
 * mailbox-driven death). `coll`
 * (may be NULL / empty) is the world the steering probes and floor
 * queries run through. Call once per gameplay frame, at the actor-pool
 * tick (func_001AFD70), before the weapon update so shots see this
 * frame's positions. */
void em_enemy_update(const EmCollision *coll, const float player_pos[3]);

/* Write the incoming-damage mailbox (actor +0x36): low bits = amount,
 * high bits = weapon-type flags. The CRATE consumes it on its IDLE
 * poll, the BUG every tick (func_00128B80 — flinch below lethal,
 * death at it); a worm's mailbox is never read — engine-true (s66):
 * the write lands and dies with the slot, exactly like the engine's
 * teardown-only +0x36. (Attackers can't normally reach a worm anyway:
 * the victim filters below reject it before any write happens.) */
void em_enemy_damage(int i, int16_t code);

/* Player-side damage mailbox (same +0x36 semantics, pointed at the
 * player): the lunge writes it, em_game.c consumes it into
 * EmPlayerStatus.health. Returns the pending code and clears it. */
int em_enemy_player_hit_take(void);

/* s76 bug-latch / shake-off (clip 54): the count of bugs currently
 * clinging to the player, and the shake-off that detaches them all (the
 * player's shake reaction calls it on the clip's completion). */
int  em_enemy_latched_count(void);
void em_enemy_shake_off(void);

/* Hitscan support for em_weapon.c — keeps the em_collision world API
 * untouched (port choice, documented in em_weapon.h):
 *
 * ALL THREE queries run the engine's MODEL-keyed victim filter first
 * (func_00183AC0 / func_00183B80, s66): the WORM (model 0x0D) is
 * rejected — rays pass through it, the auto-aim lock never fills on
 * it, melee whiffs past it. Crates (model 0x06 family) and BUGS
 * (global creature models 0x0F/0x10 — mailbox-shootable, s68) are
 * victims.
 *
 * em_enemy_acquire: nearest live VICTIM within `max_dist` of `from`
 * whose XZ bearing lies inside the facing cone (dot >= cone_cos).
 * Remains the MELEE victim resolver (the knife's reach stand-in); the
 * BULLET's acquisition is the decoded func_00199220 screen-cone chain
 * in em_weapon.c, fed by the two queries below.
 *
 * em_enemy_ray_test: nearest live VICTIM whose hit sphere intersects
 * the segment [from, to] (the per-victim test the bullet runs BEFORE
 * crediting a world hit). Writes the entry point. Returns index or -1.
 *
 * em_enemy_targetable: the func_00199220 candidate gate — slot live
 * (engine status byte != 0), VICTIM by model (the func_00183B80
 * filter — worms excluded), HP > 0 (the +0x34 halfword test). Real
 * instance slots only (gib/pad virtual draw slots always 0).
 *
 * em_enemy_aim_point: the func_00183C40 class-keyed AIM POINT — the
 * port's hit-sphere center (pos + per-kind aim height), the same point
 * em_enemy_ray_test tests against. Unchecked index = garbage in,
 * caller gates with em_enemy_targetable first. */
int em_enemy_acquire(const float from[3], float yaw, float max_dist,
                     float cone_cos, float aim_out[3]);
int em_enemy_ray_test(const float from[3], const float to[3],
                      float hit_out[3]);
int  em_enemy_targetable(int i);
void em_enemy_aim_point(int i, float out[3]);

/* Draw accessors for the render chain. em_enemy_draw returns 0 for an
 * inactive slot, EXCEPT while the death placeholder is still fading
 * the frozen corpse pose out (a few frames after the burst).
 *
 * em_enemy_draw_tint: the slot's per-draw RGBA modulation for
 * em_gfx_draw_skinned_tinted, or NULL = draw untinted via
 * em_gfx_draw_skinned (the exact pre-tint state). Same index space
 * and pointer contract as the palette: em_game.c records the pointer
 * at chain-build time and em_enemy_update writes this frame's values
 * before the close-out flush. Tinted slots: tendril spikes (room
 * tint -> green by the parent pad's open phase, alpha = ramp/300),
 * gibs (white, alpha 1 -> 0 over the exit fade) and the no-gib
 * corpse fade. Live crawlers, crates and generator pads return NULL. */
int em_enemy_count(void);
int em_enemy_draw(int i, EmGfxMesh **mesh, const float **palette,
                  uint32_t *bone_count);
const float *em_enemy_draw_tint(int i);

/* Introspection (debug / self-tests). */
int  em_enemy_alive(void);        /* live instances (INIT/IDLE/ATTACK) */
int  em_enemy_state(int i);       /* engine lifecycle value, or -1     */
int  em_enemy_kind(int i);        /* EM_ENEMY_KIND_*, or -1            */
int  em_enemy_hp(int i);          /* the +0x34 halfword                */
void em_enemy_pos(int i, float out[3]);

/* Destroy the GPU mesh + free the model. */
void em_enemy_shutdown(EmGfx *gfx);

#ifdef __cplusplus
}
#endif

#endif /* EM_ENEMY_H */
