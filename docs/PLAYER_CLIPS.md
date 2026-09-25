# Player clips: the first level's clip set, the full export, and clip chaining

Date: 2026-09-24 (lane "player-clips"). Target: the pinned boot ELF and the
player's clip bank (disc `chunk28/f01_id3c`, runtime address `0xD689C0`, the
player record's +40). This document holds clip ids, header facts, addresses
and results only; no original code, data or disassembly.

Files of this lane:

- `tools/export_player_clips.py`: the exporter (section 3).
- `src/game/em_pose_chain.c/.h`: the chaining translation over decoded
  channels (section 4).
- `tools/test_pose_chain_reference.py`: its original-instruction oracle.
- this document; the binding notes are section 5.

## 1. Which clips the first level plays

Before the display step the live `assets/player_channels.empc` held 14
clips (0..5, 0x40..0x43, 0x45, 0x47, 0x15C, 0x15D) and `em_pose_bank.c`
refused chained clips. Since the display step (section 6) the live player
pose runs on the whole raw bank. The first level plays at least the 41
clips below.

**How the list was made.**

- **Route.** Every row of the route traces (`../Extermination/build/s87/route/NN_*/trace.json`,
  beats 00..14, FIRST_LEVEL_ROUTE.md) records the player record's +20C, the
  requested clip id. The recorder is `route_capture.py`, where the field is
  `clip`. Each beat's final RAM image adds its +2C.
- **Exit.** Beat 15 (the level exit, opt-in) adds three more clips.
- **Chain.** The bank's follow-on link (clip header +4) of every route clip.
  Only two route clips are chained, 0x5E and 0x73.
- **Prior.** Four clips of the existing export are not on any route beat:
  0x40, 0x41 and 0x43 (pickup and door clips, PLAYER_POSE.md) and 0x15D.
  0x15D is the idle fidget that `00161020` requests through 001749A0 with
  flags 1 and blend 8 after 300 idle ticks (decomp `src/func_00161020.c`).

The exporter re-derives the route and chain sets on every run. It fails if a
route trace shows an id that is not in the list, or if the list is not closed
under follow-on links.

| clip | frames (+2) | next (+4) | +6 | evidence | route beats |
|---|---|---|---|---|---|
| 0x000 | 80 | -1 | 0 | route | 00..15 |
| 0x001 | 120 | -1 | 0 | route | 00..15 |
| 0x002 | 45 | -1 | 0 | route | 00..12, 14, 15 |
| 0x003 | 40 | -1 | 0 | route | 00..12, 14, 15 |
| 0x004 | 20 | -2 | 0 | route | 05, 12 |
| 0x005 | 10 | -2 | 0 | route | 00, 01, 08..12, 15 |
| 0x040 | 45 | -2 | 0 | prior (pickup) | |
| 0x041 | 45 | -2 | 0 | prior (pickup) | |
| 0x042 | 45 | -2 | 0 | route (battery pickup) | 01 |
| 0x043 | 150 | -2 | 0 | prior (door) | |
| 0x045 | 150 | -2 | 0 | route (fence door) | 09 |
| 0x047 | 200 | -2 | 0 | route (elevator) | 04 |
| 0x04B | 90 | -1 | 0 | exit | 15 |
| 0x04D | 60 | -1 | 0 | exit | 15 |
| 0x04E | 30 | -1 | 0 | exit | 15 |
| 0x05E | 20 | 0x5F | 1 | route (slide entry) | 06 |
| 0x05F | 30 | -1 | 0 | chain of 0x5E; also requested by the slide state (em_player_slide.c) and 00224B80's recovery (em_player_recovery.c) | |
| 0x060 | 10 | -2 | 0 | route | 06 |
| 0x061 | 30 | -1 | 0 | route | 06 |
| 0x065 | 30 | -2 | 0 | route | 06 |
| 0x069 | 10 | -2 | 0 | route (vault) | 12, 14 |
| 0x06B | 48 | -2 | 0 | route | 12, 14 |
| 0x06E | 25 | -2 | 0 | route (landing) | 10, 11, 12 |
| 0x070 | 20 | -2 | 0 | route (climb) | 05, 11, 13 |
| 0x072 | 60 | -1 | 0 | chain of 0x73; also requested by the fall / drop states (em_player_fall.c, em_player_running_jump.c) | |
| 0x073 | 20 | 0x72 | 1 | route (fall) | 10, 11, 12 |
| 0x077 | 40 | -2 | 0 | route | 11 |
| 0x078 | 50 | -2 | 0 | route | 05 |
| 0x079 | 60 | -2 | 0 | route | 11, 13 |
| 0x08C | 1 | -2 | 0 | route | 05, 11, 13 |
| 0x0E3 | 50 | -2 | 0 | route (ladder) | 10 |
| 0x0E6 | 90 | -1 | 0 | route | 10 |
| 0x0E8 | 40 | -2 | 0 | route | 10 |
| 0x0EA | 40 | -2 | 0 | route | 10 |
| 0x0F0 | 90 | -2 | 0 | route | 10 |
| 0x156 | 60 | -2 | 0 | route (Roger scene) | 10 |
| 0x15C | 121 | -2 | 0 | route (panel) | 03 |
| 0x15D | 180 | -2 | 0 | prior (idle fidget) | |
| 0x164 | 45 | -2 | 0 | route | 10 |
| 0x165 | 45 | -2 | 0 | route | 10 |
| 0x166 | 80 | -1 | 0 | route | 10 |

**What the list does not prove.** A route shows what one play-through
requested. The translated player states can request many more clips than the
route shows: `em_player_reaction`, the closures, the hang and ladder states
and the weapon states name about 150 further ids. Nobody has shown whether
those paths are reachable in AREA11. For that reason the exporter writes the
whole bank by default (section 3), so an unobserved request never meets a
missing clip. `--first-level` restricts the export to this list.

**The chains never completed on the route.** The per-row clocks (+3C) show
both chained clips interrupted before their end:

- 0x5E (06): an 8-tick transition, then +3C = 15, because the slide entered
  at frame 5. Clip 0x61 was requested one tick later.
- 0x73 (10, 11, 12): an 8-tick transition, then +3C = 10 (entered at frame
  10). Clip 0x6E was requested after 3 ticks.

A longer slide or fall reaches the chain step. It is part of the first level
but not of the recorded route.

## 2. The original chaining rule

The rule comes from 001C64F0 (`anim_advance_time`) with 001C8480, 001C8D50,
001C8710 and 001C87C0, and from 001C67E0 (`anim_clip_init`). The record
fields are +2C (the clip word, where 0x8000 marks a transition), +3C (the
clock), +20C (the requested id) and node 0's +8E (the hold frame).

- **Resolve.** Every step of at most 1.0 first resolves +2C & 0x7FFF through
  the bank's directory (001C6120 / 001C8480).
- **Events.** Outside a transition, the clip's event table at header +14 is
  scanned. The first pair whose frame equals float_to_int(+3C) ORs its flags
  into the result. No clip of the player bank has an event table (all 459
  checked), so on the disc this never fires.
- **Clock above 1.** +3C -= step and 001C87C0 advances the node channels.
  During a transition the result gets 0x8000.
- **Clock at or below 1, inside a transition.** The transition ends: +2C
  loses 0x8000, +3C = frames - (float)+8E, and 001C8710 samples the clip at
  +8E.
- **Clock at or below 1, next = -2.** The clip holds at its end; the result
  gets 0x1000.
- **Clock at or below 1, next = -1.** The clip loops: result 0x3000, +3C =
  frames, and 001C8710 samples frame 0.
- **Clock at or below 1, next is a clip id: the chain step.** In order:
  1. +2C = next | 0x8000 and the result gets 0x4000.
  2. +3C = (float) the ending clip's signed +6 halfword.
  3. The follow-on clip is resolved.
  4. 001C8D50(nodes, 0.0, +3C) freezes the evaluated pose as the source and
     seeds a +3C-tick transition toward the follow-on clip's frame 0.

**The hold frame goes stale across a chain.** The chain never writes +8E,
and only `anim_clip_init` does. When the chain transition ends, the
follow-on clip therefore starts at the frame of the last `anim_clip_init`,
not at 0. For the route's fall (0x73 requested at frame 10), 0x72 would start
at frame 10. For the slide (0x5E at frame 5), 0x5F would start at frame 5.

**+20C is not +2C.** After a chain, +20C still names the chained-from clip
(0x73) while +2C plays 0x72. 001749A0 with flags 0 compares its id with +20C,
so a repeated request for 0x73 is refused (result 1) and does not restart
the clip.

The bank has four chained clips. All four have +6 = 1, so each chain
transition lasts one tick:

| clip | frames | chains to | frames |
|---|---|---|---|
| 0x36 | 60 | 0x35 | 60 |
| 0x5E | 20 | 0x5F | 30 |
| 0x73 | 20 | 0x72 | 60 |
| 0x177 | 80 | 0x178 | 80 |

## 3. The exporter: `tools/export_player_clips.py`

Run `python3 tools/export_player_clips.py` (about 1 s). It reads the user's
extracted bank `../Extermination/extract/chunk28/f01_id3c.bin` and writes
three files, all ignored:

- `assets/player_clips_full.bank`: the raw bank, 2,455,552 bytes. This is the
  region `em_pose_host_workers` maps at 0xD689C0.
- `assets/player_clips_full.empx`: decoded keys for all 459 clips, 3,921,000
  bytes. It follows the EMPC key layout and adds the header's +4 link, its +6
  halfword and the +14 event pairs. The EMPX v1 layout is in the tool's
  docstring.
- `build/player_clips_full/export.json`: the hashes, every per-clip header
  fact, the route clip ids with their beats, and every check.

It checks the following before writing anything:

1. **The bank equals captured RAM.** The bank file equals RAM from the
   player's +40 on, byte for byte, in all 23 captures that hold the player
   record: the playable image, handoff, panel, panel animation, both elevator
   images, the status hub and route beats 00..15.
2. **Every clip lies inside that bank.** Each exported clip's full extent
   (header, parents, the three section directories, every 12-byte key record
   up to the 0xFFFF sentinel, the event table) is inside the verified bank.
   Every follow-on link names a clip of the bank.
3. **The list covers the route.** Every route +20C / +2C id is in
   `FIRST_LEVEL`, and `FIRST_LEVEL` is closed under follow-on links.
4. **Shared clips match the old export.** The 14 clips shared with
   `assets/player_channels.empc` decode to byte-identical key payloads.

`tools/test_pose_chain_reference.py` also checks the decoded values against
the original key decoders 001C84D0 / 001C85D0. The quick run decodes 976 keys
of the four first-level chain clips; `EM_TEST_FULL=1` decodes 16,371 keys of
all 41 first-level clips.

It leaves the existing `export_player_pose_channels.py` and its asset alone.

## 4. `em_pose_chain`: the chaining translation over decoded channels

**Two translations of the same routines.** The raw-record translation
already exists and is chain-capable:

- `em_pose_host_workers.c` translates 001749A0, 001749F0, 001C61D0 and
  001C67E0.
- `em_player_stage_workers.c em_player_stage_anim_advance` translates
  001C64F0, including the chain branch.
- `test_pose_host_workers_reference.py` runs a follow-on sequence over
  captured RAM with the real bank (its `follow` case), and the stage-workers
  oracle runs random chained headers.

What did not exist was chaining in the decoded-channel world (EmPoseBank /
EmPosePlayback / EmPoseTransition) that the live `em_player_pose` uses.
`em_pose_chain` is that translation.

**API.**

| function | original routine |
|---|---|
| `em_pose_chain_bank_load` | loads EMPX |
| `em_pose_chain_find` | 001C6120 |
| `em_pose_chain_frames` | 001C61D0 |
| `em_pose_chain_clip_init` | 001C67E0 |
| `em_pose_chain_advance` | 001C64F0, with events and the chain |
| `em_pose_chain_request` | 001749A0 |
| `em_pose_chain_arbiter` | 001749F0 |
| `em_pose_chain_channels` | the evaluated node channels |

**State.** EmPoseChain holds +2C, +20C, +3C, node 0 +8E, the playback
cursors and the transition.

**Reuse.** The channel arithmetic is em_pose_bank's cursors and
em_pose_transition's seed, step and blend, unchanged. The clock uses
`em_ee_float.h`. float_to_int and 00128250 are `em_stream_lanes_001281C0` and
`em_stream_lanes_00128250`.

**Fail-stop.** Each of these returns -1:

- a clip that is missing from the bank;
- a sample frame outside a clip;
- a transition length that is not a whole number of ticks in 1..65535;
- a nonzero-blend init before any pose exists.

**The oracle, `tools/test_pose_chain_reference.py`.** It runs the original
001749A0, 001749F0 and 001C64F0 (with every callee: 001C67E0, 001C8480,
001C6120, 001C8710, 001C87C0, 001C8D50, the key walks and decoders, 001CA0A0,
001281C0 and 00128250) unmodified. They run over the playable image and the
route beat images, alongside em_pose_chain on the EMPX.

After every operation it compares:

- the result word;
- +2C, +20C, +3C and node 0 +8E;
- per node, every channel field the routines own: +0..+2C, the +30/+40
  rotation pair, +50, +54, +58/+5C/+60 and the +66/+68/+6A key indices.

The native values come from the playback cursors, or from the transition
while +2C has 0x8000. Every RAM store the original makes must fall in those
fields or in the routines' globals (D_00275BEC..BFB and the D_008111F0
scratch record).

The cases are:

- both first-level chains, entered with the route's blend and frame (8.0 at
  frame 10 for the fall, 8.0 at frame 5 for the slide) and through zero
  blends;
- a chain inside a split step (dt 2.5), and under clip rates 1.2 and 0.8;
- fractional init frames (the target is sampled at float_to_int(frame));
- the D_008106F3 freeze on a transition and on a clip;
- a request that interrupts the chain transition;
- the same-id request after a chain;
- a loop end and a hold end;
- a synthetic event table, patched into the RAM header and into a native
  EMPX variant alike.

Results:

| run | cases | images | fields compared | differences | time |
|---|---|---|---|---|---|
| default | 26 of 42 | 3 (playable, 06, 10) | 473,928 | 0 | ~3-5 s |
| `EM_TEST_FULL=1` | 224 | 16 (playable + beats 00..14) | 3,459,456 | 0 | ~18-23 s |

**Mutation checks** (each a temporary edit, then reverted; the oracle failed
every time):

- starting the follow-on clip at frame 0 instead of +8E;
- gating 001749A0 on the playing clip instead of +20C, which is
  `em_player_pose_select`'s current rule. It fails `same_id_after_chain` on
  every image.
- dropping the 0x4000 chain flag.

## 5. Binding notes

### 5.1 Loading the full set and resolving chains

Whichever module owns the clock, it must load a bank that contains every clip
a bound state can request, including 0x5F and 0x72:

- **Record-owned path (recommended, 5.2).** Map
  `assets/player_clips_full.bank` read-only as an EmPoseRegion at 0xD689C0
  (the player's +40 word). `em_pose_host_workers` and
  `em_player_stage_anim_advance` then resolve clips, chains and events from
  the original bytes exactly, as their oracles already show over this bank.
- **Decoded-channel path.** Load `assets/player_clips_full.empx` with
  `em_pose_chain_bank_load`. `em_pose_bank_load` refuses the file, because it
  rejects chained clips, nonzero +6 and more than 64 clips. Then:
  - replace `EmPlayerPose`'s playback, transition and reset_frame with one
    `EmPoseChain`;
  - `em_player_pose_select(force 0)` becomes `em_pose_chain_request(flags 0)`,
    and `force 1` becomes `em_pose_chain_request(flags 1)` or
    `em_pose_chain_arbiter`, matching the original caller;
  - `em_player_pose_advance` becomes `em_pose_chain_advance` (pass
    D_008106F3 as freeze);
  - publishing uses `em_pose_chain_channels`.

**Four current `em_player_pose` behaviors differ from the original.** The
oracle shows each difference, or the cited routines do:

1. **Same-clip gate.** `em_player_pose_select` compares the playing clip
   rather than +20C. The two differ after a chain, and the mutation check
   above reproduces the difference.
2. **Transition target frame.** A nonzero-blend select samples its target at
   the fractional source frame. 001C8D50 samples at float_to_int(frame)
   (`fractional_frame` case). No current caller passes a fractional frame
   with a blend, so this is latent.
3. **No +8E.** Its transition end restarts at the select's own frame, which
   equals +8E except after a chain.
4. **Release table.** `em_player_pose_release` hard-codes the release column
   of D_00248C90 (the halfword at +0 of the 12-byte row) for the 14 exported
   ids, and fails on any other clip. The original 00182DF0 reads the row for
   whatever clip is current. A binding must read the 459-row table
   (`tools/export_player_tables.py` exports it; the stage lane already reads
   its +8 rate column, D_00248C98) rather than a fixed id list.

`player_pose_request` also invalidates the pose on any clip outside the
14-clip bank. This is the "unsupported ordinary clip request" path that
blocks FLOOR.

### 5.2 Which module should own 001749A0 / 001749F0 / 001C61D0 / anim_eval_skeleton

**Recommendation: `em_pose_host_workers`, over the player's live record
(EmPlayerLiveActor), with the raw bank region.** (Done in the display step:
section 6.) `em_player_pose` should stop
owning these routines for the player. It can remain a publisher of channels
for the interaction runtime until that runtime reads the record, and then
retire.

The evidence:

- **One storage for the fields.** The originals take the actor record: +20C,
  +2C, +3C, +40, the +110 node records, node 0's +8E. The state callbacks all
  pass that record. `em_pose_host_workers` keeps these fields in the record
  itself (the scene coordinator's "single storage for original bytes").
  `em_player_pose` keeps private copies (playback, reset_frame) that have no
  +20C and no +8E, so it cannot express the stale +8E or the +20C/+2C split
  (5.1 items 1 and 3).
- **The consumers already bind it.** The translated state lanes (fall, hang,
  reaction, recovery, major2, climb, slide, ladder, weapon states) take their
  request, arbiter, clip-frames and skeleton workers from
  `em_pose_host_request` / `_arbiter` / `_clip_frames` / `_eval_skeleton` and
  the `em_pose_view_*` adapters. `em_locomotion_display.c` (0017B660 /
  00179D20 / 00179FF0) also reads raw node records through this module's
  leaves. With `em_player_pose` as owner, every one of those lanes would need
  a second adapter layer.
- **It is exact where `em_player_pose` is not.** `em_pose_host_001C6DA0`
  (anim_eval_skeleton) and 001C9940 run the VU0 lanes through
  `em_ee_float.h`, and `test_pose_host_workers_reference.py` compares them
  byte for byte with the original over the route captures. The
  `em_player_pose_palette` hierarchy is a host float composition with a
  measured tolerance, and it says so itself ("makes no VU bit-equality
  claim"). The census shows 001C6DA0 live only through the em_pose_bank /
  em_player_pose evaluation, and lists 001749F0's live stand-in
  (`em_player_pose_select`) as "no oracle of this entry".
- **Chains, events and hold frames already work there.**
  `em_player_stage_anim_advance` (001C64F0), bound to this module's clip
  workers, already translates the chain step, and its oracles cover it over
  the real bank.

`em_pose_chain` is the fallback if the decoded-channel display must stay
live for the player first. It is exact against the same original
instructions, so either path is original. The record-owned path leaves only
one clock and one bank.

### 5.3 Makefile hunks (not applied: the record owns the player's pose, section 6)

```make
# COMMON: em_pose_chain.c and its one dependency. A private lane build with
# these two added links with zero warnings; no live code references them yet.
           src/game/em_pose_bank.c src/game/em_pose_transition.c src/game/em_player_pose.c src/game/em_player_pose_host.c \
+          src/game/em_pose_chain.c src/game/em_stream_lanes_original.c \

+.PHONY: test-pose-chain-reference
+test-pose-chain-reference:
+	python3 tools/test_pose_chain_reference.py
```

## 6. The binding (display step, 2026-09-24)

**One owner: the record.** 001749A0, 001749F0, 001C61D0, 001C64F0 and
anim_eval_skeleton for the player are `em_pose_host_workers` (and
`em_player_stage_anim_advance`) over the player's own record, as section
5.2 recommends. `em_player_pose` and `em_pose_chain` are no longer on the
player's live path: `em_player_pose` still poses the status models (the
special bank runs on the record since census L22: 00183090 through
`player_pose_commit_tick`; Roger runs on the same workers over his own
record, em_area11_roger), and `em_pose_chain` stays a verified, unbound translation of the same
routines over decoded channels.

**The module.** `src/game/em_player_record_pose.c/.h` holds the storage
and binds it; it re-implements none of the routines:

- the record: em_player.c's live `EmPlayerLiveActor` (the stage's record);
- the bank: `assets/player_clips_full.bank` (section 3), read-only at EE
  0xD689C0, the record's +40;
- the node records: 21 x 0xD0 bytes at EE 0x7D5840.., the record's +110
  words;
- D_00248C90's +0 halfword per clip: `assets/player_clip_row0.emch`, a new
  column of `tools/export_player_tables.py` (EMCH v1: count 459, signed
  halfwords). 0015BCF0 reads it to choose the evaluator, 00182DF0 for the
  release;
- the globals: D_00275BF8..BEC, D_008111F0, the scratchpad words, and
  D_008106F3 by pointer at the canonical byte (001C87C0's velocity reset);
- 0015BCF0's animate step, the one block it translates: with +2F3 = 0 and
  +303 = 0, anim_eval_skeleton (001C6DA0) for a nonzero row, else
  001C68C0; 001C68C0 for +2F3 = 3 / 4; 001C6960 otherwise (decomp
  `src/func_0015BCF0.c`, byte-matched).

The attach writes the record's structural words (+C = 21, +40, +60..+6C =
1.0, the +110 array, +164 = 0; every captured image holds exactly these,
which the oracle checks) and runs 0015C420's pose half:
+20C = D_00248A00[+235] (clip 0), bone_init_default_2 and 001C68C0.

**The pose host on the record.** `em_player_pose_host.c` keeps its API and
its port bookkeeping; every pose operation is now a record operation:

| host operation | original on the record |
|---|---|
| a request at frame 0 (`player_pose_request`, idle, acquire, Use, tier-2 stop) | 001749A0(p, clip, force, blend) |
| a request at a source frame (walk entry, run stop, gait tier change) | 001749F0(p, clip, blend, frame) |
| the stage advance, idle and script ticks | 001C64F0 (`em_player_stage_anim_advance`) |
| the opening release, a legacy re-seed | 00182DF0's 2F3 branch: +20C = 0, 001C63E0 |
| a script owner's takeover (census L22) | 00183090 (`em_player_stage_commit`: 001C63E0 for a +2F3 of 1 / 3, 001C67E0 for a +1F2 request), then 001C64F0 by +1F4 |
| the special-bank release (census L22) | 00182DF0's nonzero-2F3 branch: +40 = the default bank, +20C = D_00248A00[+235], 001C63E0 |
| the takeover release | 00182DF0: a negative +20C or a zero D_00248C90 +0 row requests 00174AB0, then 00174A50(16) |
| the foot-stop begin | 0017B910's anim_eval_skeleton, nodes 17 / 18 at +C0 |
| every published palette | 0015BCF0's animate step: the node world matrices +90, and the owner matrix +D0 in the model's trailing slot (the identity in actor space) |

The palettes are world-space now (the record's +B0 / +C4 are the port's
g.pos / g.yaw at the evaluation), so `player_pose_publish` no longer applies
`palette_apply_placement`. The em_player.c `+20C` mirror is gone: +20C is
the record's own field.

**The display.** em_player.c runs the animate step after every stage
(`player_pose_animate`, after 0015BCF0's tail writes). em_player_frame.c
displays the record's matrices for a stage the takeover consumed or a
translated routine owned (`player_states_record_display`); the port's own
idle/walk callbacks keep their legacy baked display until census L12. A
stage whose (+4, +5) is not the port's idle/walk advances the record by
+34 whatever a port stand-in holds. em_player_stage_live.c binds the
stage's clip workers (bone_init, clip_init, clip_resolve, skeleton_frame,
001C8710, 001C87C0, anim_sample_bones, request) to the record's
`em_pose_host_stage_*` and declares `player_states_bind_display(1)`.

**What changed live, and the evidence.**

- The source clock, clip, transition and flags are unchanged:
  `tools/test_player_pose_live_reference.py` over the newgame-control
  reentry trace matches the original first-control capture on all 56
  callbacks (clip, clock bits, transition, flags); newgame-control travels
  9.599989 as before.
- The level smoke passes its six live phases against the captures. Its
  tick log differs from the pre-step build in float ulps of pos / yaw /
  camera between ticks 1572 and 2187 only (the walk to the battery, until
  the elevator refusal re-places the player). The cause is one foot-stop
  begin (frame 1551, tier 1): its feet 17 / 18 are now anim_eval_skeleton
  on the record, EE-exact (for example foot 17 z 225.288284, where the
  host composition gave 225.28833), so the stop's step differs in the last
  bit. The new feet are the original's computation (the pose-host oracle
  compares 001C6DA0 with the original over the captures).
- The takeover and foot-stop palettes (panel, battery pickup, elevator,
  fence door, the walk/jog stops) are the record's evaluated skeleton
  instead of the host composition plus placement. Over the 14 clips of
  the old export, 40 frames each from frame 0 at one placement, the two
  palettes differ by at most 9.2e-5 in any matrix word (a one-time
  comparison of em_player_pose_palette + palette_apply_placement against
  em_player_record_pose_palette, 2026-09-24); the record's are the
  original's words (the oracle below).

**The oracle, `tools/test_player_record_pose_reference.py`** (make
`test-player-record-pose-reference`). It builds the live module and runs
the original 001749A0 / 001749F0 / 001C64F0 (with every callee) /
001C6DA0 / 001C68C0 over the captured images, after every operation
comparing the whole record, all node records, D_00275BF8..BEC, D_008111F0
and the scratchpad words:

- the attach's structural words equal every captured image's;
- re-evaluating each captured record (+B0 = +A0) through the module gives
  the captured node world matrices byte for byte (the original's skeleton
  of that frame);
- every first-level clip of section 1, requested with 001749A0 (flags 1,
  blend 8 or 0) or 001749F0 (a source frame), then advanced by its
  D_00248C98 rate with the animate step after each callback; 0x5E and 0x73
  run into 0x5F and 0x72.

| run | images | clip cases | callbacks | time |
|---|---|---|---|---|
| default | playable, 05, 06, 10 | 46 of 492 (all 41 clips) | 374 | ~5 s |
| `EM_TEST_FULL=1` | playable + beats 00..14 (16) | 1,968 (41 clips x 3 requests x 16 images, each through its whole length) | 126,720 | ~16 min (8 workers) |

Every case in both runs compared exact; the capture re-evaluation is exact
on all 16 images.

**Not done here.** The port's idle/walk display (L12), the
low-health row (+235's latch and 0017B490, L12), and the closure binder
that engages FLOOR (L02) remain (the special bank on the record is live
since census L22).

## 7. Limits

- The first-level list is exact for the recorded route. Whether the
  translated states' other clip requests are reachable in AREA11 is not
  proven. The whole-bank export covers them.
- `em_pose_chain` refuses non-integer transition lengths, where the
  original would run a fractional prev_t. The four chained clips use +6 = 1,
  and every first-level request seen uses a whole-tick blend.
- The event path is verified only on a synthetic table, since the disc bank
  has none.
- Clips 0x36/0x35 and 0x177/0x178 chain through the same code but are not
  first-level clips. The oracle does not run them.
