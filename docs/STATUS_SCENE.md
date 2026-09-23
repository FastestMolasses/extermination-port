# Status scene workers: the status models and the module-load wait

Status: 2026-09-23 (WP-5). The status-model owners and their static actor
pool are **live** in AREA11: `em_status_models` binds them as the hub's
model workers and draws them (section 7). The module loader is translated
and tested but **not wired**; the module-load wait stays open (section 3).

Files:
- `src/game/em_status_scene_original.{h,c}`: the native translation.
- `src/game/em_status_models.{h,c}`: the live binding (workers, pool, draws).
- `tools/test_status_scene_reference.py` (`make test-status-scene-reference`):
  the original-instruction oracle and the capture checks.
- `tests/status_scene_original_test.c` (`make test-status-scene-original`):
  the native contract test (ASan/UBSan).
- `tools/export_status_models.py` and `tests/status_models_test.c`
  (`make test-status-models`): the model export and the live binding over
  the status-hub capture.

The status background 0020A7A0 is not here: its one translation is
`em_status_background.c` (docs/STATUS_HUB.md), the only D_002655A0 owner.

## 1. What is translated

| Original | Native entry | Source used |
|---|---|---|
| 001AFF10 / 001AFF90 static actor alloc / free | `em_status_scene_alloc_001AFF10`, `em_status_scene_free_001AFF90` | byte-matched C |
| 001AF800 a record's bone release, 001AFEB0 pool bone release, 001AFE60 pool clear | `em_status_scene_bones_001AF800`, `_release_001AFEB0`, `_clear_001AFE60` | byte-matched C |
| 001B0000 pool walk | `em_status_scene_walk_001B0000` | matched C |
| 0020E3A0 code -> glyph | `em_status_scene_glyph_0020E3A0` | byte-matched C |
| 0020E1E0 letter-model spawn | `em_status_scene_letter_0020E1E0` | C, checked against the .s |
| 0020E250 letter sequence | `em_status_scene_letters_0020E250` | byte-matched C |
| 0020E460 letter behaviour | `em_status_scene_letter_0020E460` | the .s (the C is NEARMISS) |
| 0020E6F0 menu player (Dennis) | `em_status_scene_player_0020E6F0` | C, checked against the .s |
| 0020EC80 menu player publish (with 001031E0) | `em_status_scene_publish_0020EC80` | byte-matched C |
| 001F4BF0 menu player glow (D_008104E4 == 1) | `em_status_scene_glow_001F4BF0` | the .s (asm function) |
| 001FF080 + 001AB740 load request | `em_status_scene_loader_request_001FF080` | byte-matched C |
| 001FF0D0 slot-2 loader task | `em_status_scene_loader_001FF0D0` | byte-matched C |
| 001FF830 bank streamer (loader state 0) | inside 001FF0D0 | byte-matched C |
| 001FF3F0 chunk sub-streamer | inside 001FF830 step 2 | the .s (the C is NEARMISS and wrong in two places, section 4) |
| 001FEF70 follow-up bank, 001AB7D0 task stop | inside 001FF0D0 | byte-matched C |

Float arithmetic uses the shared `em_ee_float.h`, the EE COP1 model of
EE_FLOAT_MODEL.md. The module has no other port dependency.

## 2. Behaviour (every number is read from the original code)

### The static actor pool D_0028B020

- **24 records of 0x2F0 bytes.** 001AFF10 takes the first record whose +0
  is 0: +0 = 2, +0x14 = its own address, +0x60..+0x6C and +0x80..+0x8C =
  1.0, +0x94 = -1, +0x99 = +0x9A = 0. A full pool returns NULL.
- **001AF800(a)** returns the +9 bone slot words +0x110.. one by one
  (each slot is cleared and pushed back on the slot stack) and clears +9
  and +0xC. **001AFEB0** runs it on every record in use; **001AFE60**
  clears all 24 records.
- **001AFF90(a)** frees the record its +0x14 names: 001AF800, then the
  words +0x0..+0xF, +0x36, +0x90, +0x98 and +0x1F0..+0x2EF are cleared.
- **001B0000**, for every record in use: 001CB590(record, 0x2F0, +9) (the
  current actor D_00275B44/48 and its bone array D_00275B40), then the
  record's behaviour +0x10.

### 0020E250 / 0020E3A0 / 0020E1E0, the status letters

- **0020E250** issues one 0020E1E0(0020E3A0(code)) per code, in this order:
  - always -1, then 0x10;
  - if CA4 == 2: 0xC, then stop;
  - if CA4 == 0: 0xA, then CA6, then stop;
  - otherwise -2, then either
    - (CA4 == 1): 0xB, CA6;
    - (any other CA4): CA5, CA6, CA7;
  - and after that, 0x15 if CA6 == 4.

  CA4..CA7 are D_00810CA4..D_00810CA7. They are named by address only.
- **0020E3A0** maps -2..21 to '0' '/' '2' '3' '4' '5' '6' '1' '7' '8' '9' ':' ';' '<' '=' '/' '/' '/' '@' '/' '/' '/' '/' 'm'. Anything outside that range gives '/'.
- **0020E1E0(code)** does the following:
  1. a = 001AFF10(). If the pool is full, it does nothing.
  2. a+0x10 = 0x0020E460, the letter behaviour (below).
  3. 001CA6E0(a, 001C6120(D_0028A56C, code)). Its 001CA5E0 stores the model at +0x44.
  4. +0xC = (u8)001C6150(+0x44), which is the byte at model + 8.
  5. +0xD = (u8)code.

### 0020E6F0, the menu player

- **State +4 = 0:**
  1. **Model.**
     - Variant D_008104E4 == 0: costume D_00810C60 1 gives D_0028A588, 2 gives D_0028A58C, anything else gives D_0028A57C.
     - Variant == 1: the same, except that the base model is D_0028A590.
     - Any other variant: D_0028A584.
  2. +0xC = (u8)001C6150(+0x44).
  3. For each i < +0xC: +0x110 + 4i = 001AF7C0().
  4. +9 = +0xC, then 001CB5B0(+0xC).
  5. +0x40 = D_0028A580.
  6. **Clip.** If health D_00810858 <= 35: bone_init_default_2 001C63E0(a, 0xA) and +0xB = 1. Otherwise 001C63E0(a, 0x1C2) and +0xB = 0.
  7. 001CA5F0(a, 0xB).
  8. **Tint.** +0x80/84/88 = 0.01 × (−80 / −100 / −30 × infection D_0081085C). +0x8C = 0.
  9. **Scale and rotation.** +0x38 = 1, rot = (0, pi, 0), +4 = 1.
  10. **Position.** pos = view column 3 + 40·column 2 + 7.4·column 0 + 2.4·column 1, where the view matrix is D_00810610..3C (row-major 3×4).
- **State 1:**
  1. **Breathe.** +5 == 0: +0x38 += 0.01, and +5 = 1 once the result is not < 1.3. Otherwise: +0x38 -= 0.01, and +5 = 0 once the result is <= 1.
  2. **Tint.** Each channel = (0.01 × (k × infection)) × +0x38. +0x84 is clamped to -127 when it is < -127.
  3. **Yaw.** +0xC4 += 0.01. When the result is not <= pi, subtract 2pi.
  4. **Clip swap-back.** If health > 35 and +0xB == 1: anim_clip_init 001C67E0(a, 0x1C2, 16.0, 0.0), then +0xB = 0.
  5. anim_advance_time 001C64F0(a, 1.0), then 0020EC80(a).
- **State 2, 3 or any other value:** 001AFF90(a), the free.
- **Too many bones.** The record is 0x2F0 bytes, so it has room for 120 bone slots. A bone count above 120 makes the original store past the record; the test shows that it does. The native module faults (BAD_INDEX) instead.

### 0020E460, the letter behaviour

- **State 0:** if the free bone-slot count D_00275BCC (signed) is below
  +0xC, +4 = 3 (freed on the next walk). Otherwise +0xC slots from
  001AF7C0 go to +0x110.., +9 = +0xC, then 001CB5B0(+0xC), 001C62C0
  (bone_init_default_1) and 001CA5F0(a, 0xB) (draw method 001CB580).
  +0x80..+0x88 = 1.5, rotation (0, pi, 0), +4 = 1, and the position is
  view column 3 + 40 x column 2 + (-18.4) x column 0 + 1.3 x column 1
  (each a mul.s then an add.s).
- **State 1:** 001C6380 (placement); for the 'm' glyph (0020E3A0(0x15))
  001026A0(bone 0 + 0xC0, bone 0 + 0x90, D_0024A340); then 001D2040(0, 1),
  the +0x4C draw, 001D2040(0, 0).
- **Any other state:** 001AFF90.

### 0020EC80, the menu player publish, and 001F4BF0

- **Object matrix** at 0x70003400: identity, rotate x/y/z by +0xC0/C4/C8,
  then times the -1 diagonal (0x70003440), rotate x by pi, row 3 = the
  position +0xB0 (001031E0), and a copy to 0x700036A0; then 001C69A0 (the
  bone pose).
- **D_008104E4 == 1:** the point (3.2, -1.5, -0.6, 1) goes through bone
  slot 2's world matrix (001026A0) and 001F4BF0 draws the glow at it with
  colour words (0x20, 0x70, 0x80, 0x80).
- Then 001D2040(0, 1), the +0x4C draw (001CB580), 001D2040(0, 0).
- **001F4BF0(position, colour):** one rand() (00122BB8, the shared LCG),
  k = (c3 + ((c3 x ((r >> 23) & 0xFF)) >> 8)) >> 1, channel n =
  (cn x k) >> 7 packed c0 | c1 << 8 | c2 << 16, then 001CD520(0, 2,
  position, TEX0 0x20045B0599421EF0, rgb, 3.0, 3.0, 1.5).

### The module loader: 001FF080 → slot 2 → 001FF0D0

- **001FF080(state, module)** calls 001AB740(2, 001FF0D0). That sets record 0x28A790 to state 1 and clears +8..+0x17. It then stores +8 = state and +0xE = module.
- **Each frame**, the dispatcher 001AB6A0 (em_task) promotes 1 to 2 and calls 001FF0D0 in the same pass. Slot 2 runs after slot 0 (the world/status frame).
- **001FF0D0:**
  - Nothing happens while D_00282157 != 0.
  - +8 = 0: 001FF830(+0xE).
  - +8 = 1: 001FFCD0 (worker). Then, if +8 == 0x63 and 001FEF70(CA4, CA6) != -1, +0xE = that bank and +8..+0xC = 0.
  - +8 = 2: 00200360 (worker).
  - +8 = 0x63: D_00275BD8 = 0 and 001AB7D0 (slot state 0).
- **001FF830, on step +9:**

  | Step | Action |
  |---|---|
  | 0 | Pick the buffer by module (below), step 1, then header read `00200780(0x28A480, 0x289BC0, module << 11, 0x800)`. |
  | 1 | Poll 00200730: 1 → step 2; any other nonzero value → step 0. |
  | 2 | 001FF3F0; when it reports 1 → step 3. |
  | 3 | Step 4, then payload read `00200780(0x28A488, C74, h[4] + h[0x14], h[8] - h[0x14])`. |
  | 4 | Poll: 1 → step 5; any other nonzero value → step 3. |
  | 5 | Step = 7, then a cursor commit by kind: 0 gives 73C, 2 gives 744 = 748, 3 gives step 6. |
  | 6 | 001FB370(C74); a nonzero result gives 748 = result and step 7. |
  | 7 | 00200830 per section (+0x10 of them, each advancing by the size word at +0x24 of record +0xE + i), then the relocations D_0028A490[e >> 24] = (e & 0xFFFFFF) + C74. Finally +8 = 0x63 and +9 = 0. |

  Buffer and kind by module, in step 0:

  | Module | Buffer | Kind |
  |---|---|---|
  | 2, 3 | D_0028A738 | 0 |
  | 1, 0x27–0x29, 0x37 | 0x01800000 | 1 |
  | 0x1D | D_0028A5A0 | 2 |
  | 0x32–0x35 | D_0028A744 | 3 |
  | 0x36 | D_0028A748 | 2 |
  | 0x2A, 0x2B | 0x01800000 if spad 0x70003B90 == 0, else D_0028A748 | 1 |
  | any other | D_0028A748 | 1 |

- **001FF3F0**, on +0xB:
  - **0:** C70 = 0x289BC0 and +0x14 = h[0xE]. A zero count **returns 1**. Otherwise +0x16 = 0, step 1, and it falls into case 1.
  - **1:** Reads chunk entry i = +0x16 from `C70 + 8i + 0x20/0x24` with `00200780(0x28A488, C74, entry + h[4], size)`.
  - **2:** Polls. A result of 1 moves to the next state and increments +0x16; any other nonzero result steps back one.
  - **3:** Calls 00200830(C74). It then decrements +0x14: at 0 it goes to state 0 and returns 1; otherwise it goes to state 1.

## 3. The ITEM-root load wait (beat 01 pop-up, beat 03 BATTERY prompt)

- **The wait in the ITEM root.** State 3 of 0020EE50 calls 001FF080(0, t[0x16]); module 0x21 is the BATTERY page. It then waits until D_00275BD8 == 0.
- **Rows in the route traces** (UI+4/+5/+6 in `ui`):

  | Route | State 3 step 0 | Step-1 rows | First state 5 |
  |---|---|---|---|
  | 01 (battery pickup) | row 193 | 194..217 | row 218 |
  | 03 (panel) | row 390 | 391..414 | row 415 |

- **Measured wait.** Both routes wait **24 loader dispatches**: the 001FF080 frame plus 23 more. BD8 clears in the dispatch of the last step-1 row, and the root sees it in the next frame.
- **The captured loader leaves this record.** Route 03's RAM still holds:
  - the module-0x21 header at D_00289BC0: count 1, payload 0x50800, no sections, no relocations;
  - slot 2 = state 0, fn 0x1FF0D0, +8 = 0x63, +0xE = 0x21, kind 1, +0x16 = 1;
  - C74 = D_0028A748 = 0x19A3F40.

  The native loader run over that header with immediate I/O ends in exactly those bytes.
- **The native minimum is 10 dispatches.** Steps 0, 1, 2 (three dispatches for the one chunk), 3, 4, 5 and 7 give 9 calls, plus one call at 0x63.
- **The other 14 dispatches are I/O time.** They are 00200730 busy polls, or D_00282157 read gates, spread over the header read, the one 0x50800-byte chunk read and the empty payload read. The captures do not record how the 14 are split.
- **Next step (needs the emulator lane).** In route 03 f391..f414, log slot 2's +9/+0xB, 00200730's v0 and D_00282157 once per frame.
- **The port today.** The port's reads are synchronous, and `em_status_runtime.c` loads modules 0x1F/0x21 instantly. That is why the level smoke leaves the prompt window out of the comparison.
- **The lead's decision.** Reproducing the 24 dispatches requires the loader translated here, bound with an I/O model whose busy counts come from that probe. The I/O model is a runtime-timing property, not game code.

## 4. Corrections found

- **001FF3F0 (NEARMISS C).**
  - The chunk entries are read from **D_00275C70** + 8·index + 0x20/0x24, not from D_0028A488 + 8·index.
  - A zero chunk count returns **1**, not 0: v0 still holds the case-1 compare constant.
  - The oracle confirms both points. The mutation "return 0" is caught.

## 5. Workers (each is required when reached; NULL or a negative result faults)

Live bindings are in `em_status_models.c` (section 7).

| Worker | Original | Live binding |
|---|---|---|
| `w_001AFF10` | static-actor alloc | `em_status_scene_alloc_001AFF10` over the module's pool |
| `w_001AF800_slot` | one bone slot's clear and push | the module's own slot records (the port has no D_00275BD0/D_00275BCC stack; every slot request is available, as in em_area11_bindings.h) |
| `w_001CB590` | current actor D_00275B44/48 | records the walked record |
| `w_call` | the +0x10 behaviour or +0x4C draw | 0x20E6F0, 0x20E460, and the draw 0x1CB580; any other pointer faults |
| `w_001C6120` | model lookup (bank, code) | the exported glyph models of D_0028A56C; any other bank or glyph faults |
| `w_001CA6E0` | model bind | +0x44 = the model (001CA5E0), then 001CA5F0(a, 0) |
| `w_001C6150` | byte at model + 8 | the exported skeleton's node count |
| `w_001AF7C0`, `w_001CB5B0` | bone slot pop, bone-array setup | the module's slot records |
| `w_001C63E0`, `w_001C67E0`, `w_001C64F0` | bone_init_default_2, anim_clip_init, anim_advance_time | `em_player_pose` over the player bank's clips 0x1C2 and 0x0A (docs/PLAYER_POSE.md) |
| `w_001C62C0`, `w_001C6380`, `w_001029C0`, `w_00102B08`, `w_00102BB0`, `w_00102A60` | bone_init_default_1, placement, identity and rotations | `em_owner_services_original` |
| `w_001026D0`, `w_001026A0` | VU0 matrix and row products | the VU0 forms of em_ee_float.h (as 001C9610); 001026A0 only for 0020EC80's glow point (D_0024A340, the 'm' glyph's vector, is not exported: faults) |
| `w_001C69A0` | the bone pose | per node: the evaluated channels' quaternion to matrix, rows scaled by the channel scale, then 001C9610 over the object matrix scaled by +0x60 |
| `w_0020EC80`, `w_001AFF90` | publish, free | the translations |
| `w_001D2040` | GS state packet channel 0 | only (0, 1) before and (0, 0) after a draw |
| `w_00122BB8` | rand | the **shared** LCG `em_random_next()` (low 31 bits). A value < 0 faults (BAD_RESULT). |
| `w_001CD520` | the glow sprite | not translated: faults (reached only when D_008104E4 == 1) |
| `w_001FFCD0`, `w_00200360` | loader states 1 and 2 | not bound (the loader is not wired) |
| `w_00200780`, `w_00200730` | start a read, poll (0 busy, 1 done, other error) | not bound |
| `w_00200830`, `w_001FB370` | per-section DMA; kind-3 finaliser (modules 0x32-0x35) | not bound |

## 6. Verification

`make test-status-scene-reference` (`tools/test_status_scene_reference.py`) executes the original instructions listed in section 1 from the pinned ELF (SHA-256 checked). Everything they call is stubbed: the calls are recorded and given scripted or token results. Floating-point results are computed with `ee_float_model`.

For each case the test compares:
- every worker call and argument (the 64-bit TEX0 included);
- every modelled byte;
- that the original writes no byte outside the modelled set.

Default run: about 3 s; `EM_TEST_FULL=1`: 50 s (2026-09-23; full counts: 175 pool cases, 376 letter cases, 247 0020E460 cases, 7,431 player states plus 900 lockstep ticks, 48 0020EC80 and 1,690 001F4BF0 cases, 159 whole loads). Quick counts: 111 glyph codes, 59 pool cases, 186 letter cases (all 280 0020E250 inputs in full), 87 0020E460 cases, 355 player states plus 160 lockstep ticks, 16 0020EC80 and 85 001F4BF0 cases, 158 loader dispatches and 75 whole loads.

Case groups:
- **Pool:** 001AFF10 over patterns of used records including a full pool, 001AFEB0 (001AF800 per record in use) and 001AFE60, 001AFF90 freeing itself or another record, and the 001B0000 walk over random pools.
- **0020E3A0:** 111 codes.
- **0020E1E0:** 96 cases. **0020E250:** 90 of 280 inputs in the quick run, all 280 in full.
- **0020E460:** state 0 for bone counts 0/1/2/21/120 against free slot counts around them (and the hub view), state 1 for the glyphs 'm', '/', '@', '0', 0x15, 'l' and 0xED, the free states 2/3/4/0x80/0xFF, random states and a lockstep run from state 0.
- **0020E6F0:**
  - every variant and costume;
  - health at 35 and its neighbours, and NaN;
  - bone counts 0/1/21/120, plus the 121 overflow;
  - the breathe and yaw edges;
  - the -127 clamp at equality and its neighbours;
  - the free states;
  - random states;
  - a lockstep run from state 0 with a mid-run clip swap-back: 160 ticks in the quick run, 900 in full.
- **0020EC80:** D_008104E4 = 0, 1, 2 and 0xFF over random actors and rand values (the whole scratchpad object matrix, the glow point and every call).
- **001F4BF0:** colour words from zero to full 32-bit values and rand values at every field edge; a negative rand() faults.
- **Loader:**
  - 001FF080 over a dirty record;
  - whole loads of every buffer-selection arm with chunk counts 0/1/3, sections and relocations, and immediate, busy and error I/O scripts (75 in the quick run, 159 in full);
  - the kind-3 001FB370 path;
  - the gate, 001FFCD0/00200360 with 001FEF70 chaining, 0x63 and idle states;
  - steps 3/5/7/8+ from arbitrary cursors;
  - a relocation index past D_0028A5A0: the original aliases D_0028A5A0, the native module faults.

Capture checks (exact bits, over the user's RAM images):
- **Letters.** From status-hub's CA4..CA7 = FF 05 00 07, the native 0020E250 produces '/', '@', '0', '1', '2', '8'. Pool records 1..6 match it exactly after their 0020E460 state 0: callback 0x20E460, +0xD, +0xC, position, colour and rotation, and +0x44 as the executed original 001C6120 returns it over the captured RAM.
- **Menu player.** The native run from state 0 over the captured globals reaches the captured breathe/yaw pair at the tenth call. All modelled bytes of pool record 0 match: position (7.4, -2.4, 40), tint -0, clip flag, bones 21, model words.
- **Loader.** See section 3: the native minimum is 10 dispatches, and the route 01/03 waits are 24.

**Negative controls** (measured when the module was written): the host add in place of the EE add, swapped 7.4/2.4, 001FF3F0's empty count returning 0, a wrong glyph, a missing kind-2 mirror and a missing 0x15 letter are caught. Two defects cannot change any result, so no test can catch them: div.s by 2 against mul.s by 0.5 (both exact scalings) and `c.lt` against `c.le` at the -127 clamp (at equality it stores the same value).

`make test-status-scene-original` (`tests/status_scene_original_test.c`, ASan/UBSan) pins the port-side contract:
- NULL, failing and out-of-range workers;
- the latched fault;
- the pool-full no-op;
- the 121-bone fault;
- the free without globals;
- a module-0x21 load in 10 dispatches;
- the D_00282157 gate;
- a header address outside D_00289BC0;
- a missing state-1 streamer.

`make test-status-models` (`tests/status_models_test.c`, ASan/UBSan) runs the live binding over the status-hub capture (section 7).

## 7. The live binding (`em_status_models`)

- **Where it runs.** The AREA11 interaction host loads it from `assets/status_models` (`python3 tools/export_status_models.py`; the host refuses to load without it) and binds:
  - the page event CONFIGURE (0020DFA0): 001AFE60, then 001029C0(D_00810610) and D_00810624 *= -1 (the models' UI view; `em_status_models_configure`);
  - CLEAR_DRAW (001AFEB0) and RESET_DRAW (001AFE60);
  - the hub's model workers (0020CDC0 phase 1): INSTALL_DRAW (`a = 001AFF10(); a+0x10 = 0x20E6F0`), BUILD_MODELS (0020E250 over the D2 bytes CA4..CA7) and ACTORS_TICK (001B0000);
  - the inputs D_00810858/5C (g.status.health/infection), D_008104E4 (g.pd_infected) and D_00810C60 (g.status).
- **The export.** `tools/export_status_models.py` reads the user's disc files and the status-hub capture: the menu player D_0028A57C (variant 0, costume 0; extract/chunk28/f00_id3b.bin) with its skeleton records, clips 0x1C2 and 0x0A of the player bank D_0028A580 (f01_id3c.bin), and the glyph models '/', '@', '0', '1', '2', '8' of the letter bank D_0028A56C (extract/chunk27/f01_id37.bin), which the capture's pool records 1..6 bind. It checks that the captured RAM holds exactly these bytes at those globals and that each record's +0x44 is the library entry. Texels come from the capture's GS memory, where the hub had them resident. Any other glyph, variant (D_0028A584/D_0028A590) or costume (D_0028A588/D_0028A58C) is a missing asset: the worker faults. The first level never needs one: every route capture 00..14 (../Extermination/build/s87/route) holds CA4..CA7 = FF 05 00 07, D_008104E4 = 0 and D_00810C60 = 0, the inputs of exactly these models (so 0020EC80 never reaches 001F4BF0 there either).
- **Draw.** A walk queues one draw per 001CB580 call: the record's model with its node world matrices (+0x90 of each bone slot) as the palette and 001CB4F0's lighting mode 1 (001D8C20(1) -> 001D8C30 case 1: no light directions or colours, ambient lane k = 128 + actor +0x80 + 4k through the 8388608 bias, w from +0x8C). `em_status_models_render` draws the queue with fog off on 0020DFA0's UI camera: D_00810610 as the view, the UI zoom (224 / tan(25 deg), set with the CONFIGURE event) through `em_mat4_perspective_gs`.
- **Draw order.** 0020CDC0 phase 1 step 1 calls 0020A7A0 (the background sprites), then 001B0000 (the model packets), then 00209DF0 (the 2D layer). `em_status_runtime_render` queues the background, flushes it at once (`em_gfx_overlay_backdrop_flush`, the ordered 2D layer), draws the models (the runtime's `hub_models_draw` hook) and then prepares 00209DF0's layer, which end_frame draws after every 3D draw. The walk itself runs at the tick (ACTORS_TICK) and the background steps at the render; a rand() from 001F4BF0 would therefore come before this frame's background rand() instead of after it, but 001F4BF0 always ends in the untranslated 001CD520, which faults, so no run continues past that order.
- **Evidence.**
  - `make test-status-models` runs the workers from a cleared pool as 0020CDC0 does over the status-hub capture's globals: at the tenth walk the menu player's breathe/yaw equal the capture, pool records 0..7 equal it in every modelled byte, and all **27 node world matrices** (21 menu-player nodes and one per letter) are **bit-exact**. It also checks one draw per record with lighting ambient 128 + tint, the pool release and clear, a second visit, and the faults (a walk before 0020DFA0, an unexported glyph, variant 1).
  - The level smoke's status phase asserts the capture's seven records (0x20E6F0, then 0x20E460 with the six glyphs) and one draw per record per hub frame after the first (the first walk only initialises them).
  - The host fixture's hub route asserts that the backdrop is flushed before the model draws in every hub frame.
  - Compared with hub.png (the smoke's `EM_LEVEL_SMOKE_HUB_CAPTURE`, 2026-09-23), the menu player's bright-pixel box is (371..439, 46..191) against the capture's (372..438, 46..192) in the 640x480 image; the SPR4 model and the ammunition icon sit where the capture shows them.
- **Load wait (not bound).**
  - At `EM_ITEM_LOAD_MODULE` (em_item_root, 0020EE50 state 3), and at 0020CDC0's own `001FF080(0, sel)`:
    1. `em_task_register(2, <adapter>)`;
    2. `em_status_scene_loader_request_001FF080(&rec->state, rec->user, 0, module)`.
  - The adapter calls `em_status_scene_loader_001FF0D0` with an `EmStatusSceneLoader` whose d275BD8 is the canonical BD8 (em_item_root's `asset_busy`; copy it in and out). d282157 is `r_00282157`. d810CA4/CA6 are the D2 bytes.
  - `w_00200780` must supply the module's 0x800-byte bank header (disc-derived, exported locally; its location is the captured D_0028A480 descriptor, LSN 0x9D7D0 + module).
  - `w_00200730`'s busy counts are the open timing question of section 3.
  - Until this is bound, em_status_runtime's instant 0x1F/0x21 load stays as it is (FIRST_LEVEL_AUDIT H7, PARTIAL).
