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

`em_interaction_projection_publish` implements original `001DD980` and
`001DD950`. It publishes the render-context center, scale and eye-to-target
distance; it does not copy desired camera vectors. Square root uses the
original SDK implementation. `tools/test_interaction_projection_reference.py`
executes both original setters and the SDK body for 1,606 camera pairs,
including four saved original panel/elevator cameras. All 24 output bytes
agree in each case. This establishes the setter arithmetic; consuming these
values in the native depth renderer remains a separate integration task.

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

`camera_commit_original` preserves the argument gate of `0018C0D0`.
Nonzero argument uses a four-unit forward offset, except mode0A uses -1.
Zero argument uses no offset for top-mode3; other top modes offset only
camera modes1 and2. Thus status phase5's nonzero argument still offsets
even with top-mode3. The native commit regression covers 1,623 cases;
original normalization and matrix helpers are explicit test boundaries.

The recovery byte belongs to both scripts and status. Original `0018B9C0`
decrements it before every camera state branch, following player and owner
updates in both ordinary and scripted frames. A script write of80 becomes79
at that frame's camera stage. A Use poll that sees1 remains inhibited even
though the later camera stage makes it0. Status completion writes70, and
the final consumed status frame preserves70 because it commits the camera
without running the camera state update. The recovery oracle checks 5,120
camera states, 16 original outer-frame call orders, 256 native Use gates,
and five original status completion/release pairs.
