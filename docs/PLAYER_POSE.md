# Original player pose channels

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
`0x03`, `0x04`, `0x05`, `0x47`, `0x15C` and `0x15D`.
There are 21 original nodes and a trailing identity palette slot in the native
model. No original binary content is checked in.

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
zero for 47, 15C and 15D, so it first forces 00174AB0 (clip 0, flags 1, blend 0).
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
does not identify a clip request. Clip 4 is available but its full foot-placement
state callback remains outside this binding.

Only the acquired idle/script paths use `em_player_pose_palette` until
ordinary display blending is replaced. The palette includes the 21-node
actor-local hierarchy and its identity slot. The host applies owner placement
and publishes hip mirrors after a successful palette result. Status/menu frames
freeze all of these animation workers.

`player_pose_set_stage_hook` installs a worker returning -1 on failure, 0 when
ordinary processing should continue, or 1 when the callback was consumed. An
already acquired player requires a consumed callback. Missing source data or an
unsupported ordinary worker is reported explicitly; it cannot be replaced with
a display-matrix decomposition or guessed cursor. Ordinary unsupported actions
can still display through the legacy renderer, but subsequent acquisition fails.

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
detection and all 662 scripted 47/15C callbacks used by the original clock test.
`make test-pose-reference` runs three independent source-address checks:

- 15,600 executed original quaternion/TRS/velocity/channel comparisons, with
  8/16 transition intervals and fractional steps.
- All 4,700 exported nonterminal keys against the original decode functions.
  Four immutable original captures match 2,100 channel float words and 252 key
  cursors exactly: opening handoff idle 0, first-control idle 40, panel idle 5,
  and panel animation 15C at frame 30.
- 5,467 conditional request/release, normalized gait cursor restoration and
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
mirrors and explicit invalid-source failure under ASan/UBSan. The first idle
callback seeds counter300; case1 is blocked by the original transition-fade
state while its animation continues to advance. End-to-end scene interaction
regression and walk/jog stop and aborted-entry source workers remain required.
None of these checks makes the legacy displayed locomotion matrix blend faithful.
