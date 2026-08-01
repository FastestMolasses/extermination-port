/* em_weapon.h — the SPR4 rifle: native weapon state machine + firing loop.
 *
 * Native translation of the engine's weapon system (decomp repo
 * Extermination/docs/FINDINGS.md "WEAPON SYSTEM", 2026-06-10, section 8
 * port contract):
 *
 *   func_001703E0 /      armed-stance tops (player +0x05 modes 0x1D..0x20,
 *   func_0016FCF0        major-state byte +0x06)  -> em_weapon_update()
 *   func_00170A60        rifle FIRE SUB-MACHINE (BYTE-MATCHED; sub-state
 *                        +0x07, families 0xA/0x14/0x1E) -> the AIM
 *                        trigger logic. Everything this header says
 *                        about the fire cadence traces to this file.
 *   func_001607D0        the input/action machine that OWNS the trigger:
 *                        semi fires off the PRESSED mask (D_00810E74),
 *                        burst/auto off the HELD mask (D_00810E70), both
 *                        through func_0017A8B0
 *   func_0017B300        RELOAD ammo move (BYTE-MATCHED): mag = min(30,
 *                        reserve), reserve NOT subtracted ->
 *                        weapon_reload(); the reload ANIM is the
 *                        D_00248B98 per-sub table pick (0x11B), see
 *                        the PLAYER ANIMS block
 *   func_001861C0        the BULLET (hitscan: one func_0019A570(origin,
 *                        end, 7, 0x20) query per shot) -> the fire-event
 *                        resolution inside em_weapon_update()
 *   gun actor +0x2E      fire-event halfword: the player SM posts 1
 *                        (func_00170A60's shot states do
 *                        `*(short *)(*(e+0x20) + 0x2E) = 1`), the gun
 *                        behavior func_00188630 consumes and clears it
 *                        NEXT tick, firing func_001861C0 +
 *                        func_00187CC0 together -> the port keeps this
 *                        exact one-frame latency. CONFIRMED against
 *                        both byte-matched files.
 *
 * ENEMY HITS (design choice, documented): on the PS2 the segment query
 * itself reports the hit ACTOR (*0x700031D4 in the scratchpad result
 * block) because movable hulls live in collision set 0. The port keeps
 * the em_collision world-geometry API untouched; instead the bullet (a)
 * aims at the round-robin slot of the decoded func_00199220 screen-cone
 * acquisition (the "TARGET ACQUISITION" block below; +5-unit overshoot,
 * the engine's targeted-endpoint rule), and (b) runs em_enemy_ray_test —
 * segment vs every live enemy's hit sphere — BEFORE crediting the world
 * hit; the nearest of enemy-vs-world wins. An enemy hit writes 5 into
 * the victim's +0x36 mailbox; the enemy behavior consumes it in its own
 * tick (crawler HP 1 = one-shot kill).
 *   CITATION CORRECTED 2026-07-31: the 5 is func_001861C0's OWN write,
 *   `*(short *)((char *)s4 + 0x36) = 5;`, on the leg that handles a
 *   locked/secondary actor hit — NOT "func_001B41F0(victim, ..., 5, 0)"
 *   as previously recorded. func_001861C0 calls func_001B41F0 with four
 *   arguments (victim, hit point, gun+0xC0 direction, the hit record's
 *   +0x1C aux); func_001B41F0 is the knockback/impact-FX applier, and
 *   its own mailbox write is `victim+0x36 = p6 | p5` from two further
 *   arguments this caller never passes. The VALUE the port writes is
 *   still source-attested; the mechanism named was not.
 *
 * UN-ACQUIRED SHOTS (CORRECTED 2026-07-31): func_001861C0's no-lock leg
 * scales the fire direction by 4.5, not by the laser's 260 —
 *     func_00103230(scratch, gun+0xC0, 4.5f);
 *     func_001028B8(scratch, scratch, gun+0xA0);
 * so a shot with no acquired target dies 4.5 units past the muzzle.
 * The rifle is a lock-on weapon: the acquisition is what aims it. The
 * port used to fly un-acquired shots the full 260 units, which let it
 * hit at ranges the original never could.
 *
 * TARGET ACQUISITION + LOCK (func_00199220 DECODED 2026-06-11 — full
 * formula decode in em_weapon.c's constants block; retires the old
 * distance + 10-deg world-cone stand-in): every AIM tick the module
 * refreshes the engine's 3-slot target table (D_008106E0/E4/E8 -> the
 * nearest three valid targets) through the decoded validity chain —
 * targetable (status/HP), dist(player, aim point) < 260, the SCREEN-
 * SPACE cone |sx| <= 66, |sy| <= 45 GS-center pixels (sx = 256*ndc_x,
 * sy = 168*ndc_y through em_gfx_last_viewproj — the engine projects
 * through its spad camera matrix 0x70003AC0; the +50s/+45s spread
 * terms ride the gun's +0x214 float, which — CORRECTED 2026-07-31 —
 * IS written: func_001854E0 sets it every frame it runs, as a
 * distance falloff off its own 65-unit probe (miss -> 0.0; hit at
 * d -> d2 = d - 20, then d2 < 0 ? 1.0 : (240 - d2)/240). It stays 0
 * for this port's stance only because func_00188630 selects the OTHER
 * drawer, func_00185760, for action codes 0x31/0x34 with
 * D_008105C8 == 0, and that drawer never touches +0x214), an actor ray
 * that
 * must hit the candidate itself (muzzle -> aim*1.2) and a clear world
 * LOS ray to that hit point. Slot 0 IS the lock: the laser flips to
 * the warm (1.0, 0.6, 0.2) beam + 5.0-unit warm dot (func_00185760's
 * locked arm) and em_weapon_lock_steer (func_0017AF70) creeps the aim
 * blends toward it at <= 0.02/frame (snap inside 0.02) — em_game's
 * player_move applies it when the stick is idle (the engine's 0x1D
 * stance clears the lock under manual steering: func_0016FCF0 state 2
 * does `if (p[0x302] != 0 || p[0x275] != 0) D_008106E0[0] = 0;`).
 * MANUAL ROUND-ROBIN — CONFIRMED 2026-07-31 in BOTH stance tops
 * (func_001703E0 and func_0016FCF0, state 2), which run
 *     if (D_00810CA4[0] == 0 && p[0x274] != 0) {
 *         p[0x2F0]++;  if (p[0x2F0] > 2) p[0x2F0] = 0;
 *     }
 * once per AIM tick (in func_001703E0 immediately before func_00199220
 * and the fire dispatch; func_0016FCF0 runs the same increment but
 * feeds its lock from func_00185A10/func_00185E30 instead — it never
 * calls func_00199220). func_001703E0 state 0 seeds +0x2F0 = 0 at the
 * stance entry. The trigger latch +0x274 is set by func_0017A8B0 on
 * each accepted trigger event and cleared by every shot state.
 * WHICH ROUNDS ADVANCE — CORRECTED 2026-07-31, re-verified 2026-07-31
 * against the BYTE-MATCHED func_00170A60 (the mechanism is always the
 * same: a round advances iff +0x274 is STILL SET when the next tick's
 * stance top runs, because the stance top's increment is the only
 * writer of +0x2F0):
 *   semi press                       ADVANCES — case 0 sets e[7] = 0xA
 *     and BREAKS without clearing +0x274, so the shot state runs on the
 *     NEXT tick, after the stance top has already incremented (see the
 *     ACCEPT->SHOT LATENCY note below);
 *   semi queued refire               ADVANCES (case 11 leaves
 *     +0x274 = 1 on the way out);
 *   burst rounds 2 and 3             ADVANCE (case 22's chain arm
 *     `if (e[0x274] != 0) e[7]--;` does not clear the latch that
 *     func_001607D0 just re-armed) — a burst walks the slots;
 *   full-auto refires                DO NOT (case 31 is the same shape
 *     plus an explicit `e[0x274] = 0;`) — a held auto burst pours every
 *     round into the slot the opening press selected.
 * The shot then aims at slot[cycle] — func_001861C0 reads the byte at
 * D_008102B0+0x2F0: 1 -> E4, 2 -> E8 (each falling back to E0 when the
 * slot is empty), anything else -> E0. The engine's reticle markers
 * over the slots (func_001DD170, in func_00199220's tail) are
 * untranslated.
 *
 * AMMO MODEL (the engine's TOTAL-pool rule, FINDINGS "INVENTORY LOCATED"):
 *   mag      D_00810C62, u8  — rounds in the magazine, max 30
 *   reserve  D_00810CB4, s16 — TOTAL rounds INCLUDING the mag
 * Each shot decrements BOTH; a reload sets mag = min(30, reserve) and
 * leaves the reserve untouched.
 *
 * FIRE MODES (D_00810C61): 0 = semi (engine sub-states 0xA/0xB),
 * 1 = 3-round burst (0x14..0x17), 2 = full-auto (0x1E..0x20).
 * TRIGGER EDGE vs LEVEL — CONFIRMED 2026-07-31 in func_001607D0's armed
 * cases (0x31/0x32/0x34/0x35), which all read
 *     if (D_00810E74 & FIRE) { if (D_00810C61 != 0) return 0;
 *                              return func_0017A8B0(p, 0); }
 *     if (D_00810E70 & FIRE) { if (D_00810C61 == 0) return 0;
 *                              return func_0017A8B0(p, 0); }
 * i.e. SEMI latches +0x274 on the PRESS edge only, BURST and AUTO latch
 * it from the HELD level only. The port fired all three off the press
 * edge, so a burst/auto trigger that was already down when the machine
 * returned to the trigger-wait state — the ordinary case after a dry-mag
 * auto reload — stalled until the player released and re-pressed. It now
 * splits by fire mode.
 * A released burst also ends at the round in flight (see 0x14..0x17).
 * PORT DEVIATION, on record (2026-07-31): on the engine the press frame
 * has BOTH masks set, and the PRESSED arm is tested first — so in burst
 * or auto mode that frame hits `if (D_00810C61 != 0) return 0;` and
 * latches NOTHING. Burst/auto therefore latch on the frame AFTER the
 * press. The port's WAIT arm reads the HELD mask, which is already up on
 * the press frame, so its burst/auto trigger accepts one tick earlier
 * than the engine's. Not corrected: the weapon regression test pins the
 * auto round count to the port's schedule and lives outside this module.
 * ACCEPT->SHOT LATENCY, also on record: func_00170A60 case 0 only WRITES
 * the shot sub-state (e[7] = 0xA / 0x14 / 0x1E) and breaks — the shot
 * state body runs on the following tick. The port fires inside the
 * accept tick, so every port shot leads the engine's by one frame (two
 * for burst/auto, with the press-frame skip above). The cadence that
 * follows the shot is engine-exact either way.
 * CADENCE INTERVAL +0x2F4 — func_0017A8B0 is BYTE-MATCHED and stores
 *     *(float *)(arg0 + 0x2F4) = (float)func_001C61D0(*(int *)(arg0+0x40), v);
 * where v is the first entry of the stance's clip table (D_00248B70 for
 * stances 0x1D/0x1E) and func_001C61D0 — also byte-matched — returns
 * the container header halfword at +2, i.e. the clip's FRAME COUNT.
 * func_001607D0 only ever calls it as func_0017A8B0(p, 0), so the row
 * is the aim-ladder base clip (0x112, 25 frames for the SPR4).
 * The shot states then overwrite it (func_00170A60):
 *   0xA  (semi)  leaves +0x2F4 alone -> the full ladder length;
 *   0x1E (auto)  `*(float *)(e + 0x2F4) = 12.0f;` unconditionally;
 *   0x15 (burst) `if (*(short *)(e + 0x28) < 2) *(float *)(e+0x2F4) = 12.0f;`
 *                — CORRECTED 2026-07-31: only rounds 1 and 2 of a
 *                burst get the 12-frame cadence. Round 3 keeps the
 *                ladder length the refire re-latch left there, so a
 *                burst's TAIL is the long interval, not a fourth
 *                6-frame slot.
 * Counter +0x276 gains 2/frame, so SEMI fires at most one round per 13
 * ticks (the cadence IS the 12.5-tick recoil replay), burst/auto rounds
 * space 6 frames. The old port-wide flat 12 made semi twice the engine
 * rate (the user-reported missing fire rate).
 *
 * FIRE SUB-STATE MACHINE — re-read 2026-07-31 directly from the
 * BYTE-MATCHED func_00170A60 (its switch cases are written in decimal:
 * 10/11 = 0xA/0xB, 20..23 = 0x14..0x17, 30..32 = 0x1E..0x20). The
 * engine's fire machine holds a SUB-STATE byte (+0x07) through the
 * cadence, and the SEMI family is rate-gated exactly like auto — one
 * press can never beat the interval:
 *
 *   0    TRIGGER WAIT: sets +0x2F2 = 1, then, if the trigger latch
 *        +0x274 is up: with ammo it picks 0xA / 0x14 / 0x1E by
 *        D_00810C61 and fires IMMEDIATELY (no counter test — the state
 *        is only reachable a full cadence after the last shot); on an
 *        empty mag it plays the dry click 0x169 and, for burst/auto
 *        ONLY (`if (D_00810C61[0] != 0) e[7]++`), steps to the
 *        release-wait state 1 — semi stays here and re-clicks on every
 *        press. If the latch is DOWN it instead tests the raw pad bit
 *        `D_00810E74[0] & 0x200` (L3) for the manual top-up. L3 is
 *        checked here and in the burst tail 0x17 — never mid-cadence.
 *   0xA/0xB  SEMI shot + cadence: +0x276 += 2/tick; a NEW press during
 *        the cadence latches the queued-shot flag +0x2A — but ONLY from
 *        counter >= interval - 8 (presses in the first ~7 ticks of the
 *        25-frame semi cadence are DROPPED, the engine sampling
 *        window); at counter >= interval (semi: the ladder-clip length
 *        25): counter = 0, then EITHER mag empty ->
 *        UNCONDITIONAL reload func_0017B300(.,1) (the dry-mag auto
 *        reload happens at the expiry, NOT on the next press), OR
 *        queued -> step back to the shot state (fires the NEXT tick),
 *        OR back to WAIT.
 *   0x14..0x17  BURST: same cadence; +0x28 counts the rounds and 3 ends
 *        the burst into 0x17. The chain to rounds 2/3 is CONDITIONAL —
 *        CORRECTED 2026-07-31; case 22's arm is
 *            else if (func_001607D0(e, 1) == 0) {
 *                if (e[0x274] != 0) e[7]--;   // fire the next round
 *                else               e[7] = 0; // straight back to WAIT
 *            }
 *        and in burst mode only func_001607D0's HELD arm re-arms +0x274,
 *        so RELEASING the trigger mid-burst ends it at the round in
 *        flight (a tap = one round) and drops to the trigger-wait state
 *        0, NOT to the 0x17 release wait. The port used to chain all
 *        three rounds unconditionally and then always land in 0x17.
 *        CORRECTED 2026-07-31 — 0x17 is NOT an
 *        8-tick pause. It is a TRIGGER-RELEASE wait, the same body as
 *        states 1 and 0x20:
 *            stop = func_001607D0(e);
 *            if (stop == 0) { if (e[0x274] == 0) stop = 1;
 *                             else { e[0x274] = 0; stop = 0; } }
 *            else stop = 0;
 *            if (stop) e[7] = 0;
 *        and func_001607D0 re-latches +0x274 for as long as the fire
 *        button is down, so the burst machine only re-arms once the
 *        trigger is RELEASED. The old 8-tick model let a held trigger
 *        chain bursts indefinitely. L3 IS honored in 0x17.
 *   0x1E..0x20  AUTO: same cadence; a still-held trigger refires via
 *        the step-back (every 6 frames). On a dry mag with no reserve
 *        it clicks and steps to 0x20 — another release wait.
 *
 * The fire tick itself performs the first cadence increment (the
 * engine's shot states fall through into the cadence head), so the
 * port seeds counter = 2 at every shot.
 *
 * KEY MAPPING (engine-faithful since the s29 live decode of the default
 * config block — em_input.h "ENGINE DEFAULT BUTTON CONFIG" has the full
 * spad 0x70003B70..7E table):
 *   R1 (E) HELD   draw + stay in the armed stance; release = holster
 *                 (config slot 0x3B7C = R1, the weapon-draw hold).
 *   CIRCLE (L)    FIRE — the real default-config trigger (config slot
 *                 0x3B78 = 0x0020 = CIRCLE, live-verified s29). CROSS
 *                 stays USE/confirm (slot 0x3B76 — the door use scan).
 *   L3 (key 2)    manual reload (top-up) — CONFIRMED 2026-07-31: the
 *                 engine tests the raw pad bit `D_00810E74[0] & 0x200`
 *                 (NOT config-mapped, unlike every other action, which
 *                 goes through a 0x70003B7x config slot), then calls
 *                 func_0017B300(., 2). That routine is BYTE-MATCHED and
 *                 its mode-2 arm reloads only when mag < 30 AND
 *                 reserve > mag, so a full mag ignores L3. Checked in
 *                 the trigger-wait sub-states only (engine states
 *                 0 and 0x17), never mid-cadence.
 *   CIRCLE (L) / SQUARE (J) while HOLSTERED = the KNIFE attacks (light
 *                 combo / heavy stab — the s36 melee decode; the
 *                 "KNIFE / MELEE" block below). SQUARE while AIMING =
 *                 the FLASHLIGHT toggle (the "FLASHLIGHT" block below).
 *
 * RELOAD SOUNDS (live-pinned s29; TIMING DECODED 2026-07-31 from
 * func_0016F600, the armed top's major-state-3 handler): reload START
 * plays 0x163 (the shared weapon-handling foley, same id as holster —
 * the engine fires it with func_001FBD50(., 0x163, 0, 300) in the same
 * tick it requests the reload clip pair, so the port keeps the two
 * together at the state entry). The MAG ACTION 0x168 (snd_0351) is NOT
 * a fixed ~0.5 s offset as the port previously assumed: it is the
 * per-sub-weapon table pick D_00248680[+0x275] (sub 0 = 360 = 0x168,
 * which independently confirms the s29 id), and func_0016F600 plays it
 * on the exact tick the reload clip's END flag lands — i.e. at the end
 * of the clip window, together with the ramp-out setup. Dropping the
 * stance mid-reload no longer cancels it: see RELOAD below.
 *
 * RELOAD SHAPE (DECODED 2026-07-31, func_0016F600's own sub-mode byte
 * +0x07 running 0..3 inside major state 3):
 *   0/1  a NINE-tick aim-blend BLEND-IN (+0x28 = 8, per-tick deltas
 *        (0.5 - blend) / 8 on +0x27C/+0x278, the pre-reload pair saved
 *        to +0x2E0/+0x2E4) BEFORE the clip pair and 0x163 are
 *        requested. NOT modelled by the port — the port has no aim
 *        blends of its own and its reload-clip regression test pins
 *        the clip at the state entry; the engine's reload therefore
 *        starts nine ticks later than the port's.
 *   2    wait for the clip-end flag (+0x200 bit 0x1000). This is the
 *        ONLY point the engine re-samples the weapon-draw hold, so
 *        releasing R1 mid-reload does NOT abort the reload — it plays
 *        out and the holster (+0x06 = 0x65) commits at the clip end.
 *        Holding it instead plays D_00248680[sub] (0x168), re-commits
 *        the aim-pose clip D_00248B88[sub] and arms the ramp-out.
 *   3    a NINE-tick blend RAMP-OUT back to the saved blends; only at
 *        its expiry does +0x06 become 2 (AIM) and firing unlock.
 * NINE, not eight — CORRECTED 2026-07-31. The counter is seeded to 8
 * but READ BEFORE its own decrement and compared against 0:
 *     cnt = *(short *)(arg0 + 0x28);
 *     *(short *)(arg0 + 0x28) = cnt - 1;
 *     if (cnt == 0) { ...commit +0x06 = 2... }
 * so it is seen as 8,7,...,1,0 and commits on the ninth tick. The same
 * pre-decrement shape drives func_001703E0's 0x63/0x64 holster blend.
 * The port models 2 and 3: the RELOAD state window is the clip length
 * PLUS nine ticks, and the mid-reload stance drop is deferred to the
 * clip's end.
 *
 * HOLSTER PATH — DOWNGRADED 2026-07-31. Only this reload leg reaches
 * major state 0x65 directly. A plain R1 release from the AIM stance
 * goes through func_001607D0 case 0x31, which sets +0x06 = 0x63; the
 * stance top then runs 0x63/0x64 (a nine-tick aim-blend ramp, same
 * pre-decrement shape as above) and only THEN falls into 0x65, which
 * is where func_001749A0(., 0x111, ...) and
 * func_001FBD50(., 0x163, 0, 300.0f) actually fire. The port jumps
 * straight to 0x65, so its holster clip and foley start ~nine ticks
 * early. Not corrected here — the missing blend state also moves
 * em_weapon_is_aiming() and therefore the aim camera, which belongs to
 * a whole-transition pass, not a constant fix.
 *
 * FIRE-CHAIN TAIL (live-pinned s29, wired with the same scheduling
 * pattern): a shot that ray-hits the WORLD (not an enemy) plays the
 * wall impact/ricochet 0x189 two frames after the fire sound (the
 * fire event's one-frame mailbox latency + one armed tick), and EVERY
 * shot ejects a casing whose floor bounce 0x16A plays 42 ticks
 * (~0.7 s) after the shot — the casing countdowns keep ticking through
 * reloads/holsters (the brass is already in the air). An ENEMY hit
 * plays no impact sound — the victim's flinch/death path owns that
 * audio. Surface-variant impact ids are NOT pinned (see the em_sfx.h
 * flag on the 0x188/0x18A/0x18B family): 0x189 plays for every wall.
 *
 * LASER SIGHT — the gun actor's per-frame drawers func_001854E0 /
 * func_00185760. DRAWER SELECT re-read 2026-07-31 from the BYTE-MATCHED
 * func_00188630 (it is the action code D_008104A0 that selects, not
 * player +0x318):
 *     if (code == 0x32 || code == 0x35)
 *         if (D_008104A1 == 1)
 *             D_008105C8 == 0 ? func_001854E0 : func_00185760;
 *     else if (code == 0x31 || code == 0x34)
 *         if (D_008104A1 == 1 && D_008105A2 != 0)
 *             D_008105C8 == 0 ? func_00185760 : func_001854E0;
 * — so this port's stance (code 0x31, D_008105C8 == 0) draws through
 * func_00185760, the 260-unit beam+dot pass, and func_001854E0 (which
 * probes only 65 units and writes the +0x214 acquisition spread) owns
 * the alternate stance. LASER HIDE WINDOW: CONFIRMED — the +0x2F2
 * (mirror D_008105A2) gate above applies to exactly the 0x31/0x34
 * group, and func_00170A60 CLEARS e[0x2F2] in every shot state
 * (cases 0xA/0x15/0x1E), re-setting it only at the cadence expiry
 * (cases 0xB queued / 0x16 / 0x1F) and on each WAIT tick (case 0).
 * The laser VANISHES from the
 * shot tick until that shot's cadence runs out, blinking one tick
 * between chained rounds — the original's laser drops out while
 * firing; the port mirrors the flag tick-for-tick (w.laser_vis). The
 * DOT billboard is offset HALF ITS SIZE off the surface along the hit
 * normal (enemy hits: back along the ray) so it never half-clips into
 * the wall. Every aim frame the gun raycasts gun+0xA0 ->
 * gun+0xA0 + dir*260 with the same query FORM as the bullet
 * (func_0019A570(origin, end, 7, 0x20)) and clips the laser at the hit
 * point (no hit: the full 260-unit endpoint — the laser still draws).
 * The 260 here is confirmed twice in func_00185760: once as the
 * func_00103230 probe scale, once as func_001E2BA0's last argument.
 * Note it is the LASER's range, not the bullet's — see UN-ACQUIRED
 * SHOTS above. Render, via em_gfx's world-space beam pass:
 *   BEAM (func_00185760 -> func_001E2BA0). The tint vec4 IS confirmed
 *     from the raw words func_00185760 stages: unlocked
 *     {0x3F333333, 0, 0, 0x3F800000} = (0.7, 0, 0, 1); with a LOCKED
 *     target (D_008106E0 nonzero — the port's lock slot 0)
 *     {0x3F800000, 0x3F19999A, 0x3E4CCCCD, 0x3F800000} =
 *     (1.0, 0.6, 0.2, 1).
 *     DOWNGRADED 2026-07-31: the beam's INTERIOR — "32 consecutive
 *     segments, per-vertex color = base * max(sin(ph), 0), random
 *     phase start, 0.025*len step" — lives inside func_001E2BA0, which
 *     is still INCLUDE_ASM in the decomp. That shape is OBSERVED from
 *     the s23 capture, not source-derived; do not treat it as decoded.
 *   DOT (func_001CD520 at the endpoint) — the CALL is CONFIRMED
 *     (func_00185760 stages every argument below); calling the result a
 *     BILLBOARD is an inference, not a decode — func_001CD520 itself is
 *     still INCLUDE_ASM, re-checked 2026-07-31. The engine call is
 *       func_001CD520(0, 2, endpoint, 0x20045BA5154222DCLL,
 *                     f12, f12, 2.0f, packed_rgba)
 *     with f12 = 3.0f unlocked / 5.0f locked, and, for
 *     v1 = (func_00122BB8() >> 0xF) & 0x1F, the colour bytes
 *       unlocked (v1 + 0x50, 0, 0, 0x80)
 *       LOCKED   (v1 + 0x70, v1 + 0x40, v1 + 0x20, 0x80)
 *     (0x80 = GS 1.0, hence the port's /0x80). The port samples the
 *     exported glow sheet for that texture key (assets/fx/
 *     laser_dot.emtx — the 0x...4222DC half of the 64-bit argument, a
 *     32x16 soft radial blob squeezed onto the square quad). Texture
 *     absent: the flat-color glow fallback.
 *     The same pass also stamps the struck actor's +0x0A byte to 0x80
 *     (func_00185760, "laser on me") — untranslated.
 * Both draws are additive with depth test on / write off, exactly the
 * GS states of the original pass (em_gfx_beam / em_gfx_beam_dot_tex).
 *
 * PLAYER ANIMS (wired 2026-06-10 s24, fire recoil s25 — FINDINGS "ANIM
 * ID MAPPING" + "FIRE ANIM MECHANISM"): the state entries drive the
 * real clips through em_game's scripted-anim mailbox (the +0x1F2
 * request / +0x20C commit path; the anim id is the clip-table id in
 * the re-exported player.emdl):
 *   DRAW    anim 0x110 once at rate 1.4 (D_00248C90 rate_scale)
 *   AIM     anim 0x112 HELD (em_game_anim_hold — the SPR4 sub-0 aim
 *           ladder base, D_00248B70[0][0]; the pitch-step blend +0x278
 *           is not translated yet). FIRE = each shot REWINDS the held
 *           clip to frame 0 at 2 frames/tick (em_game_anim_hold_restart
 *           — the engine's bone_matrix_publish samples the committed
 *           ladder clip at frame = fire counter +0x276, which resets
 *           per shot; the recoil snap is baked into the clip's front
 *           frames). There is NO separate fire clip: 0x31/0x32/0x34/
 *           0x35 are the four armed-stance ACTION CODES at +0x1F0
 *           (stance 0x1D/0x1E/0x1F/0x20 respectively — fire-mode
 *           INDEPENDENT; the fire families only read the code for the
 *           shot sound 0x164 vs 0x165), and library containers
 *           49/50/52/53 are unrelated clips (s23's id=index guess for
 *           them is corrected in FINDINGS).
 *   RELOAD  anim 0x11B (283) — THE TRUE RELOAD CLIP (decoded
 *           2026-06-11: func_0016F600's reload entry requests
 *           D_00248B98[sub-weapon]; sub 0 = 283, the slot after the
 *           aim ladder. The old 0x33 was the +0x1F0 ACTION CODE —
 *           library clip 51 is a KNOCKDOWN, the user's "stagger";
 *           the code/clip coincidence is the same trap s25 already
 *           sprang for the fire codes 0x31/0x32/0x34/0x35). 60 fr
 *           rate 1.0, HELD at its last frame until the aim hold
 *           replaces it (the second half of the stagger fix: a plain
 *           request released the clip into ~2 frames of locomotion
 *           idle before the aim pose recommitted — a full-pose snap
 *           at every reload end; the engine's stance top re-selects
 *           the stance pose every frame, no idle interlude). The
 *           state window is the clip length PLUS the decoded NINE-tick
 *           blend ramp-out (func_0016F600 sub-mode 3 — the counter is
 *           seeded 8 and read before its decrement, see "RELOAD SHAPE";
 *           this line still said "8-tick" after that correction — the aim pose
 *           re-commits at the clip's end flag but the major state
 *           only returns to AIM once the ramp counter expires, so
 *           firing stays locked for the whole span). The DRAW clip
 *           holds the same way.
 *   HOLSTER anim 0x111 once, then locomotion resumes by itself
 * Every state window gates on the committed clip's honest length
 * (em_game_anim_frames -> ceil(frames / rate) ticks); the old fixed
 * frame counts remain only as flagged fallbacks for a clip-less EMDL.
 * While aiming the player is PLANTED (movement locked to turn-in-place;
 * the decision + engine evidence live in em_game.c player_move).
 *
 * MUZZLE ANCHORING — CONFIRMED 2026-07-31 against the BYTE-MATCHED
 * func_00188630, whose two-point form is
 *     scratch = (-3.0f, D_0024A224[sel], 0.0f, 1.0f)
 *     gun+0xA0 = M * scratch                  // ray origin
 *     gun+0xB0 = M * D_0024A220[sel]          // barrel tip
 *     gun+0xC0 = normalize(gun+0xB0 - gun+0xA0)   // fire dir
 * with M = D_00810550 (the hand frame) and the row index
 *     sel = (mode == 0 && D_00810CA4 == 0) ? 7
 *         : (mode == 0 && D_00810CA4 == 2) ? 6
 *         : mode;                             // mode = D_00810525[0]
 * (the previous note's "else row 0" was loose: outside camera mode 0
 * the table is indexed BY THE CAMERA MODE). The port is camera mode 0,
 * manual aim -> row 7 = (6.0, 1.088, 0, 1). Because both points share
 * the row's y and z = 0, the fire direction is exactly the hand bone's
 * local +X axis. The hand matrix is the player palette's node-4 matrix
 * (the rifle attach node), published by the gfx layer
 * (em_gfx_last_skinned_bone — one frame of latency by construction).
 * Fallback when the loaded player EMDL lacks the weapon clips: the old
 * flagged chest-height/yaw stand-in.
 *   BEAM-DRAW ORIGIN DOWNGRADED: gun+0x1F0, where the beam starts, is
 *   NOT a hand-frame point. func_00188630 builds it as
 *   func_001026A0(act + 0x1F0, *D_00275B40 + 0x90, D_0024A2A0 + mode*16)
 *   — through the CAMERA matrix, indexed by camera mode. The port's
 *   hand-frame (3.6, 0.5, 0) is a visually-adjacent PORT STAND-IN.
 *
 * MUZZLE FLASH — CONFIRMED 2026-07-31 against func_00187CC0 /
 * func_001F4F40 / func_001F5040. func_00187CC0 spawns the FX actor and
 * seeds it with func_00102948(fx+0xB0, gun+0xB0), i.e. AT THE BARREL
 * TIP; its variant byte is 0 for the port's ordinary case (camera mode
 * != 3 and func_0015D2F0() not in {2, 0x82}); func_001F5040's variant-0
 * schedule is chunk27 model 0xD at init, model 8 on frames 0/1/2 (plus
 * a func_001F4F90(., 2.4f) pass), model 7 on frame 3, and frame 0xF
 * flips the state to "free". Scale seeds
 * 0.15 + 0.049999997*(4.656613e-10*rand) and grows by a step triple
 * seeded 0.15 and multiplied by 0.8 every frame. Additive. The
 * port draws the engine's own model-per-tick schedule as TEXTURED
 * additive billboards through the beam pass, sampling the REAL
 * exported effect sheets (assets/fx/flash_puff/_star/_ball.emtx —
 * export_props --fx; the models sample them full-frame): spawn tick
 * = the 0xD puff, ticks 0..2 = the model-8 star streak + muzzle
 * ball, tick 3+ = the model-7 star on the puff sheet. The FX
 * ROTATION LERP is TRANSLATED (2026-06-11 — retires the 0.8^t
 * intensity stand-in): from engine tick 4 the rotation triple chases
 * -128 DEGREES at 0.35/tick (func_001F5040 .L001F53A8; one value to
 * all three components, variant 0 starts 0) — the port rolls the
 * star quad around the gun axis by it (em_gfx_beam_tex_roll); the
 * textured flash draws at CONSTANT intensity (the engine writes no
 * color fade — the 0.8 decay belongs to the scale velocity), dying
 * by the model swap + scale spread + roll and vanishing at tick 15
 * (sheets absent = flat-color fallback, which keeps its old decay;
 * tracer func_001860A0 still untranslated). There is NO
 * crosshair and NO hit-pulse overlay: the real game aims with the
 * laser dot alone (s23 live aim capture).
 *
 * AIM CAMERA HOOKUP (APPLIED 2026-06-10 s24): the engine lowers the
 * camera's follow target while aiming (camera struct +0x8C target-height
 * offset, default 6.0 — FINDINGS "CAMERA SYSTEM"; the live s23 aim
 * capture ran camera mode 1 with +0x8C = 2.0 in AREA02). em_weapon
 * exposes em_weapon_is_aiming(); em_game's camera_mode_dispatch()
 * applies it by replacing its target-height term:
 *
 *   cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1],
 *       g.pos[1] + (em_weapon_is_aiming() ? cam->aim_h : CAM_TGT_HEIGHT),
 *       CAM_TGT_CAP);
 *
 * Holstered (the default) the module queues nothing and touches nothing,
 * so default-run frame output stays byte-identical to pre-weapon builds.
 *
 * ------------------------------------------------------------------------
 * KNIFE / MELEE (decoded 2026-06-10 s36 — decomp FINDINGS "KNIFE/MELEE
 * DECODED"; retires the s29 "SQUARE = unidentified action" open item).
 *
 * Engine architecture: the knife is permanent equipment with TWO attacks
 * on TWO DIFFERENT BUTTONS (NOT tap-vs-hold), dispatched by the action
 * machine func_001607D0 from the unarmed actions (+0x1F0 codes 0..7):
 *
 *   CIRCLE press (config slot 0x3B78 — the FIRE button) while no rifle
 *     is drawn -> player mode 0x21 (action code 0x36) = func_001735C0,
 *     the LIGHT 3-HIT COMBO machine;
 *   SQUARE press (config slot 0x3B74) while no rifle is drawn ->
 *     player mode 0x22 (action code 0x37) = func_00173E60, the HEAVY
 *     single stab.
 * CONFIRMED 2026-07-31 in func_001607D0's unarmed cases (0x00 and
 * 0x01..0x07), which test in this order: the two weapon-draw HOLDS
 * (0x3B7E -> stance 0x1E/code 0x32, 0x3B7C -> stance 0x1D/code 0x31),
 * then `D_00810E74 & *0x70003B78` -> +5 = 0x21, +0x1F0 = 0x36, then
 * `D_00810E74 & *0x70003B74` -> +5 = 0x22, +0x1F0 = 0x37. Both melee
 * arms read the PRESSED mask, both draw arms the HELD mask, and the
 * draws are tested first — which is why em_weapon_update runs the
 * rifle switch before melee_update.
 *
 * FLASHLIGHT (2026-06-11 weapon-fidelity pass — user-attested identity
 * for the s36 open item "what does D_00810D3C arm?"; MODEL CORRECTED
 * 2026-06-11 against the real game): while the rifle IS drawn (armed
 * stances 0x31/0x32/0x34/0x35), SQUARE routes to the SUB-WEAPON action
 * func_0017A970; with attachment 0 (D_00810CA6 == 0) it TOGGLES the
 * FLASHLIGHT — the real game's gun light (the flag the engine keeps in
 * D_00810D3C, replayed on the next rifle draw). CONFIRMED 2026-07-31:
 * func_0017A970 is BYTE-MATCHED and its D_00810CA6 == 0 / arg1 == 0
 * arm is exactly
 *     if (D_00810D3C == 0) { D_00810D3C = 1;
 *                            func_001FBD50(&D_008102B0, 0x179, 0, 300.0f);
 *                            D_008106C7 = 1; }
 *     else                 { D_00810D3C = 0;
 *                            if (D_008106C7 != 0) D_008106C7 = 0; }
 * — sound on the rising edge only, no timer anywhere, and
 * func_001607D0's armed cases route the 0x3B74 PRESSED bit to
 * func_0017A970(p, 0) and the HELD bit to func_0017A970(p, 1), so only
 * the press toggles. The flag is a PERSISTENT PREFERENCE:
 *   - toggle ON: flag set, sound 0x179 vol 300 (pinned);
 *   - toggle OFF (second press): silent (engine: 1->0 plays nothing);
 *   - REPLAY ON DRAW — CONFIRMED 2026-07-31, and now wired: the
 *     draw-completion routine func_0016F530 (BYTE-MATCHED) ends with
 *         if (D_00810CA6 == 0 && D_00810D3C != 0) {
 *             func_001FBD50(&D_008102B0, 0x179, 0, 300.0f);
 *             D_008106C7 = 1; }
 *     so re-drawing the rifle with the light already on re-announces it
 *     with 0x179. The port kept the flag but emitted no sound;
 *   - NO timer, NO auto-off, ZERO battery drain — the light never
 *     runs out (user-attested vs the original; the s28b 300-frame
 *     burst the port previously hung off this toggle belongs to the
 *     SEPARATE shoulder-light stealth system, see below);
 *   - the preference persists across holsters/re-draws until toggled.
 * SHOULDER-LIGHT BURST (the separate s28b system — FINDINGS "BATTERY
 * LOCATED ... L3 light", the player +0xA light byte family): the
 * 300-frame (5 s) auto-off burst (+0x28 = 0x12C, down-counting every
 * frame on the player spine 0x00161138; expiry commits anim/event id
 * 0x15D — the turn-off gesture AND the pinned 920 ms switch sound —
 * and drains zero battery, live-verified s28b). The port KEEPS that
 * code (em_weapon.c "SHOULDER-LIGHT BURST") but it is UNHOOKED from
 * Square — nothing arms it until the L3 stealth-light input path is
 * decoded. em_weapon_flashlight_timer() introspects it (always 0).
 * RENDERING (2026-06-11 render-decode session — retires the s47 "visual
 * TODO" flag): the ENGINE TRUTH, pinned by an exhaustive static sweep
 * of the boot ELF (decomp FINDINGS "FLASHLIGHT RENDER DECODE"), is that
 * the toggle draws NOTHING — no beam geometry, no glow sprite, and no
 * vertex-light change is keyed on D_00810D3C or player +0xA (their only
 * readers are gameplay: the enemy-AI awareness checks, the pose-row
 * substitution and the sounds; the engine's per-actor VU1 light matrix
 * carries an ALWAYS-ON camera-direction light instead, and level
 * geometry is baked). The port deliberately DEVIATES: em_weapon_update
 * sets the gfx layer's forward SPOT term (em_gfx_spot_light) from the
 * hand-frame muzzle ray (the laser's anchor; yaw fallback without the
 * clips) — gated EXACTLY like the laser, AIM phase only: light and
 * laser appear together while aiming and vanish together during the
 * draw/reload/holster clips and holstered (the reference capture of
 * the original). GEOMETRY (2026-06-11 weapon-visual pass): the spot
 * leaves the BARREL TIP (the muzzle front, gun+0xB0 — not the
 * in-receiver ray origin) and its cone angle is ASSET-DERIVED: the
 * chunk27 library's LIGHT-CONE mesh family (entries 0x10/0x11/0x16
 * — tessellated shells, apex -> radius 25 at length 200 = half-angle
 * 7.13 deg, sampling the 0x...3222E9 additive glow sheet) pins
 * tan(theta) = 0.125, so the projected disc at the ~30-unit aim wall
 * distance is ~7.5 units = 2.5x the 3-unit laser dot (the reference
 * size; the old ~12-deg cone read about twice too big). The disc
 * stays SHARP: 1-deg smoothstep rim inside the asset angle. The spot
 * term is LEVEL-ONLY (em_gfx.h): the directional character path adds
 * no spot, so the player/gun never catch their own light. The
 * VISIBLE BEAM is the real cone mesh: assets/fx/light_cone.emdl
 * (chunk27 entry 0x10, decomp export_props --cone) drawn additively
 * from the tip along the aim ray through the beam pass's triangle
 * queue — its planar-projected glow UVs fade the shell toward the
 * wide end (bright at the gun, dissolving mid-air). Intensity gain
 * is port-tuned (the engine stacks shells 0x10/0x11/0x16; flagged).
 * Deviation documented at the API (em_gfx.h "Flashlight spot
 * light").
 * EM_CAPTURE_LIGHT=1 (debug instrumentation): synthesizes ONE Square
 * toggle on the first aim frame so headless captures show the lit disc
 * (use with EM_CAPTURE_AIM=1).
 *
 * LIGHT COMBO (mode 0x21, func_001735C0; per-attack rows idx 0 of the
 * boot-ELF tables — idx 1 is an alternate-context row, anim ids
 * 0x1BD..0x1C1, selected by player +0x236, untranslated):
 *
 *   hit  anim   len  dmg  sound  gate(+0x3C vs T)  chain window
 *   1    0x10B  35   3    0x17D  T=24 (D_002486A0) open at len-19 (D0)
 *   2    0x10C  35   3    0x17E  T=26 (D_002486A8) open at len-19 (D4)
 *   3    0x10D  50   5    0x17F  T=41 (D_002486B0) none (combo ends)
 *
 *   (Clip lengths CORRECTED 2026-06-11: the s36 table measured the
 *   pre-directory-fix bake — the old enumeration shifted every player-
 *   library id >= 54 by up to +3, so "0x10B = 50fr" was really 0x10E's
 *   length. True directory lengths: 0x10B 35, 0x10C 35, 0x10D 50,
 *   0x10E 50, 0x10F 25, 0x110 20, 0x111 20, 0x112 25 — ASSET-derived,
 *   read from a fresh fixed-resolver bake that is byte-identical to the
 *   re-exported player.emdl. These are clip-directory measurements, not
 *   a source decode: no recovered function states a clip length, and
 *   anim_ticks() prefers the loaded EMDL's own value anyway.)
 *
 *   - The swing sound + the damage-mailbox write fire together at the
 *     IMPACT gate, unconditionally (range gating is the TARGET's job,
 *     below). +0x25E hit-marker codes 0x81/0x82/0x82 feed the per-swing
 *     effect dispatcher (func_00187350 -> func_00182430, untranslated).
 *   - COMBO: a FIRE-button press during the swing buffers in +0x2E; at
 *     the chain window the next attack starts (blend 1.0). The buffered
 *     chain is checked on the WHIFF path — a CONFIRMED hit instead
 *     EARLY-EXITS to the recover states (engine: the target's +0x0A
 *     flag read back the tick after the mailbox write -> state 0x50).
 *   - RECOVER (states 0x50/0x51/0x52, hit-confirm only): a FIVE-tick
 *     pause — CORRECTED 2026-07-31; 0x50 seeds +0x28 = 4 and 0x51 reads
 *     the counter before its own decrement (`t = +0x28; +0x28 = t - 1;
 *     if (t == 0)`), so it is seen as 4,3,2,1,0 and the clip commits on
 *     the fifth tick. Same pre-decrement shape as the reload ramp
 *     (NINE, not eight); verified in func_001735C0 and in the
 *     BYTE-MATCHED func_00173E60. Then anim 0x10F (25 fr, idx-0 arm,
 *     blend 4.0 — idx 1 uses 0x1C1), then the 0x63/0x64 exit ramp.
 *     A whiffed swing exits at clip end with NO recover anim.
 *
 * HEAVY (mode 0x22, func_00173E60): anim 0x10E (50 fr, via the
 * D_002754A8 row), damage 0xF = 15 at gate T=43 (D_00248700), sound
 * 0x17F, marker 0x83, release T=29 (D_00248704), then clip-end exit /
 * the same hit-confirm recover. During the swing func_00173DD0 steers
 * yaw toward the goal at D_002486F0[gait] deg-style rates (PORT: the
 * player stays planted — steer untranslated, flagged).
 *
 * TIMING NOTE (2026-06-11: the corrected clip lengths RESOLVE the s36
 * contradiction): under the true lengths the down-count reading
 * impact_tick = len - T is self-consistent for ALL FOUR attacks —
 * impacts at frame 11/9/9 (light 1..3) and 7 (heavy), each safely
 * BEFORE its release gate (len - releaseT = 15/20/20 light, 21 heavy)
 * and inside the clip; the up-count reading would put every release
 * before its impact. The port keeps impact_tick = max(3, len - T)
 * (the 3 covers the request->commit mailbox latency + blend-in) — a
 * live capture remains the final word but is no longer load-bearing.
 *
 * DAMAGE / RANGE: the engine machines write the damage to the melee
 * target link (player +0x18) +0x36 mailbox and let the TARGET-side
 * polls do the range work (e.g. func_00219870 state-1 reads the link's
 * status byte and runs its own func_0019AA80 segment/proximity test) —
 * there is NO global knife-range constant in the player code. The port
 * resolves the victim at the impact tick: nearest live enemy within
 * EM_MELEE_REACH (12.0 — the engine's documented hands-reach, the
 * use-scan dist^2 <= 144 of func_0019A910 mode 6; PORT STAND-IN) inside
 * a 60-degree frontal cone (PORT constant), damaged through the same
 * +0x36 mailbox as the bullet (em_enemy_damage).
 *
 * VISUAL (RESOLVED 2026-06-11 — retires the s36 "no rebind found"
 * flag): there IS no rebind, and none is needed. The hip-HOLSTER node
 * 14 (the knife's attach node, s9) is itself KEYED INTO THE HAND by
 * the swing clips: in every true melee clip (0x10B..0x10F) node 14
 * rides hand node 20 at ~1.0 u for the whole swing (hip distance
 * balloons to 9-15 u) and re-seats on the thigh at the end; in idle/
 * walk/reload it stays parked at the thigh (3.9 u constant). The
 * knife model attached to node 14 therefore swings with the attack
 * automatically — the port's player.emdl (node-14 attachment) already
 * renders this correctly with the corrected clips.
 *
 * KEY MAPPING: CIRCLE = L (light 3-chain, holstered only), SQUARE = J
 * (heavy stab holstered / FLASHLIGHT toggle while aiming). NOT
 * inverted — re-checked 2026-07-31 directly in func_001607D0's unarmed
 * case 0x00, where the CIRCLE slot (0x3B78) writes +5 = 0x21 /
 * +0x1F0 = 0x36 and the SQUARE slot (0x3B74) writes +5 = 0x22 /
 * +0x1F0 = 0x37, in that order.
 */
#ifndef EM_WEAPON_H
#define EM_WEAPON_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_collision.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Weapon states — native names; engine mapping in the comments (the
 * armed-stance major-state byte player +0x06, jtbl arms in section 2 of
 * the FINDINGS "WEAPON SYSTEM" write-up). */
enum {
    EM_WPN_HOLSTERED = 0, /* not in an armed stance (+0x05 not 0x1D..0x20) */
    EM_WPN_DRAW,          /* major 0 ENTER (func_001703E0 state 0,
                           * CONFIRMED): func_0017B300(., 0) reload-if-
                           * empty, then func_001749A0(., 0x110, 0, 1.0f);
                           * the same entry zeroes +0x276, +0x274, +0x2F2,
                           * +0x2F0, +0x275 and the mailbox halfword +0x2E,
                           * and seeds the aim blends to 0.5. NO SOUND —
                           * corrected 2026-07-31: state 0 plays nothing.
                           * Major 1 (wait for the 0x1000 clip-end bit) is
                           * collapsed into the port's DRAW timer, and its
                           * exit — func_0016F530, BYTE-MATCHED — is what
                           * fires 0x162 at vol 300 (not 150) plus a 0x179
                           * re-announce when the flashlight preference
                           * D_00810D3C is already set. The port now plays
                           * both at DRAW -> AIM                          */
    EM_WPN_AIM,           /* major 2 AIM/FIRE loop (fire sub-machine)      */
    EM_WPN_RELOAD,        /* major 3 (func_0016F600): anim 0x11B gates
                           * firing; the mag is already refilled
                           * (func_0017B300 first)                         */
    EM_WPN_HOLSTER        /* major 0x65: func_001749A0(., 0x111, 0, 1.0f)
                           * + func_001FBD50(., 0x163, 0, 300.0f). NOTE
                           * (2026-07-31): the engine reaches 0x65 from
                           * an R1 release only via the 0x63/0x64 blend
                           * (nine ticks) — the port collapses those
                           * away, see "HOLSTER PATH" above            */
};

/* Fire-mode select — the engine's D_00810C61 values. */
enum {
    EM_WPN_MODE_SEMI  = 0,
    EM_WPN_MODE_BURST = 1,
    EM_WPN_MODE_AUTO  = 2
};

/* Reset to HOLSTERED with this ammo state (mag = rounds in the magazine,
 * reserve = TOTAL pool including the mag). Scene-init slot. */
void em_weapon_reset(uint8_t mag, int16_t reserve);

/* Per-frame update: the player-side state machine + fire sub-machine and
 * the gun-side fire-event consumption (one-frame latency, see header).
 * `coll` (may be NULL / empty) is the world the hitscan ray runs through;
 * the ray leaves from the hand-bone muzzle point along the gun axis
 * ("MUZZLE ANCHORING" above; chest-height/yaw is the flagged fallback
 * for a player EMDL without the weapon clips). Call once per gameplay
 * frame. */
void em_weapon_update(const EmCollision *coll, const float player_pos[3],
                      float player_yaw, const EmFrameInput *in);

/* Queue this frame's weapon visuals: the LASER SIGHT (world-space beam +
 * hit dot through em_gfx_beam/em_gfx_beam_dot — header block above) in
 * the AIM state, plus the MUZZLE FLASH FX while one is alive (a flash
 * outlives a stance drop, like the engine's pool FX actor). Also caches
 * `gfx` for the update stage's hand-bone reads. Queues nothing while
 * holstered with no live flash, so the default frame stays
 * byte-identical. */
void em_weapon_render(EmGfx *gfx);

/* 1 while the weapon is in the armed stance (DRAW / AIM / RELOAD — the
 * engine's player weapon modes 0x1D..0x20), 0 holstered/holstering. The
 * camera consumes this for the aim-state target-height offset (struct
 * +0x8C) — see "AIM CAMERA HOOKUP" above for the one-line em_game use. */
int em_weapon_is_aiming(void);

/* --- KNIFE / MELEE (header block above; engine modes 0x21/0x22) -------- */

/* Melee phases — native names; engine mapping in the comments. */
enum {
    EM_MELEE_IDLE = 0,   /* not in a melee mode (+0x05 not 0x21/0x22)   */
    EM_MELEE_SWING,      /* a light hit 1..3 or the heavy stab playing
                          * (engine majors 1..3 of func_001735C0, or
                          * 1..4 of func_00173E60)                      */
    EM_MELEE_RECOVER     /* hit-confirm recover (engine 0x50/0x51/0x52:
                          * a FIVE-tick pause + anim 0x10F. 0x50 seeds
                          * +0x28 = 4 and 0x51 reads it BEFORE its own
                          * decrement, so it is seen as 4,3,2,1,0 —
                          * verified in func_001735C0 and the BYTE-
                          * MATCHED func_00173E60; MELEE_RECOV_PAUSE.
                          * This line still said "4-tick" after that
                          * correction landed in the code.             */
};

/* 1 while a melee attack (swing or recover) owns the player — em_game's
 * player_move plants the player exactly like the armed stance (the
 * engine's melee modes replace the locomotion modes outright; the heavy
 * yaw steer func_00173DD0 is untranslated, flagged). */
int em_weapon_is_melee(void);

/* Introspection (debug / self-tests). */
int em_weapon_melee_state(void);  /* EM_MELEE_*                          */
int em_weapon_melee_combo(void);  /* current light combo hit 1..3, or 0
                                   * (the heavy reports 0; see _heavy)   */
int em_weapon_melee_heavy(void);  /* 1 = the active swing is the heavy   */
int em_weapon_melee_swings(void); /* attacks started since reset         */
int em_weapon_melee_hits(void);   /* impact-tick victims since reset     */

/* FLASHLIGHT (header block above): em_weapon_flashlight = the
 * persistent D_00810D3C preference flag (1 while set — note the SPOT
 * renders only in the AIM phase). em_weapon_flashlight_timer = frames
 * left on the SEPARATE shoulder-light burst (the dormant s28b system;
 * 0 until its L3 input path is decoded) — self-test introspection. */
int em_weapon_flashlight(void);

/* --- LIGHT BEACON (engine D_008106C7) --------------------------------
 *
 * DECODED 2026-07-31 against the byte-matched func_0017A970. The engine
 * keeps a SECOND flag alongside the flashlight preference: turning the
 * light on sets D_008106C7, turning it off clears it. The port had no
 * equivalent, which is why the flashlight here is purely cosmetic.
 *
 * It is what makes the light matter. Its consumers in the recovered C:
 *
 *   func_00138900 (byte-matched) — the 0x138xxx actor family's state 2.
 *     While the beacon is set AND the listener (D_00810360) is within
 *     150.0 units (func_001B15D0), a per-frame counter climbs; when it
 *     ticks the actor ADVANCES OUT OF ITS DORMANT STATE, plays clip 3
 *     and fires cue 0x816 at 300.0. Standing near a dormant actor with
 *     the light on wakes it. That is a stealth mechanic the port does
 *     not have.
 *   func_0016F5D0 (byte-matched) — consumes it as a ONE-SHOT: if set,
 *     clears it. So the beacon is not simply a mirror of light_on.
 *   func_0018A6B0 — clears it whenever the mode byte D_00810CA6 leaves 0.
 *   func_00185A10 — picks a movement mode on beacon == 0.
 *   func_0016F530 (byte-matched) — sets it on an entity state-kick,
 *     gated on D_00810CA6 == 0 && D_00810D3C != 0.
 *   func_00188ED0, func_001D1C50 — further gates, both NEARMISS.
 *
 * NOT YET WIRED TO ENEMIES, deliberately. func_00138900 is state 0 of a
 * six-state dispatcher (func_001386E0) for an actor family at 0x138xxx —
 * it is NOT the crate (0x1551B0) or any kind this port currently models,
 * and attaching its 150-unit wake to the crate would be inventing a
 * mapping the decomp does not support. Wire it when that actor family is
 * ported; the flag and its accessors are here so nothing has to be
 * re-derived then. */
int  em_weapon_light_beacon(void);
void em_weapon_light_beacon_clear(void);
int em_weapon_flashlight_timer(void);

/* LASER visibility (self-tests): 1 = the beam pass draws the laser this
 * frame. AIM state AND the engine's +0x2F2 flag — the LASER HIDE
 * WINDOW, CONFIRMED 2026-07-31 against the BYTE-MATCHED pair: the
 * gun-tick gate func_00188630 requires D_008105A2 != 0 for the
 * 0x31/0x34 stance group before it will run a laser drawer at all, and
 * func_00170A60 writes e[0x2F2] = 0 in each shot state (0xA/0x15/0x1E)
 * and e[0x2F2] = 1 only at a cadence expiry (0xB queued, 0x16, 0x1F)
 * or on a WAIT tick (case 0). So the laser vanishes during each shot's
 * cadence and blinks for one tick between chained rounds — exactly the
 * original's laser dropping out while firing. */
int em_weapon_laser_visible(void);

/* TARGET LOCK + ROUND-ROBIN (the "TARGET ACQUISITION + LOCK" header
 * block — func_00199220 / func_001861C0 / func_0017AF70):
 *
 *   em_weapon_lock_target — the lock slot 0 (D_008106E0) as an
 *     em_enemy index, -1 = no lock. Valid during the AIM state only
 *     (cleared everywhere else, like the engine's stance entries).
 *   em_weapon_target_slot(k) — slot k of the 3-slot table (0..2 =
 *     D_008106E0/E4/E8), -1 = empty. Introspection/self-tests.
 *   em_weapon_target_cycle — the +0x2F0 round-robin index 0..2.
 *
 *   em_weapon_lock_steer — the func_0017AF70 LOCK STEER: maps the
 *     muzzle->lock angular error into aim-blend space by the baked
 *     ladder half-angles and creeps <= 0.02 blend units/frame toward
 *     it (snap inside 0.02; full constants in em_weapon.c). Returns 0
 *     (outputs untouched) without a lock / outside AIM / during a
 *     shot's cadence; else 1 with the steered blends.
 *     GATES, re-verified 2026-07-31: func_0017AF70's own first
 *     statement is `if (arg0[0x2F2] == 0) return;`, and its only
 *     caller-side gate is func_00170A60's head,
 *     `if (a1 == 0) { if (D_008106E0[0] != 0) func_0017AF70(); }`.
 *     NOTE that func_001703E0 (the stance top the port otherwise
 *     follows) calls func_00170A60(p, 1), so in THAT top the steer
 *     never runs; it is func_0016FCF0's state 2 that calls
 *     func_00170A60(p, 0). The port applies the steer unconditionally
 *     in AIM — a documented widening, not a decode.
 *     em_game's player_move calls it after the manual stick steer with
 *     the stick idle — the engine's order and its manual-input
 *     lock-drop (func_0016FCF0 state 2:
 *     `if (p[0x302] != 0 || p[0x275] != 0) D_008106E0[0] = 0;`). */
int em_weapon_lock_target(void);
int em_weapon_target_slot(int k);
int em_weapon_target_cycle(void);
int em_weapon_lock_steer(const float player_pos[3], float player_yaw,
                         float pitch_in, float yawb_in,
                         float *pitch_out, float *yawb_out);

/* Live ammo state — the HUD's EmPlayerStatus mirrors these. */
uint8_t em_weapon_mag(void);
int16_t em_weapon_reserve(void);

/* Fire-mode select (D_00810C61). Out-of-range values are ignored. */
void    em_weapon_set_fire_mode(uint8_t mode);
uint8_t em_weapon_fire_mode(void);

/* Introspection (debug / self-tests). */
int em_weapon_state(void);    /* EM_WPN_* */
int em_weapon_shots(void);    /* rounds actually fired since reset */
int em_weapon_reloads(void);  /* reloads (auto + manual) since reset */
int em_weapon_last_hit(void); /* last resolved shot: 1 hit, 0 miss, -1 none */

/* The honest state windows, in gameplay ticks: ceil(clip frames / rate)
 * for the committed anim (draw 0x110 @1.4, reload 0x11B @1.0, holster
 * 0x111 @1.0), or the flagged fallback constants when the loaded player
 * EMDL lacks the clip. em_weapon_reload_ticks adds the blend ramp-out
 * of func_0016F600 sub-mode 3 — NINE ticks, not eight (the counter is
 * seeded 8 but read before its decrement; see "RELOAD SHAPE") — so the
 * RELOAD state, and the fire lock with it, outlasts the clip by those
 * nine ticks. The weapon self-test derives its checkpoint schedule from
 * these, so the test stays honest for any asset. */
int em_weapon_draw_ticks(void);
int em_weapon_reload_ticks(void);
int em_weapon_holster_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_WEAPON_H */
