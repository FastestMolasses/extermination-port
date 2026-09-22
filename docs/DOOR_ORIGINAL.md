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

Kickoff `001BBE40`, the actual door script, and room transition `001BC150`
remain required host workers. The recovered destination row is selected by
the original side decision; the old manifest's single entry is insufficient.
GPU upload, lighting and live owner-walker integration remain separate.
Arming a door without its transit workers faults while retaining ownership;
it must not invent a locked-door response or a successful room change.
