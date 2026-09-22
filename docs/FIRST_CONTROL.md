# First-control movement evidence

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.
The native port is not assumed faithful. These changes use original instruction
execution and a fresh original state 04 probe, not the earlier port's behavior.

## Matched-duration probe

The isolated original state 04 starts at game frame 4083, player
(250.800003,229.890442,209), body yaw 0.610865295, camera forward
(-0.491901666,-0.457632422,0.740679026). Its archive was hashed before/after
and never saved over. PINE sampled 76 consecutive game frames with no gaps;
changing samples within a frame were retained, and final samples used for
comparison. Raw pad [128, 0] was visible for exactly 30 frames 4086–4115.
Local ignored evidence: `../Extermination/build/startup-reference/first_control_poll.json`.

| Original frame | Player callback result |
|---|---|
|4086|Request walk clip 1, stay motionless; no body turn on this request callback.|
|4087–4093|Turn in place while animation flags contain 0x8000.|
|4094|Blend flag clears; arm locomotion mode 1/substate 1, still zero speed.|
|4095|First translation at speed 0.05.|
|4096–4097|Speed 0.1; promotion callback followed by substate re-arm callback.|
|4101|Speed is one binary32 ULP below 0.3, because EE arithmetic truncates.|
|4102–4103|Speed 0.3; promotion then re-arm.|
|4111|Reach run speed 0.8.|
|4116|First neutral pad callback arms mode 2 and still translates at 0.8.|
|4117–4131|Run-down subtracts 0.03125 each tick.|
|4132|Speed zero; request stop clip 5, mode 4.|
|4144–4145|Stop clip ends; return to idle state and request idle clip 0.|

Original 30-input displacement: 9.599849; corrected native: 9.600048.
Native endpoint (245.482315,229.891891,216.992691), original endpoint
(245.475342,229.891922,216.987808). The remaining horizontal endpoint difference
is about 0.0085; camera evolution and movement arithmetic are under audit.
The prior native shortcut moved 16.100002 over the same 30-input duration.
Native full-opening regression also verified 1303 held-input cinematic ticks
with exactly zero horizontal motion, finite camera/player values and a walkable
original grid surface after movement. Evidence is in ignored
`build/opening_control/motor_run.log` and `after_motor_fix.png`.

## Source addresses and scope

- 00174AC0: gait 0 exits before turning; moving turn bands use prior actor +38 speed.
- 00178C84: horizontal translation uses sin(bodyYaw) for X and cos(bodyYaw) for Z.
- 001612D0: heading 00174AC0, scalar 0017BC40, animation 0017C030, translation 00178B90.
- 0017BC40: scalar mode/substate, tier holds, run-down and published +204/+208.
- 00161020/0017B5C0: eight-tick initial walk blend; low gait 1 additionally waits
 for turn completion. The native host retains this bound, rather than its old
 assumption that movement begins as soon as yaw is within 22.5 degrees.
- 0015BA50: consume prior +204 animation multiplier, reset it to 1, advance animation,
 then dispatch player state. The relevant clip rate table entries are all 1.
- 0017B660: transfer normalized animation phase across gait clip lengths;
 blend to adjacent tier only during rise/fall substates.
- Original state 04 animation bank 0xD689C0: idle 0 = 80, walk 1 = 120, jog 2 = 45,
 run 3 = 40, stop 4 = 20, stop 5 = 10 frames. The entry helper starts walk at
 source 64 = 120−D00248740[0], whose original value is 56.

`make test-player-motor-reference` executes the original function's unmodified
EE instruction stream and reads its original tables. It compares 11,482 valid
state updates bit-for-bit, including finite rounding, and optionally checks all 21
live movement speed samples in the 30-input capture. No original instructions or
binary assets are embedded in the test. `make test-player-heading-reference`
checks 2,360 raw-pad/camera cases; its SDK trigonometric calls are host models,
so this is not a byte-faithful replacement of the SDK transcendental library.

Remaining limitations: native ground/wall response does not yet supply the
original +314 obstruction flags to the motor; interrupted entry/release and
post-run stop animation states need their complete callbacks; pose blending is
still interpolation of exported matrices rather than original bone quaternion
blending. Ordinary idle phase at handoff is not yet proven bit-for-bit. The
movement and camera endpoints are close, not identical.
