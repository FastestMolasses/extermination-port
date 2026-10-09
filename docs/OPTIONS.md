# The options screen, its memory-card screen and the title's load screen (audit 1b item 15)

Chain step OPTIONS (2026-10-08). The options screen SELECT opens in
gameplay, its rows and row screens, the memory-card screen of its load row,
the quit prompt, and the same memory-card screen reached from the title
menu's LOAD GAME entry after a death, on the original code, checked against
the decomp capture lanes OPTIONS (opt_00..opt_08) and DAMAGE (dmg_05)
(decomp docs/CAPTURES_C10.md "OPTIONS" and "DAMAGE"; recordings
`../Extermination/build/c10/options/` and
`../Extermination/build/c10/damage/dmg_05_load_screen/`). Addresses are the
original's; no instruction of the original is reproduced here.

## 1. State

| Path | Original | Port | Checked by |
|---|---|---|---|
| Opening the screen | 0x1AE040 state 1's classifier 001AE7E0 r == 1 (SELECT, or the pad phase D_00810E50 != 4): D_008106C4 = 2, 001FBC50, the cue 0xC | live (section 2); the lead decision Q1 that withheld SELECT is deleted | test-scene-classify-reference, test-scene-frame-reference; opt_00..08 |
| The screen | 0022A650 with its list 0022AEA0 and text 001FCBD0 / 001FCE30 | live (em_options_original, em_options_live) | test-options-reference; opt_00..08 |
| Vibration, sound | 00201720 (the rumble 001B61C0 when vibration is switched on; the sound's mono byte, committed by 001FB100's step H through 00119870) | live; mono reaches the mixer (section 4.4) | test-options-reference, test-area11-sfx-reference (pass M); opt_01, opt_02 |
| Screen position | 00201F70 over 0x70003B94 / 96 (screen module 0x2B through 0022A590) | live (the offset stored and, since 2026-10-09, shown: main-loop steps R 001AB4E0 and U 00100550 hand it to the display registers and the picture moves by it, the user's decision (a) in LAUNCHER_OPTIONS.md; GS_EXACT.md section 11. Direction inferred from the register meaning: Left (x + 1) moves the picture one framebuffer pixel right, Up (y + 1) two lines down) | test-options-reference; opt_03; test-display-env-reference; test-gs-display |
| Brightness | 00202BA0 (the calibration picture, screen module 0x2B) | live | test-options-reference; opt_04 |
| Button config | 00202D10, then 001AF470 over the masks 0x70003B74..83 | live (the masks are canonical; the player reads them there) | test-options-reference, test-startup-load-gaps-reference (001AF470); opt_05 |
| Default | 00201C50 | live | test-options-reference; opt_06 |
| Load row | 001AF6F0, then the memory-card screen 00225AC0(0) (section 5) | live up to the slot choice; a chosen slot faults (002267A0) | test-options-reference; opt_07 |
| Quit | 0022B420 (No: back; Yes: r == 3, 001AD140 as the death does) | live; Yes is bound but no recording plays it | test-options-reference; opt_08 |
| Closing | r == 1: 001AF1C0 (the settings into the progress block, 001AF470), the cue 0xD, 001FAE70(0) | live | test-options-reference; opt_00..08 |
| The title's LOAD GAME after a death | 001AC070 state 2's 00225A00 and D_00275BE0 = 1, state 5's 00225AC0(0) every tick | live (section 4.3); the boot's title faults (no AREA11 binding) | test-title-menu-reference, test-options-reference; dmg_load |
| Saving | 00225AC0(1) from 0020CDC0 (request 6) and 001AD740 | unreachable in AREA11 (section 7) | decomp save_scan.json |

## 2. Opening and closing

The scene coordinator's frame machine (em_scene_frame.c, em_sf_001AE040)
classifies the frame with 001AE7E0; r == 1 is SELECT, or a pad phase byte
D_00810E50 other than 4, and the machine goes to state 2 with D_008106C4 =
2, 001FBC50 and the cue 0xC (em_scene_bindings.c w_001FB9F0). Until this step
the port withheld SELECT from the classifier (the lead decision Q1,
SCENE_COORDINATOR_DESIGN.md section 9); that mask, em_sf_001AE040_q1, its
message and its test cases are deleted.

Every frame of state 2 runs 0022A650 through the binding (w_0022A650) and
draws its stream in the same tick; the world is frozen (no 001D1EA0 runs),
as on the game-over screen. Its result: 1 closes (001AF1C0, the cue 0xD,
001FAE70(0)); 2 (a game loaded) is reached only through 00227300, which
faults first; 3 (quit, Yes) runs 001AD140.

## 3. The translations and their oracle

`src/game/em_options_original.c` translates, by hand from the original
instructions (the decomp's C where it is byte-matched): 0022A650, 0022A590,
0022AEA0, 00201720, 00201C50, 00201F70, 00202BA0, 00202D10, 0022B420,
001AF6F0, 001AF1C0, 001AF150, 001FCBD0, 001FCE30, 00225AC0, 00225720,
00225D20, 00226070, 00225A20, 00225CF0, 002256E0, 00225700, 00226010,
001FE9A0, 001FECB0, 001FE920 and 001FE8D0. 001AF470 runs as its one
translation, em_slg_001AF470; 00225A00 as em_area01_room_00225A00. Two
decomp NEARMISS bodies disagree with the instructions and the translation
follows the instructions: 0022B420 returns 1 for Circle and 2 for Triangle
(the C has them swapped), and 00226070's state 1 returns 2 only for its
Triangle result (the C returns the poll value).

`make test-options-reference` (`tools/test_options_reference.py`) executes
the original routines over the OPTIONS and DAMAGE recordings' RAM, every
callee outside the lane run as original code too, and compares RAM, the
scratchpad and the arguments at every callee entry and at the end. Scripted
boundaries: the card SDK 00114988 / 00114848, 001FBC50, 001FABB0, 001B61C0,
00200970's packet, 001FF080's task slot and the two card screens past the
recordings (002267A0, 00227300). EM_TEST_FULL=1: 3,337 designed cases (the
screen's states with the pad edges and repeats that act on them, every row
and setting, the text with and without its token, the card screen's states,
every poll result, one, two or no cards); 491 of the 492 branch outcomes,
the missing one listed as unreached (0x202D40 not taken: 00202D10's table
D_00264F98 has no type-3 entry). The default runs a covering sample (153
cases, 491 of 492 outcomes, about 16 s).

## 4. The binding

### 4.1 Storage

`src/game/em_options_live.c` runs the translations over views of the port's
one storage at their original addresses (the AREA01 UI lane's memory
model): the settings D_00810118 (16 bytes), the running task's record
*0x70003B6C, the card record D_00810040 (0xD4 bytes), the progress block's
canonical ranges, the message block D_002821B0, the busy byte D_00275BD8,
the masks 0x70003B74..83 and the screen offset 0x70003B94 / 96. The
settings, the masks and the offset are canonical in EmSceneState since this
step (d810118, spad3B74, spad3B94 / 96); their earlier copies are gone:

- the vibration byte D_00810119 (em_pad_actuator) is d810118[1];
- the sound byte D_0081011C (em_stream_live's 001FB100) is d810118[4];
- the masks (em_player_closure_live's pad configuration) are spad3B74, and
  the player binding no longer runs its own 001AF470 at bind time: the New
  Game's 001AF2C0 does, in em_pickup's progress reset;
- the progress bytes 0x810708, 709, 70C and 754..757 are written by
  001AF2C0 / 001AF1C0 and read back by 001AF150 (em_scene_state.h).

The boot's 001AB430 (em_scene_settings_001AB430) stores the defaults.

### 4.2 Callees

Every callee is dispatched by its original address: the 2D layer 00207D00 /
00207E40 / 00207F80 and 0020A7A0 to em_page_draw over the status pages' GS
memory (the screen modules 0x2B and 0x2A applied as they load; the EMSP
export carries their windows, tools/export_status_pages.py), the glyph and
line draws 001CC1E0 / 001CC170 / 001FC770 to em_message_live (style
D_00275828), 001FE480 to em_message_bank_string over the help container
*D_0028A498, 001CBA50 / 001C5FB0 to the page text, the libc leaves to the
views, the cues 0020CD40 / 60 / A0 to the host's 001FB9F0, the loader
001FF080 to em_status_runtime_module_load (the loader task in slot 2 clears
D_00275BD8 at its 0x63 step; tools/export_module_loader.py exports 0x2A and
0x2B), 00200970(1) to em_status_runtime_restore, the fades, 001FBC50,
001FABB0, 001D2830 and 001B61C0 to the interaction host's workers, and the
card SDK to em_memcard (section 5). 002267A0 (a chosen slot's directory
read) and 00227300 (the load) fault. A missing view, a refused draw or a
failing worker latches the call's fault and the coordinator stops.

### 4.3 The title's load screen

After a death the title flow runs again (DAMAGE.md section 6). On the menu's
LOAD GAME (cursor 1), 001AC070 state 2's verdict runs 00225A00 (the card
record cleared) and sets D_00275BE0 = 1 in that tick (em_startup's
EM_STARTUP_LOAD_GAME request; em_frontend load_begin). State 5 then calls
00225AC0(0) every tick (EM_STARTUP_LOAD_GAME_FRAME; em_frontend load_frame)
through the options binding the AREA11 interaction host keeps after the
death; its draw stream is drawn over the title's black frame. The result 1
(the screen's exit) completes the request in the same tick and the flow is
back in state 2 (the menu loads again); 2 cannot occur (00227300 faults
first). The title flow also does what 001AC070 does at every entry that the
screen reads: 0x70003B90 = 0 (the game task writes 2) and the pad words
D_00810E74 / E70 / E50 from the frame's step C (em_frame_scene_input). The
boot's title has no AREA11 world and no binding: LOAD GAME there fails the
run with a message (no recording; the first level reaches the load screen
only after a death). The title's OPTIONS entry (00200A40) is not on the
first level's recordings and stays pending.

### 4.4 Mono

The sound row's byte is committed by 001FB100 (step H) to D_0028215B and,
through 00119870, to the EE sound library's D_0027F778. Both readers have
their mono arm now: 001FBF50's (em_sfx_compute_gains: both channels the
volume) and 001179E0's (em_sfx_volume_words). `make
test-area11-sfx-reference` pass M runs the original over both arms.

## 5. The memory card (platform boundary)

`src/game/em_memcard.c` answers the SDK's libmc calls the screen makes at
host speed: 00114988 GetInfo(port, slot, &type, &free, &format) and
00114848 Sync(mode, &cmd, &result). Port 0 (slot 1 on the screen) is the
host directory `data/memcard/slot1`, port 1 `data/memcard/slot2`, relative
to the working directory (the macOS app's directory, the iOS app's
Documents). A directory holds the card's files in the original's data
layout (the card's root: the game's save directory and its files, byte for
byte); the first level writes and reads none. GetInfo reports type 2 (a
formatted PS2 card) for a slot whose directory exists; its result is 0 once
the port has been asked since the boot, else -1. The boot's card check
(em_frontend's stand-in for 001AB9D0 state 3) creates both directories and
asks both ports, as the original's boot check 0022A460 does: the
recordings' GetInfo results in the load row are 0 on both ports. A free or
format pointer, a slot other than 0, a port other than 0 / 1 or a missing
directory faults (no recording shows what the card server returns then).
The card helpers' globals (D_00275C58..6F, D_00264E30..3B, D_00275840..47)
have one storage there.

## 6. The side runs

`make test-level-smoke-options` (part of `make test-level-smoke-side`) plays
nine side runs opt_00..opt_08 from truck_crossing (src/game/em_level_smoke_test.c
"options"; the capture tool's closed-loop policies with its pad latency),
each checked by `tools/test_level_smoke.py` and, state by state, by
`tools/level_smoke_options.py`: the settings, the task record's options
bytes, D_008106C4, the offset, the masks, the pad mode and phase, the
committed sound byte, the rumble and, from the load row on, the card record
and the busy byte, sampled after every frame on both sides. The sequence of
distinct states must equal the recording's and every state must last the
recording's rows, except a screen module's load (host speed, never longer)
and the last state.

`make test-level-smoke-damage` adds **dmg_load** (from crevice_prompt):
dmg_flame's way to the title after the death, then dmg_05: Cross on LOAD
GAME, the load screen to its slot choice, Triangle back to the menu.
`tools/level_smoke_damage.py` checks it as dmg_flame up to the title, then
the title rows (the frontend's "startup: title row" lines in level-smoke
runs) from the Cross's fade-out to the menu: 13 states equal to the
recording's, each for its rows except module 0x2A's load (9 rows; the disc
23) and the title's module 1 (host speed), both presses to their fade-outs
as recorded, and the card record's load mode, slot choice and exit result.

Known difference (left out of the comparison on purpose): the card record
D_00810040 before the load row clears it. The original's boot card check
0022A460 leaves its last state there; the port's boot check is the native
stand-in and leaves zeros. Nothing reads the record before 001AF6F0 /
00225A00 clears it.

## 7. Not reachable in the first level (fail-stop kept)

- **Saving.** 00225AC0(1) is called only by 0020CDC0 (request 6, posted by
  00157F60 for a save terminal, model 0x38: none in AREA11) and 001AD740
  (the game task's +9 = 3, set only after AREA21's store to 0x70003B93).
  Proof: decomp CAPTURES_C10.md "Save paths: the first level offers none"
  (`build/c10/options/save_scan.json`).
- **A chosen slot and a load** (002267A0, 00227300; r == 2): no recording
  chooses a slot; both fault.
- **Quit, Yes** (r == 3 -> 001AD140): bound, not recorded.
- **The title's OPTIONS entry** (00200A40) and LOAD GAME on the boot's title.

## 8. Curiosity

The button configuration's labels (help group 8) have a line for the 7th
row (line 0x24: a reload label qualified by the laser sight, the qualifier
in brackets) that is never drawn: 001FCBD0 finds its token character '['
and, since the byte after it is not '7'..'9', draws nothing for the line
(the oracle's text cases run it). Decomp docs/CURIOSITIES.md entry 12.

## 9. Re-exports

    python3 tools/export_status_pages.py     # + the options' windows, the help container, modules 0x2A / 0x2B
    python3 tools/export_module_loader.py    # + screen modules 0x2A and 0x2B
