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
 *                  IDENTITY CORRECTION" — the identity itself is an s68
 *                  pointer-scan result, not re-derivable from the C).
 *                  The port's EM_ENEMY_KIND_BUG (see "BUG KIND" below).
 *                  CORRECTED 2026-07-31: BOTH ARE NOW RECOVERED
 *                  (decomp src/func_00128C10.c, src/func_0012A5D0.c —
 *                  // NEARMISS, so body-correct C, scheduling not
 *                  byte-exact). The "still undecompiled stubs" note that
 *                  stood here was STALE. What the recovered C says:
 *                    - both share the 5-state skeleton on actor byte
 *                      +0x04 (0 spawn / 1 live / 2 reaction / 3 teardown
 *                      / 4 respawn-hold) and both measure the player
 *                      through the GLOBAL record D_008102B0 at +0xA0 —
 *                      so neither of them is the player.
 *                    - func_00128C10's live state is a WANDER, not an
 *                      attack. Sub 0 idles on clip 1 (blend 4.0, rate
 *                      1.0) until func_001B13F0(player, self, R) trips,
 *                      R = 100.0 for creature kinds < 4 and 20.0/40.0
 *                      for kinds >= 4 (20.0 when (sub+0xE4 >> 8) == 1);
 *                      sub 1 re-tests at 150.0, drops back to sub 0
 *                      after 0x5A = 90 ticks out of range, advances at
 *                      10.0/24.0; sub 2 turns to the goal heading at
 *                      0.34906587 rad/tick (20 deg); sub 3 runs clip 6
 *                      at speed 0.8 / rate 2.6, turning 0.06981317
 *                      rad/tick (4 deg) and re-rolling the heading by
 *                      +-0.69813174 rad (40 deg) on each timer expiry.
 *                      Creature kinds 4 and 9 are the PANIC kinds: they
 *                      remap to 3 / 8, take speed 0.6 / rate 1.6 and
 *                      hold (rand & 0x30) + 0x3C = 60..108 ticks.
 *                    - every goal heading there is a RANDOM full-circle
 *                      bearing, func_001B1470(6.2831855f *
 *                      (func_00122BB8() & 0xF0) / 256.0f) — one of 16
 *                      quantized compass points. The engine brain does
 *                      NOT home on the player; proximity only TRIGGERS
 *                      the run. And nothing in either brain, nor in the
 *                      leap chain below, writes damage to the player.
 *                    - func_0012A5D0's live state is a 14-way jump table
 *                      (jtbl_0026D000, sub-states 0..13) over move
 *                      bodies: func_00128640/0012AFC0/0012B410 (idle,
 *                      walk), 0012B970/0012BE20/0012C490/0012CAA0/
 *                      0012D240 (the hit + leap set), 001C2770 +
 *                      0012D580/0012DD70 + 001C3D60 (climb),
 *                      0012D850/0012D940, and 0012B850 (death). The old
 *                      "decoded structurally, 14-case sub jtbl" note was
 *                      RIGHT; its 2026-06 retraction was itself wrong.
 *                      RE-CONFIRMED 2026-07-31 arm by arm against
 *                      src/func_0012A5D0.c: `switch (p[5])` really does
 *                      carry cases 0..13 with exactly those callees
 *                      (10 and 11 share the func_0012D940 body, so 14
 *                      sub-states over 13 distinct arms).
 *                  Shared INIT func_00128AB0 (BYTE-MATCHED) reserves
 *                  func_001B10B0(actor, 0x10, 0x11) when the global
 *                  D_00810788 == 0xFF, else func_001B10B0(actor, 0xF,
 *                  0x11), and records which it took at sub+0xE1. NOTE:
 *                  those are func_001B10B0 SLOT ids, NOT the actor's
 *                  +0x03 MODEL byte — the worm reserves 0x14/0x13 yet
 *                  carries model 0x0D. That distinction matters for the
 *                  victim filters (see "victim filter" below).
 *                  HP: func_001289C0 (BYTE-MATCHED) writes +0x34 =
 *                  func_00128390(actor, sub+0xE1 != 0), and
 *                  func_00128390 (BYTE-MATCHED) returns 15/30, or 30/50
 *                  when the difficulty byte D_0081070A is nonzero — so
 *                  slot 0x0F -> 15 HP, slot 0x10 -> 30 HP.
 *                  Mailbox ROUTER func_00128B80 is polled EVERY live
 *                  tick: on ANY nonzero +0x36, or on the global
 *                  instakill flag D_0081080F, it forces actor[0] = 3,
 *                  state 2, subs 0 — it compares no HP. ORDER CORRECTED
 *                  2026-07-31: the `+0x36 = +0x34` instakill copy is the
 *                  LAST thing the router does, AFTER the state writes
 *                  (and after func_0012E070) — it stages full-HP damage
 *                  for the NEXT tick's driver, it does not precede the
 *                  transition. Reaction driver func_00129FC0 (BYTE-MATCHED)
 *                  then debits the LOW BYTE of +0x36 from +0x34 and
 *                  plays flinch 0x1D, or knockdown 0x1B when the damage
 *                  word carries 0x2000, or sub[0xFB] has bit 7 set, or
 *                  sub+0xE4 is 0 / 0x400 / 0x500; the corpse's own clip
 *                  is the collapse 0x20 (+ sound 0x1B7) in case 3 on the
 *                  way out. Init/idle clip is 1 (func_001287F0(e, b, 1,
 *                  4.0f) in live sub 0) — but its "90-frame" length was
 *                  never in the code: the 0x5A there is sub 1's
 *                  out-of-range fallback timer. Clip length = OBSERVED.
 *                  The recovered func_0012C490 (a MOVE BODY of
 *                  func_0012A5D0 sub-state 5, not a standalone brain) is
 *                  a 9-state LEAP chain: clip 0x13 with the body hop
 *                  func_00128830(actor,0,0,-2.5) and sfx 0x1AE, then
 *                  clip 0x14 with hop (0,2.5,0), clip 0x15 with hop
 *                  (0,1.5,3) + sfx 0x1AA and a 0x78-tick spin, and clip
 *                  0x11 on recover — with NO contact test anywhere in
 *                  it. (So clips 0x11/0x13/0x14/0x15 ARE decoded —
 *                  CONFIRMED 2026-07-31 in src/func_0012C490.c, states
 *                  2/3/4/7 of its 9-state jtbl_0026D0A0; the old
 *                  "clips 7/8/10/12/13/17/19 all undecoded" line was
 *                  too pessimistic. The only probes in the chain are
 *                  func_001C25E0 / func_001C2770 / func_001B5360 —
 *                  ground and ledge queries; not one of the nine states
 *                  touches the player record.) STILL RETRACTED:
 *                  func_001B5360 is
 *                  NOT a "shared contact resolver (radius-6 box +10u
 *                  ahead)" — it is a GROUND-SNAP probe traced with
 *                  func_0019A570(from, to, 6, 0), and 6 is that
 *                  function's collision CHANNEL MASK. CONFIRMED
 *                  2026-07-31 against the recovered func_0019A570: it
 *                  does `flags = arg2 & 0xFF` and runs one callee per
 *                  bit (1 -> func_001A6440 with `arg3 & 0xFFFF` as the
 *                  exclusion id, 2 -> func_001A0B10, 4 ->
 *                  func_0019D330). Careful: func_0019AB20 — the crate's
 *                  probe — uses a DIFFERENT callee set for the same
 *                  mask bits, so a mask value does not carry across the
 *                  two probe entry points.
 *                  The bug's real bite volume and damage are NOT
 *                  decoded — CONFIRMED 2026-07-31 as a negative: none
 *                  of func_00128C10 / func_0012A5D0 / func_0012C490
 *                  writes anything into the player record D_008102B0;
 *                  every reference to it is a READ of +0xA0 feeding a
 *                  func_001B13F0 range test.
 *                  *** PORT BUG (em_enemy.c, not fixable from this
 *                  header): the port runs an APPROACH -> BITE -> RECOVER
 *                  shape that HOMES on the player. The engine brain
 *                  neither homes nor bites. Whoever owns em_enemy.c
 *                  should re-shape the bug tick to proximity-trigger ->
 *                  random-bearing turn -> timed run, using the decoded
 *                  radii/turn rates/speeds above — every one of which
 *                  was re-read out of src/func_00128C10.c on 2026-07-31
 *                  (100/150/10/24 and 20/40; 0.34906587 then
 *                  0.06981317 rad/tick; 0x3F4CCCCD = 0.8 speed at
 *                  0x40266666 = 2.6 rate; panic kinds 0x3F19999A = 0.6
 *                  at 0x3FCCCCCD = 1.6 for (rand & 0x30) + 0x3C
 *                  ticks). ***
 *   actor +0x34    HIT POINTS, s16 (crate init = 1 — func_001551B0
 *                  state 0 writes `+0x34 = 1`, and state 4 kills on ANY
 *                  nonzero +0x36 with no arithmetic at all;
 *                  bug init = 15 (slot 0x0F) / 30 (slot 0x10), 30/50 on
 *                  the difficulty byte, via func_001289C0 ->
 *                  func_00128390 — genuinely consumed by func_00129FC0;
 *                  worm init = 10 — func_00154040 writes `+0x34 = 0xA` —
 *                  but VESTIGIAL, and CORRECTED 2026-07-31 (the old
 *                  "the whole chain contains NO +0x34 access" contra-
 *                  dicted its own previous sentence): that init store is
 *                  the ONLY +0x34 touch in the worm chain — nothing ever
 *                  reads it back. The dispatcher func_00153F10 carries no
 *                  readable logic (byte-matched as raw .word) and the
 *                  sub-machine func_00154120 accesses NEITHER +0x34 NOR
 *                  +0x36 anywhere. The only +0x36 write in a worm's life is
 *                  the release teardown's `+0x36 = 0` (func_001AFC10,
 *                  BYTE-MATCHED). Worms are NOT shootable.)
 *   actor +0x36    INCOMING-DAMAGE MAILBOX, s16 — attackers write it,
 *                  the behavior polls + clears it in its own tick. There
 *                  is NO central HP system. LAYOUT (func_00129FC0,
 *                  BYTE-MATCHED): the AMOUNT is the LOW BYTE
 *                  (`*(unsigned char *)(actor + 0x36)`); the high bits
 *                  are flags — 0x4000 = play the hurt voice (arming the
 *                  60-frame cooldown at +0x28), 0x2000 = force the
 *                  knockdown branch. (The other hurt helper
 *                  func_00153B50 subtracts the whole halfword; the byte
 *                  form is the bug's.) The CRATE consumes it on its IDLE
 *                  poll, the BUG every tick (router func_00128B80 ->
 *                  reaction driver func_00129FC0); the worm never reads it.
 *                                                 -> em_enemy_damage()
 *   victim filter  BOTH filters are BYTE-MATCHED leaves (func_00183AC0,
 *                  func_00183B80 — both .word-encoded asm leaves in the
 *                  decomp) and both were read out in full.
 *                  AUDIT CORRECTION 2026-07-31, the class mask: the
 *                  2026-07 note that stood here read the immediate
 *                  right and its MASK wrong. Both leaves do
 *                  a ZERO-EXTENDING byte load of +0x02, then AND with
 *                  the sign-extended
 *                  immediate 0xFFFFFF1F. On a zero-extended byte that
 *                  is `(actor[0x02] & 0x1F) == 2` — the low FIVE bits.
 *                  All THREE flag bits 0x20/0x40/0x80 are masked off,
 *                  so a 0x80-flagged class-2 actor IS a victim. The
 *                  claim "bit 0x80 must be CLEAR", and the instruction
 *                  to make em_enemy.c mask 0x9F, were both WRONG:
 *                  the port filter must mask 0x1F.
 *                  The same 5-bit class field turns up in three other
 *                  recovered functions, which is why this reading is
 *                  safe: func_001AFA90 (BYTE-MATCHED) tests
 *                  `cls & ~0xE0` while storing the WHOLE byte at +0x02;
 *                  func_001551B0 state 2 tests `record[4] & ~0xE0`;
 *                  func_0019AB20 stages `actor[0x02] & 0x1F` as its
 *                  trace class. ~0xE0 == 0x1F: the allocator mask and
 *                  the filter mask are the SAME, they do not differ.
 *                  Each filter then switches on the MODEL byte
 *                  actor[0x03]:
 *                    func_00183AC0 (bullet/laser victim): REJECTS models
 *                      0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13;
 *                      model 0x06 is a victim only while +0x9F == 0;
 *                      every other model is a victim.
 *                    func_00183B80 (second / targetable): REJECTS 0x0D,
 *                      0x0E, 0x0F, 0x13; model 0x06 again needs
 *                      +0x9F == 0; model 0x12 is a victim only when
 *                      +0x0D == 1; every other model is a victim.
 *                  (Both reject sets re-derived instruction by
 *                  instruction from the two byte-matched leaves, audit
 *                  2026-07-31; the branch targets land on the shared
 *                  `v0 = 0` merge for every model listed.)
 *                  CONFIRMED, therefore: the WORM (model 0x0D) is
 *                  rejected by both — bullets, auto-aim and melee all
 *                  pass through it, and the model exclusion (not the
 *                  class byte) is doing the work. Worms are dodged, not
 *                  shot; their only deaths are their own burst/despawn.
 *                  The crate family (model 0x06) is a victim while
 *                  +0x9F == 0. CONFIRMED (func_00183AC0/func_00183B80
 *                  both branch to the +0x9F arm on model 6 and
 *                  return `+0x9F == 0`).
 *                  DOWNGRADED 2026-07-31 — "the BUG is a victim
 *                  (mailbox-shootable)": the recovered C does NOT
 *                  support it. The 0x0F/0x10 cited for the bug are
 *                  func_001B10B0 slot ids from func_00128AB0, not model
 *                  bytes; the bug's real +0x03 comes from the nest
 *                  registry record (func_001551B0 state 2 copies
 *                  `child[3] = record[6]`), which is disc data we cannot
 *                  read. Note that IF 0x0F/0x10 were model bytes the bug
 *                  would be rejected by func_00183AC0 exactly like the
 *                  worm. Treat "bugs are shootable" as OBSERVED (s68
 *                  live), not source-derived; the port keeps them
 *                  shootable on that basis alone.
 *   actor +0x0A    GROUP-ALARM flag. CONFIRMED 2026-07-31 against the
 *                  recovered func_001551B0 (// NEARMISS, body-correct)
 *                  state 4: on a nonzero +0x36 the dying crawler walks
 *                  the whole live actor list (the D_00275BC0 chain via
 *                  +0x1C — no radius, no distance test) and writes
 *                  `+0x0A = 1` only into a recipient that BOTH has a
 *                  nonzero +0x52 AND carries a leaping model byte
 *                  (6 / 0x1C / 0x1E / 0x1F / 0x50). +0x52 is 0 on every
 *                  placed crate (live-read s76), so a destroyed crate
 *                  wakes NO neighbour (user-reported 2026-06-12; the old
 *                  "wakes every crate" was wrong). Worms carry model
 *                  0x0D, fail the model gate, and never read it.
 *   actor +0x52    NOT "on-surface" — the sense is the other way round.
 *                  CONFIRMED 2026-07-31 (func_001551B0 state 0 tail +
 *                  the recovered func_0019AB20):
 *                  INIT stages from = (pos.x, pos.y - 2, pos.z) with the
 *                  probe extent -3.0, and func_0019AB20 subtracts that
 *                  extent from the start Y, so the traced column really
 *                  does run pos.y+1 down to pos.y-2. It writes 0 when
 *                  the probe returns 4 — func_0019AB20's mask-bit-2
 *                  (static world, func_0019C830) channel — else 1. So 0
 *                  = resting on level geometry (every placement) and 1 =
 *                  standing on a prop or on nothing. The alarm wake and
 *                  the state-2 recovery loop are both gated on the
 *                  unusual value. func_001AFA90 (BYTE-MATCHED) zeroes it
 *                  at allocation — CONFIRMED 2026-07-31,
 *                  `*(short *)(self + 0x52) = 0`. Note func_0019AB20
 *                  handles only mask bits 2 (-> func_0019F730, result
 *                  2) and 4 (-> func_0019C830, result 4); bit 1 is not
 *                  its channel at all, so mask 7 and mask 6 behave
 *                  identically there.
 *   +0x2D0..0x2EC  4 precomputed diagonal probe CORNERS (INIT, absolute
 *                  world positions that never move again). CONFIRMED
 *                  2026-07-31 in func_001551B0 state 0:
 *                  the vector (r,0,r,0) turned by the spawn matrix
 *                  (func_001026A0) and laid out 90 deg apart,
 *                  r = 4.5961943 (= 3.25*sqrt2, bits 0x40931406) for
 *                  model byte 6/0x1E and the default, 2.1213202
 *                  (= 1.5*sqrt2, bits 0x4007C3B6) for 0x1C/0x50/0x1F —
 *                  corner spans 6.5 and 3.0 units. The steer phase
 *                  (state 1 sub 0) probes each as a vertical 2-unit
 *                  column with func_0019AB20 mask 7. CORRECTED: the
 *                  +-3 deg turn (0x3D56774F ~= 0.0524 rad = pi/60) away
 *                  from the blocked side is applied ONCE, when the leap
 *                  is set up, to the leap rotation matrix at sub+0x40 —
 *                  it is NOT a per-frame heading nudge. With no side
 *                  blocked it instead jitters the three euler deltas by
 *                  (rand-0.5)/60, /50, /60.
 *   hop physics    CONFIRMED: velocity integration with vertical gravity
 *                  0.052/tick (func_001551B0 state 1 sub 1, `+0x2C8 -=
 *                  0.052f` each motion frame).
 *
 * FIDELITY NOTES (engine-true conditions + remaining flagged items —
 * details at each site in em_enemy.c):
 *  - WAKE (decoded; corrected s76; CONFIRMED 2026-07-31 by reading the
 *    recovered func_001551B0 end to end): the placed crawler IDLE wakes
 *    only via the group alarm or dies to damage — func_001551B0
 *    contains NO player-distance test (no func_0019AA80/func_0019A570
 *    call, no player-position global anywhere in the function); the old
 *    port-invented 32-u distance wake is REMOVED. AND the group-alarm
 *    wake is itself gated on the
 *    recipient's +0x52, which is 0 on every placed crate BY
 *    CONSTRUCTION (see +0x52 above: 0 is what the INIT column probe
 *    writes when the crawler is resting on level geometry)
 *    — so in practice a placed crate NEVER wakes from a neighbour's
 *    death and just sits as a destructible box until damaged (s76,
 *    user-confirmed). A lone undamaged placement sits forever. The
 *    free-roaming attacker is the WORM (no idle state, needs no wake).
 *  - DAMAGE WINDOW (decoded; CONFIRMED 2026-07-31 against the recovered
 *    func_001551B0): state 4 polls +0x36 every tick; state 1 never
 *    polls it — its only +0x36 access is `+0x36 = 0` on the landing
 *    transition into state 2. So damage written mid-run DEFERS (the
 *    boxed-in abort at state 1 sub 0 returns to state 4 WITHOUT
 *    clearing +0x36, so it kills on the next IDLE tick) and is
 *    ABSORBED when the run ends in the landing/suicide burst. State 2
 *    then re-reads +0x36 purely to decide damage-kill vs suicide burst
 *    (see DEATH below). The port implements exactly this for the crate.
 *    The WORM consumes NOTHING — now source-confirmed, not just
 *    live-observed: func_00154120 (the whole live sub-machine) accesses
 *    neither +0x34 nor +0x36, func_00153F10 is raw .word with no
 *    readable logic, and func_00154040's `+0x34 = 0xA` is a write-only
 *    init store nothing reads back (CORRECTED 2026-07-31 — the previous
 *    blanket "no +0x34 access whatsoever" was wrong about the init).
 *    So no worm code path CONSUMES either field. Combined with the
 *    model-0x0D rejection in both victim filters (file header), the old
 *    "unfound HP consumer" open item is CLOSED: there isn't one. HP=10
 *    is vestigial init data. The port's former every-tick worm mailbox
 *    poll (the shootable stand-in) is REMOVED.
 *  - LUNGE DAMAGE (decoded s62; CONFIRMED 2026-07-31 against the
 *    recovered func_00154120): the old 0x400A/amount-10 write was an
 *    invention. The engine worm damages the player only by LATCHING —
 *    `D_008102BF = 2`, `D_008104D4 = 5.0f` on the approach touch
 *    (state 0) / `15.0f` on the lunge connect (state 3), and
 *    `D_008102B0[0] |= 2`. GATE the old note missed: BOTH latches only
 *    fire when `D_008102B0[0] == 1` first, so a player already in
 *    another status never re-latches. The port posts the decoded 15
 *    (0x41700000 in src/func_00154120.c case 3; the approach latch in
 *    case 0 is 0x40A00000 = 5.0f — re-read 2026-07-31)
 *    through its player-mailbox bridge on the lunge connect (health
 *    route); the 5.0 approach latch, the == 1 gate and the
 *    latch/shake-off mechanic are UNTRANSLATED (flagged).
 *    Stalk timings, CONFIRMED 2026-07-31 in the same function: the
 *    clip-2 windup is armed on the state 1 -> 2 transition, not inside
 *    state 2. state 0 waits for
 *    the anim-done bit (0x1000), plays clip 0 and arms +0x28 = 0x78 =
 *    120 ticks; state 1 counts that down while turning toward the
 *    player XZ (D_00810350/D_00810358) at 0.0698131695 rad/tick; state
 *    2 is the windup (clip 2) and on anim-done snaps the yaw, plays
 *    clip 3 and sound 0x431; state 3 resolves.
 *    The lunge connect test: func_0019AA80(a, b, 0x20) is a SEGMENT
 *    query, not a 32-u radius. CONFIRMED by the recovered
 *    func_0019AA80: it stages the two point args as a segment at
 *    0x70003190/+0x10, writes the two 1.0f constants, and hands
 *    `arg2 & 0xFFFF` straight to func_001A7280 — so 0x20 is a mask/id
 *    handed to the sweep, not a radius. TIGHTENED 2026-07-31: the
 *    recovered func_001A7280 (a low-confidence 72% NEARMISS, but the
 *    OUTER scan stage is the verified part) reads that argument as a
 *    per-AXIS FLAG MASK — it tests bits 0x10 / 0x20 / 0x40 against each
 *    collision record's own axis flag bytes (ANDed with the world flags
 *    at +0x5C/0x5D/0x5E) and skips the record when the mask comes out
 *    0. So 0x20 selects one axis class of world geometry for the sweep;
 *    it is definitively NOT a distance. What the innermost per-vertex
 *    accept/reject stage does with a passing record stays UNRESOLVED
 *    (that stage is the low-confidence part of the recovery).
 *    DOWNGRADED 2026-07-31, the endpoints: the recovered func_00154120
 *    reads BOTH of them off the GLOBAL at D_00275B40 —
 *    `*(char **)(D_00275B40 + 0x34) + 0xC0` and
 *    `*(char **)(D_00275B40 + 0x40) + 0xC0` — NOT off the worm actor it
 *    was passed. Whether those two sub-objects are the worm's own rig
 *    nodes 13 (neck) and 16 (head), and whether the swept list is the
 *    player's +0x58 bone-anchored spheres, rests on the s66 LIVE
 *    memcheck alone; the recovered C neither confirms nor refutes it.
 *    Treat the node numbers as OBSERVED, not source-derived. The port runs it: the worm's neck->head rig segment
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
 *    The husk PICK is decoded and CONFIRMED 2026-07-31 against the
 *    recovered func_001551B0 state 2 sub 0: the whole re-orient +
 *    rebind arm is gated on `+0x36 != 0 && (model == 6 || model ==
 *    0x1E)` — i.e. damage kills only, and only those two variants; a
 *    suicide-burst landing skips it because state 1 already zeroed
 *    +0x36. Inside it, model 6 (the wooden crate, every exported
 *    scene's placements) takes func_001C6120(D_0028A56C, 0x22) — husk
 *    0x22 + its brown splinter family — and the else arm takes husk
 *    0x29 + the grey-cyan chunk family. Because of the gate, that else
 *    arm is reachable only for model 0x1E.
 *    The port's burst launches from the matching family, husk first
 *    (em_enemy.c GIB_FILES). RETRACTED: the engine adds NO knockback
 *    corpse-slide. CONFIRMED 2026-07-31 — the recovered state-2 arm
 *    (src/func_001551B0.c, case 2 sub 0) builds an
 *    identity matrix in the scratch D_700036E0 (func_001029C0), turns
 *    it by an RNG draw of 0..3 quarter-turns (1.5707964 / 3.1415927 /
 *    4.712389 rad, so the set is {0, 90, 180, 270}, four ways) and
 *    multiplies it into the corpse's own transform (func_001026D0)
 *    with the render position at +0x100 saved and restored — a random
 *    re-orientation of the husk, and no velocity anywhere. The
 *    gameplay slot still
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
 *    lunge clip's authored 21.27 u/s root speed — PROVENANCE TIGHTENED
 *    2026-07-31: that number is ASSET data (the leech clip bank, clip 3
 *    = 120 f with 42.2 u of root travel; decomp FINDINGS "CRAWLER
 *    RESOLVED" clip-bank section), NOT a constant in any recovered
 *    function. No decompiled C states a lunge speed. Note also that
 *    func_00154040 (BYTE-MATCHED) writes 0.5f to actor+0x80 —
 *    CONFIRMED 2026-07-31, `*(int *)(arg0 + 0x80) = 0x3F000000`, with
 *    +0x84 = 0x3F600000 = 0.875 and +0x88/+0x8C = 1.0f, overriding the
 *    (1,1,1,1) func_001AFA90 hands every fresh actor.
 *    DOWNGRADED 2026-07-31, what that field MEANS: the old note read it
 *    as a size scale and concluded "the live leech is half the authored
 *    size". The recovered C does not settle that — func_001AFA90 calls
 *    +0x80..0x8C the "anim scale" quad, and the one renderer we have
 *    recovered that reads it (func_001E9E60) consumes +0x80/84/88 as a
 *    per-axis COLOUR blend target and +0x8C as a W multiplier, not as a
 *    mesh scale. So do NOT scale the port's root motion or mesh by it
 *    on this evidence; the 21.27 u/s figure stands as authored ASSET
 *    data until a recovered consumer says otherwise.
 *    The connect test is the decoded segment-vs-player arm
 *    (func_0019AA80 mask 0x20, audited 2026-07-31) + the port's
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
 *     the DECODED jitter CYCLE — every clause CONFIRMED 2026-07-31 in
 *     the recovered func_001551B0 state 4: rattle
 *     func_001FBD50(self, 0x19C, 0, 300.0f); the wait is redrawn as
 *     `((rand >> 16) << 8) >> 15` = 0..255 ticks; the shudder fires on
 *     counter values that are odd AND below 9 (so exactly 1/3/5/7 —
 *     four frames), reading table column (counter-1)>>1 and row
 *     `((rand >> 16) * 7) >> 15` = 0..6 redrawn each cycle, from the
 *     0x30-byte rows / 0xC-byte columns of D_002468B0/B4/B8; and the
 *     matrix is restored cleanly (copy_qw4 from the +0x1F0 snapshot)
 *     when the counter hits 0. The whole block is gated on bit 0 of
 *     +0x0E, which INIT clears when the crate owes its nest group no
 *     untriggered record — so a gore-only crate sits PERFECTLY STILL.
 *     CONFIRMED 2026-07-31 (func_001551B0 state 0 clears bit 0 of +0x0E
 *     via `+0x0E &= 0xFFFE` when the record walk counts zero
 *     un-triggered records; state 4 runs the whole rattle/shudder block
 *     only under `+0x0E & 1`. Both the wait redraw and the row redraw
 *     are separate func_00122BB8 draws taken on the same expiry tick).
 *     The amplitudes/periods are flagged port constants —
 *     the D_002468B0/B4/B8 table values are not exported;
 *   - HP 1: DAMAGE (the +0x36 mailbox — bullet or knife) BURSTS it,
 *     broadcasting the group alarm; the engine has NO proximity
 *     trigger (decoded — the old ~10-u port trigger is removed). Husk
 *     gibs fly through the shared gib launcher and the nest-group
 *     BUGS hatch at the crate position. The REGISTRY WALK is CONFIRMED
 *     2026-07-31 in func_001551B0 state 2 sub 0: `t = D_0024A850[area];
 *     if (t == 0) t = 1; p = *(char **)(D_0024D820[area] + (t +
 *     self[0x56]) * 4)`, then 0x2C-byte records until a leading -1,
 *     each un-triggered one (func_001B11E0 == 0) spawned through
 *     func_001AFA90(record[4]) — record[4] is the CLASS byte, the
 *     argument func_001AFA90 masks with ~0xE0 and stores whole at
 *     child +0x02. The child fields the walk fills, CORRECTED
 *     2026-07-31 to the offsets the recovered C actually writes:
 *     child +0x03 (model) = record[6], child +0x2E = record[6] >> 8,
 *     child +0x0D = record[8] (this is the field the old note
 *     mislabelled "kind"; the placement kind/link pair is child
 *     +0x54/+0x56 = record[0xC]/record[0xE]), child +0x9A =
 *     record[2], and position = crate pos + record[0x10..0x18]
 *     (rotation +0xC0..0xC8 = record[0x1C..0x24]). The per-area
 *     record CONTENTS below stay DATA (disc tables the port cannot
 *     read): office links 0-4
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
 *     same nest group). RUN TIMER, CONFIRMED 2026-07-31 (func_001551B0
 *     state 1 sub 0 tail): `if (model == 6) +0x2A = (int)((60.0f *
 *     pos.y) / 12.0f); else +0x2A = 0xB4;` — so 180 ticks for every
 *     variant EXCEPT model byte 6, which instead gets 60 * its world Y
 *     / 12, and byte 6 IS the port's default crate, so that branch is
 *     the live one here. Damage during the run defers/absorbs exactly
 *     as decoded (see em_enemy.c).
 *
 * BUG KIND (s68 "CREATURE IDENTITY CORRECTION" — the crate hatchling
 * and the game's ubiquitous enemy): the 15-node insectoid. Its
 * "GLOBAL creature slot 0x0F (variant A, grey-blue chitin) / 0x10
 * (variant B red flesh)" is really the func_001B10B0(actor, 0x0F|0x10,
 * 0x11) reservation func_00128AB0 makes, keyed on the global
 * D_00810788 == 0xFF (CONFIRMED 2026-07-31: func_00128AB0 takes slot
 * 0x10 and stores sub+0xE1 = 1 when D_00810788 == 0xFF, slot 0x0F and
 * sub+0xE1 = 0 otherwise, and func_001B10B0 uses that argument as an
 * index into the resource table D_0028A490 — a SLOT id, never a model
 * byte). "Event flag 0x30" was the older name
 * for that global and is NOT re-derivable from the C — treat the flag
 * NUMBER as observed; the global is D_00810788. Variant B is UNMODELED
 * port-side: the port always loads variant A.
 * Decoded, kept: HP 15 (slot 0x0F) / 30 (slot 0x10), 30/50 on the
 * difficulty byte D_0081070A — func_001289C0 -> func_00128390, both
 * BYTE-MATCHED — and the EVERY-TICK mailbox consumption. The two-stage
 * consumption chain and the clip roles are CONFIRMED 2026-07-31:
 * func_00128B80 is only the ROUTER (any nonzero +0x36, or the global
 * instakill flag D_0081080F, forces actor[0] = 3 / state 2 / subs 0;
 * it compares no HP, and when D_0081080F is set it copies +0x34 into
 * +0x36 as its LAST act, staging a full-HP debit for the next tick), and
 * func_00129FC0 (BYTE-MATCHED) is the driver that subtracts the LOW
 * BYTE of +0x36 from +0x34 and plays FLINCH 0x1D, or KNOCKDOWN 0x1B
 * when the damage word carries 0x2000, or sub[0xFB] has bit 7 set, or
 * sub+0xE4 is 0 / 0x400 / 0x500 — a heavy-hit branch that fires on
 * survivable hits too, NOT a death clip. The corpse's own animation is
 * the COLLAPSE 0x20 (case 3, with sound 0x1B7) just before the case-4
 * free. The port asks for 0x20, falls back to 0x1B, then to the
 * frozen-pose alpha-fade. Init/idle clip 1 — but the "90-frame" length
 * is OBSERVED, not decoded (the 0x5A in the brain is a fallback timer,
 * not a clip length).
 * BRAIN, CORRECTED 2026-07-31: func_00128C10 and func_0012A5D0 are NO
 * LONGER STUBS — both are recovered (// NEARMISS) and read out at the
 * top of this file. The old "decoded structurally, 14-case sub jtbl"
 * note was RIGHT (func_0012A5D0's live state really is a 14-way
 * jtbl_0026D000) and its retraction is itself withdrawn. Two of the
 * port's brain choices are now known to DIVERGE from the engine and
 * are PORT BUGS to be fixed in em_enemy.c (this header cannot):
 *   (1) the port homes toward the player at a flagged port turn rate.
 *       The engine picks a RANDOM 16-point compass bearing every time
 *       and only uses proximity as a TRIGGER (radii 100 / 150 / 10 /
 *       24, or 20 / 40 for creature kinds >= 4; turn 0.34906587 then
 *       0.06981317 rad/tick; run speed 0.8 at anim rate 2.6).
 *   (2) the port walks to a flagged standoff and stops. The engine
 *       runs a timed leg, re-rolls the heading by +-0.69813174 rad and
 *       keeps going, with panic kinds 4/9 remapping to 3/8 at speed
 *       0.6 for 60..108 ticks.
 * Dealing NO damage remains correct as far as the C goes: no recovered
 * brain (nor the func_0012C490 leap chain, nor func_001B5360) writes
 * damage to the player, so a bug that reaches the player just crowds
 * it. Clips 0x11/0x13/0x14/0x15 are decoded as the func_0012C490 leap;
 * the rest of the attack set is still undecoded.
 * DOWNGRADED 2026-07-31 — placement-pose codes: the recovered
 * func_00129780 does NOT split its selector as "param 4/5/9 =
 * floor/wall/ceiling". Its 13-entry jtbl_0026CFA0 groups 0/1/5/6 (one
 * forward probe), 2/7 (a +-3 lateral pair), 3/4/8/9 (six +-6 axis
 * probes), 10/11 and 12 (commit a turn). The floor/wall/ceiling
 * reading was never in the code. Either way it is untranslated —
 * every port bug hatches floor-posed. MESH:
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
 * no alarm. Decoded off the recovered func_00156620 and CONFIRMED
 * 2026-07-31: HP 1 (state 0 writes `+0x34 = 1`; state 1 leaves for
 * state 2 on ANY nonzero +0x36) and the damage-only EXPLOSION (state 2
 * phase 0 fires func_001EFD20(0x80000013) at pos with Y+7, then — for
 * model byte 0x18/0x2A — FX 0x8000001C and sound 0x1A1 through
 * func_001FC580 — arms +0x28 = 2, and phase 1 counts that down and
 * hands to state 3 = free two ticks later). func_001FC580 (BYTE-MATCHED)
 * does route 0x1A1, via its 0x19F/0x1A0/0x1A1 arm — CONFIRMED. Phases
 * 2/3 of that state are a flung-debris FLIGHT arm, but model 0x18/0x2A
 * exits at phase 1 and never reaches it, so "no movement" holds for the
 * drum specifically. PURELY VISUAL: nothing in the function writes
 * damage to another actor — no radius damage, no chain.
 * CORRECTED 2026-07-31, twice:
 *   - the "idle wobble MECHANISM" was never in the engine. CONFIRMED:
 *     state 1 does not touch a transform; sub+0x74 (aim), +0x38 (speed,
 *     from D_00246A00) and sub+0x78 (pitch, from D_00246A10) are all
 *     written in STATE 2 phase 0 as flung-debris parameters. The port's
 *     wobble is removed.
 *   - the drum is NOT shootable from any range. CONFIRMED: state 1's
 *     tail takes func_001028D0(scratch, D_00810350, pos), squares it
 *     with func_00102738, compares against 50.0f * 50.0f, and only
 *     inside that publishes via `actor[1] = 1; func_001B1D20(self)`;
 *     outside it takes func_001B17A0 and is no candidate. The port gates
 *     em_enemy_acquire / ray_test / targetable on that radius
 *     (EGG_TARGET_R in em_enemy.c). MESH: assets/enemy_egg.emdl (the area-11 model-table
 * entry 0x0E carve — decomp tools/export_props.py --egg; capped-cylinder
 * drum mesh), placeholder upright box when absent. Instances: manifest
 * `enemy egg <x> <y> <z> <yaw>` lines, dispatched here (em_enemy_add_kind
 * with EM_ENEMY_KIND_EGG). Full ledger in em_enemy.c "THE EGG / DRUM".
 *
 * GENERATOR (FINDINGS "GENERATOR — func_0015A2C0 RESOLVED", session
 * 28): the engine's most-placed creature behavior (class 0x0D, model 3,
 * 129 placements) — an organic infected-growth FLOOR PAD that charges
 * while the player stands on it and births worms. Decoded machine —
 * every state below re-read 2026-07-31 out of the recovered
 * src/func_0015A2C0.c and its two box helpers
 * (boot-ELF .data tables read locally; the TABLE CONTENTS remain DATA
 * the port cannot read, and are flagged as such at each site):
 *
 *   placement      kind(+0x54) 0..6 -> config rec D_00248120, stride 5
 *                  FLOATS (`D_00248120 + self[0x54] * 5` in the
 *                  recovered init). CONFIRMED 2026-07-31 via
 *                  func_001A8840, which box-tests
 *                  |dx| <= rec[0], |dz| <= rec[2], |dy| <= 1.5 + rec[1]
 *                  — so rec[0]/[1]/[2] really are X/Y/Z HALF-extents and
 *                  the +1.5 Y tolerance is real. The "Y = 1.0" value
 *                  itself is DATA (a disc table the port cannot read).
 *                  link(+0x56) 0/1/2.
 *   INIT           link 1/2 draws ONE BYTE from count table D_002481B0
 *                  (link 1, counter D_008106EC) / D_002481D0 (link 2,
 *                  counter D_008106ED) and stores it back into +0x56 as
 *                  the RUNTIME MODE: 0 = inert pad, 1 = breather/trap
 *                  (+ an immediate pair of kind-0xE TENDRIL FIELDS,
 *                  func_0015A200(self, 0xE, 0) and (self, 0xE, 1) — see
 *                  "TENDRIL FIELD" below), 2 = WORM EMITTER.
 *                  link 0 = mode locked 0 (inert). CORRECTED
 *                  2026-07-31: the ROW index is NOT an RNG draw — the
 *                  recovered C reads `row = *(int *)0x70003B68 & 3`,
 *                  the scratchpad FRAME COUNTER, so the row is fully
 *                  deterministic in frame parity. The column is the
 *                  per-link global counter & 7, post-incremented
 *                  (CONFIRMED). Only the mode-2 DELAY pick below uses
 *                  the real RNG func_00122BB8.
 *   trigger        +0x0A = "player inside my box THIS frame" — the
 *                  pair pass func_001A8BE0 -> func_001A8840 box-tests
 *                  the player against the config extents; the behavior
 *                  consumes and clears it each tick (`self[0xA] = 0` on
 *                  the state-1 tail). CONFIRMED 2026-07-31 (both
 *                  functions recovered; func_001A8BE0 walks the
 *                  D_00275BA0 list, skips actors whose +0x00 lacks
 *                  bit 0, and dispatches to func_001A8840 exactly on
 *                  model byte 3). NOT the group alarm the crawlers use.
 *                  ONE GATE the old note omitted, and it is behavioural:
 *                  func_001A8840 only writes +0x0A (and only arms the
 *                  trap arm) when the pad's own state byte +0x0D == 0.
 *                  With +0x0D == 1 an in-box player instead fires
 *                  func_00187EC0(7, 0) and the pad never charges at all;
 *                  any other +0x0D value does nothing. The port models
 *                  only the +0x0D == 0 pad.
 *   mode 2         sub 0: 121 CONSECUTIVE in-box frames (+0x20 charge
 *                  += 1 while +0x0A, reset to 0 on exit, fires once the
 *                  charge passes 120.0) -> spawn ONE kind-0xD worm AT
 *                  THE GENERATOR ORIGIN (func_0015A200 copies +0xB0
 *                  verbatim with func_00102948, and installs
 *                  func_00153F10 as the child's brain) -> +0x2E++ ONLY
 *                  on a successful spawn; >= 4 -> sub 2 EXHAUSTED
 *                  forever (sub 2 has no body); else sub 1 = delay
 *                  D_002481F0[i] frames where CORRECTED 2026-07-31
 *                  `i = ((func_00122BB8() >> 16) * 3) >> 15` — a SCALED
 *                  draw over 0..2, not the modulo the old note claimed
 *                  (same range, different distribution: the top 15 bits
 *                  are what select the row), counted down WITHOUT
 *                  needing the player, then sub 0 again. All CONFIRMED.
 *                  The values {1800, 3600, 5400} = 30/60/90 s are DATA.
 *   mode 1         sub 0: 100-frame in-box charge driving the morph
 *                  phase +0x80 = charge/100 -> OPEN (+0x0B=1, +0x20 set
 *                  to 60 = the hold, +0x80 = 1.0, refreshed to 60 while
 *                  the player stays): breathing sound 0x42F fired when
 *                  (*(int *)0x70003B64 & 0x7F) == 0, i.e. every 128
 *                  frames, and the particle fountain func_0015A750.
 *                  All CONFIRMED 2026-07-31 in src/func_0015A2C0.c
 *                  (sub 0 fires on `!(charge < 100.0f)`, so the 100th
 *                  in-box frame; sub 1 recomputes +0x80 as +0x20/60).
 *                  The trap hit is NOT in this function
 *                  — it is func_001A8840's, and it too is CONFIRMED:
 *                  when the pad's +0x0B is set (and D_00810707 != 1 and
 *                  the player status byte == 1) it writes 5.0f at
 *                  player+0x22C and sets the player status byte to 3 —
 *                  all of it inside the `+0x0D == 0` arm noted under
 *                  "trigger" above.
 *   destructible?  NO — CONFIRMED on the strongest ground: func_0015A2C0
 *                  contains no +0x34 or +0x36 access at all, so nothing
 *                  can damage it. The only way it stops is the 4-worm
 *                  cap. CORRECTED 2026-07-31, the two supporting
 *                  arguments that used to stand here: (a) func_00183AC0
 *                  does not "reject class 0x0D" — it requires class
 *                  (+0x02 & 0x1F) == 2 (mask re-derived 2026-07-31, see
 *                  "victim filter"; the 0x9F written here before was
 *                  wrong) and rejects a MODEL-byte set that
 *                  does not include model 3; (b) the pair pass does NOT
 *                  skip model 3 — func_001A8BE0's `case 3` is exactly
 *                  the generator's arm. Neither argument holds; the
 *                  conclusion still does.
 *   visual         NOT a model-table entry. TIGHTENED 2026-07-31, the
 *                  division of labour the old note blurred:
 *                  func_001E9580 (BYTE-MATCHED) BUILDS the private
 *                  0xA060-byte record at D_00275C1C + uid * 0xA060;
 *                  func_001E9E60 only RENDERS from it (a GS/DMA display
 *                  list over 6 segments). uid is the placement's +0x0E.
 *                  CONFIRMED in func_001E9580: an 8x8 vertex
 *                  lattice at record +0x60 with a dome height profile,
 *                  per-area palette and params keyed on
 *                  (D_00810700 << 8) | D_00810701, origin from the
 *                  actor's +0xB0, and cell spacing at record +0x30/+0x34
 *                  taken straight from the two floats func_0015A2C0
 *                  stages — the config half-extents rec[0]/rec[2]
 *                  DOUBLED. All CONFIRMED 2026-07-31.
 *                  DOWNGRADED: "blending a rest
 *                  shape by the +0x80 phase" — func_0015A2C0 calls
 *                  func_001E9E60(uid, STATE), not the phase, so how (or
 *                  whether) +0x80 reaches the renderer is UNRESOLVED.
 *                  (func_001E9E60 does read an actor's +0x80/84/88 as a
 *                  per-axis colour-blend target, but it is handed the
 *                  uid, not the pad actor, so that is not a path for
 *                  this +0x80 — hence still UNRESOLVED, not confirmed.)
 *                  Nothing to export — the port uses an ORIGINAL
 *                  placeholder mound scaled to the footprint decoded
 *                  above (audit 2026-07-31), with the phase as a swell
 *                  (flagged stand-in until the morph pipeline is
 *                  reimplemented).
 *
 * PORT FIDELITY (deviations flagged in em_enemy.c): the mode draw and
 * delay pick use the module's deterministic LCG — note the engine's
 * mode draw is not random either (frame counter & 3), so this is a
 * deviation in KIND, not just in stream; the second box pass
 * (generators waking nearby D_00275BB0-list
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
 * em_enemy_alive. Decoded machine (sub-state +0x05) — every state
 * below re-read 2026-07-31 out of the recovered tick func_001549C0
 * (// NEARMISS) with its two BYTE-MATCHED gates func_00154460 (the
 * SCAN trigger box) and func_001545B0 (the 0.92x ellipse reject):
 *
 *   spawn       a mode-1 generator attaches the pair (idx 0/1 —
 *               concentric rings) at pad init, parent-linked; nothing
 *               kills them but the scene reset.
 *   0 SCAN      trigger = player inside 3x the parent pad footprint
 *               (|dy| <= 3 + recY). On trigger: anchor = (player X,
 *               pad Y, player Z); scatter 12 targets on the ring
 *               r = 5.5 + rand01*2.0 u, i.e. 5.5..7.5 (idx 0) /
 *               7.0 + rand01*2.5, i.e. 7.0..9.5 (idx 1)
 *               (CORRECTED 2026-07-31 against func_001549C0 case 0:
 *               the draw is ONE-SIDED — `5.5f + 2.0f * (rand * 2^-31)`
 *               with rand in [0, 2^31) — never below the base radius.
 *               The old "+-" reading let spikes erupt inside 5.5 u.)
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

/* Spawn kinds — the decoded engine brains (provenance audited
 * 2026-07-31):
 * CRAWLER = the worm/leech (dispatcher func_00153F10 — byte-matched but
 * only as raw .word, so it carries no readable logic; a word scan of it
 * finds no +0x34/+0x36 access either, which is as far as it can be
 * pushed. The readable arms are init func_00154040 (BYTE-MATCHED) and
 * sub-machine func_00154120 (// NEARMISS), both recovered.
 * The kind-0xD runtime creature; born attacking — the engine never
 * places one, so a manifest `enemy crawler` line is a port
 * convenience), CRATE = the placed crawler func_001551B0 (the disguised
 * prop; bursts into gibs + its nest group's BUGS on DAMAGE or at the
 * end of its alarm-driven suicide run — s68), BUG = the nest hatchling
 * (func_00128C10 / func_0012A5D0 — CORRECTED 2026-07-31: NOT stubs any
 * more, both are recovered // NEARMISS. The port's
 * approach -> in-place bite -> recover shape is a PORT INVENTION that
 * the recovered brains contradict — the engine wanders on random
 * bearings and never bites. See "BUG KIND" above for the decoded radii,
 * turn rates and speeds, and for the two flagged port bugs), EGG = the
 * AREA-11 metal DRUM fixture func_00156620 (a stationary destructible
 * decor PROP that explodes when shot — no children, no attack, no
 * movement, no idle wobble; shootable only inside 50 u — "EGG KIND"
 * above). */
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
 * <n>` manifest lines). DOWNGRADED 2026-07-31: the group COUNTS this
 * parameter carries are not source-derived — the recovered
 * func_001551B0 gives us the registry WALK (0x2C-byte records until a
 * leading -1) but never the record contents, which live in disc tables
 * the port cannot read. The counts were read out of those tables
 * locally: treat them as DATA, and the defaults below as port
 * fallbacks, not as decoded engine behaviour. `bugs` < 0 = the
 * default group (2, the office modal group — flagged fallback); 0 =
 * a gore-only crate (the office link -1 majority). `variant` = the
 * placement MODEL byte (`variant <v>`; < 0 = the default 6 — every
 * exported scene's crates): it picks the burst's HUSK FAMILY exactly
 * like the engine's damage-kill rebind (func_001551B0 state 2 sub 0,
 * CONFIRMED 2026-07-31 — byte 6 -> func_001C6120(D_0028A56C, 0x22), the wooden
 * crate's brown set; else husk 0x29, grey-cyan. The engine gates that
 * whole rebind on `+0x36 != 0 && model in {6, 0x1E}`, so its 0x29 arm
 * only ever runs for model 0x1E; the port applies the family split to
 * any variant, which is a deliberate port generalisation). */
int em_enemy_add_crate(EmGfx *gfx, const float pos[3], float yaw,
                       int bugs, int variant);

/* Place one GENERATOR pad (engine class 0x0D / func_0015A2C0 — see
 * "GENERATOR" above; em_game.c's manifest parser dispatches
 * `enemy generator x y z yaw [kind k] [link n]` lines here, defaults
 * kind 1 / link 2 for bare lines). `cfg` = the engine kind 0..6 (the
 * D_00248120 footprint row, stride 5 floats); `link` = the placement
 * link 0/1/2 (0 = inert pad, 1/2 = draw the runtime mode from the
 * count table at init — CONFIRMED 2026-07-31 against func_0015A2C0's
 * `switch (*(short *)(self + 0x56))`, where case 0 falls straight
 * through and leaves the mode at 0, and cases 1/2 overwrite +0x56 with
 * the drawn byte. The table CONTENTS stay DATA.)
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

/* Write the incoming-damage mailbox (actor +0x36): the AMOUNT is the
 * LOW BYTE, the high bits are flags (0x4000 = hurt voice + 60-frame
 * cooldown, 0x2000 = force the knockdown branch) — func_00129FC0,
 * BYTE-MATCHED. The CRATE consumes it on its IDLE poll (func_001551B0
 * state 4 kills on any nonzero value, no arithmetic), the BUG every
 * tick through the router func_00128B80 -> driver func_00129FC0 (the
 * router compares no HP; the driver debits the low byte and picks
 * flinch vs knockdown); a worm's mailbox is never read — engine-true
 * (source-confirmed 2026-07-31, not just s66 live):
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
 * (func_00183AC0 / func_00183B80, both BYTE-MATCHED and read out in
 * full 2026-07-31 — see "victim filter" at the top of this file for the
 * exact reject sets). The WORM (model 0x0D) is rejected by both — rays
 * pass through it, the auto-aim lock never fills on it, melee whiffs
 * past it. CONFIRMED. Crates (model 0x06) are victims while their
 * +0x9F is 0. CONFIRMED. DOWNGRADED: BUGS are kept shootable on the
 * s68 LIVE observation only — the 0x0F/0x10 once cited here are
 * func_001B10B0 slot ids, not model bytes, and as model bytes they
 * would both be REJECTED by func_00183AC0.
 *
 * em_enemy_acquire: nearest live VICTIM within `max_dist` of `from`
 * whose XZ bearing lies inside the facing cone (dot >= cone_cos).
 * This XZ cone is a PORT construction — no recovered function does it.
 * It remains the MELEE victim resolver (the knife's reach stand-in);
 * the BULLET's acquisition is the func_00199220 screen-cone chain in
 * em_weapon.c, fed by the two queries below. That one IS decoded
 * (CONFIRMED 2026-07-31 against the recovered func_00199220: walk
 * D_00275B8C/D_00275B94, gate on `actor[0] != 0 && func_00183B80 &&
 * *(short *)(actor + 0x34) != 0`, reject distance >= 260.0f, project
 * through the camera and apply the D_00810CA4 cone — radial
 * 50 + 55*s in lock-on mode 1, else the box 66 + 50*s by 45 + 45*s —
 * then two func_0019A570 traces, mask 0x20 onto the candidate and
 * mask 6 for world LOS, then a 3-slot insertion sort by distance).
 *
 * em_enemy_ray_test: nearest live VICTIM whose hit sphere intersects
 * the segment [from, to] (the per-victim test the bullet runs BEFORE
 * crediting a world hit). Writes the entry point. Returns index or -1.
 *
 * em_enemy_targetable: the func_00199220 candidate gate. CONFIRMED
 * against the recovered func_00199220: `actor[0] != 0 &&
 * func_00183B80(actor) != 0 && *(short *)(actor + 0x34) != 0`. Note
 * the third test is != 0, NOT > 0 (tightened 2026-07-31); it is
 * equivalent in practice only because func_00129FC0 clamps +0x34 at 0.
 * Real instance slots only (gib/pad virtual draw slots always 0).
 * (func_00199220 also rejects candidates beyond 260.0 u and applies a
 * screen-space cone before the ray tests — that part lives in
 * em_weapon.c.)
 *
 * em_enemy_aim_point: the func_00183C40 AIM POINT — the
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
