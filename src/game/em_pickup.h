/* em_pickup.h — collectible items + display props: the engine's
 * item-pickup system, decoded 2026-06-11 (decomp repo
 * Extermination/docs/FINDINGS.md "ITEM PICKUP SYSTEM FULLY DECODED")
 * and RE-VERIFIED against the recovered C 2026-07-31 (see the
 * "RE-VERIFIED" notes below — three of that decode's statements were
 * WRONG and the port carried the bugs; FINDINGS itself is still stale
 * on them).
 *
 * SECOND AUDIT PASS 2026-07-31. Every "RE-VERIFIED"/"CONFIRMED" note in
 * this header was re-read against the decomp source a second time and
 * all of them hold verbatim (func_0015AFA0, func_001C4820, func_001B1190,
 * func_001B11E0, func_0015AC00, func_0015AE20, func_00183EF0,
 * func_001C40B0, func_001F1110, func_001F1180, func_001B6EA0,
 * func_001C47A0/4720/4760). Two NEW corrections came out of that pass,
 * marked CORRECTED-2 below:
 *   (1) the use scan arms ONE object across the WHOLE interactive list
 *       (func_00184BA0) — items and examines could both fire on a single
 *       CROSS press in the port; and
 *   (2) func_001C40B0's count-array ledger here was incomplete: ten cases
 *       besides `default` also write the count array, UNCLAMPED.
 *
 * THE DECODE OVERTURNS the s11/s15/s17 framing: the main placement
 * tables' kind-0xB records (class 0x0004, behavior func_001C4820 — the
 * office supply-room ammo-box/crate stacks) are DISPLAY PROPS in the
 * engine. They never carry the interactive class flag 0x80, are never
 * pushed onto the use scan's interactive list, and func_001C4820 has
 * no take path (RE-VERIFIED: src/func_001C4820.c is BYTE-MATCHED and
 * is a three-way switch on the actor state byte +0x04 — 0 INIT
 * func_001B0FD0/func_001C6380, 1 func_001B17A0 + the +0x4C method,
 * 2/3 func_001AFC10 free. There is no +0x0B reader and no take call).
 * The engine's real COLLECTIBLE ITEMS come from the
 * per-area DEFERRED-SPAWN REGISTRY:
 *
 *   D_0024D820[area] -> [sub-state] -> group lists of 0x2C records
 *   (func_001B6910 / func_001B6660), spawn-condition gated — cond 1 =
 *   "spawn only if NOT taken": taken(puid) = bit `puid` of the
 *   per-area TAKEN ARRAY D_00810860 + 32*D_00810700(area), SET by
 *   func_001B1190 when the collected actor frees and TESTED by
 *   func_001B11E0 at every area (re)load. That bit array IS the
 *   engine's pickup persistence. RE-VERIFIED: both functions take the
 *   puid as a BYTE and BAIL OUT on (arg & 0xFF) == 0
 *   (src/func_001B1190.c byte-matched: `v1 = a0 & 0xff; if (v1 == 0)
 *   return;`), so a record whose puid byte is 0 neither persists nor
 *   is ever suppressed — see the uid note under PORT MAPPING.
 *
 * Engine flow, function by function:
 *
 *   func_0015AC00   item INIT (src/func_0015AC00.c, NEARMISS — logic
 *                   authoritative). RENDER SCALE switched on the
 *                   MODEL/library id byte +0x0D (NOT the item type):
 *                   1.5 for id 0x5B, 2.0 for id 0x6D and the explicit
 *                   set {0x40,0x41,0x42,0x45,0x4D,0x4E,0x4F,0x55,
 *                   0x56,0x57,0x59,0x6C}, else 1.0 — written to
 *                   +0x60/+0x64/+0x68, i.e. the SCALE leg of the
 *                   func_001C6380 world TRS (NOW PORTED, see below;
 *                   it used to be silently dropped and every item
 *                   drew at 1.0). Model bind branches on the low
 *                   nibble of the take-family byte +0x03: nibble 1 ->
 *                   +0x80..0x88 = 4.0 and func_001B0FD0 (the per-area
 *                   table *(D_0028A59C)); else func_001B1020(self,
 *                   *(self+0x0D), -1, 0) -> the GLOBAL chunk27
 *                   library *(D_0028A56C). Then func_001C6380(self),
 *                   status byte +0x00 = 1, +0x08 = 3 (the use-scan
 *                   ITEM archetype), +0x30 = &D_00275488 (the radius
 *                   descriptor; its {10.0, 3.5} CONTENTS are .rodata,
 *                   asserted by FINDINGS and NOT visible in the
 *                   recovered C — see the constants below), and the
 *                   +0x2D0 timer sub-struct via func_001F1110 with a
 *                   class arg (nibble 1 -> 1, 2 -> 4, 0 -> 5 when
 *                   +0x0D == 0x34 else 0, default 0). NOT ported —
 *                   flagged, the s60 "pickup-instance auras" item.
 *   func_00184BA0   player USE SCAN (src/func_00184BA0.c, NEARMISS) —
 *                   runs ONLY on the CROSS press edge (D_00810E74 &
 *                   spad 3B76, s58); bails whole-frame if any of
 *                   0x70003B8D / D_0028A9A0 / D_008106EF is set, then
 *                   walks last frame's object list keeping only
 *                   objects with +0x00 bit 0, +0x02 bit 0x80 and
 *                   +0x0B == 0, per candidate:
 *                   RE-VERIFIED: the walk keeps ONE winner for the whole
 *                   press — `best = 10000.0f` then, per passing
 *                   candidate, `v = *(float*)0x70003B98; if (v < best)
 *                   { best = v; winner = obj; }`, and after the loop
 *                   `winner[0xB] = 4; *(char*)0x70003B8D = 3; return 1`
 *                   (a func_00183EF0 return of 2 short-circuits and arms
 *                   that candidate immediately). ITEMS AND EXAMINE
 *                   OBJECTS ARE ENTRIES OF THE SAME LIST, so one press
 *                   can arm at most one of them — see CORRECTED-2 under
 *                   PORT MAPPING.
 *   func_00183EF0   archetype-3 ITEM branch (jtbl_0026D810[3];
 *                   src/func_00183EF0.c, NEARMISS — logic
 *                   authoritative):
 *                     - XZ distance <= desc[0] (= 10.0 per FINDINGS)
 *                     - dy = player.y - item.y in [-(desc[1]+17),
 *                       +desc[1]] (desc[1] = 3.5 per FINDINGS; the
 *                       17.0f literal IS in the recovered C and
 *                       widens the window for items ABOVE the player
 *                       — shelf items)
 *                     - FACING: |wrap(player_yaw - bearing-to-item)|
 *                       <= **pi/2**, AUTO-PASS when distance <= 7.0
 *                       (take-family 0 / default; family 1 instead
 *                       tests |wrap(pi + player_yaw - item_yaw)|,
 *                       family 2 with class-nibble 7 is the
 *                       wall-mount pitch variant — all three feed the
 *                       SAME pi/2 gate).
 *                     CORRECTED 2026-07-31: this was pi/4 in the port
 *                     and in FINDINGS. The recovered C's case-3/case-4
 *                     block ends in `if (fabs(ang) <= 1.5707964f)
 *                     { ...; return 1; } return 0;` and RETURNS from
 *                     inside — it never reaches the 0.7853982f (pi/4)
 *                     tail, which only cases 0/1/2 fall through to.
 *                     The port was rejecting items the engine accepts
 *                     over the whole 45..90-degree off-axis band.
 *                   func_00183EF0 == 2 takes immediately; otherwise
 *                   the nearest passing candidate by the distance
 *                   parked at spad 3B98 wins -> actor +0x0B = 4,
 *                   spad 3B8D = 3.
 *   func_0015AE20   the ARMED handler (behavior func_0015AFA0 state 1;
 *                   src/func_0015AE20.c, NEARMISS — arms on
 *                   `+0x0B & 4`, a BIT test):
 *                   queues one of two take SCRIPTS —
 *                     - player in action 0x2D or D_008104E6 set: the
 *                       INSTANT script D_00248480 {op7 subD enter
 *                       scripted, op9 CALL func_001B6EA0, op7 sub4
 *                       exit | STOP}
 *                     - else the GRAB-ANIM script D_002482C0: the
 *                       pick-up anim id is patched by ITEM HEIGHT vs
 *                       player.y (D_00810354): y < py+6 -> 0x42 (low),
 *                       y < py+13 -> 0x41 (mid), else 0x40 (high);
 *                       then op-A wait-anim-done, the op-9 take, exit.
 *                       CONFIRMED verbatim: the recovered C computes
 *                       `lo = 6.0f + D_00810354` and tests item.y
 *                       (+0xB4) `< lo` then `< 7.0f + lo` — i.e. the
 *                       port's py+6 / py+13 tiers are exact.
 *                   Script completion -> lifecycle 2 -> func_001B1190
 *                   (SET the taken bit from +0x9A) + func_001AFC10
 *                   free: the item DESPAWNS.
 *   func_001B6EA0   the TAKE NATIVE (op 9; BYTE-MATCHED): by take
 *                   family +0x03, item type read as a u16 at +0x2E —
 *                     0 -> func_001C47A0(type, 1): the INVENTORY ADD
 *                          switch func_001C40B0 then the FOUND request
 *                          D_008106B0 = 1 / D_008106B1 = type
 *                     1 -> func_001C4720: MAP array D_00810CB8[type]++,
 *                          request kind 2
 *                     * -> func_001C4760: KEY-ITEM array
 *                          D_00810CC3[type]++ ALWAYS, request kind 3
 *                          only posted for types >= 0x20
 *                   (all four BYTE-MATCHED: src/func_001B6EA0.c,
 *                   func_001C47A0.c, func_001C4720.c, func_001C4760.c)
 *   func_001C40B0   the inventory switch (s18's "0x001C4100";
 *                   src/func_001C40B0.c, NEARMISS — logic
 *                   authoritative). The DEFAULT case does
 *                   count[D_00810C64 + type] += n, then clamp
 *                   `>= 100 -> 99`.
 *                   CORRECTED-2 2026-07-31: "default case only" was
 *                   WRONG. The recovered C's cases 0x01, 0x02, 0x03,
 *                   0x04, 0x0C, 0x0D, 0x0E, 0x1B, 0x1C and 0x1D ALSO do
 *                   `D_00810C64[arg0] += arg1` — but with NO clamp (the
 *                   u8 simply wraps); their clamps apply to the linked
 *                   METER, not to the count. Full ledger of the recovered
 *                   switch, so nobody has to re-derive it:
 *                     0x01           count += n; D_00810CA8 += 6
 *                     0x02, 0x03     count += n; D_00810CAA += 6
 *                     0x04           count += n; D_00810CAC += 1
 *                                    (on cap also D_00810CAE = 0)
 *                     0x0C,0x0D,0x0E count += n; D_00810CB0 += 1 ONLY
 *                                    when D_00810C70/71/72 are all set
 *                     0x0F           count = n (raw store, no add/clamp)
 *                     0x10           the magazine case, below
 *                     0x11 / 0x12    D_00810CA8 += n*6 / n*12 (no count)
 *                     0x13 / 0x14    D_00810CAA += n*6 / n*12 (no count)
 *                     0x15           D_00810CAC += n (no count; on cap
 *                                    also D_00810CAE = 0)
 *                     0x16           D_00810CB0 += n (no count)
 *                     0x1B/0x1C/0x1D count += n; D_00810CB2 += n*12/36/48
 *                                    with the D_00810CB7 "peak" raise +
 *                                    clamp of CB2 to that peak
 *                     default        count += n, clamp `>= 100 -> 99`
 *                   The non-battery consumable meter clamps above are
 *                   `>= 100 -> 99`; battery charge uses its capacity.
 *                   Case 0x10 SPR4 MAGAZINE takes NO
 *                   part of the default clamp; it does count += n,
 *                   pack counter D_00810C63 += n, reserve
 *                   D_00810CB4 += 30*n, empty mag D_00810C62
 *                   auto-fills to 30, and on packs >= 99:
 *                   `D_00810CB4 -= (packs - 98) * 30` then packs =
 *                   count = 98.
 *                   CORRECTED 2026-07-31: that last step SUBTRACTS the
 *                   over-cap packs' rounds back OUT of the reserve.
 *                   FINDINGS and this header both said it "folds into
 *                   the reserve", and the port dropped it as a no-op.
 *                   The port applies the DEFAULT clamp to every
 *                   type outside 0x10 and 0x1B..0x1D, so it clamps the seven cases the
 *                   engine leaves unclamped and it gives 0x0F/0x11..0x16
 *                   a count they never get — FLAGGED below, and left
 *                   alone deliberately: the port already folds take
 *                   families 1/2 into this one array, so a type here is
 *                   NOT reliably the engine's stat index and per-case
 *                   fidelity on top of that folding would be false
 *                   precision. Battery types 0x1B..0x1D now preserve
 *                   the original wrapping count and half-unit charge /
 *                   capacity behavior; their AREA11 records are verified.
 *   func_001AE7E0   the FOUND presentation: a nonzero D_008106B0 makes
 *                   the main-mode controller OPEN THE STATUS SCREEN,
 *                   which routes to the item's page/database record
 *                   (func_0020CDC0 consumes B0/B1 — the "Found:" lines
 *                   are message-bank group 4, names group 3).
 *
 * PORT MAPPING (deviations FLAGGED):
 *  - Manifest lines (written by the decomp repo's export_level.py
 *    --pickups; em_game's parser owns the line):
 *        pickup <type> <x> <y> <z> <yaw> <uid> [<model.emdl>] [prop]
 *    <uid> = (area << 8) | engine-puid — one flat taken-bit set keeps
 *    the engine's per-area D_00810860 semantics (uid >> 5 lands on
 *    exactly the engine's area*8 + puid/32 word). CORRECTED
 *    2026-07-31: the no-persistence rule is on the PUID BYTE, not on
 *    the whole uid — func_001B1190 (byte-matched) and func_001B11E0
 *    both return early on `(arg & 0xFF) == 0`, so a record with puid 0
 *    in a NON-ZERO area (uid 0x0100, 0x0B00, ...) must also never take
 *    a bit. The port used to test `uid > 0` and would have marked such
 *    an item permanently collected — a pickup that the engine
 *    re-spawns on every area re-entry would vanish for the rest of the
 *    run. `prop` marks the placement-table kind-0xB DISPLAY PROPS:
 *    rendered, never collectible (grounded in func_001C4820's missing
 *    take path, above — not merely observed).
 *  - Collection condition = the decoded archetype-3 test (CROSS edge,
 *    10-u ring, the [-20.5, +3.5] dy window, **pi/2** facing with the
 *    7-u auto pass; nearest wins). RE-VERIFIED against
 *    src/func_00183EF0.c `case 3: case 4:` — `bd <= **desc`, then
 *    `dy >= 0 ? dy <= desc[1] : fabs(dy) <= 17.0f + desc[1]`, then
 *    `bd <= 7.0f -> ang = 0` else `ang = wrap(player_yaw - atan2(bx,
 *    bz))` accepted while `fabs(ang) <= 1.5707964f`. All four literals
 *    (17.0f, 7.0f, 1.5707964f and the atan2 form) are in the recovered
 *    C. The take-family-1/2 facing variants
 *    are NOT modeled (no placed family-1/2 item is closer than its ring
 *    to another pickup; the bearing test stands in — FLAGGED), but they
 *    share the pi/2 gate so the tolerance is right for all three.
 *  - ONE WINNER PER PRESS (CORRECTED-2 2026-07-31). func_00184BA0 walks
 *    a SINGLE interactive list — items, examine objects and the rest all
 *    live in it — and arms exactly one object per CROSS press, the one
 *    with the smallest planar distance (`if (v < best) { best = v;
 *    winner = obj; }`, then `winner[0xB] = 4; return 1`). The port scans
 *    items and examines in separate modules, so before this correction a
 *    single press could take an item AND start an examine script in the
 *    same frame. em_pickup now publishes this frame's item winner
 *    (em_pickup_scan_dist) and can give it back (em_pickup_scan_release);
 *    em_examine, which runs second, yields to a nearer item or releases a
 *    farther one. STILL FLAGGED: doors are a third module and are not in
 *    this arbitration — em_game gates them doors-first via the movement
 *    lock (see em_game.c), which is an approximation of the same rule.
 *    Tie-break: an exact distance tie goes to the ITEM (the engine's
 *    `<` makes ties go to whichever object came first in the list, which
 *    the port has no equivalent of).
 *  - The engine's archetype-3 LINE-OF-SIGHT re-check (func_00183EF0:
 *    when the candidate's class nibble is 7 or its think fn is
 *    func_00219550, a func_0019A910 raycast from player.y+16 to the
 *    item rejects the candidate on a 0x2800 hit) is NOT ported — the
 *    port has no world raycast on this path. FLAGGED: items behind
 *    thin geometry are reachable in the port.
 *  - RENDER SCALE (NEW 2026-07-31): func_0015AC00's per-model scale is
 *    now applied to the baked pose, keyed on the chunk27 library id
 *    parsed out of the instance's `item_XX.emdl` filename (the id IS
 *    the filename — decomp tools/export_props.py writes
 *    `item_{model_id:02x}.emdl`). Before this, every collectible drew
 *    at 1.0 while the engine draws the whole equipment-model family at
 *    2.0 and library id 0x5B at 1.5 — e.g. the office card keys
 *    (library model 0x4F) were half the size they should be. Applies
 *    to collectibles only: display props run func_001C4820/
 *    func_001B0FD0, which never execute the func_0015AC00 switch, and
 *    `area_item_XX.emdl` ids are all < 0x40 (default 1.0) anyway.
 *  - The take runs the 2-scripted-frame latency then the take. The
 *    latency count itself is a PORT STAND-IN (EM_PICKUP_TAKE_FRAMES):
 *    the engine's op7/op9 script records are data we have not decoded,
 *    only their opcode order is known. The GRAB-ANIM variant IS modeled (player clips 0x40..0x42 now ship in
 *    player.emdl): on take, pickup_grab_clip selects the clip by item
 *    HEIGHT vs player.y (< py+6 -> 0x42 low, < py+13 -> 0x41 mid, else
 *    0x40 high — func_0015AE20's D_00248354 patch) and
 *    em_game_player_interact_anim plays it ON THE PLAYER + raises the
 *    scripted-anim lock for its duration (the same lock the AREA-11
 *    terminals use). DEVIATION (FLAGGED): the engine waits for the clip
 *    to finish (op-A sub-3 wait-anim-done) BEFORE the op-9 take; the
 *    port keeps the take effect synchronous at the 2-frame mark and
 *    plays the clip + lock at the same instant — the inventory/taken-
 *    bit/despawn contract is byte-identical, only the visible
 *    grab-then-take ordering is collapsed. The engine also runs an
 *    op-0 sub-8 camera move during the grab — NOT ported (no take-time
 *    camera; FLAGGED).
 *  - Inventory = a u8-per-type count array mirroring D_00810C64
 *    (+ the 0x10 magazine case: pack counter + 30 reserve rounds per
 *    pack, handed to em_weapon through em_game's pickup hunk, and the
 *    corrected over-cap subtraction). The map/key arrays (take
 *    families 1/2) fold into the same count array — FLAGGED
 *    simplification (the engine keeps three arrays). Likewise types
 *    0x0F and 0x11..0x16 get the default count here although the
 *    engine handles them specially (see func_001C40B0 above) —
 *    FLAGGED; those are the unported weapon/meter variants.
 *  - NOT PORTED (FLAGGED): the engine's new-game seed. func_001AF2C0
 *    calls func_001C40B0(0x10, 2) and sets the loaded magazine
 *    D_00810C62 = 30, so a fresh game starts with 2 magazine packs +
 *    60 reserve rounds in this array. em_pickup_reset zeroes instead;
 *    the port's starting ammo is owned by em_weapon, and seeding it
 *    here would double-count. The visible consequence is the status
 *    screen's magazine row at new game.
 *  - NOT PORTED (FLAGGED): func_001C40B0 case 0x10 also auto-fills an
 *    EMPTY loaded magazine (D_00810C62 == 0 -> 30). The port hands all
 *    30 rounds per pack to the reserve and lets em_weapon decide.
 *  - The FOUND presentation is an em_hud "Found: <name>" line (real
 *    font + message-bank group-3 name) instead of the engine's
 *    auto-opened status screen — FLAGGED stand-in (page interiors are
 *    CONTENT TBD; see em_hud.h).
 *  - No pickup aura, no take/Found sound (none is decoded on the take
 *    path itself; the engine's audible feedback is the status screen
 *    opening). RE-VERIFIED: func_001F1110 (BYTE-MATCHED) initialises
 *    the actor's +0x2D0 sub-struct — phase float +4 = 40 + rand()%120,
 *    +0 = 0, the class arg stored as a short at +8, +0xA/+0xC = 0 —
 *    and func_001F1180 (NEARMISS) is its per-frame runner, a state
 *    machine ending in a GS-packet emit (func_001F0A60). "Aura" is
 *    FINDINGS' reading of that draw tail; neither function is ported,
 *    so nothing here depends on the label being right.
 *  - THE AREA-11 BATTERY (item TYPE 0x11, placement record 10 / engine
 *    behavior ov 0x00823E80): on take, in addition to the normal
 *    despawn + taken-bit, em_pickup calls em_game_set_battery(1) — the
 *    engine's `li 0xFF; sb -> D_00810811` ("battery in inventory"
 *    byte; INVESTIGATION_area11_elevator.md §2). That flag gates the
 *    terminal power-up (em_examine.c, batch-2 contract C). The battery
 *    is identified solely by item TYPE 0x11 (the manifest emits it via
 *    the ELEVATOR parser's `pickup 0x11 ... battery` line; type 0x11
 *    is unique to this key-item). FLAGGED simplification: the engine's
 *    take also calls func_001C4760 (key-item acquire), func_001FAE70,
 *    func_001AEE10 — those auxiliary effects (key-item DB page, cue)
 *    are not ported; the D_00810811 flag is the load-bearing one for
 *    the opening progression.
 */
#ifndef EM_PICKUP_H
#define EM_PICKUP_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Slots per scene. The richest exported scene (snow) places 11 items;
 * office sub-1 = 3 items + 7 props. */
#define EM_PICKUP_MAX  24

/* Use-scan constants (func_00183EF0 archetype 3, desc D_00275488) —
 * shared with the self-test. PROVENANCE, checked 2026-07-31 against
 * src/func_00183EF0.c:
 *   - the 17.0f widening, the 7.0f auto-pass ring and the 1.5707964f
 *     (pi/2) facing gate are LITERALS in the recovered C;
 *   - desc[0]/desc[1] themselves are .rodata at D_00275488, which the
 *     recovered C only takes the address of. {10.0, 3.5} is asserted
 *     by FINDINGS "ITEM PICKUP SYSTEM" §3 and is NOT independently
 *     re-checkable from the decompiled source. Treat those two as
 *     FINDINGS-sourced data, not code-derived. */
#define EM_PICKUP_RADIUS      10.0f   /* desc[0]: XZ ring (FINDINGS)   */
#define EM_PICKUP_DY_UP        3.5f   /* desc[1]: player above item    */
#define EM_PICKUP_DY_DOWN     20.5f   /* desc[1] + 17.0: item above    */
#define EM_PICKUP_AUTO_RING    7.0f   /* facing auto-pass distance     */
/* The archetype-3/4 facing tolerance. CORRECTED 2026-07-31 from pi/4:
 * func_00183EF0's case-3/case-4 block returns out of
 * `if (fabs(ang) <= 1.5707964f)`; the 0.7853982f tail belongs to the
 * archetypes that fall through (0/1/2), which items never use. */
#define EM_PICKUP_FACING  1.5707964f  /* pi/2                          */
/* PORT STAND-IN, not decoded: the engine's take-script record data is
 * unread — only its opcode order (op7 enter / op9 call / op7 exit) is. */
#define EM_PICKUP_TAKE_FRAMES  2

/* The SPR4 magazine-pack item type (func_001C40B0 case 0x10). */
#define EM_PICKUP_TYPE_MAG  0x10

/* Player GRAB clips played on take (func_0015AE20's GRAB-ANIM patch of
 * D_00248354 by item HEIGHT vs player.y; FINDINGS "ITEM PICKUP SYSTEM"
 * §4). item.y < py+6 -> LOW, < py+13 -> MID, else HIGH. */
#define EM_PICKUP_GRAB_LOW   0x42
#define EM_PICKUP_GRAB_MID   0x41
#define EM_PICKUP_GRAB_HIGH  0x40

/* Add one placed pickup. `model_file` (scene-dir relative) may be NULL
 * — the instance is then collectible but draws nothing (and logs).
 * `prop` = display prop: rendered, never collectible. Returns the slot
 * index, -2 when the uid's taken bit is set (the engine's cond-1
 * spawn suppression — nothing placed), or -1 on error. */
int em_pickup_add(EmGfx *gfx, const char *scene_dir, int type,
                  const float pos[3], float yaw, int uid,
                  const char *model_file, int prop);

/* Free the scene's instances + meshes. Inventory and the taken-bit
 * set SURVIVE (engine: D_00810C64/D_00810860 are global game state —
 * that survival IS the pickup persistence across scene reloads). */
void em_pickup_scene_clear(EmGfx *gfx);

/* New-game wipe: inventory + taken bits (boot only; the engine memsets
 * the 0x640-byte game-state block at D_00810700 — func_001AF2C0). */
void em_pickup_reset(void);

/* Per-frame: the use scan (CROSS edge -> arm) + armed-take pump.
 * `scan` = 0 suppresses the scan (the engine gates on the scripted
 * frame selector spad 3B8D — the port passes 0 while a door transit
 * or the damage lock owns the player). */
void em_pickup_update(const float player_pos[3], float player_yaw,
                      const EmFrameInput *in, int scan);

/* Render-chain accessors (door/enemy draw contract): slot count + one
 * draw per LIVE slot — returns 0 for despawned/model-less slots. */
int em_pickup_count(void);
int em_pickup_draw(int i, EmGfxMesh **mesh, const float **palette,
                   uint32_t *bone_count);

/* 00219550's separate model73 child, allocated through 001C5570. The
 * manifest binds it explicitly to a pickup UID. Transform and lifetime
 * follow that owner; color.xyz is the original base RGB and color.w the
 * random brightness amplitude (001F54E0). Returns -2 for a taken owner. */
int em_pickup_light_add(EmGfx *gfx, const char *scene_dir, int owner_uid,
                        const char *model_file, const float color[4]);
void em_pickup_lights_draw(EmGfx *gfx, const float viewproj[16]);

/* Inventory — the D_00810C64 mirror (one u8 count per item type),
 * plus the magazine-pack counter (D_00810C63 mirror). */
const uint8_t *em_pickup_items(void);          /* [256] */
uint8_t        em_pickup_item_count(int type);
uint8_t        em_pickup_mag_packs(void);

/* Original battery charge/capacity (001C40B0 cases 0x1B..0x1D), in
 * HALF-units. UI display units are these values >> 1. The setter is
 * for the original battery menu's timed discharge/recharge; it clamps
 * invalid host requests to [0, capacity]. Both survive scene clears and
 * are wiped only by em_pickup_reset, like the inventory counts. */
int  em_pickup_battery_charge(void);
int  em_pickup_battery_capacity(void);
void em_pickup_battery_set_charge(int half_units);

/* One-shot event takes (consumed by em_game's pickup hunk):
 *  - ammo: reserve rounds to add (func_001C40B0 case 0x10's 30/pack);
 *    em_game applies them to em_weapon when the weapon state allows
 *  - found: the just-collected item TYPE for the em_hud Found line,
 *    -1 = none pending */
int em_pickup_ammo_take(void);
int em_pickup_found_take(void);

/* Persistence introspection (self-test): the taken bit for `uid`. */
int em_pickup_taken(int uid);

/* USE-SCAN ARBITRATION (func_00184BA0's single-winner walk — see the
 * "ONE WINNER PER PRESS" note above). Valid only for the remainder of
 * the frame in which em_pickup_update ran; cleared at its next entry.
 *   em_pickup_scan_dist   -> 1 and writes the winner's PLANAR distance
 *                            (the engine's spad 0x70003B98 value) when
 *                            THIS frame's scan armed an item, else 0.
 *   em_pickup_scan_release-> give that arm back, because a nearer
 *                            object elsewhere in the engine's one list
 *                            won the press instead. */
int  em_pickup_scan_dist(float *out_dist);
void em_pickup_scan_release(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_PICKUP_H */
