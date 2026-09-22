# Original AREA11 distant door

The canonical door is source `0082A3C0`, callback `001BC350`, model-table
index14 and animation bank39. `export_door_original.py` verifies the model
and bank against the original first-control RAM and produces ignored
`assets/scene_snow/door_original/` resources. The initialized pose uses
clip0 at source0, which differs from the model's rest pose.

`em_door_original` implements the original pooled controller and its nested
script phases. An ordinary passive callback does not advance animation.
It builds the pose, publishes actor+B0 plus (0,10,0), then calls draw even
when the visibility result is zero. The runtime retains canonical owner
status, class and armed bytes, original placement, raw channels and current
palette. Scene bindings and GPU objects must be removed before freeing it.

`make test-door-original` compares 5,662 original instruction state/order
cases. `make test-door-original-runtime` verifies 123 compressed channel
keys and the first-control capture: all 50 channel floats, all16 owner
matrix words and all32 door-palette words agree exactly. An actual-resource
ASan/UBSan fixture exercises 240 passive callbacks, canonical binding,
required-worker failure retention and teardown.

`em_door_transit` now implements kickoff `001BBE40` and request commit
`001BC150`. It sets the side latch, patches the script, faces the player,
aligns its position mirrors, starts the script, then performs its first pump.
The original side decision chooses destination2 or1 from row2755F8.
Sound401/402 is also selected by side; it is not an open/close sound pair.
Its oracle executes1,349 kickoff/SDK cases and112 destination cases, including
the fade-before-request-byte ordering and untouched same-area fields.

`em_door_program` runs the actual ordinary-door24DE40/DC00 records. It binds
frame sub0, camera D/sub5, player43/45 blend1, object B/sub6 and terminal
wait70/90. B/sub6 plays FBD50(owner,cue,0,300) before initializing the object
clip0/2 with blend0/start0. The C64F0 result stored at script+E is signed
animation flags, not a frame number. The script's skip-phase byte at+0C is
also BC0E0's animation-active gate; the adapter synchronizes both views on
every pump and start. The timer seeds on its first callback, decrements on
later callbacks, and finishes only on the following zero check.

`tools/test_door_program_reference.py` compares732 original controller and
script callbacks across both sides and four readiness schedules. The actual
resource sanitizer fixture reaches entry2 after98 ordinary callbacks and
entry1 after78, using raw player/object channels and shared ownership. It
stops at the issued room request: it does not assert room loading or release.
The frame prepares camera_top2/selector2 without adding letterbox bars.

The scene must bind real face/alignment, camera retarget, sound, fade and
world-transition services to these adapters. GPU upload, lighting and live
owner-walker integration remain separate. The locked subtype15 program has
additional native workers and is deliberately unsupported by this AREA11
subtype3 adapter. Arming without a required worker retains a concrete fault;
it never invents a locked-door response or a successful room change.
