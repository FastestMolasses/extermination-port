# Startup and load gaps (census lane L34)

Date: 2026-09-23 (session s87). Scope: census lane **L34-startup-and-load-gaps**
of `docs/FIRST_LEVEL_CENSUS.md` — the 25 title / New Game / load / opening
originals, 1,626 instructions.

Files (new, owned by this lane):

- `src/game/em_startup_load_gaps.h` — every API below.
- `src/game/em_startup_load_gaps.c` — display, DMA watchdog, screen-flow task,
  pad assignment, state-0 re-arm, node start, pad read, opening-script actor,
  overlay init, collision tables.
- `src/game/em_startup_load_gaps_sound.c` — 001FB100 / 001FC6E0 and the
  sound-bank upload 001FB370 / 001FB3E0 / 001FB910.
- `tools/test_startup_load_gaps_reference.py` — the original-instruction oracle.

Nothing here is bound into the live app yet. The live files belong to the
coordinator chain; section 4 says exactly what each binding replaces.

## 1. Result

| Function | Census before | After | Translation | Evidence |
|---|---|---|---|---|
| 001AB6A0 task dispatch | unverified | **live** (em_task.c, verified) | `em_task_dispatch` | oracle, unit + captured table |
| 001AB740 task register | unverified | **live** (em_task.c, verified) | `em_task_register` | oracle |
| 001AB790 task replace | unverified | **live** (em_task.c, verified) | `em_task_replace_current` | oracle |
| 001AB4E0 display envs | missing | verified-unbound | `em_slg_001AB4E0` | oracle, unit + captured |
| 001AB590 DMA watchdog | missing | verified-unbound (really a hardware boundary, section 4 item 10) | `em_slg_001AB590` | oracle |
| 001AC070 screen-flow task | unverified (legacy stand-in) | verified-unbound | `em_slg_001AC070` | oracle |
| 001AF470 pad assignment | missing | verified-unbound | `em_slg_001AF470` | oracle, all 256 configs + captured block |
| 001AF5C0 player reset | stand-in | verified-unbound | `em_slg_001AF5C0` | oracle, unit + opening RAM |
| 001AF690 status block reset | stand-in | verified-unbound | `em_slg_001AF690` | oracle |
| 001AF710 bone-slot stack | stand-in | verified-unbound | `em_slg_001AF710` | oracle |
| 001AFCA0 state-0 re-arm | stand-in | verified-unbound | `em_slg_001AFCA0` | oracle |
| 001B0F60 node start | missing | verified-unbound | `em_slg_001B0F60` | oracle |
| 001B57E0 pad read | unverified | verified-unbound | `em_slg_001B57E0` | oracle, unit + every capture |
| 001B5F40 libpad state machine | unverified (partial) | verified-unbound | `em_slg_001B5F40` (+ `em_slg_001B62A0`) | oracle |
| 001BB0E0 opening-script actor | unverified | verified-unbound | `em_slg_001BB0E0` | oracle, unit + the 2 resident actors of the opening RAM |
| 001FB100 sound frame | stand-in | verified-unbound | `em_slg_001FB100` | oracle, unit + captured |
| 001FC6E0 delayed cues | missing | verified-unbound | `em_slg_001FC6E0` | oracle |
| 001FB370 bank-load gate | missing | verified-unbound | `em_slg_001FB370` | oracle |
| 001FB3E0 bank upload | missing | verified-unbound | `em_slg_001FB3E0` | oracle |
| 001FB910 SIF DMA kick | missing | verified-unbound | `em_slg_001FB910` | oracle |
| 008237C0 AREA11 overlay init | missing (critic 7.2: stand-in) | verified-unbound | `em_slg_008237C0` | oracle + every capture |
| 00199C50 collision tables | missing | verified-unbound | `em_slg_00199C50` | oracle, unit + every capture |
| 0015C1F0 player model kind | verified-unbound | unchanged | `em_player_misc_0015C1F0` | test_player_misc_workers_reference |
| 001AB7D0 task stop | verified-unbound | unchanged | em_status_scene_original | test_status_scene_reference |
| 001FEF70 bank selector | verified-unbound | unchanged | `em_status_scene_bank_001FEF70` | test_status_scene_reference |

Also translated, not in the census (it never ran on the route): 001B62A0, the
pad-block reset 001B5F40 calls on a disconnect.

For the next census pass: 22 rows move (3 to live, 19 to verified-unbound);
none is live-original yet except the task table, because the bindings in
section 4 are coordinator work.

## 2. Verification

`python3 tools/test_startup_load_gaps_reference.py` (proposed target
`make test-startup-load-gaps-reference`, section 5).

- Default: about 7–8 s wall. Every case class and boundary, fixed-seed
  samples of the sweeps, the opening capture and one route beat.
- `EM_TEST_FULL=1`: about 25–35 s. Exhaustive sweeps and all 15 route beats.

How it works (house style of `tools/test_player_fall_reference.py`):

- The original instructions run from the user's pinned ELF (SHA-256 checked)
  and, for 008237C0, from the AREA11 overlay extracted from the user's disc,
  written at 0x823500. No instruction or data bytes are embedded.
- Every callee that is not translated here is hooked on the original side and
  scripted per case (return values, and for some callees writes into the
  compared storage, so re-reads after a call are exercised). The native side
  gets the same script through its workers. The test asserts that the set
  of jal targets of each routine is exactly the hooked set plus the
  translated routines plus the two C-runtime routines that run as original
  code (memset 00121A28, block_copy 00121870). So nothing runs on one side
  only.
- Compared after every call: every byte of the storage the translation owns,
  every worker call in order with its arguments, and the return value. Every
  byte the original writes must lie in the compared storage (stack
  excluded), so a write the translation lacks fails instead of passing.
- Branch coverage: all 97 conditional branches of the 23 executed routines
  are taken both ways by the default run. The test fails if one is not.
- Fail-stop: each entry, given an unbound worker set, returns -1 and writes
  nothing.
- The interpreter is the shared one of `tools/test_player_slide_reference.py`,
  with three additions local to this test: the three DMA CHCR registers as
  memory, the PCPYH instruction (the C-runtime memset uses it) and write and
  branch recording. None of these routines does float arithmetic. 001BB0E0
  passes a float bit pattern through (compared as bits), and 001AF5C0 and
  001AF690 store the constant 1.0f.

Captured states (the opening RAM `build/startup-reference/opening_ee.bin` +
scratchpad, and the route beats `build/s87/route/*/eeMemory.bin`):

- 001B57E0 over the captured pad block with libpad reporting the stable
  state 6. Every capture is in phase 4, the read path, so the route runs only
  that path of 001B5F40. This agrees with the census: 00110E58, 00110F60,
  001110B0 and 001B62A0 never ran.
- 001FB100 / 001FC6E0 over the captured sound globals (block_copy runs as
  original code).
- 001AB4E0 over the captured display environments and spad 3B94/3B96. The
  SDK routine 001002E0 runs as original code on both sides: natively, the
  worker runs it in a second interpreter over the native env bytes.
- 00199C50 over the captured collision file D_0028A598. Its result equals the
  captured scratchpad 0x700031F8..0x70003253 byte for byte in every capture.
- 001AB6A0 over the captured task table (slot 0 = 001ACEC0 running, slots 1
  and 2 free in every capture).
- 001BB0E0 over each pool node whose behaviour is 001BB0E0. The opening RAM
  holds two. None of the route beats holds one.
- 001AF470(D_00810708) against the captured block spad 0x3B74..0x3B83. The
  route uses button config 0.
- 008237C0 against the captured D_00275C1C / C24 / C28 / C2C.
- 001AFCA0 over the opening RAM.

## 3. Per function

Offsets are original record offsets. "Workers" are the callees a binding must
supply; each is a function pointer in the routine's worker struct, and a
NULL one faults.

### 001AB6A0 / 001AB740 / 001AB790 — the task table (em_task.c)

The oracle runs em_task.c itself against the originals. Each case sets up
three slots with random states (0, 1, 2, 3, 4, 5, 0xFF), functions and
private bytes. Then it runs one to three dispatches, sometimes with a
registration outside the dispatch. The task functions act through the
original 001AB790 / 001AB740 / 001AB7D0 on one side and through
em_task_replace_current / em_task_register / the current record on the
other: replace themselves, register another slot, stop themselves, write a
private byte, or wake a slot. Compared: state bytes, function identity,
the 24 private bytes, the call order, and the slot the scratchpad cursor
names during each call. Bytes +1..+3 must be untouched. The captured
tables are replayed too.

The translation matched in every case. Deviations that the oracle does not
exercise, all off the route:

- `em_task_dispatch` skips a slot in state 2 whose function pointer is NULL.
  The original calls the word at +4 unconditionally. **Lead:** make this a
  fault (fail-stop rule).
- After a dispatch the original cursor 0x70003B6C rests on 0x28A7B0 (past the
  table), and `em_task_current()` is NULL. A 001AB790 outside a dispatch
  would write past the table in the original. The port returns NULL.
- `em_task_register` refuses a slot outside 0..2. The original does not
  check.

Recommendation: `tests/task_test.c` is superseded by this oracle (retirement
rule 4). The lead decides.

### 001AB4E0 — display environments (main loop step R)

For D_00810EA0 and then D_00810EC8 (0x28 bytes each) it calls
001002E0(env, psm 0, 512, 224, dx, (short)(dy << 1)). Then it sets the FBP
field (bits 0..8 of the 64-bit DISPFB word at env +0x10) to 0 and 0x38. dx
and dy are spad 0x70003B94 / 0x70003B96, sign-extended.

- Binding: step R of `em_frame_step` (after the vsync wait), over two
  0x28-byte env images the Original-profile presenter reads. The FBP value
  selects which of the two 512x224 frame buffers is displayed. The worker
  001002E0 is the SDK display-environment builder, a boundary with no port
  translation. Its content can be produced by running it as the test does,
  or by a future translation.
- Limit: no live presenter reads these images yet. The port shows its own
  framebuffer.

### 001AB590 — DMA CHCR watchdog (main loop step L)

For D0, D1 and D2 CHCR byte 0, a nonzero ASP field (bits 5:4) is cleared.

- Binding: none possible. The native port has no DMAC. The translation
  exists so that the census row is closed honestly. **The next
  classification should mark this row as a boundary** (hardware DMAC),
  like the other SDK/driver rows.

### 001AC070 — the title / continue screen-flow task

One tick of the task in slot 0 between the boot task and the game task.
Spad 3B90 is cleared first. States (record +8): 0 resets D_00275BD4, calls
001AEDB0(0) and 001D1EF0, then picks state 1 / sub-mode 1, or state 2 /
sub-mode 3 when D_00275BDC is set. 1 waits for 001AC3B0. 2 runs the title
prompt 001AC480: result 3 toggles the sub-mode (+0xE) between states 3 and
1; result 1 takes the latched selector +0xF (0 → state 4, D_00275BE0 = 0;
1 → 00225A00, state 5, D_00275BE0 = 1; other → state 6, +0xC = 0). 3 runs
anim_frame_top_a 001ACA20; a 2 or 3 bumps the retry counter (wrapping after
2), calls 001FBC50, 001D2880 and 001AEDB0(0), and returns to state 2. 4
calls 001AB790(001ACEC0) and returns without the tail. 5 runs 00225AC0(0).
6 waits for 00200A40. Every path except 4 ends with 001D2830(3, 1). Edge
bytes +9/+0xA are cleared on every transition, as the original does.

- Binding: replaces `em_game_legacy_continue_task_001AC070` (em_game.c,
  installed through `w_001AB790` in em_scene_bindings.c) and the title
  flow's 001AC070 states in em_startup.c / em_frontend.c. The record is the
  task's `EmTask.user` (user[k - 8] = +k). Globals: D_00275BD4, D_00275BDC
  (the scene state's canonical from-death flag) and D_00275BE0.
- Workers: 001AEDB0 → em_fade.c (live); 001AC480 → the title prompt
  (em_startup.c, live-translated; test_title_menu_reference);
  001AB790 → `em_task_replace_current` with the 001ACEC0 task;
  001FBC50 → the lane L36 translation (verified-unbound; `em_sfx_stop_all`
  is the live stand-in). 001D1EF0 and 001D2830 are census-missing (lane
  L32). 001D2880 is reported no-effect (UM_001D2880).
  001AC3B0, 001ACA20, 00225A00, 00225AC0, 001AF150 and 00200A40 never ran on
  the route, so their workers may stay NULL. They fault if reached.
- Route: states 2 → 4 only (census: 001AC3B0, 001ACA20, 001AF150 never ran).

### 001AF470 — pad button assignment

Writes the eight halfwords at spad 0x3B74..0x3B82 for config 0, 1 or 2 (low
byte of the argument). Any other config writes nothing. Config 0 is the
default block `em_input.h` documents. Configs 1 and 2 swap the
SQUARE/CROSS/CIRCLE and R1/R2 roles.

- Binding: the 001AF2C0 New Game reset calls it with D_00810708
  (`em_game_new_game_reset_001AF2C0`, em_game.c). The block is read by the
  use/fire/status/draw tests through spad 3B74..3B82. The live readers use
  their own copies (em_door.c, em_camera.c, the player closures), so this
  block is the storage they should read.
- Route: config 0 in every capture.

### 001AFCA0 with 001AF5C0, 001AF690, 001AF710 — the state-0 re-arm

001AF5C0 zeroes the 0x320-byte player record D_008102B0. It then stores +0x14
= the record's own address and +2 = 0, the floats +0x60..+0x6C, +0x80..+0x8C,
+0x78 and +0x7C = 1.0, the words +0x70 and +0x74 = 0, and the halfwords
+0x94 = -1 and +0x96 = 0x3D. Last it calls 001D8BF0(player, 1). 001AF690
zeroes D_008101E0 (0xD0), D_008101D0 (0x10) and D_00810130 (0xA0), then sets
D_0081060C = 1.0. 001AF710 zeroes the 0x480 bone slots of 0xD0 bytes at
D_007D5840 and writes their addresses into D_007D4640[]. It then sets the
stack cursor D_00275BD0 = D_007D4640 and the count D_00275BCC = 0x480.
001AFCA0 runs the three, then 001AF8E0 and 001D0660, then sets spad 31F4 = 0.

- Binding: replaces `em_game_legacy_state0` inside the `w_001AFCA0` binding
  (em_scene_bindings.c). Storage: `player` = the player record bytes
  (`EmPlayerLiveActor.bytes`); `player_self` = the port's word for the
  record's own address (the original stores 0x008102B0; the lead decides the
  convention); `status` = D_00810130..D_008102AF (the status hub's blocks,
  em_status_hub_ui.h); `bones` = the bone-slot stack that
  `em_roger_actor_original` pops and pushes. `EmSlgBoneSlots` uses the same
  address-word views as its `EmRogerActorWorld` (slots / slots_base
  0x7D5840, slot_stack / slot_stack_base 0x7D4640, d00275BD0, d00275BCC).
- Workers: 001D8BF0 → `em_roger_actor_001D8BF0` (verified-unbound);
  001AF8E0 → `em_actor_pool_reset_001AF8E0` (live); 001D0660 → 001F0310
  (census-missing) + 001E7780 (overlay module dispatch, a boundary).

### 001B0F60 — node start (001BBDA0's kickoff)

If 001B0EA0(node) reports nonzero it returns 1. Otherwise it sets node +0x40
= D_0028A574, calls bone_init_default_2(node, (short)n), increments node +4
and returns 0.

- Binding: `em_door_original`'s 001BBDA0 kickoff (S2 opening). Workers:
  001B0EA0 → em_owner_services_original / em_fan_original (verified-unbound); 001C63E0 →
  em_pose_host_workers / em_owner_services_original (verified-unbound). Data:
  the D_0028A574 word.

### 001B57E0 / 001B5F40 / 001B62A0 — the pad read (main loop step C)

001B5F40(out = D_00810E70, pad = D_00810E40) stores libpad's state
(00110B80(port +4, slot +8)) at +0xC. A disconnected pad (state 0) clears
the phase bytes +0x10/+0x11/+0x12 and calls 001B62A0. On phase +0x10:

- phase 0, state 6 or 2: read the mode id through 00110E58 (1, then 2).
  Mode 4 goes to phase 4, or phase 1 after 00110F60(1, 3). Mode 7 goes to
  phase 1 after 00110F60(1, 3), or, once +0x11 is set, to phase 2 after
  001110B0 on the actuator bytes +0x1E.
- phases 1 and 2: wait until the state is neither 5 nor 7.
- phase 4 (the read): +0x2A = +0x14, then 001B5940(out, pad, 1 for state
  6 / 0 for state 2). State 7 resets instead.

001B57E0 clears the six halfwords D_00810E70..7A and re-centres
D_00810E64/65 to 0x80 whenever 001B5F40 returns 0.

- Binding: replaces the fixed "stable DualShock, analog read" assumption in
  `frame_input_read` (em_frame.c step C), over the byte image
  D_00810E40..D_00810E7B. Workers: libpad 00110B80 / 00110E58 / 00110F60 /
  001110B0 are the platform pad boundary (the native gamepad reports a
  state: 6 for a connected pad, 0 for none; mode ids and actuator
  alignment as libpad would); 001B5940 → `em_pad_unpack` (em_input.c,
  live).
- Route: phase 4 in every capture, so only the read path ran.

### 001BB0E0 — the opening-script actor

The behaviour 001BAC00 installs for the opening script's spawn entries.
node +0x20 is the spawn entry (+4 key, +0xA kind, +0xC time step);
node +0x24 is the owner, whose +0x2E is a done mask indexed by node +0x2E.
States (node +4):

- 0: 001BAD40(node, entry); when it returns 0, go to state 1 in the same
  tick.
- 1: if the owner has marked this index done → state 2. Otherwise, unless
  the key is 0x270D or 0x270C, act on the kind:
  - 6: anim_advance_time, 001C68C0, 001F9660(node, key);
  - 0: 001BA580(node, key), anim_advance_time, 001C68C0;
  - 3: nothing;
  - 4: 001C68C0 and the node's +0x4C method, without the +1 flag;
  - 5: 001C5C90;
  - other: anim_advance_time, 001C68C0.

  The advancing kinds set node +1 = 1 and call the +0x4C method.
- 2: state 3, then 001BA540 unless the key is 0x270D or the kind is not 0.
- 3: 001AFC10 (free).

- Binding: the pool behaviour for nodes whose +0x10 is 0x1BB0E0
  (`em_actor_roster.c` lists it as "opening-script actor (script op 0x14)").
  It replaces the model/palette playback of `em_opening_actor.c`, which is
  not a translation of this routine. The view is built per call: `node` =
  the record bytes, `entry` = the spawn entry bytes, `owner` = the script
  owner's record.
- Workers: 001BAD40 (census-missing, lane L19); 001BA580, 001BA540, 001C5C90
  → `em_roger_actor_original` (verified-unbound); 001C68C0 →
  em_pose_host_workers (verified-unbound); 001C64F0 → anim_advance_time
  (`em_player_pose_advance`, live; also the player stage workers' .advance); 001AFC10 →
  `em_actor_pool_free_001AFC10` (live); +0x4C → the node's draw method.
  001F9660 never ran on the route.

### 001FB100 / 001FC6E0 — sound bookkeeping (main loop step H)

Unless D_00821058 == 1 (the movie driver's frame), it calls
001F9CF0(D_00821058). When the committed output mode D_0028215B differs
from the requested D_0081011C, it commits the new mode: 00119870(mode), then
0011A608 on the two channel masks 1 << D_00281FD4 and 1 << D_002820F4. Mode
0 uses the windows (0x3FFF, 0) / (0, 0x3FFF); any other mode uses (0x3000,
0x3000) twice. Then it copies the 0xC0 bytes D_00281B70 → D_00281C30 and
runs 001FC6E0. 001FC6E0 walks the ten cue records D_00281F30 {delay, cue,
a2, a3}: a nonzero delay counts down; at zero, a cue other than -1 is
started (001FB9F0(cue, 0x1000, a2, a3), skipped for cue 0) and set to -1.

- Binding: step H of `em_frame_step`, after G. No live call site exists: the
  copy stand-in `em_sfx_frame_snapshot` has no caller. Storage: `d281B70` =
  em_sfx's `requested` (+0xC0 = `snapshot`), 48 words each, matching
  EM_SFX_TRACKS. The cue table D_00281F30 has no port storage yet.
- Workers: 001F9CF0 → em_stream_lanes_original (verified-unbound, L36);
  00119870 / 0011A608 → the SPU output mode, a boundary for the native
  mixer; 001FB9F0 → em_sfx's sfx_start (live).
- Route: D_0081011C = 0 and no pending cues in the captures.

### 001FB370 / 001FB3E0 / 001FB910 — the sound-bank upload

001FB370 is a gate on D_00282150 (0 → 1 and run, 1 → run, other → reset
both state bytes and return 0). 001FB3E0 is one step per call on
D_00282151, over the bank file `a0`: header +8 count, +0xC records, +0x10
payload size, +0x18 payload offset; entries at the cursor
{size, destination offset, bucket}. The states:

- 0: initialise.
- 1: on a bucket change, release that bucket's handles and re-seat the
  destination from the 5-word base table D_00264890; allocate the IOP
  scratch buffer (0010F8F8).
- 2: kick the SIF DMA (001FB910).
- 3: wait on 00119450, or skip the wait when busy.
- 4: align the destination to 0x40 and register the block (001194B8) into
  the bucket's handle slot; -1 retries.
- 5: poll, with a 0x50-tick timeout and a busy flag.
- 6: publish (001199F0(h, 100)), free the scratch buffer (0010F968), and
  advance.

Once the (signed char) record count reaches the header's total, it clears
both states and returns (a0 + payload + 0x40) & ~0x3F. 001FB910 builds the
descriptor {src, dst, size, 0} once per entry and uses the kernel calls at
0010BC00 (syscall 0x78), 0010BAA0 (0x64, argument 0) and 0010BBE0 (0x77,
count 1). It then polls 0010BBC0 (0x76): 1 while the transfer runs, 0 when
done, -1 when no transfer id was returned.

- Binding: the kind-3 finaliser of the module loaders (001FF590 state 5 and
  001FF830 state 6, both boundary rows). In the port, that is the
  `w_001FB370` worker of em_status_scene_original (not bound) and the S1
  area load of the AREA11 sound bank. `EmSlgBankFile` is the loaded file
  (EE address + bytes). `base` = D_00264890, which an exporter must read from
  the user's ELF (the test reads it the same way). Workers: all IOP/SIF/SPU
  boundary. The native equivalent of "upload to SPU RAM and register" is
  em_sfx_bank.c's bank load. The destinations and handles this machine
  computes are the original's SPU layout.
- Native fail-stop beyond the original: a bucket outside the 5 base words,
  a handle slot outside D_00281D50..D_00281F2F, or a file read outside the
  loaded file faults. The original would read or write neighbouring memory.
- Route: the opening capture shows buckets 1..3 holding 3, 1 and 1 handles.

### 008237C0 — the AREA11 overlay init

It sets D_00275C28 = 0x20, D_00275C1C = 0x0082AD00 (the overlay's
per-level record block), D_00275C2C = 0 and D_00275C24 = 0. The census
row's address is splat 00823780 plus the 0x40 overlay header.

- Binding: at the AREA11 overlay load, the point where the original's
  overlay dispatcher runs the init. Readers: 001E9580 / 001E9E60 (the
  per-level records at D_00275C1C + i * 0xA060) and 001E7780. The critic's
  note stands: the port's roster spawn currently covers what those readers
  feed. This translation gives the values once a reader is ported.

### 00199C50 — the collision scratchpad tables

From the collision file D_0028A598 it sets spad 31F8 = the file address and
spad 31FC / 3200 / 3204 / 3208 / 3210 = the file address + header words
+0, +8, +0x10, +0x20, +0x18. Spad 320C = the halfword +0x24, sign-extended.
It builds five cumulative entries 3214..3224 (step = 2 * spad 320C), then
3228..323C: six further steps when header +0x1C == 0xC, otherwise zeros.
Then spad 3250 = D_0028A5A8 and the halfword spad 324C = *D_0028A5A8.

- Binding: replaces the no-effect `UM_00199C50` in the state-0 chain
  (em_scene_bindings.c). em_collision.c and the em_coll_* walkers read these
  scratchpad words as their table bases (for example their `count` is spad
  320C); they should take them from this storage. `EmSlgCollFile` = the AREA11
  collision file as loaded (EE address + bytes) plus D_0028A5A8 and its
  halfword.
- Route: the translation reproduces the captured scratchpad of every
  capture.

### 0015C1F0, 001AB7D0, 001FEF70 — verified-unbound, binding notes only

- 0015C1F0: bind `em_player_misc_w_0015C1F0` (em_player_misc_workers.c)
  at 001B07C0's call site. It replaces the reported no-effect
  `UM_0015C1F0` in em_scene_bindings.c.
- 001AB7D0: em_status_scene_original's slot-2 loader stops itself
  (`*slot_state = 0`). It becomes live when that loader is bound. With
  em_task that is `em_task_current()->state = 0`, which this oracle also
  exercises against the original 001AB7D0.
- 001FEF70: `em_status_scene_bank_001FEF70` (status scene loader, lanes
  L20/L35). It is bound together with that loader.

## 4. Binding summary for the lead

1. Task table: already live and now verified. Change the NULL-function skip
   in `em_task_dispatch` to a fault.
2. `w_001AFCA0`: `em_slg_001AFCA0` in place of `em_game_legacy_state0`.
   Workers: `em_roger_actor_001D8BF0`, `em_actor_pool_reset_001AF8E0`, and
   001D0660, which needs 001F0310 plus the overlay-dispatch boundary. Its
   bone-slot stack becomes the one storage the Roger module's
   EmRogerActorWorld views.
3. State 0: `em_slg_00199C50` in place of `UM_00199C50`;
   `em_player_misc_w_0015C1F0` in place of `UM_0015C1F0`.
4. Step C: `em_slg_001B57E0` over the D_00810E40 image, feeding the
   existing 001B5940 translation.
5. Step H: `em_slg_001FB100`, with em_sfx's requested/snapshot arrays as
   D_00281B70/C30 (this retires `em_sfx_frame_snapshot`).
6. The 001AC070 task: `em_slg_001AC070` replaces
   `em_game_legacy_continue_task_001AC070` and em_startup's title-flow
   states. This needs 001D1EF0 and 001D2830 (census-missing, L32) first;
   otherwise their NULL workers fault at the first tick.
7. New Game reset: call `em_slg_001AF470(map, D_00810708)` where 001AF2C0
   does, and point the spad 3B74..3B82 readers at that block.
8. Opening script actors: `em_slg_001BB0E0` as the 001BB0E0 pool behaviour
   once 001BAD40 (L19) is translated.
9. The rest (001AB4E0, 008237C0, 001B0F60, the bank upload) wait for their
   consumers: the presenter, the per-level record readers, the door
   kickoff, and the module loaders.
10. 001AB590 should be reclassified as a boundary.

## 5. Makefile (for the lead; not edited by this lane)

Test target:

```make
# Census lane L34: the title / New Game / load originals, executed from the
# user's ELF and compared with em_startup_load_gaps*.c and em_task.c.
.PHONY: test-startup-load-gaps-reference
test-startup-load-gaps-reference:
	python3 tools/test_startup_load_gaps_reference.py
```

When the first binding lands, add
`src/game/em_startup_load_gaps.c src/game/em_startup_load_gaps_sound.c` to
COMMON. Both compile with zero warnings in the app build (checked with the
private lane build `build/b7-startup-load-gaps`) and under
`-Wall -Wextra -Werror -Wpedantic`.

## 6. Limits

- The unit cases are synthetic states with scripted callees. The captured
  states cover what the route leaves in RAM: phase 4 of the pad, the idle
  sound frame, the two resident script actors, and the collision tables.
  The S0/S1 transitions (title ticks, the bank upload while it runs) were
  not captured mid-flight, so their route evidence is the census plus the
  resulting RAM (the bank counts, the pad block, the overlay globals).
- 001002E0 (display), libpad, the SIF/IOP/SPU calls and the overlay dispatch
  are boundaries. The oracle checks the arguments passed to them, not
  their effect.
- Nothing in this lane is live except the task table. Section 4 lists the
  bindings in the order they can land.
