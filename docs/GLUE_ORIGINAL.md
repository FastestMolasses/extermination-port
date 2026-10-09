# The first level's glue rows on their originals (chain step GLUE)

Date: 2026-10-08. Scope: FIRST_LEVEL_AUDIT.md section 1b items 10, 11 and
12, the startup, input and frame glue the port still ran as its own code,
and the census rows that had no oracle executing them
(FIRST_LEVEL_CENSUS.md section 1.64).

## 1. What runs now

| Original | Where | Translation | Oracle |
|---|---|---|---|
| 001B57E0 / 001B5F40 (pad read, main-loop step C) | em_frame.c `frame_input_read` | `em_slg_001B57E0` (em_startup_load_gaps.c) | test_startup_load_gaps_reference |
| 001AFCA0 (state-0 re-arm) with 001AF5C0, 001AF690, 001AF710 | em_scene_bindings.c `w_001AFCA0` | `em_slg_001AFCA0` | test_startup_load_gaps_reference |
| 001AB790 (001AC070 state 4's task replace) | em_game.c `em_game_install_new` | `em_task_replace_current` (em_task.c) | test_startup_load_gaps_reference |
| 0015CF90 (the vitals copies) | em_player_frame.c `em_player_0015BCF0` | `em_glue_0015CF90` (em_glue_original.c) | test_census_unverified_reference |
| 001B1190 (the taken bit) | the pickups' PERSIST: em_area11_interaction_host.c `pickup_event` -> `em_area11_bindings_001B1190` | `em_gun_rest_001B1190` (em_security_gun_rest.c) | test_security_gun_rest_reference, test_census_unverified_reference |
| 001FC280 (the area ambient loop) | em_scene_bindings.c `em_scene_bindings_001FC280` (001FAE70's first call) | `em_glue_001FC280` (em_glue_original.c) | test_glue_reference |

### The pad read

Step C pumps the platform events and samples the native pad as before
(`em_input_pad`: the keyboard, the controller through
`em_input_stick_from_round_gate`, nothing from a controller in a headless
run), builds the 8-byte libpad read buffer (`em_pad_raw`) and runs
001B57E0 over the pad block D_00810E40..D_00810E7B. The block has one
storage per byte: +0x00..+0x29 is em_pad_actuator's block (installed with
`em_frame_set_pad_block`), except the gait byte +0x17 and the analog bytes
+0x24..+0x27, which with the six halfwords +0x30..+0x3B are the
EmPadUnpack fields (`em_frame_pad_block`); +0x2A..+0x2F (the read-time mode
id) are em_frame's.

libpad is the platform boundary. scePadGetState (00110B80) answers 6, a
connected DualShock on port 0, slot 0. The block starts in the state
libpad's negotiation leaves (phase 4, mode id 7; every capture holds it),
so 001B5F40's phases 0..2 never run: their calls (00110E58, 00110F60,
001110B0) fault if they are reached. 001B5F40's read calls 001B5940, which
is `em_pad_unpack` over the native buffer. D_00810E50, which the scene
coordinator reads, is the block's phase byte.

### The state-0 re-arm

`w_001AFCA0` runs `em_slg_001AFCA0` over the player record, a staging
image of D_00810130..D_008102AF with D_0081060C, the one bone-slot stack
(`em_area11_boxes_bone_stack`) and spad 31F4. Its two worker positions run
the port's area binds in the order they had before:

- `state0_001AF8E0`: 001AF690's camera half to the camera's storage, the
  spawn values, the player stage, render context and camera binds (a scene
  without an original world: its legacy placement), then 001AF8E0 (the
  pool reset and its class-list half), the host's per-area teardown and the
  world model bank.
- `state0_001D0660`: 001F0310 with the effect binders, then 001E7780
  (AREA01's at the level exit's arrival).

Before the call: the host teardown of the old pool's hooks,
`em_game_legacy_state0` (the legacy display's g fields; no original
storage), the area's collision world and message bank, and
`player_states_reset`. 001AF710 ran inside `em_area11_bindings_reset`
before; that reset now calls `em_area11_boxes_area_reset`, and
`em_area11_boxes_reset` (a host that never ran the area build) still runs
001AF710 itself.

### 0015CF90

`em_glue_0015CF90` reads the player record's +0x220, +0x228, +0x234 and
+0x235 and stores D_00810706 / D_00810707 (canonical progress bytes; 706
since this step), D_00810858 / D_0081085C (g.status.health / infection,
their one storage, which is also the record's vitals view: the stage loads
them before 0015BA50 and stores them after it, so the copy is the identity
there) and, when +0x220 <= 0.0 on the EE compare and D_008106B9 is 0, the
latch B9 = 1. It runs after the whole stage; the original runs it before
0015CBA0 and 00187350, which neither read nor write its bytes. A frame
whose stage did not run (the legacy struggle, a scene without an original
world) publishes the port's vitals into the record first
(`player_states_vitals_publish`). 001B07C0 reads D_00810706 from the
progress region and stores its mask (& 1) back, as the original does.

### 001B1190

The pickup owners' PERSIST event (0015AFA0's and 00219550's
001B1190(+0x9A)) goes from em_pickup.c to the host's event hook, which
calls `em_area11_bindings_001B1190`: `em_gun_rest_001B1190` over
D_00810700 and the canonical D_00810860 rows, the translation the gun
cable already used. em_pickup.c keeps only the reader of the bits
(001B11E0's test). A row past D_00810B5F (areas from 0x18) is not
canonical: the binding faults where the original would write.

### 001FC280

`em_glue_001FC280` walks D_0024D650[area][room] + entry * 0x30 to the
record's +0x20 word (the exported spawn tables, `em_spawn_table_read`):
the high half is the loop id (arithmetic: 0xFFFF is -1), forced to 0x44E in
area 0x0B with event 0x30 (D_00810788) set; a changed id stops the cached
loop (0011A070, `em_sfx_stop_track`; not reached in the first level),
caches the id and starts the new loop (001FB9F0,
`em_sfx_submit_001FB9F0_track`; an id without an exported sound faults);
then the two 00119828 calls with the low half. The cache D_00282160..70 is
em_scene_bindings' `s_ambient`; 001FBC50 sets D_00282160 = -1.

## 2. Evidence

- `make test-glue-reference` (about 1 s, every case in the default run):
  the original 001FC280 against `em_glue_001FC280` over all 972 cases (every
  spawn record the ELF's table reaches under four cache states, and
  D_00810788 = 0xFF outside area 0x0B) and the 16 route captures as
  captured; every call with its arguments, D_00282160..70, no store
  outside them, the routine's jal targets exactly its three callees, all
  five branches both ways, and the fail-stop of each missing worker.
- `make test-census-unverified-reference`: 0015CF90 over 120 synthetic
  cases and the player record of beats 00..14 (all five stores byte for
  byte); 001B1190 through the binding's views over areas 0..0x17 and every
  uid byte (the fail-stop from area 0x18), the capture 00 -> 01 (the
  battery take), and em_pickup.c handing PERSIST to the host unchanged.
- `make test-startup-load-gaps-reference`: 001B57E0 / 001B5F40 (unit cases
  and every capture's pad block), 001AFCA0 with its callees, 001AB790.
- The level smoke: `check_taken_and_vitals` (AREA11's taken row goes
  through the route snapshots' values in order; D_00810706 / 707) and
  D_00810706 / 707 in `check_exit`'s rows (+0x235 = 2 from exit_00 f325,
  back to 0 at the arrival's 001B07C0). The whole main line through the
  AREA01 arrival gives the previous build's tick log apart from the
  host-timed stream, loader and flame-loop fields.
- `make test-area11-interaction-host`: the battery take sets its taken bit
  through the same translation.
- Mutations checked on a scratch copy: a logical shift of the loop id, a
  native float compare in 0015CF90 and a masked D_00810706 store each fail.

## 3. Limits

- No capture records libpad's negotiation (phases 0..2); the native pad
  never reaches it.
- The vitals copies D_00810858 / D_0081085C share their storage with the
  record's vitals view (g.status), so the copy is the identity in the port.
- 001AC070, 008237C0, 00199C50 and 001D19E0 stay unbound; their census
  rows say why. (001AB4E0, unbound here, is bound since 2026-10-09 as main-loop
  step R: em_display_env_live, census 1.68, GS_EXACT.md section 11.)
