# Original AREA11 pickup owners

Live since WP-6 (2026-09-23). The AREA11 interaction host
(`em_area11_interaction_host.c`) binds the seven item owners (00219550 x6,
0015AFA0 for the map) to `em_pickup_original` at load, publishes them
through the translated 001B17A0, resolves Use for them in its single
00184BA0 scan and ticks each owner at its own pool node
(`em_area11_bindings.c`). The former legacy use scan, two-frame take, flat
inventory add, Found line and the interact-clip lock are deleted; placed
items of scenes without a bound owner are drawn and never taken.

## Original evidence

Tests read the user's pinned `SCUS_971.12` locally, SHA256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.
No original instructions, disassembly, scripts, meshes, or textures are
checked into this change. `tools/export_pickup_programs.py` reproduces two
660-byte EMSC files under ignored `assets/scene_snow`, from original
`002482C0..0024853F` and `00266620..0026689F`.

The callbacks differ in ways the former port and readable decompilation
missed:

| Behavior | `0015AFA0` / `0015AE20` | `00219550` |
|---|---|---|
| Initial class | 7 | 4 |
| On armed bit4 | phase0→1 | phase0→1, class flags→87 |
| Grab-height thresholds | player Y+6, then+7 | player Y+6, then+6 |
| Short program | `00248480` | `002667E0` |
| Grab program | `002482C0` | `00266620` |
| Script completion | lifecycle1→2 | cue194, persist, lifecycle→3, child lifecycle→3, class flags→4 |
| Following owner tick | persist then free | free |

Both thresholds use two separately rounded original `ADD.S` operations.
Both start functions receive actor+1F0 and do not tick the script on the
arming frame. Neither clears the armed byte before freeing. Class7 still
runs its aura boundary when scratch3B92 is zero, then publication/drawing;
class4 publishes/draws even on its completion frame. The native core begins
after model initialization; it does not claim to recover every initializer.

The original class4 C candidate repeats the first height comparison,
misstates the script-start pointer, and writes child state4. Raw execution
proves the second threshold is+12, the pointer is actor+1F0, and the child
state is3. These decompilation source corrections remain a separate PS2
compile/gate task; this native change does not promote those candidates.

## Programs, inventory, and status ordering

The short program is op07/sub13 enter, op09 consume, terminal op07/sub4.
The grab program additionally turns toward the owner, requests clip40..42,
settles the camera target, and waits for the real animation-end flag before
consuming. Every ordinary command here returns advance1 and no original
record sets force-continue. Consuming therefore yields before terminal
release: the next outer frame can enter status while the actor and its
script still exist. Status pauses both player/script workers. After status
returns, release and the owner cleanup continue in their original order.

Op07/sub13 sets selector1/camera-top2, waits for the actual player-ready
signal, then sets scratch3B92 and script skip-phase1. It does not add the
sub2 letterbox entry or clear the activity array. The shared frame command
now implements both paths separately.

`001B6EA0` dispatches on the owner subtype:

- 0: call `001C40B0(type,1)`, then write request B0=1 and B1=type.
- 1: increment the map byte D_00810CB8[type] with byte wrap, then write
  B0=2 and B1=type.
- Other values: increment the key byte D_00810CC3[type] with byte wrap; only
  types >= 0x20 write B0=3/B1=type. Lower types preserve an existing request.

**The item block is canonical D2 progress** (em_scene_state.h, WP-6):
D_00810C60, the pack count C63, the counts D_00810C64[t], the meters
CA8..CB0, the battery charge CB2 (s16) and capacity CB7, and the map/key
bytes, which overlap the counts exactly as in the original (CB8[t] is
C64[0x54 + t], CC3[t] is C64[0x5F + t]). em_weapon keeps D_00810C61 (fire
mode), D_00810C62 (loaded magazine) and D_00810CB4 (reserve); the game binds
the last two with `em_pickup_set_weapon_ammo`.

**001C40B0** is `em_pickup_items_001C40B0` (em_pickup_items_original.c,
from its .s): every case, with each clamp comparing the value reloaded after
its store (a count byte wraps before the 99 clamp is tested), and case 0x10
writing the pack count, the reserve (+30 a pack), the loaded magazine (30
when empty) and the 98-pack cap directly (W13; the former pending-rounds
queue is deleted). It addresses bytes by original address through a
resolver; a byte the port does not hold (outside the item block, or the
weapon bytes unbound) faults.

**Requests and pages.** The host's status hook writes B0 then B1. The
battery types 0x1B..0x1D open the ITEM root's BATTERY page (WP-5). The
other AREA11 takes post their original requests too: 0x1E/0x1F (B0 = 1, the
ITEM child 002160B0), 0x10 (B0 = 1, SPR4 00211970), key 0x32 (B0 = 3,
DATABASE 00214020) and the map 0x08 (B0 = 2, MAP 0020F950). Those pages are
not translated: the host's 0020CDC0 faults with a report naming the page
(fixture `other_take`).

**The class-7 aura** (the map owner 0015AFA0): 0015AC00's state 0 calls
001F1110 (one rand() for the first countdown) and 0015AE20's tail calls
001F1180 while D_70003B92 == 0 (read after the script step). Both are
translated (`em_pickup_aura_001F1110/001F1180`, from the .s: the readable C
of 001F1180 has its variant test inverted; variants 4, 2 and 1 run the
camera-facing test on the owner's +0xD0 rows). The countdown, the rand()
draws, the facing test and the angle/phase ramps are live, so the shared
LCG advances as in the original. The draw block (0011E2A8, the sprite
record, 001026A0, 001F0A60) is not translated: the sprite is not drawn,
reported once.

## Facing and camera

The exported op0E record contains step `3E32B8C3` (0.174532935 radians).
`em_pickup_turn` executes the recovered bearing and bounded angle-step
arithmetic; it writes the actual player yaw. `em_pickup_camera_settle`
implements op00/sub8: first call seeds phase1 without moving the target;
later calls update X/Z through `0018C6A0` and Y through `0018C4B0`.
For each axis, absolute difference<=1 uses difference/4 and reports that
axis settled; larger differences use absolute difference/6 for X/Z or/8
for Y, capped at0.4 or0.3 respectively, with the original sign. The script
advances when all three settled bits are set. The host must publish eye
and target through its `001DD980` boundary on every call, including entry.

Broader facing tests exposed a one-ULP bug in the shared SDK atan helper:
the original76-byte coefficient block at26C5D8 stores four high parts,
then four low parts, then eleven polynomial coefficients. Old decompilation
comments reversed the high/low names. The EMIS bytes were already correct;
the corrected C struct names restore the original cancellation order.
The public binary layout remains unchanged. The SDK zero-vector errno/error
hook is outside this numerical helper; no replacement `atan2f` is used.

## Host contract

`em_area11_interaction_host_load` binds every placed pickup with
`em_pickup_original_bind` after the other owners (a taken owner, not
spawned by 001B6660, returns -2), and binds each owner's status, class and
armed bytes into the interaction scene. Whole-world teardown releases the
bindings (`em_pickup_original_unbind_all`); instances, inventory and taken
bits stay. At the pool node:

- state 0 (the first call): 00219550 spawns its 001C5570 light child (the
  node keeps it as the owner's +0x2EC); 0015AFA0 runs 0015AC00's 001C6380
  matrix and 001F1110 (`em_area11_interaction_host_pickup_state0`);
- later calls: `em_area11_interaction_host_pickup_tick` runs one owner
  update (`em_pickup_original_tick_one`) over the shared frame view and
  the program's canonical skip byte. D_00810354 is the player's Y;
  D_008104A0 and D_008104E6 are 0 on every port path (0x2D is written only by
  0016D130, which the port does not run; +0x236 has no port writer). The
  completion writes the child's +4 = 3 (the child node frees itself, 001C5680
  state 3); the call that frees the owner frees the node (001AFC10). Status
  frames tick no owner.

Hooks: op0E sub1 is `em_pickup_turn` on the player heading; op00 sub8 is
`em_pickup_camera_settle` on the actual target, then the 001DD980
publication; PUBLISH is `em_owner_services_001B17A0` with 001B1630 on
g.cam.eye/fwd and 001B1B70 over the item's pool record (census L07: the
collision world's class lists, the item's collision cell on the class-4
list while its class is 4, the interactive list with class bit 0x80; the
record's +0x02 is stored from the owner's class byte after every tick). State
0 of 00219550 re-transforms the item's cell (001C6380 over the record's
+0xB0/+0xC0/+0x60, then 001A2370; src/func_00219550.c), before its 001C5570
child. `tools/test_collision_world_capture.py` compares the live cells of
uids 19 and 21..25 with route captures 00 and 04 byte for byte; TAKE_SOUND
is 001FBD50(owner, 0x194, 0, 300), whose cue has no exported AREA11 sample
(WP-14): reported once and dropped, like the status page's system cues.

Use: the host's 00184BA0 predicate runs `em_interaction_pickup_candidate`
for the items, with 0019A910 mode 6 over the collision world (the translated
walkers 001A1390 / 0019D770: the published class-4 cells and the grid). The
item's identity is its pool record, so a ray that ends in the item's own
published cell (kind 2, owner the item) accepts, as in the original; a
published wall of another owner or a grid wall between the player's +16 and
the item rejects. The crates, drums and truck do not publish their cells yet
(L25, L23), so a ray through them is not rejected where the original would
reject it (for example the item inside the crate at (311.6, 249.8, 328.7));
the port's own walker did not see them either.

## Validation

- Original owner instructions:6,216 lifecycle, event-order, and height cases.
- Original consume helpers:144 item/map/key/status and byte-wrap cases.
- Original facing helpers/SDK:1,104 cases, including the one-ULP regression.
- Original camera-settle handler and workers:1,188 cases.
- Expanded shared original82D0 oracle:5,184 sub2/sub4/sub13 cases.
- Both pre-existing scanner oracles pass after the SDK correction, including
  the two captured original publication lists and pickup mode6 LOS contract.
- ASan/UBSan actual-program fixture covers short item/map/key paths, grab
  item/map paths, status pause, delayed cleanup, persistence/reentry,
  independent-scan suppression,660 truncated files and12 malformed records.
- Existing pickup-light and panel interaction tests pass.

- Original 001C40B0, 001F1110 and 001F1180 with its SDK vector leaves
  (`make test-pickup-items-reference`, tools/test_pickup_items_reference.py):
  675 inventory cases over random item blocks (every byte of
  D_00810C00..DFF compared) and 85,571 aura cases in full mode (the
  countdown, the facing test in both outcomes and the draw record/argument
  scripts).
- The live battery take against route 01 (the level smoke's battery phase).

Run `make test-pickup-owner-reference test-pickup-items-reference
test-pickup-original test-interaction-frame-reference
test-interaction-scan-reference test-pickup-lights test-area11-interaction-host`.

The raw oracles compare original instruction flow under the project's
finite EE arithmetic model (truncated sums/products, rounded scalar
division). They are not a physical-EE rounding proof or a new live pickup
matrix/visual capture. The player lane separately exports and validates
45-frame terminal clips40..42; this batch does not claim the whole scene
is integrated or that every status page is complete.
