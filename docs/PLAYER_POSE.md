# Original player pose channels

> **Since the display step (2026-09-24) the player's pose is not this
> module's.** The player's clip clock, node channels and skeleton live in
> the player record and are worked by `em_pose_host_workers` through
> `em_player_record_pose` (PLAYER_CLIPS.md section 6); `em_player_pose_host.c`
> keeps its API over the record. `em_player_pose` / `em_pose_bank` /
> `em_pose_transition` still pose the status models, and their tests keep
> running (Roger and the special bank run on the original pose workers since
> census L22: `em_area11_roger`, `player_pose_commit_tick`); the
> player-specific entries below (acquire, idle tick, release, script tick,
> gait base) are no longer on the player's live path. What follows is the
> module's history and its remaining users' reference.

The new pose core retains original quaternion, translation and scale channels.
It supplies the source pose for interaction acquisition and release without
decomposing the port's displayed matrices. `em_player_pose_host.c` tracks the
ordinary source at the real player callback boundary while retaining the
existing ordinary display. The shared interaction scene host can consume its
acquire, idle, script and release helpers.

## Original evidence

`tools/export_player_pose_channels.py` reads the original player bank from
`../Extermination/extract/chunk28/f01_id3c.bin`, runtime bank `0xD689C0`. The
ignored `assets/player_channels.empc` contains decoded keys, original key times,
hold flags, parent indices and clip headers for clips `0x00`, `0x01`, `0x02`,
`0x03`, `0x04`, `0x05`, `0x40`, `0x41`, `0x42`, `0x43`, `0x45`, `0x47`,
`0x15C` and `0x15D`.
There are 21 original nodes and a trailing identity palette slot in the native
model. No original binary content is checked in.

Door43/45 use original headers57AF0/5D320:21 nodes,150 frames,next=-2,
blend0 and no event table. Their twelve-byte rows at248C90 are
`(0,0,0,0,1.0)`, so release forces idle0/blend0 before the ordinary
same-clip blend16 request. `export_door_player_clips.py` replaces only those
two existing palette slots; the other55 model clips and every geometry,
texture and table byte survive unchanged. Adding their raw keys also
preserves all12 existing EMPC clip payloads exactly. No live door-player
matrix capture has yet been asserted.

The following sources establish the implemented boundaries:

- 001C84D0 / 001C85D0 decode original rotation and vector keys.
- 001C86A0 seeds transition velocity; 001C87C0 advances node channels.
- 001C8D50 freezes evaluated source channels and samples the transition target.
- 001CA0A0 blends quaternions without normalization, preserving its sign branch
  and upper clamp. 001CA1C0 converts the result to a matrix.
- 001C67E0 initializes a clip. A zero blend resolves its one-step transition
  immediately. A nonzero blend retains the source pose initially. On the final
  callback, 001C64F0 resets to the stored integer source frame without consuming
  target frame 1. The original SDK converter 00128250 truncates the positive
  source frame; the oracle executes that routine and its decode callee.
- 001749A0 preserves a same-clip request when flags are zero. 001749F0 always
  calls the initializer, including when restoring the same base clip.

The arithmetic helpers are original, self-contained code. Captured original
execution distinguishes nearest division from ordinary truncation and also
distinguishes the smaller operand's single guard bit in add/sub cancellation.
These are the finite arithmetic rules of the saved PCSX2 reference execution.
They are not a complete EE FPU implementation or a universal hardware claim.
Multiply and add round separately; a fused host expression changed captured
values. Quaternion values remain unnormalized throughout.

## Acquisition, scripted playback and release

0015BA50 mode 1 advances the existing animation, then calls 0015B130. Its
selector branch performs 00174A50(default clip, blend 8), then 00182D70, and
returns before the ordinary movement/state callback. Readiness is immediate.
It does not wait for the visual transition and does not advance the new idle
on that same callback.

00174A50 passes flags zero to 001749A0. Therefore acquisition while already in
idle clip 0 preserves the current cursor and pose. A different source clip
starts the eight-callback transition. The eighth callback restores idle frame
0; the following callback advances to frame 1.

`em_player_pose_script_tick` is the optional shared runtime pose worker. It must
run after every `EmInteractionAnimation` callback, including a blend-1 request
that returns palette result 0. It performs the corresponding raw channel init
or advance, validates remaining time and flags against the separate temporal
core, and replaces a published baked palette with the channel-derived palette.
A missing callback faults instead of silently seeking to a later frame.

Release 00182DF0 tests the original `D00248C90[clip * 6]` halfword. The row is
zero for 40–42, 47, 15C and 15D, so it first forces 00174AB0 (clip 0, flags 1, blend 0).
Its subsequent default-clip blend-16 request sees the already-current clip 0
and returns without reinitializing it. A same-idle release also preserves the
cursor. The flagged locomotion rows 1 through 5 can use the blend-16 branch.
The native API is scoped to these verified healthy-player rows, with no
external skeleton override. Unsupported rows fail explicitly.

## Host call contract

Load the EMPC bank once alongside the player model and keep it alive until
`EmPlayerPose` is no longer used. At the opening's actual player release, seed
clip 0, frame 0. This initial cursor is directly present in immutable original
state 03: clip 0, remaining 80, ordinary player mode, external-bank flag clear.
State 04 after 120 additional ticks has remaining 40. The release path calls
001C63E0 to reset all node adjustments and seed the default clip at frame 0.

For each ordinary player callback:

1. If the shared interaction already owns the player, run its player worker;
   its idle or scripted pose worker owns the one animation advance.
2. Otherwise advance the ordinary base pose using the previous callback's
   animation multiplier. The relevant original clip rates are all 1. Idle
   advances by 1 even when the movement scalar is zero.
3. Attempt shared takeover before `player_move()` or any ordinary state
   callback. On acceptance, skip that state callback and do not add another
   idle advance. On a blocked takeover, continue the ordinary state callback.
4. Apply real clip requests from that state callback with `select`. Use its
   explicit source frame and blend duration. The generic `force` argument
   distinguishes 001749A0's same-clip gate from 001749F0's unconditional init.
5. In the ordinary locomotion matrix stage, call `gait_base` for its current
   tier/substate/blend. This reproduces 0017B660's source-channel side effects.
   It does not implement the adjacent-tier display blend.

The existing host's post-`player_move()` palette block is too late for initial
acquisition. `g.walk_t` is also not an authoritative source cursor. During a
tier blend, 0017B660 evaluates a second clip for display, then restores the
original base clip through 001749F0 when blend is below one. That reset converts
its source cursor to an integer. The source channels must remain distinct from
the displayed tier blend. The original displayed blend itself uses 001C9D50's
matrix-to-quaternion conversion; this is not permission to decompose the
displayed result as an interaction source.

The bound first-level requests are walk entry (clip 1, source 64,
blend 8), run stop (clip 5, source 4, blend 6), return from that stop (idle,
blend 12), interrupted run stop (clip 2, source 27, blend 4), and idle fidget
(15D, source 0, blend 8, then default blend 8). Their callback timing must come
from the existing original-backed state workers; comparing rendered weights
does not identify a clip request. Walk and jog foot-placement stops are now
bound through the separate original B910/C030 worker described below.

Only the acquired idle/script paths use `em_player_pose_palette` until
ordinary display blending is replaced. The palette includes the 21-node
actor-local hierarchy and its identity slot. The host applies owner placement
and publishes hip mirrors after a successful palette result. Status/menu frames
freeze all of these animation workers.

`player_pose_set_stage_hook` installs a worker returning -1 on failure, 0 when
ordinary processing should continue, or 1 when the callback was consumed. An
already acquired player requires a consumed callback. Missing source data or an
unsupported ordinary worker is reported explicitly; it cannot be replaced with
a display-matrix decomposition or guessed cursor.

Legacy stand-ins (WP-2/H12). Aim, R2, melee, door transit/arrival/lock,
examine, interact, the hit machine, the legacy scripted-clip mailbox and low
health still display through the legacy renderer and
have no original source channels. They call `player_pose_legacy_hold()`
(declared in `em_player.h`), which freezes the source: no advance, and ordinary
requests, Use polling, foot-stop and idle work are skipped. The source is not
destroyed. Once `player_move` gets past every stand-in, it calls
`player_pose_legacy_release()`. That re-seeds the source as 00182DF0's
nonzero-2F3 branch does: `bone_init_default_2(D_00248A00[+235])` initializes
the row default at frame 0 with no blend, and the tail leaves idle state 0 with
counter 300. This is the only 00182DF0 path that does not read the previous
channels, and the stand-ins have no channels to read. The row is an
approximation: the original indexes D_00248A00 with the whole +235 byte, but
the host derives it from live health only (row 0 healthy, row 1 at or below
35). Bit 1 of +235 (set by 001756E0, 00161790, 00162190, 00162A40; rows 2/3,
clips 0x4B/0x55) is not modelled, and bit 0 is a latch (set when 0021C350 or
0015D100 take +220 to 35 or below, cleared by 0015C700), not a live comparison.
The host keeps holding while `pd_state == 2`, `sa_req/sa_cur` is set, or health
is at or below 35. Acquisition re-seeds a source held by a player-driven
stand-in (aim, R2, melee, door, examine, interact) first; this is a host
adaptation, and 00182B30's refusal set is not modelled. While the hit, `sa_*`
or low-health hold is active, acquisition is refused explicitly (-1). The
message names the live blocker, not the stand-in that first froze the source:
those three holds do not re-register while an earlier hold (for example aim)
is active, so the first owner can be stale. Row 1 (low health) defaults to clip 0x0A, which is not
exported, so at low health the hold stays.

A failed foot-stop begin (palette or solve failure, a clock below 1, tier-2
select failure) is a native unsupported path, not an original stand-in.
`player_pose_unsupported_hold()` reports it once with the reason and holds; the
next `player_move` then snaps the source to the row default with no blend. That
snap is a host adaptation: 0017C030 mode 3 runs the 0017B910 solve without a
failure case. An active pose transition is not a failure: mode 3 has no blend
gate, so the begin runs on the transition's current channels and clock (see the
foot-placement section). The clock-below-1 refusal lives in
`em_player_foot_stop_begin`; 0017B910 instead clamps the walk duration to 1 and
uses 10 for jog, so that refusal is also not original. `player_pose_invalidate()` is kept only for genuine native failures:
a failed advance, an unknown clip, a failed re-seed, or a foot-stop callback
fault.

`player_pose_align` applies the original 00182F90 delta (target minus feet) to
feet and cached hip. It also shifts the native displayed palette as a host cache
adaptation; the original routine itself does not write matrices. `player_pose_face`
changes live yaw and republishes placement without advancing channels. Op4/sub8
at 001B9C10 does not update saved scratch Euler. `player_pose_script_euler` retains
that saved value until alignment or the real 0015BCF0 player-tail equivalent.
The tail also refreshes the displayed bone-1 hip. This preserves the distinction
between saved 3B50 and live actor C0 for camera D5.

## Validation and remaining boundaries

`make test-player-pose` runs ASan/UBSan tests against the exported bank. It covers
conditional acquisition/release, interrupted transitions, missing-callback
detection and all 962 scripted 40–42/47/15C callbacks used by the original clock test.
`make test-pose-reference` runs three independent source-address checks:

- 15,600 executed original quaternion/TRS/velocity/channel comparisons, with
  8/16 transition intervals and fractional steps.
- All 5,590 exported nonterminal keys against the original decode functions.
  Four immutable original captures match 2,100 channel float words and 252 key
  cursors exactly: opening handoff idle 0, first-control idle 40, panel idle 5,
  and panel animation 15C at frame 30.
- 6,553 conditional request/release, normalized gait cursor restoration and
  original source-frame conversion routine. The clock comparison additionally
  exercises fractional rates, split-step flags, loops and terminal holds.

The native actor-local hierarchy is checked against those captures after owner
placement. Maximum absolute matrix errors are 0.00006103515625 for handoff,
panel idle and panel animation, and 0.0000457763671875 for first-control idle.
This measured host matrix tolerance is separate from exact channel equality;
no VU matrix byte-match is claimed. Captured node adjustments are identity in
all four fixtures. Nonidentity aim/head adjustments and other clip families
need their own original-backed worker before this palette path can cover them.

The initial eight-callback transition has an original-instruction oracle but
has not yet been compared against a live capture of all eight displayed poses.
The live host regression completed the original opening with 1,303 movement-
locked callbacks, then 30 movement, 18 run-stop and 8 interrupted-stop callbacks.
`tools/test_player_pose_live_reference.py` compares these 56 consecutive source
states with the original full-actor trace: clip, remaining float bits, transition
bit and flags all match exactly. This includes the first stop advance of 1.8:
remaining 6 becomes 4.199999809265137, as recorded in the original. Resetting its
rate to one at the stop request would be incorrect.

`make test-player-pose-host` checks idle/fade countdown, fidget timing, shared
callback ownership, release without an extra idle advance, placement/Euler
mirrors, the legacy hold/re-seed lifecycle, walk and jog foot-placement stops
begun three callbacks into a twelve-tick blend, and explicit invalid-source
failure under ASan/UBSan. The legacy case holds for 30 callbacks, releases, and then
jogs. The tier-2 foot-placement stop still fires and runs to phase 3. The
clip-5 stop request is accepted, and acquisition succeeds, both after release
and directly from a held source. Acquisition is refused (-1) while the hit and
`sa_*` holds are active. The first idle
callback seeds counter300; case1 is blocked by the original transition-fade
state while its animation continues to advance. End-to-end scene interaction
regression remains required.
None of these checks makes the legacy displayed locomotion matrix blend faithful.


The follow-up host binding restores the aborted walk-entry states 99/100 and
consumes the previous animation multiplier even when locomotion mode is idle;
a pending turn can legitimately leave that multiplier zero. The successful
Use caller now has an explicit `player_pose_use_accepted()` operation: original
1798D0 clears movement and conditionally requests default clip 0/blend 0 before
the next callback acquires script ownership. An already-idle cursor survives.

`tools/test_player_pose_host_reference.py` executes original 182F90 with its
SDK vector callees in 1,203 cases, including captured panel/elevator placements.
All tested feet, hip and saved-Euler words agree after bounded VU rounding.
It also checks 187 original 61020 aborted-entry callbacks, 7 original 1798D0
reset cases, and 32 idle/walking Use-poll gates. It also executes
00182DF0's 2F3 branch for rows 0 and 1, with 1C63E0 and 1C6150 as recorded
boundaries. Row 0 seeds clip 0, and the native legacy release produces clip 0,
remaining 80, no transition, and idle state 0 with counter 300. Row 1 seeds clip
0x0A, which is absent from the bank, and the native host refuses the re-seed.
It also executes 0017C030 mode 3 and its 0017B910 solve for 60 foot-stop begins
made while a pose transition is active (walk and jog, blends 4 to 16), with the
native host's evaluated feet and transition clock as inputs: mode 3 reaches the
solve, and the step words, mode 5 and the jog clip-4/blend-10 request match. Shifting native cached matrices remains a host adaptation; the
original alignment function only shifts the position mirrors. The sanitizer
host test passes the corresponding lifecycle and invalid-source cases.

`player_use_set_hook` installs the real Use check after the current action
priority branches, before ordinary movement. It is separate from the earlier
player-stage takeover hook. Original idle case 1 gates Use on the fade state;
idle entry case 2 does not. Return states 99/100 and walking re-entry state 63
do not poll. An accepted idle check continues its physics tail, whereas an
accepted walking check returns before that tail, matching 61020 and 612D0.

## Walk and jog foot-placement stops

`em_player_foot_stop.c` implements 0017B910 entry and 0017C030 mode 5. Entry
evaluates the existing raw source skeleton, then selects foot node 18 when
remaining time is below the original healthy-row boundary (58 for walk, 24
for jog), or node 17 otherwise. 0017C030 mode 3 calls 0017B910 without a blend
gate. During an active transition the host evaluates the transition's current
channels and passes the transition clock, which is what actor +3C holds then
(`tools/test_player_pose_live_reference.py` compares +3C with it through the
stop blends). These are source node positions rather than
the legacy tier-blended display. Their planar distance from working feet is
rotated by the original SDK Euler routines to produce each callback's step.

Walk retains clip 1 and halves its residual planting interval, clamped to at
least one. Its moving callbacks publish animation multiplier 2. Jog requests
clip 4 at frame 0 with blend 10, moves for ten callbacks, then waits for the
original clip-end flag. Both return through idle state 0 on the following
callback and its default blend 12. The new path publishes channel-derived
palettes only during this stop and its idle return; ordinary gait display
blending retains its documented approximation.

`make test-player-foot-stop-reference` executes 618 original B910 entries and
16,179 C030 callbacks, including the original software square root and SDK
matrix/vector routines. Given equal evaluated foot-node inputs, every checked
step, duration, rate and output-position word matches. Source-hierarchy world
matrix error remains the separately measured tolerance above, not a claim of
bit-identical foot-node positions for every pose.

The optional `EM_CONTROL_LOW_GAIT=1` or `2` setting extends the real
`EM_STARTUP_TEST=newgame-control` input fixture. It uses Option/Command plus W
for 60 callbacks after the opening, releases movement for 100, and requires
the foot-stop worker to execute and return to a valid idle source. It does not
seed positions or animation clocks. This is a native integration check; its
final displacement has not been compared with an equal-duration original
low-gait runtime capture.

## Pickup animation subset

The original current-bank headers for pickup clips 40, 41 and 42 occur at
offsets `0x543F0`, `0x55530` and `0x56810`. Each has 21 nodes, 45 frames,
next clip -2, initial blend 0 and no event table. Their original D00248C90
rows have release flag 0 and rate 1. The original whole animation bank is
byte-equal to the immutable panel-confirmation EE capture.

`tools/export_pickup_player_clips.py` replaces only these three EMDL palette
ranges from the decoded raw source channels. It checks hierarchy, headers and
nonoverlapping ranges, then verifies that all 54 other clips, geometry, texture
bytes and tables remain unchanged. Running it again is byte-idempotent. The
raw worker accepts the same three clips and processes every blend-1 commit,
sample and terminal callback. Release forces default clip 0 with blend 0.

The original clock oracle retains the previous 662 panel/lever callbacks and
adds 300 pickup callbacks, covering blend 0 and 1, repeat requests, terminal
holds and release. With blend 1, each pickup first publishes its terminal flag
at callback 46 when the commit callback is numbered 0. Raw key decoding is
checked for every exported pickup key. A live pickup matrix capture remains
outstanding; the earlier captured matrix tolerances cover the shared evaluator
on idle/panel fixtures and are not a per-pose byte-match claim for pickups.

## Footstep clock (WP-15 P14/P15)

The footstep dispatcher 00187350 reads the same source state this host
exposes through `player_pose_source`: clip +20C, remaining clock +3C (the
transition clock during a blend) and flags +200. Over the re-entry fixture
those three values match the original capture on every frame 4094-4141
(`PLAYER_FLOOR.md`).
