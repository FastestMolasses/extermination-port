# Interaction alignment and camera publication

`em_interaction_alignment` implements the numerical portion of original
`001B6F00`: transform a homogeneous local point through the owner's world
matrix, retain the player's ground height, and wrap owner yaw plus an offset
to `(-pi, pi]`. The matrix is the owner transform, not its displayed bone0.
Every VU product and sum retains its original rounding boundary. The caller
then applies the position through the independently verified `182F90` host
binding and preserves the original order of face and align commands.

`tools/test_interaction_alignment_reference.py` runs original `B6F00`, its
SDK matrix routine and angle wrapper for 1,639 cases, including three actual
panel owner matrices. All target and yaw bytes agree. Final placement is a
separate boundary covered by `test_player_pose_host_reference.py`.

`em_interaction_projection_001DD980` implements original `001DD980` up to its
tail call: the eye-to-target distance d (the original SDK square root) and the
two floats it hands `001DD950` (2 + 1.02 d, d); it does not copy desired
camera vectors. Its callers (the live camera, `em_camera_live.c`, and the
interaction host) then run the render context's `em_rcl_001DD950`, which
stores the camera target quadword at +0x2450 and the pair at +0x2460 / +0x2464
(RENDER_CONTEXT.md). `tools/test_interaction_projection_reference.py`
executes both original setters and the SDK body for 1,606 camera pairs,
including four saved original panel/elevator cameras. All 24 output bytes
agree in each case.

The player face command writes live yaw without immediately rewriting the
original hip mirrors or saved script Euler. Alignment shifts the previous
hip by the same VU-rounded translation as the ground position, then saves
the current Euler. The next player tail publishes the newly posed hip.
Native display matrices may update sooner, but cannot feed that new hip back
into another command in the same owner callback. The host regression covers
both face-then-align and align-then-face, acquired and ordinary poses, with
1,624 additional original command comparisons and a real idle pose where
the former code moved the cached hip about 0.00108 units too early.

The elevator's align, face and D5 records yield between commands; the
current refusal program does not run all three in one callback. Panel
face-then-align does run in one owner hook. The hip-state tests establish
the command boundary without claiming a same-callback refusal camera error.

`camera_commit_original` runs the translated `0018C0D0`
(em_camera_commit_original); its argument gate (the four-unit or -1 forward
offset) is in CAMERA_LIVE.md section 2. Status phase 5's nonzero argument
still offsets even with top-mode 3.

The recovery byte belongs to both scripts and status. Original `0018B9C0`
decrements it before every camera state branch, following player and owner
updates in both ordinary and scripted frames. A script write of 80 becomes 79
at that frame's camera stage. A Use poll that sees 1 remains inhibited even
though the later camera stage makes it 0. Status completion writes 70, and
the final consumed status frame preserves 70 because it commits the camera
without running the camera state update. The recovery oracle
(`tools/test_interaction_recovery_reference.py`) checks 5,120 camera states,
16 original outer-frame call orders, 256 native Use gates, and five original
status completion/release pairs. Live, 0018B9C0 is `em_camleft_0018B9C0`
through `em_camera_live_frame` (CAMERA_LIVE.md).
