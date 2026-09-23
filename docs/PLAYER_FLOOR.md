# Player floor contact: footsteps, probes, floor service, fall entry (WP-15 P14-P18)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`src/game/em_player_floor.c` translates the routines below. Each works on a
small mirror of the actor bytes it touches. Every callee the port does not
translate there is a worker; a missing worker, or one that returns a
negative value, is a fault (-1). Three oracles run the original instructions
from the pinned ELF in the bounded EE interpreter
(`tools/test_point_light_reference.py` Oracle, extended). No original bytes or
tables are embedded in the tests.

| Oracle | Original code executed | Boundaries (scripted, recorded) |
|---|---|---|
| `tools/test_player_footstep_reference.py` | 00187350, 00187EE0, 00182430, 00179B90, 001031E0 | 001FBD50 sound, 001EFD90 effect, 001F0460 decal and its SDK matrix calls, 001E8B90 wade, 00122BB8 rand values |
| `tools/test_player_floor_reference.py` | 001796C0, 00179450, 00179680, 00175900, 00175CF0, 0019A310, SDK vector routines | 0019BC40 column table, 0019AB20/0019B6C0/0019B8C0 probe results, 00175640, 0017F9E0/0017FB90, 00187DC0/00187DE0/00187EA0, SDK atan2/cos/atan/sqrt (host models on both sides) |
| `tools/test_player_probe_reference.py` | 001764E0, 00176390, 00176BE0, 001762E0, 00176C80, 001760C0, 001756E0, 0019A310, 001029C0/00102BB0/001029E8/00102918/001026A0, 001B1470 | 0019AD00/0019AFE0/0019AB20 probe results, 00176180, 00174A50, SDK sqrt/atan/fabs (host models) |

## P14/P15 footsteps: 00187350

0015BCF0 calls 00187350 once per player stage, after the state callback and
the skeleton evaluation (0x15BDD8). The step is driven by the source
animation clock, not by the display:

- **Locomotion modes** (+1F0 = 1, 2, 0x2F or 0x41). Row +20C of D_00248C90
  must carry both step frames (+2, +4). Only 16 of its 459 rows do: clips
  1/2/3, 0xB/0xC/0xD, 0x15/0x16/0x17, 0x4C/0x4D/0x4E and 0x148-0x14B. The
  phase +25E fires the left foot (node 17) when the remaining clock +3C is at
  or below frame A, then the right foot (node 18) at or below frame B. After
  that it re-arms when the tier +25C is 0 or +200 has any of 0xB000.
- **Each step.** 00182430(tier) plays the surface sound, then the gear sound
  0x138. Each is `base + 00179B90()` through 001FBD50(actor, id, 0, 300.0).
  The surface base is 0x10 + 0x11 x surface family, plus 10 for tier 3 and 5
  for tier 2. Surface 0x5B uses 0xBA when +23C is 1 and 0xCB otherwise.
  Then 00187EE0 spawns the surface effect 1.5 below the foot node, with the
  actor Euler +C0:

  | Surface | Effect |
  |---|---|
  | 5 | 0x80000028 (001EA240) |
  | 6 | 0x80000005 |
  | 7 | 0x80000068 |
  | 8 | 0x80000066 |
  | 0 | 0x80000011, or the 001F0460 wet decal while +212 is nonzero |
  | 0x5A/0x5B/0x5C | 0x80000065/0x8000001D/0x80000067 at the recorded surface height +250 |
  | 1-4, 0xD, 0xE and unlisted | nothing |

- **Other modes.** They consume the mailbox +25E = 0x80|tier. 0017C030
  writes it when a stop ends: mode 4 posts 0x83, mode 5 posts 0x81 (tier 1)
  or 0x82. The default branch plays the step and the effect at the actor
  position. Melee modes 0x36/0x37 play only the sound. Either way +25E
  becomes 0.
- **Tail.** The wet-feet timer +212 is set to 120 on surfaces 6/0x5B;
  otherwise it counts down. While +23C is nonzero (and D_00810700 is not
  0x15), the wade ripple and level run.

Captured evidence: in `collision_run_poll.json` (the full 1024 actor bytes
per original frame) the step phase fires at original frames 4094, 4117,
4134, 4135, 4137, 4153 and 4173, and the stop mailbox at 4192. During the
30-input run only two steps fire (4094, 4117), because the tier changes carry
the clock proportionally across clips whose frame B is small. The old port
trigger (the display clock crossing frames A/B, `step_crossed`) is
fabricated timing.

The oracle checks:

- all 459 table rows;
- 1,813 00182430 selections over every surface byte;
- 12,000 random states, comparing +25E, +212 and every worker call;
- 114 consecutive captured frames. Each checks the original and native
  results and the step phase and wet timer the original left;
- six calls over the whole captured playable RAM image, with the foot nodes
  read from the player's node array (player+0x110, as 001CB590 /
  anim_bone_array_setup set D_00275B40).

**Port binding** (`player_footstep_0187350` in em_player.c):

- The actor mirror comes from the pose source (`player_pose_source`: clip,
  remaining clock, flags) and the port's loco state.
- The foot nodes are the player stage's evaluated palette nodes 17/18. This
  is the same palette `player_pose_finish_palette` reads node 1 from.
- Sound goes to `em_sfx_play_at(id, feet, 300)`. The 001EFD90/001F0460/
  001E8B90 workers are bound by the coordinator (`player_footstep_set_workers`).
  While unbound, each reached call is a counted fault (`player_footstep_faults`,
  reported once). The step state still advances, as it does in the original
  after those calls.
- 0017C030's mailbox writes are posted from `player_move`:
  - stop phase 2 to 3 posts 0x83;
  - the foot-placement stop end posts 0x81 or 0x82.
- The port's player has yaw only: +C0/+C8 are passed as 0. 00175900 clears
  +C0 on floor contact.

**Pending integration (em_player_frame.c, coordinator-owned).** Call
`player_footstep_0187350(spad 3B68, D_00810700)` in `em_player_0015BCF0`
after `actor_update()`, and delete the `step_crossed`/`footstep_play` block.
The proposed diff is `build/l1-locomotion/em_player_frame.patch` (local).

A lane build with that diff was compared with the capture over the
re-entry fixture (`EM_CONTROL_REENTRY_TEST=1`, native frame = original -
2720). All 48 frames from 4094 to 4141 match in mode, tier, step phase,
clip and remaining clock. The stop fixture posts 0x83 at the native frame
of original 4144, where `first_control_poll.json` shows +1F0 4 to 0.

## P16 radial probes: 001764E0 (with 001760C0, 00176390, 00176C80, 001756E0)

001764E0 runs in the idle and walk callback tails (00161020, 001612D0), and
inside 00178B90(p, 1). It works in this order:

1. **Crawl test.** `s2 = 00176C80()` when +236 is 0. This checks three
   points at (0, 4.01, 10) for yaw + {0, +-pi/8}: each must be clear to
   0019AD00 mask 7 and must have cover within 13.99 above (001760C0).
2. **Ankle pass, walk callback only** (+4 == 1 and +5 == 1). Five lanes at
   (0, 0.05, 4.5) run 0019AD00 mask 6. The hit delta (x, y and z) is applied
   in these cases:
   - class 0x1000 or 0x800;
   - class 0x2000 with an entity of flags 4 and type 2;
   - class 0x2000 without an entity, only when the caller's `$s1 & 4` is set
     and the 0019A310 slope is outside [70, 110] degrees.
3. **Eight main lanes.** D_00275B00[4] = +314, then +314 = 0. Each lane runs:
   1. The chest sweep, 0019AFE0 mask 7 from the lane centre to (0, 4.01, 4.5).
      A hit sets the lane bit and responds.
   2. Only with no chest hit: the column 001760C0 from the lane end up to 18
      (13.8 while +236 is set).
      - **Overhang below 13.8.** A hit less than 13.8 above the feet is a
        wall. So is any hit on lanes 3-7, in modes 0x36/0x37/0x3E, at
        negative speed, or with no crawl space. For these, 0019AD00 mask 7
        probes the hit point + 0.1 up.
      - **Lowering.** Otherwise, with +236 clear, the column raises +236.
   3. The upper probe, (0, 18, 4.5) or (0, 13.8, 4.5) while +236 is set,
      after the lane matrix translation is refreshed from the corrected
      position.
   - **Response.** 00176390, then 00176BE0: apply the whole delta unless the
     node class is floor 0x4000 or ceiling 0x8000. Hull hits of an entity
     with flags 2 re-probe and shove (00176180). Cell hits check 001762E0
     (area 2 'T' targets only).
4. **001756E0 after the floor service.** With +236 set, it keeps it while
   any of seven lanes at (0, 4.01, 4.0) has cover within 13.99. It sets or
   clears +235 bit 1 to match. An idle-callback change requests the row
   default via 00174A50(12.0). Area 0x12 forces +236 on link type 6.

Findings (all from the executed original, not from labels):

- **Inverted height clause in the readable C.** The readable 001764E0 C
  (NEARMISS) inverts the column height test. At 0x176990 `bc1f` branches to
  the lowering block when the hit is NOT below 13.8. So an overhang less
  than 13.8 above the feet is probed as a wall, and a higher one on the
  front lanes, with crawl space ahead, lowers the player. The oracle caught
  this. The decomp's readable C should be corrected.
- **`$s1 & 4` is inherited.** 001764E0 uses `$s1 & 4` without setting
  `$s1`. `$s1` is not written by main (gs_readback_queue_run), the task
  dispatcher 001AB6A0, 001ACEC0, 001AD250, anim_frame_top_a/b, 001AE5E0,
  0015BCF0, 0015BA50, 0015B130, 00161020, 00178B90 or 0017C440. The state04
  register file (savestate `Internal Structures`, pc 0x1AAFF0 in main) has
  `$s1 = 1`. The only writer on the path is 001612D0's reversal-resume
  branch, which keeps the 0017B490 clip id in `$s1`. So ordinary ticks do
  not apply wall slopes in the ankle pass. The port passes the resume clip
  on that tick.
- **Ankle pass gating.** The ankle pass needs +5 == 1 at the tail. The walk
  callback writes +5 = 0 before its tail on the tick the walk ends (stop
  end, foot-stop end, reversal idle return). 00161020 writes +5 = 1 on the
  entry hand-off tick. The port's +1F0 != 0 matches both.
- **Floor-class hits.** They are not stepped past: 00176BE0 ignores them.
  The port's `probe_wall_seg` re-probed past walkable crossings. That was a
  port invention, and it is no longer used by the player.

The oracle checks 3,000 SDK lane transforms (bit-exact SDK sine/cosine, VU
accumulate order, truncation), 2,500 random 001764E0 states (every probe
call and argument, +314, +236, D_00275B00[4], position), 2,500 001756E0
states, and 115 captured first-control frames. For those frames, the
recorded +314 lanes are answered with wall hits, and both sides reproduce
each frame's captured +314.

**Port binding** (`player_wall_probes` / `player_clearance_release` in
em_player.c):

- 0019AD00/0019AFE0 map to `em_collision_move_probe` (the movement
  walkers) plus the port's door hulls (`em_door_probe`, class 5) for mask
  bit 0. The nearest hit wins.
- 001760C0 maps to `em_collision_segment_query` over the column. The
  original's 0019AB20 walkers 0019F730/0019C830 are not translated. The
  segment walkers stand in for them; both are front-facing and nearest-first.
- Published actor cell 18 (the battery panel) carries its owner bytes
  +2/+3 = 0x84/0x24 from the captured playable RAM (owner 0x7AA590). Other
  owners fault.
- Unbound workers are counted faults (`player_probe_faults`):
  - 00176180 hull shove;
  - 001762E0 area-2 shove;
  - 00174A50 low-clearance row request (rows 2/3 have no exported clips).

Regression: `EM_STARTUP_TEST=newgame-control` keeps displacement 9.599989
and endpoint (245.475342, 229.891876, 216.987976). The stop, low-gait 1/2
and foot-stop fixtures keep 18.649982, 4.049953 and 14.350012, with PASS.

The re-entry fixture still reproduces the panel contact (lane 1, +314 =
0x02 at ticks 7/8):

| | Tick 7 | Tick 8 | Final XZ |
|---|---|---|---|
| Native before | 0.266091 | 0.306747 | (238.753937, 226.391403) |
| Native now | 0.266112 | 0.306763 | (238.753906, 226.391418) |
| Original, frame 4141 | | | (238.753982, 226.391403) |

The lane directions now come from the SDK routine instead of host
sinf/cosf. The remaining difference is upstream (yaw/position arithmetic,
see FIRST_CONTROL.md); both results are within 0.0001 of the original.

## P17 floor service: 00175900 (with 00175CF0, 0019A310) - translated, not bound

The walk tail lowers +B4 by 0.4 (0.8 on surface 0x35; idle 0.2). Then
00175900(p, 1) runs:

1. It clears +23B/+9C (unless mode 0x30 or surface 0x35).
2. **Floor probe.** 0019AB20(p, +B0, +280 = (0, -13.8, 0), 6) runs a
   vertical segment from 13.8 above the feet down to them. On a hit,
   00175CF0 applies it:
   - It records +238 (class), +23B (attribute) and +9C (the 0019A310 slope).
   - It adds the delta.
   - It links +214 from the hit entity.
   - Wall or linked-slope floors slide down the fall line.
   - Other floors set +A = 1 (3 or 2 for search hits), with 0x80 when
     linked, and +237/+218 for class 0x1000.
3. **Search.** With no hit, eight points 2 units out are probed, first hit
   wins.
4. **Surface record.** 0019B6C0 from 18 above the feet down to them sets
   +B, +23A and +250, with the 0x5A/0x5B/0x5C first-contact one-shots. A miss
   clears those and sets +250 = +B4.
5. **Object probe.** In contact without a surface record, 0019B8C0 mask 7
   gives +23A, or surface 4 for nav links of type 4 with ids
   2/10/12/24/40/42. It clears +25F, and +C0 outside state 0x1C.

Oracle: 5,000 random states covering every worker path, plus 115 captured
frames.

**Not bound in the port:**

- The ungrounded continuation is 001796C0 (below). It needs 0019BC40 (the
  column scan) and the fall callback 00162DB0 (player state 5), and neither
  is translated.
- Binding the floor service alone would leave a step-off without its
  original continuation.
- The port keeps its floor snap and PLAYER_FALL_ENTRY stand-in
  (em_game_internal.h, labelled port-side). The +23A for footsteps keeps
  coming from the port floor probe (`footstep_floor_attr`).

## P18 fall check: 001796C0 (with 00179450, 00179680) - translated, not bound

001796C0 runs last in the tail, and does nothing while +25F is set.

- **In contact** (+A). It zeroes the drop accumulator +2EC. A class-0x1000
  contact (+237) enters the slide state (+5 = 0x1C, +1F0 = 0x30) unless +5
  is 0x1D/0x1E.
- **First ticks without contact.** Each tick adds -0.04 to +2EC and moves
  +B4 by it, until +2EC is at or below -0.04 x (2 for mode 0x3A, else 3).
- **Then.** 00179450 rebuilds the column table (0019BC40) and takes the
  highest flagged entry below the feet, writing its delta to +258.
  - With no entry, or a delta below -4.01, it enters the fall state:
    00179680 sets +5 = 5, +6 = 0, +1F0 = 11, +25F = 2 and clears +236.
  - Otherwise it keeps stepping down (-0.04 per tick, clamped at -4). A
    0019AB20 hit of class 0x1000 also enters the fall state.

Oracle: 4,000 00179450 queries, 500 00179680 entries, 6,000 001796C0
states (every outcome, including the -4.01 boundary), and 460 captured
frames.

**Blocked:**

- 0019BC40 (a 378-line column scan over the class-4 hull list and the grid
  buckets) is not translated.
- 00162DB0 (the state-5 fall callback, 211 lines, which also uses 00179450)
  is not translated.
- Until both exist, the port keeps PLAYER_FALL_ENTRY. Its comment already
  names it a port-side stand-in for this table-driven hand-off.
