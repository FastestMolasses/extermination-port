# Scene coordinator design (WP-3)

Status: design, 2026-09-22. It does not change any code. It implements FIRST_LEVEL_AUDIT.md WP-3 and supplies the
backbone that WP-4…WP-12 plug into.

Evidence used, all read for this document:
- **Decomp C** (Extermination/src): anim_frame_top_b.c (0x1AE040, NEARMISS), 001AE7E0 (byte-matched), 001AE5E0 and
  001AE6B0 (NEARMISS; the jal counts 13 and 14 were checked against the splat .s), 001AFD70 (NEARMISS; walk polarity
  and the pre-call load of D_00275B44 were checked in the .s), 001AD010 (NEARMISS; its only callees are 001FABB0,
  001FBC50 and 001FABB0, per the .s), 001AD140, 001ADF50, 001AD250, 001ACEC0, 001AFCF0, 001AFCA0, 001AD360,
  001AD4E0, 001ADF00, 001AD230, 001B6990, 001AFA90, 001AFA50, 001C5C50, 001C5930, 001C1D00, 001F0360, 001D1EA0,
  001AAD00, 0018A6B0, 0015CF90, 0015C160.
- **Measured order:** docs/ORIGINAL_FRAME_ORDER.md, with the raw JSON in Extermination/build/s87/frame_trace/.
- **Placement class bytes:** placement table 0x82A3C0 in build/startup-reference/playable_ee.bin. Only the class
  and callback of records 11–14 were read.

Addresses and constants only. There is no disassembly here, and no bulk original data.

---

## 1. Verdict on the two designs

**Chosen architecture:** Design B's *incremental migration* built on Design A's *address-named core, canonical
storage and fail-stop worker table*.
- From B: the phase order, the shadow-mode classifier, the isolated state-0 step, the per-node legacy adapters,
  the allow-list-driven trace comparison and the live smoke.
- From A: the one-function-per-original core, `EmSceneState` as the only storage for every original byte,
  NULL-worker faults, the trace hook inside the core, the pure-move split of em_game.c before any rewiring, the
  exact pool (free list, tail pointer, class-0xC reserve) and the census test.

### Errors found in Design A (corrected here)

| Claim | Evidence | Correction |
|---|---|---|
| A3: 001AE7E0 returns 0 for B8, B9, fade, 3B8D or B3 "in that order" before checking CE, C5 or B0 | byte-matched C | The order is B8→0, B9→0, **CE→3, C5\|B0→2**, fade≠0→0, 3B8D≠0→0, (E74&0x100)\|(E50≠4)→1, B3→0, (E74&0x800)\|(E74&0x10)→2, else 0. Status requests and CE therefore win during fades and cutscenes. |
| Game over is 3B93 → 001AD010 → +9=3 → 001AD740, with game_over_tick as the interim 001AD740 | 0015CF90 sets B9=1 when player +0x220 ≤ 0; 001AD4E0 body; 001ADF00 body | Death sets **B9**. At fade substate 2, 001AD140 sets +8=3, +9=2 → **001AD4E0**. It loads screen 0x27 through 001FF080(0,0x27), waits 0xF0 ticks or Cross (E74&0x40), fades, and at substate 2 sets +9=4 → **001ADF00**, which clears 3B93 and replaces the task with 001AC070. The 3B93 → +9=3 → 001AD740 route is separate: no main-ELF code sets 3B93 (001ADF00 and 001AFCF0 only clear it). |
| A5: the walker's link copy is "a self-copy unless the behaviour republished another actor" | .s: D_00275B44 is loaded into a saved register **before** the jalr | The copy is always a self-copy and has no effect. The native walk omits it. |
| B6: the roster skips class 0x0B, which implies record 13 is skipped | record 13 class byte = 9 (playable_ee.bin) | 001B6990 **allocates** record 13. It is absent later because it was freed: its slot 0x7A96E0 is reused by the opening-script actor 001BB0E0. |
| The roster allocates the whole captured list, tail included | spawn sites (§4.3) | Only 001B6990's output (deferred group, then placements) and 001C5C50's node are spawned at state 0. Every other tail node is spawned by the owner that spawns it in the original. |
| S4 lands state 0, classifier, variants, game over and status in one step | — | Split into S9–S12 so that each behaviour change is isolated from the one-tick frame-index shift. |
| Main loop: "F message/audio, G transition, then N/O" | trace §1 | H (001FB100) and I (001B5B70) run every frame; M/N/O run only when D_00821058==1. This is outside WP-3 (§8). |

### Errors found in Design B (corrected here)

| Claim | Correction |
|---|---|
| Freeing the captured `next` during a walk: mark it dead and skip it | That is a substitution. The original would continue into a freed node. The native walk **faults** with the owner's address (fail-stop), and a test asserts this. No AREA11 trace shows the case. |
| `EmSceneSpad` aliases 3B8D to g.frame_selector "until migrated" | Two storages for one byte is the highest-risk bug class. 3B8D moves into `EmSceneState` in the same step that deletes g.frame_selector (S11). |
| Workers as `emit(ctx, enum, arg)` | Replaced by a typed table named by address, with a trace hook (A). Oracle comparability is kept, because every call goes through the hook. |
| Roster exporter includes "the 001C5C50 transients" | 001C5C50 is a state-0/state-4 worker, not roster data. |
| Binding 0018A6B0 ×7 to an em_weapon_update aggregate | Their identity is unverified. 0018A6B0 reads the player's +1 and +0x1F0 and is spawned by 0018A880 from the player-init path (0015C310/0015C420; 0015BA50, 00188AC0). Leave them UNBOUND. Legacy weapon code stays at its current position inside the player stage until they are identified. |

### Trace versus labels

The trace wins on **order, counts and addresses**. Its role labels are not evidence. Corrections:
- #30 001C5930 is the **area-title** actor, not an equipment controller. It draws string D_002671C0[D_00289B40[area][0]+sub] for 0x12C ticks, is suppressed while 3B8D∈{1,2,3}, and goes to state 3 when B8≠0.
- #19 00823E80 is the **opening controller** (it pumps 001BA1F0 → 001B7B30 in state 02), not a "key item".
- #9 001BC350 is the room-move door.

One observation from the trace: **3B8D=2 while in status state 3** (st14). Many original functions write 3B8D, among them 002149F0, 00215870 and 002160B0 in the status stack (§3.2). The coordinator must not assume that state 3 implies selector 0.

---

## 2. Original frame machine (the behaviour to implement)

### 2.1 Placement in the main loop 0x1AAE40 (measured)
The steps run in this order:
- A: loop top.
- B: 001D1AE0.
- C: 001B57E0, input.
- D: 001AEBE0, the letterbox gate. It reads 3B90 and C4 from the previous tick.
- **E: 001AB6A0 → slot 0 → 001ACEC0 → 001AD250 → 001AD4D0 → j 0x1AE040. The whole world frame runs here.**
- F: 001FCA10.
- G: 001AEE70.
- H: 001FB100.
- I: 001B5B70.
- J: 00100A60.
- K: 001D7410.
- L: 001AB590.
- M/N/O: gated on D_00821058==1.
- vsync, then R through W.

The coordinator is **only step E**. em_frame.c keeps B/C/D/F/G. At the end of every task tick the coordinator forwards
`em_frame_screen_fade_gate(spad3B90, C4)` (SI-17).

### 2.2 Task chain

**001ACEC0 (+8).** Every tick writes spad 3B90=2. Then it dispatches on +8:

| +8 | Action |
|---|---|
| 0 | 001AD1A0; when it is done, +8 = D_00275BE0 ? 2 : 1 and +9=+A=0. Always calls 001D1EF0. |
| 1 | 001AD230 (= 001AF2C0; returns 4) → +8=3, +9=+A=+B=0 |
| 2 | +8=3, +9=5, +A=+B=0 |
| 3 | 001AD250 |

**001AD250 (+9).**

| +9 | Action |
|---|---|
| 0 | 001AD360 bring-up (+A steps 0–5). Step 1 waits for D_00282157==0 and then sets D_00821058=1 (the intro movie). Step 4 does 001D2830(3,1), 700=0x0B, 701=702=0 and D_00810730[0x0B]=0. When it returns nonzero: +9=5, +A=+B=0, 001AEDB0(0). |
| 1 | 0x1AE040 |
| 2 | 001AD4E0, game-over screen (§2.6) |
| 3 | 001AD740 |
| 4 | 001ADF00 |
| 5 | 001ADF50; when it returns nonzero: +9=1, +A=+B=0 |

**001ADF50 (+A).**

| +A | Action |
|---|---|
| 0 | 001AED80(0), +A++, BD8=1, 001FF080(1,0), 0021B180 |
| 1 | 0021B550; when BD8==0: 0021B840, +A++ |
| 2 | when 0021B550 returns nonzero: 001D2830(3,1), 001AEDB0(0), return 4 |

This takes at least 3 ticks.

### 2.3 0x1AE040 (+B; +C is the sub-step)

**State 0.** Sets +B=1, then calls, in order:
1. 001AFCA0 (001AF5C0 player-struct wipe, 001AF690, 001AF710, **001AF8E0 pool reset**, 001D0660 → 001E7780 overlay install, spad 31F4=0)
2. 001AFCF0
3. 001B07C0(0)
4. 001B6990 (001B6910 deferred group, then placements)
5. 001D19E0
6. 001C1DC0
7. 00199C50
8. 001AEE40(4)
9. 001FAE70(1)
10. 001C5C50
11. 001D1EF0

It then **returns: no world frame this tick.**

**State 4.** Calls, in order:
1. 001AFCF0
2. 0018AB00
3. 001B07C0(1)
4. 001C1DC0
5. D_008101E4=0, +B=1
6. 0018D7B0(0x8101E0,1)
7. 0018C0D0(0x8101E0,1)
8. 001AEE10(4,0)
9. 001FAE70(0)
10. 001C5C50

It then **falls through into state 1 in the same tick.**

**State 1.** r = 001AE7E0 (order in §1):
- **r==1:** C4=2, 001FBC50, 001FB9F0(0xC,0x1000,0x1000,0x1000), +B++ (to 2), +11=+C=+D=0.
- **r==2:** 0020E060, C4=1, +B=3, +11=+C=0, 001FBC50, 001FABB0, 00119828(0,0x3FFF,0x3FFF), 00119828(1,0x3FFF,0x3FFF). No world frame.
- **r==3:** +B=6, +C=0, 001FBC50, 001FABB0, 001AEDB0(0).
- **Otherwise, only if BD8==0:**
  - Run the variant: 001AE5E0 if 3B8D==0, else 001AE6B0.
  - Then, if B9≠0: 001AD140 when D_0028A9A0==2.
  - Else if B8≠0: 001AD010 when D_0028A9A0==2.

**State 2.** r = 0022A650:
- r==1: 001AF1C0, C4=0, +B--, 001FB9F0(0xD,…), 001FAE70(0).
- r==2: 001AEBA0(0xFF), 001AF150, 001D2610(0.0), BE0=1, +8=3, +9=5, +A..+D=0.
- r==3: 001AD140.

**State 3.**
- +C 0: when D_00282157==0, +C=1.
- +C 1: 001D1C50, 001D2830(3,1), 0020CDC0. On nonzero: 001E0CC0(0), +B=5, EF=0x46, 001AEDB0(0). Always ends with 001D1EA0(0).

**State 5.** 001AEDB0(0), 001D1EF0, 0018C0D0(0x8101E0,1), C4=0, +B=1, +C=0, 001FAE70(1), 001AEE40(0x20).

**State 6.**
- +C 0: when D_00282157==0: CE==2 ? 001FF030(CF) : 001FEFE0(CF), then +C++.
- +C 1: when BD8==0: CE=0, +B=1, +C=0, 001C1DC0, 001FAE70(1).

### 2.4 World-frame variants (NEARMISS C, the .s and the measured trace agree)

**001AE5E0, gameplay (3B8D==0):**
1. D_00810750++, spad 3B68++
2. 001CB590(0x8102B0,0x320,…)
3. **0015BCF0**(player)
4. 001CB5A0
5. **001D1C50**
6. 001C1D00(0x8101D0)
7. **001AFD70(0)**
8. **0015C160**
9. **001F0360**
10. 001CB590(0x8101E0,0xD0)
11. 0018B9C0
12. 001CB5A0
13. **001AAD00**
14. **001D1EA0(1)**

**001AE6B0, cutscene (3B8D≠0):**
1. If 3B92: spad 3B84++.
2. If 3B91==1 && D_0028A9A0==0 && (E74&0x900): 3B91=2.
3. Counters.
4. **001D1C50**
5. 001C1D00
6. **001AFD70(1)**
7. **001F0360**
8. 001CB590(player)
9. **0015BCF0**
10. 001CB5A0
11. **001AFD70(2)**
12. **0015C160**
13. 001CB590(cam)
14. 0018B9C0
15. 001CB5A0
16. 001AAD00
17. 001D1EA0(1)

**What the callees do (measured):**
- **0015C160:** 001CB590(player); 001DA6A0 when player+0x214==0 and D_00810771≠1, else 0015BF90; then the jalr to player+0x4C, which is 001CAA00 in all traces.
- **001D1C50:** 001D2830(4,0), 001B0070, [0015D2F0 and 0021B9A0(0,0,0) only when the selector is 0], 001D2830(6,1), 001D2960, copy_qw4 ×2, 001D7C30, 001D30A0. With C4≠0 it takes the 001D2830(6,0) path instead.
- None of 001D2830, 001D7C30 or 001D30A0 calls an owner: their static subtree of 43 functions has 0 jalr, and none fired at run time. Owners do their own model work through their +0x4C method: 001CAA00 by default, 001CACB0 for indicators (via 001F54E0).
- **001F0360:** 001F6210, 001F5C20, 001F6BB0, 001F6EB0, 001F40C0, then 001F0720(0,1,3,4,5,6).
- **001AAD00:**
  - First the nine hooks: 001A9D20, 001A8DA0, 001A9F60(player), 001AA140, 001A7870, 001A8BE0(player), 001A9000, 001A97B0, 001A9B10. Its indirect sites never fired.
  - Then the double-buffer swap of the per-class lists (D_00275B5C/60/64/68 is the interaction list).
- **001D1EA0(a):** if a≠0 and 001D2910(4)==0: 001E0D70, 001DDA00. Then always 001CB800(D_007635C0,…).

### 2.5 Pool (0x100 × 0x2F0 at 0x7A5640)

| Function | Behaviour |
|---|---|
| 001AF8E0 reset | builds the free list through +0x1C; head D_00275BC4, count D_00275BC8 |
| 001AFA90(cls) alloc | refuses class 0xC while fewer than 10 are free; pops the head; +0=2, +2=cls, +0x14=self, defaults |
| 001AFA50 link | tail append; head D_00275BC0, tail D_00275BBC, prev +0x18, next +0x1C |
| 001AFC10 free | takes the canonical pointer from +0x14; 001AF800 bone slots; 001AFBC0 unlink; pushes onto the free-list head (LIFO reuse is visible in the trace) |

**001AFD70(mode):**
1. spad 3B8A=0.
2. For each node:
   - read next=+0x1C first;
   - 3B8A++ (skipped nodes count too);
   - mode 1 skips class (+2&0x1F)==1; mode 2 ticks only class 1; any other mode ticks all;
   - 001CB590(node,0x2F0,+9) sets D_00275B44 and D_00275B48;
   - +1=0;
   - call *(+0x10)(node).

   A node appended during the walk is ticked in the same walk, unless the current tail appended it.
3. Cutscene: mode 1 plus mode 2 visit every node exactly once (state 15: 42+7=49).

### 2.6 Request consumers

**001AD010**, reached only from state 1 at fade substate 2:
- **B8==2 (room move):** 702=B7, +B=4. B8 is **not** cleared here; state 4's 001AFCF0 clears it.
- **Else, 3B93≠0:** if 3B93==2, 001FABB0. Then +9=3, +A=+B=0.
- **Else (area change):**
  - 700=B5, 702=B7
  - 701 = (B6==0xFF ? D_00810730[B5]&0x7F : B6)
  - +9=5, +A=+B=0
  - 001FBC50, 001FABB0

**001AD140:** +8=3, +9=2, +A=+B=0, 001FC9B0, 001FBC50, 001FABB0.

**001AD4E0 (+A):**

| +A | Action |
|---|---|
| 0 | 001D2880, 001D1EF0, +0x18=0xF0, 001AEDB0(0) |
| 1 | 001D1EF0, BD8=1, 001FF080(0,0x27) |
| 2 | when BD8==0: 001AEE10(4,0), 001FA790(0,0x1B), 001D2830(3,1) |
| 3 | 001ABF90 packet; +0x18 countdown; when fade==0 and (+0x18==0 or E74&0x40): 001AEDE0(4,0) |
| 4 | at fade==2: 001FAB50, +9=4, +A=0 |

**001ADF00:** D_00810D38=0, 3B93=0, 001D2880, 001D1EF0, 001AEBA0(0xFF), D_00275BDC=1, task replace → 001AC070.

**001AFCF0**, called only from states 0 and 4:
- clears spad 3B84 (u16), 3B93, 3B8C, 3B8D, 3B8E, 3B8F, 3B91, 3B92;
- clears the word at 0x70003258;
- memsets 0x48 bytes at D_008106B0;
- calls 001FC9B0.

---

## 3. Architecture

### 3.1 Files (end state)

| File | Role | Dependencies |
|---|---|---|
| `src/game/em_scene_state.h` | POD `EmSceneState` (§3.2), accessors, `EmSceneFault` | stdint only |
| `src/game/em_scene_classify.{h,c}` | `em_sf_001AE7E0(const EmSceneState*)` | state.h |
| `src/game/em_scene_frame.{h,c}` | `em_sf_001AE040`, `em_sf_001AE5E0`, `em_sf_001AE6B0`; states 3/5 delegate to the unchanged `em_status_frame_enter/tick` through a marshalled `EmStatusFrame` view (+B, +C, +11, C4, EF, 282157), which is written back | state.h, classify, em_status_frame |
| `src/game/em_scene_task.{h,c}` | `em_sf_001ACEC0`, `_001AD250`, `_001AD010`, `_001AD140`, `_001ADF50`, `_001AD360`, `_001AD4E0`, `_001ADF00`, `_001AFCF0` | state.h |
| `src/game/em_scene_workers.h` | `EmSceneWorkers`: one typed pointer per callee, named `w_<addr>` with the original arguments (e.g. `int (*w_001D1EA0)(void*,int)`, `int (*w_0020CDC0)(void*)` returning 1/0/-1, `int (*w_0022A650)(void*)`, `int (*r_0028A9A0)(void*)`), a `trace(ctx, addr, a0..a3)` hook, and `walk_001AFD70(ctx, mode)` | stdint |
| `src/game/em_actor_pool.{h,c}` | §4.1 | libc |
| `src/game/em_actor_roster.{h,c}` | loads `assets/scene_snow/roster.emro` (ignored) and spawns it the way 001B6910+001B6990 do | pool |
| `src/game/em_scene_bindings.{h,c}` | the **only** file that knows both the cores and the port. It owns the static `EmSceneState` and `EmActorPool`, fills the workers, and exports `em_scene_task_001ACEC0(void)` (the EmTaskFn for slot 0) and `em_scene_state(void)` | everything |
| `src/game/em_area11_bindings.{h,c}` | the per-callback binding table (§4.4): behaviour adapters and release hooks | port subsystems |
| `src/game/em_frame_trace.{h,c}` | `EM_FRAME_TRACE=<path>` JSONL, one line per task tick; instrumentation only | — |
| `src/game/em_player_frame.c`, `em_render_frame.c`, `em_game_selftest.c` | moved out of em_game.c (S5): stage functions named by address, and env-gated test scripts | em_game_internal.h |

The cores are pure C11: they do not include `g`, gfx or any subsystem. Each builds standalone into a dylib for the ctypes
oracles, following the em_status_frame pattern.

**Return convention:** ≥0 means ok; -1 means a worker fault. A fault latches `state->fault = {address, code}`, and from then on the task does nothing (fail-stop). A reached NULL worker is a fault.

### 3.2 Canonical storage (`EmSceneState`)

**Owned bytes.** Each exists once. Every other module reads and writes it through `em_scene_state()`; none keeps a copy.
- **Task bytes:** they stay in `EmTask.user`, where +8+k is user[k]. The cores receive the `user` pointer.
- **Request block:** `req[0x48]` is D_008106B0..F7, with named accessors: B0, B1, B3, B5, B6, B7, B8, B9, C4, C5, C6, C7, CE, CF, D5, EF, F3, F5.
- **Area bytes:** `d810700`, `d810701`, `d810702`, and `d810730[0x20]` (0x730..0x74F).
- **Counters:** `d810750` and `spad3B68`.
- **Scratchpad:** `spad3B84` (u16) and `spad3B8A` (u16); the bytes 3B8C, **3B8D (selector)**, 3B8E, 3B8F, 3B90, 3B91, 3B92 and 3B93; the words `spad3258` and `spad31F4`.
- **Flags:** `d275BD8` (load busy), `d275BDC`, `d275BE0` and `d8101E4`.
- **Input:** `d810E74` (pressed edge), `d810E70` (held), both in the **original** byte layout; START=0x800, TRI=0x10, SELECT=0x100, CROSS=0x40. Also `d810E50` (pad mode, 4).

**Read through workers, not owned:** D_0028A9A0, which is `em_frame_transition()->substate` (2 = hold black), and D_00282157 (audio busy).

**Writers match the original writer set, not "one writer":**
- **3B8D** is written by 0015C420, 00183250, 001833F0, 00183440, 001834E0, 00184BA0, 001AFCF0, 001B0C60, 001B6E40, 001B82D0, 002149F0, 00215870 and 002160B0. The port writes 3B8D only in the ported counterparts of those functions.
- **B9** is written by 0015CF90 (player +0x220 ≤ 0).
- **B0/B1** are written by 00157F60 (panel; B1=0x80+cost) and 001C47A0/4720/4760 (take).
- **B5..B8** are written by 001BC150 (door; B8=2) and 001B0C60 (exit).
- **B0/C5** are cleared by the status stack (0020CDC0 and its pages).
- The **request block** is cleared by 001AFCF0.

The G6 unit test greps the port and asserts that no module except the bindings declares shadow storage for these bytes.

**Progress and request bytes shared by player/set-piece translations (D2, HK).** D_00810707, D_00810792, D_00810793, D_00810813, D_00810CC3 and D_00810CB6 live in the `EmProgress` region, D_008106F1 in the request block. A translation that reads or writes one of them holds a pointer at that canonical byte (EmPlayerStageScene.d8106F1/d810CB6, EmPlayerStageGlobals.d810707, EmPlayerMajor2Scene.d8106F1/d810707, EmPlayerRecoveryScene.d8106F1, em_director_original's d810813/d810793/d810CC3, em_truck_original's `story`); its binder takes the pointer from `em_scene_progress_at` / `em_scene_req_at`. 001C4760 (D_00810CC3[a0] += a1, then B0 = 3 / B1 = a0 when a0 >= 0x20) has one translation, `em_director_original_001C4760`, and one live binding over the canonical storage, `em_director_original_001C4760_scene` (em_director_original.c is in COMMON for it). G6 lists, per migrated address, the files whose code names it (REACHERS).

### 3.3 Frame trace (native)

`EM_FRAME_TRACE` writes one JSONL line per tick:
- `{counter, task:[+8,+9,+A,+B,+C], classifier, selector, fade, events:[…]}`
- Each event is `{fn:"<caller addr>", target:"<callee addr>", args}` or, for the walk, `{fn:"001AFD70", op:"jalr", callback, class, record, binding}`.

`tools/compare_frame_order.py` normalizes this format and the original's `build/s87/frame_trace/*.json`
(`frames[].events[]{pc,fn,op,target,callback,current_actor}`) to one vocabulary:
- aligns frames by machine state, never by counter (the first frame after a state load is non-deterministic);
- reports the first divergence per frame;
- reads allowed differences from `tools/frame_order_allow.json`. Each entry carries a reason and the WP that removes it, and the list must only shrink.

---

## 4. Owner and pool model

### 4.1 `em_actor_pool` (native, not a binary overlay)

**EmActor** fields:
- status (+0), drawn (+1), cls (+2), model (+3), bones (+9), param (+0xD);
- uid (+0xE, u16), flags2 (+0x2E), kind/link (+0x54/+0x56), table_index (+0x9A);
- pos (+0xB0), rot (+0xC0);
- `callback` (the original +0x10 address, used as trace and census key);
- `behavior(EmActor*, void *world)` returning 1 ok or -1 fault;
- `release(EmActor*)`;
- `owner` (the native controller), `source_id` (EMIS), prev/next.

**Operations:** `em_actor_pool_reset_001AF8E0` (256 records; calls every live `release`), `_alloc_001AFA90`,
`_link_001AFA50`, `_unlink_001AFBC0`, `_free_001AFC10`, and `_walk_001AFD70(pool, mode, world, trace)`.

The walk semantics are exactly §2.5. There is one deliberate policy: if a behaviour frees the saved `next`, the walk faults with that node's callback.

### 4.2 Static roster (state 0, the 001B6990 position)

`tools/export_area11_roster.py` reads the user's ELF and overlay. It emits:
- the deferred group D_0024D820[11] (group 0: g0.0–g0.8), in record order;
- then the placement table 0x82A3C0, in record order, skipping only `cls&0xFF == 0x0B`.

Each record carries callback, class, model, param, uid, table index and position.

`em_actor_roster_spawn` reproduces 001B6910 and 001B6990, including "alloc failure skips the record and the index still
advances".

**Record 13** (class 9, 008257A0) **is spawned.** Its disappearance must come from its own behaviour, never from the roster. Until 008257A0 is translated, its adapter is an explicit UNBOUND node, and the census allow-list carries "r13 present; the original frees it before the opening".

### 4.3 Dynamic tail: spawned by owners, never by the roster

| Tail node(s) | Original spawn site | Native spawn |
|---|---|---|
| #29 001E55F0 (cls 12) | state 0, between 001B6990 and 001C5C50; the only reference is the function table near 0x24CCC8; the spawner is unresolved (001D19E0 subtree is the candidate) | S10 interim: the bindings allocate it at w_001D19E0; retired when the spawner is proven |
| #30 001C5930 (cls 8, area title) | 001C5C50 in states 0 **and** 4 | w_001C5C50 (exact: alloc(8), model 3, param 0, callback) |
| #31–37 0018A6B0 ×7 (cls 1) | 0018A880 ← 0015C310/0015C420 (player-init path) | interim node allocation at the player-init position; UNBOUND behaviour |
| #38, #46 001E2560 (cls 12) | table near 0x24CCC8; unresolved | interim allocation, UNBOUND |
| #39–45, #47 001C5680; #48 001C5760 (cls 12) | 001C5570, from the owning pickup (00219550, 00219870) or panel (00159210) behaviour | the pickup/panel adapter allocates its child through the pool |
| opening 001BB0E0 ×2 | 001BAC00 (script op 0x14) | em_opening_runtime allocates them through the pool (S10b); interim: allow-listed |
| walking 001EA240 | table near 0x24CCC8 (0021B9A0 rumble channel) | UNBOUND; allow-listed |

Interim allocations are flagged `interim_spawn=1` and named in the census and trace output.

### 4.4 AREA11 binding table (`em_area11_bindings.c`)

Node numbers are from ORIGINAL_FRAME_ORDER §4.

| Node | Original | WP-3 binding (interim) | Final (package) |
|---|---|---|---|
| #0–5, #6 | pickups 00219550 ×6, 0015AFA0 | since WP-6, each node its own owner: state 0 (00219550's 001C5570 child; 0015AFA0's 0015AC00 matrix and 001F1110) then `em_area11_interaction_host_pickup_tick` (`em_pickup_original_tick_one`, publication through 001B17A0); the take posts its original B0/B1; the owner's free frees the node (001AFC10) | — |
| #7, #8 | 00825940, 00827490 | group adapter on #7: legacy `em_enemy_update` (husk, crates and drums; interleave approximate) | WP-18: per node |
| #9 | door 001BC350 (r0) | legacy `em_door_update`; since S12b its commit is 001BC150 (`em_door_transit_commit`: fade, B8=2, B7) and its sub 5 is 001BC290 | WP-7: `em_door_original_runtime_tick` |
| #10–11 | fans 00827630 (r1/r2) | static (WP-1 stops the spin) | WP-11 |
| #12–15 | crates 001551B0 | covered by #7's group | WP-18 |
| #16 | flame 008235F0 (r7) | `em_area11_effect_runtime_tick` | — |
| #17 | Roger 008237E0 (r8) | UNBOUND (drawn statically as today) | WP-9: `em_roger_runtime_tick(rt, player, 1)` |
| #18 | equipment 001C5C90 (r9) | UNBOUND | WP-9 |
| #19 | opening controller 00823E80 (r10) | `em_opening_runtime_tick`; its camera is `em_opening_runtime_camera()` at the 0018B9C0 stage | WP-10 unifies it with em_script |
| #20 | manager 00823CE0 (r11) | dormant no-op, traced (it waits on D_00810788) | — |
| #21 | manager 008253F0 (r12) | legacy `director_tick` | WP-10 |
| — | manager 008257A0 (r13) | since S12a: `em_manager_008257A0` (states 0/2/3 translated from the overlay, oracle `test_manager_8257a0_reference`; frees itself on the second world frame, Q3); state 1 (event 0x30 set) faults | WP-10 (its script arm) |
| #22–23 | drums 00156620 | covered by #7's group | WP-18 |
| #24, #25 | truck 00823FF0, trigger 008251E0 | legacy `em_truck_update` on #24 (static after WP-1) | WP-12 |
| #26 | panel 00159210 (r18) | since WP-4: state 0 = its 001C5570 child and the host's D_008106D0 address; state 1 = `em_area11_interaction_host_panel_tick` (00159210/00157860 and the 001B17A0 publication) in both variants; `grate_update` keeps the static pose and cell 18 | — |
| #27 | terminal 00827B10 (r19) | since WP-4: state 0 = the floor placement (D_0081083A → +0xB4 190/230, 001C6380: `em_area11_interaction_host_elevator_state0`, WP-4 fix round) and its 001C5760 child (interim spawn); state 1 = `em_area11_interaction_host_elevator_tick` (refusal 0x82A990 / powered 0x82A750 with the carry 00828050, publication at 0x827E78) in both variants | — |
| #28 | prop 001C4820 (r20) | render-only | — |
| #29 | 001E55F0 | `em_weather` over the node's own state (`em_snow_runtime_tick_actor`, S12b); spawned by the 001C1EA0 translation over the canonical D_008106C8 since S12a; B8=2 with fade 2 → state 3 → frees itself | — |
| #30 | 001C5930 | lifecycle translated (S12b: state 0→1, B8≠0 → 3, 2/3 free); the card is the legacy em_hud title (manifest arm at load, 001C5C50 arm in state 4) | area-title card translation (0x12C ticks, 3B8D suppression, the 001C5860 line) |
| #31–37 | 0018A6B0 | UNBOUND | identify first (0018A880/0015C420) |
| #38, #46 | 001E2560 | UNBOUND | identify first |
| #39–45, #47, #48 | 001C5680, 001C5760 | group adapter on the first live member: `em_props_indicators_tick` and the pickup lights; since WP-6 a 001C5680 child whose +4 its item owner set to 3 frees itself (001AFC10), and a freed head hands the aggregate to the next member | the per-child colour and draw (001F54E0 is already per light) |

**Non-roster scenes** (office and drawbridge, used by every `EM_*_TEST` and by `tests/run_suite.sh`) keep one `legacy_world`
node. It holds today's exact legacy call order, so their outputs are unchanged.

### 4.5 Non-pool positions (stage workers)

| Worker | Binding |
|---|---|
| w_0015BCF0 | `em_player_0015BCF0`: the port's hit mailbox, then actor_update, which runs the original player stage (census L01: em_player.c `player_states_stage`, 0015BA50 / 0015B130 / 0015BCF0's tail with the stage workers bound by em_player_stage_live.c, the port's idle/walk as 0015B130's state[0]/[1]), the pose finish, the legacy bug-latch struggle, and the B9 write per 0015CF90. In cutscenes it is bound to the current opening-player path while the opening runtime owns the player (until WP-9/WP-15), and to `em_player_0015BCF0` otherwise (WP-4: the interaction host's shared player worker consumes the stage at 0015B130's prelude position through the takeover stand-in). |
| w_001D1C50 | point_light_tick, fog apply, GS setup. The port's name `render_chain_build` for this is a wrong label. |
| w_001C1D00 | render_env_init (area 0x1500 GIF arm, 001E0CF0, 001D5370) |
| w_0015C160 | player post-step: palette/+0x4C. Open question Q2: where player_pose_finish_palette belongs. |
| w_001F0360 | FX managers |
| w_0018B9C0 | camera_update and em_sfx_listener |
| w_001AAD00 | Since census L07/L08: `em_collision_world_close_out_001AAD00`, the nine list-pass hooks (em_coll_list_passes over the live lists) and then the list block (em_actor_class_lists_swap_001AAD00: every class list, the interactive list included, published and reset). The interactive list is the Use scan's one store. Unmirrored in scenes without an original roster. |
| w_001D1EA0(a) | draw flush: world when a=1, overlays |

Owners **offer** inside their behaviour (the 001B1B70 position). The player's Use scan (00160220 inside 0015BCF0) reads the previous frame's published list.

001FCA10 (the message service, WP-8) stays at main-loop step F in em_frame.c, not in the coordinator; since WP-8 it is `em_message_live`, installed at bring-up.

---

## 5. Flows

- **Status (state 3).**
  1. Classifier r==2 comes from B0/C5, or from START/TRI when fade==0, 3B8D==0 and B3==0.
  2. The r==2 arm runs with no world frame.
  3. State 3: +C0 waits for audio busy; +C1 runs 001D1C50 (C4≠0 path), 001D2830(3,1), w_0020CDC0 and 001D1EA0(0). **The world is frozen: no owner, player or camera ticks** (trace st14).
  4. State 5 returns to state 1.

  As built (WP-5): in AREA11 w_0020E060/w_0020CDC0 run the original page core (`em_status_page` in the interaction host's `em_status_runtime`) for every status screen, over the canonical B0/B1/C5/CC; its 0020E0C0 exit clears B0/C5. The START/TRIANGLE hub (0020CDC0 phase 1) is the original `em_status_hub` with `em_status_hub_ui` and 0020A7A0, inside the runtime (which owns the UI+0x20 clock); its status-model workers (0020E6F0, 0020E250, 001B0000) run the translated owners in `em_status_models`, which draws the menu player and the equipment models between the background and the 2D layer; scenes without the host keep the legacy em_hud at both positions (it clears B0/C5 on its close). The H22 audio comes out of the core: 001FBC50, 001FABB0 and 00119828 ×2 at the open, the translated 001FAE70(1) at state 5; the stream-channel volume 0x1999 that 001FBC50 and 001FC280 set is reported, not applied (no per-channel gain). The runtime's private `frame`/`queued` path is still used only by the sanitizer fixtures (not live).
- **Room move.**
  1. The door writes B7, sets B8=2 and starts the fade-out.
  2. While B8 is set the classifier returns 0 and the world keeps ticking.
  3. At substate 2, 001AD010 sets 702=B7 and +B=4.
  4. Next tick, state 4 (001AFCF0 clears B8, 001B07C0(1) re-places the player from D_0024D650[area] entry 702, camera, fade-in 001AEE10(4,0), 001C5C50) falls into state 1 and draws a world frame in the same tick.
  5. The pool is kept. The old 001C5930 left because B8≠0.

  As built (S12b): the legacy AREA11 door is bound to its original descriptor and commits through 001BC150 (step 1); the weather node takes state 3 at B8=2 with fade 2 and frees itself in the state-4 tick, after 001C1DC0 spawned its successor; the old title node leaves the tick after B8 is set and state 4's 001C5C50 spawns the new one (the legacy card is re-armed there). 001B07C0(1) is the S12a translation; its walk-out (record +0x14 = 1, entry 1 only) drives the legacy walk-out; 001B0460(1) takes the same arm as 001B0460(0) (D_008104E0 = player +0x230 was just stored 0) and keeps the S12a stand-in; 0018D7B0/0018C0D0 are reported no-effect bindings (the camera block is not canonical; the re-armed legacy camera seats at the tick's 0018B9C0).
- **Area change.**
  1. 001B0C60 (fan or Roger) writes B5..B8 with B8≠2.
  2. At substate 2, 001AD010 sets 700/701/702 and +9=5.
  3. 001ADF50 runs over at least 3 ticks. Native: w_001FF080 begins the scene load of (700,701); w_0021B550/0021B840 poll and clear BD8.
  4. Then +9=1, +B=0 → state 0 on the next tick. That tick is the full rebuild: pool reset calls every `release`, then the player wipe, overlay install, roster spawn and 001B07C0(0). It draws no world frame.
- **Game over.** Death sets B9. The world runs until fade==2, then 001AD140 → +9=2 001AD4E0 → +9=4 001ADF00 → the task is replaced with 001AC070. As built (S11b): 001AD4E0 runs as its byte-matched S3 core; the legacy GO_* pieces are its workers (the module-0x27 stand-in at 001FF080/001ABF90) and the legacy continue prompt is the interim 001AC070 task behind 001AB790; both are retired when the module 0x27 art and 001AC070/001AC480 are translated.
- **New Game.**
  1. The frontend registers `em_scene_task_001ACEC0`.
  2. +8 goes 0 → 1 (001AD230 = 001AF2C0 reset) → 3.
  3. +9=0: 001AD360 (area 0x0B.0.0; D_00821058=1 starts the intro movie).
  4. +9=5: load. +9=1: state 0, then state 1, with the selector set by the opening controller.

  Moving the port's movie start from em_frontend to 001AD360 step 1 is part of S12 (it overlaps WP-17 SI-09/SI-10). Done in S12a: see the S12a status line in section 6.
- **Unported arms.** These are classifier r==1 (state 2 / 0022A650), r==3 (state 6 / 001FF030 / 001FEFE0), and +9=3 (001AD740). If reached they **fault**. The input translator pins E50=4 so that r==1 cannot fire every frame. Whether SELECT (0x100) is withheld until 0022A650 is ported is a lead decision (Q1).

---

## 6. Implementation plan

**Rules for every step:**
- The lead alone edits the Makefile, applying the hunk each step lists.
- em_game.c has exactly one owner at a time.
- Each step uses a lane build (`build/<lane>/extermination`) with zero warnings, and runs every existing `make test-*` target and `tools/test_*_reference.py` it touches, plus `EM_STARTUP_TEST=newgame-control`.
- tests/run_suite.sh and README.md are never edited.
- Legacy code is deleted only in the step that makes its replacement live.

### Phase 1: new files and pure moves (S1–S7 can run in parallel once S1's header lands)

| Step | Owner files (disjoint) | Delivers | Verification | Retires |
|---|---|---|---|---|
| **S1 State and classifier** | em_scene_state.h, em_scene_classify.{h,c}, em_scene_workers.h, tools/test_scene_classify_reference.py | canonical struct and accessors; `em_sf_001AE7E0` | Executes byte-matched 0x1AE7E0 over B8{0,1,2}×B9×CE{0,1,2}×C5×B0×fade{0..3}×3B8D{0,1,2,4}×E74{0,0x10,0x40,0x100,0x800,0x810,0x900,0xFFFF}×E50{0,4,7}×B3 (55,296 cases); bytes and results must be identical | — |
| **S2 Frame core** | em_scene_frame.{h,c}, tools/test_scene_frame_reference.py | `em_sf_001AE040`, `_001AE5E0`, `_001AE6B0` | Executes 0x1AE040 with 0x1AE7E0 **executed**, and the jump table read from ELF rodata. Covers every state 0–6 × +C × BD8 × 282157 × 3B8D × B8/B9 × fade × 0022A650{0..3} × 0020CDC0{0,1} × CE{1,2}. Executes 0x1AE5E0 and 0x1AE6B0 over 3B92 × 3B91{0,1,2} × fade × E74. Compares ordered (callee, args), task +8..+0x11, the request block, the spad bytes and the counters. Asserts that state 0 calls no variant and that state 4 falls through. Supersedes and keeps test_status_frame_reference.py. | — |
| **S3 Task chain** | em_scene_task.{h,c}, tools/test_scene_task_reference.py | 001ACEC0, 001AD250, 001AD010, 001AD140, 001ADF50, 001AD360, 001AD4E0, 001ADF00, 001AFCF0 | Executes each over all +8/+9/+A arms, B5..B8, 3B93{0,1,2}, B6=0xFF with a D_00810730 table, 0021B550, BD8 and fade | — |
| **S4 Pool** | em_actor_pool.{h,c}, tools/test_actor_pool_reference.py, tests/actor_pool_test.c | §4.1 | Executes 001AF8E0, 001AFA90, 001AFA50, 001AFBC0, 001AFC10 and 001AFD70 over synthetic lists (length 0–40, class bytes with 0xE0 flags, modes 0–3). Behaviours append a tail child, free themselves, or free a non-next node. Compares visit order, 3B8A, +1 clears, links, free-list order and the 0xC reserve. The free-next case must fault. ASan/UBSan unit test. | — |
| **S5 em_game split** | em_game.c, em_game.h, em_game_internal.h; new em_player_frame.c, em_render_frame.c, em_game_selftest.c | pure moves (tools/split_module.py); stage functions `em_player_0015BCF0`, `em_player_0015C160`, `em_render_001D1C50`, `em_render_001C1D00`, `em_render_001AAD00`, `em_render_001D1EA0(int)`, `em_camera_0018B9C0`; every *_test_script behind `em_game_selftest_pre_frame()` | Before/after PNG hashes identical for EM_CAPTURE default, door, pickup, examine and aim; run_suite unchanged | — (moves only) |
| **S6 Trace tooling** | em_frame_trace.{h,c}, tools/compare_frame_order.py, tools/frame_order_allow.json | §3.3 | Comparator self-test: each original JSON against itself is a PASS; a synthetic swap of 001F0360 and 0015BCF0 in cut02 is detected at the right frame and index | — |
| **S7 Roster** (after S4) | tools/export_area11_roster.py, em_actor_roster.{h,c}, tools/test_actor_census_reference.py | §4.2 | Walks D_00275BC0 in the opening, handoff and playable captures (read-only; prints only addresses and counts). The native spawn must reproduce the static prefix #0–28 exactly (callback, class, table index, uid), with r13 present and flagged. Every tail callback must have a binding or a named UNBOUND entry. | — |

### Phase 2: wiring (serialized; each step owns the files listed)

| Step | Owner files | Change | Verification | Retires |
|---|---|---|---|---|
| **S8 Legacy-mode wiring** | em_scene_bindings.{h,c} (new), em_game.c | `game_task` → `em_scene_task_001ACEC0`. The chain and frame machine run through the cores. The state-1 world frame is ONE worker that calls today's gameplay_frame/cutscene_frame unchanged. The classifier is computed and traced but **not acted on** (bindings-only shadow flag). A bindings-only `legacy_state0_frame` flag keeps today's state-0 fall-through. | Every test unchanged; EM_CAPTURE PNGs byte-identical; newgame-control; test-task | `ingame_frame_machine`, `game_sub_machine` bodies |
| **S9 State 0 returns** | em_scene_bindings.c, em_game_selftest.c (capture frame defaults), em_opening_control_test.c | drop `legacy_state0_frame` | Re-baseline the frame indices by +1 **against the original** (trace: the state-0 tick has no 001AE5E0). Record every changed constant with that justification. | state-0 fall-through |
| **S10a Variant stages** | em_scene_bindings.c, em_player_frame.c, em_render_frame.c | Both variants run in original order through the stage workers. The pool position is one legacy block containing the rest of today's world updates in today's relative order. The cutscene player stage stays bound to the opening-player path. | compare_frame_order: idle04, walk04, st03, cut02, cut07, cut15; only allow-listed diffs; newgame-control; visual first control | gameplay_frame/cutscene_frame as monoliths (ORCH-14/15/16 for AREA11) |
| **S10b Pool live (AREA11)** | em_area11_bindings.{h,c} (new), em_scene_bindings.c (w_001B6990, w_001C5C50, w_001AFCA0 pool reset) | roster spawn at state 0; per-node adapters per §4.4; the legacy block is deleted once empty; non-roster scenes use `legacy_world` | census; compare_frame_order with per-node matching (callback + record); run_suite unchanged | the legacy block |
| **S11a Canonical selector and input** | em_frame.c (E74/E70/E50 in the original layout via one translation helper; E50=4), em_opening_runtime.c (reads and writes 3B8D/3B91 canonically; the 3B91 1→2 promotion moves to `em_sf_001AE6B0`), em_opening_control_test.c, tests/opening_runtime_test.c, em_game_internal.h (deletes `g.frame_selector`) | single storage for 3B8D and 3B91 | opening runtime and control tests; the trace shows identical 3B8D per frame; the G6 no-shadow grep | g.frame_selector; the private skip promotion |
| **S11b Classifier acts** | em_scene_bindings.c, em_hud.{c,h} (external open; self-toggle removed; menu inhibit → B3), em_player_frame.c (B9 write; the go_state early-out is removed), em_game.c | r==2 → state 3/5 with w_0020CDC0 = the em_hud adapter (clears B0/C5). Game over via B9 → 001AD140 → w_001AD4E0 = the legacy GO machine → 001ADF00. The unported arms (r==1, r==3, +9=3) fault. | test_status_frame_reference, test_scene_frame_reference, EM_PAUSE_TEST, EM_DEATH_TEST, newgame-control, a Start-press trace against st14 (world frozen) | em_hud_is_open and go_state early-outs; the em_hud self-open; the damage/vitals tick and its death latch (the B9 stand-in), left in the 001AFD70 legacy block by S10a, move to w_0015BCF0 |
| **S12a Area change / load** | em_scene_bindings.c (w_001FF080, w_0021B550/0021B840, w_001AD360), em_game.c (game_load_task becomes the begin/poll workers), em_frontend.c (movie start moves to 001AD360 step 1), new em_spawn_table.{h,c}, tools/export_spawn_table.py, tools/test_spawn_place_reference.py | native 001ADF50 over at least 3 ticks; state-0 rebuild; 001B07C0(0/1) from the exported D_0024D650 | Spawn placement: executes byte-matched 0x1B07C0 against the captured tables; entry 0 must match handoff/playable. An `EM_AREA_CHANGE_TEST` posts B5..B8 and asserts the tick-by-tick +9/+A/+B sequence against S3's oracle. New Game census = 49 at first control. | game_load_task; the invented spawn placement |
| **S12b Legacy door through B8** | em_door.c | legacy goto → B7, B8=2 + fade; state 4 re-places | door self-tests; a room-move trace (tick sequence: B8 set, fade to 2, +B=4, state 4 → 1 in one tick) | the in-frame `em_door_goto_pending` path for AREA11 |
| **S13 Live smoke** | new em_level_smoke_test.{h,c} (hooked beside em_opening_control_test) | `EM_STARTUP_TEST=newgame-level`, `EM_LEVEL_SMOKE_UNTIL=first_control\|status\|battery\|panel\|elevator` | At S13, first_control (+B=1, selector 0, census 49) and status open/close must pass. Later phases report NOT-LIVE until WP-4/5/6. | — |

**Phase 2 status.**
- **S8 landed (2026-09-22):** slot 0 runs `em_scene_task_001ACEC0` (src/game/em_scene_bindings.c) through the S3/S2 cores (001ACEC0 → 001AD250 → w_001AD4D0 → `em_sf_001AE040_q1`); `game_load_task` seeds +8=3, +9=1, +B=0 (legacy load, retired by S12a); state 0 binds 001AFCA0 to the moved native re-arm (`em_game_legacy_state0`, plus spad 31F4=0), 001AFCF0/001AD140/001AD010 to the cores, and the ten state-0 callees without port code (001FC9B0, 001B07C0, 001B6990, 001D19E0, 001C1DC0, 00199C50, 001AEE40, 001FAE70, 001C5C50, 001D1EF0) to explicit no-effect bindings reported once on stderr; `legacy_state0_frame` keeps the same-tick fall-through; both variants bind to one legacy worker (`em_game_legacy_world_frame`); the traced classifier is a shadow over the canonical state with E74/E70 from `em_frame_pad_block()` (never acted on; canonical E50=4 per Q8 until S11a); 3B90/C4 are forwarded to `em_frame_screen_fade_gate` (C4 stays 0, so drawing is unchanged); the Continue restart is serviced at the 0x1AE040 entry (`em_game_legacy_continue_restart`, retired by S11b). `ingame_frame_machine`, `game_sub_machine` and `game_task` are deleted. Verified against a pre-step binary built from HEAD (build/S8-before): 19 EM_CAPTURE BMPs and the newgame-control BMP byte-identical, newgame-control PASS (locked 1300, 9.599989), 20 EM_*_TEST verdicts identical, EM_FRAME_TRACE valid (the comparator matches idle04 through 0x1AE040 → 001AE5E0, then diverges at the monolithic legacy worker, as expected until S10a). Known S8 limit: the trace's selector is canonical 3B8D (0), not `g.frame_selector`, so cutscene ticks show target 001AE5E0 until S11a.
- **S9 landed (2026-09-22):** `legacy_state0_frame` and its same-tick second 0x1AE040 run are deleted from em_scene_bindings.c; 0x1AE040 runs once per tick, and the state-0 tick ends after 001D1EF0 with no 001AE7E0 and no variant, as in the original (state 0 ends with an unconditional branch at 0x1AE0DC to the epilogue 0x1AE5CC; the load trace's first player update is the tick after the spawning tick, ORIGINAL_FRAME_ORDER "New-game load"). Re-baseline result: **no constant changed**. Every frame-indexed constant (EM_CAPTURE_FRAME defaults in em_game.c, the em_game_selftest.c scripts, em_opening_control_test.c) keys on `g.frame_no`, which counts world frames (zeroed by the state-0 re-arm, incremented at the end of each world frame in em_render_frame.c); nothing in the game keys on the main-loop tick counter. The original's first world frame is the tick after state 0, and so is the port's now, so world frame N still denotes the same original frame and the whole shift lands in the tick count before it. Verified against a HEAD build (both built from one `git archive HEAD` snapshot, S9 files only): 22 EM_CAPTURE BMPs (default, frames 0/1/2, aim 1-5, door, supply, examine 1/2, rise, orient, walk, locked 1/2, snow, snow frame 0, drawbridge, cam-print) byte-identical; newgame-control PASS (locked 1300, 9.599989) with an identical BMP; the 20 EM_*_TEST verdicts identical (HEAD's pre-existing DOOR/TRANSIT/SLIDER/CINE/PAUSE/CAMREGION FAILs unchanged); EM_FRAME_TRACE now shows the state-0 tick without 001AE7E0/001AE5E0 and one extra tick before the first world frame (a 42-world-frame run takes 43 ticks instead of 42), and compare_frame_order against idle04 reports the same divergence as S8 (the legacy worker, until S10a).
- **S10a landed (2026-09-22):** the world frame runs the S2 variant cores: w_001AE5E0/w_001AE6B0 call `em_sf_001AE5E0`/`em_sf_001AE6B0`, whose stage workers are bound in em_scene_bindings.c (0015BCF0 → `em_player_0015BCF0`, gameplay only; 001D1C50, 001C1D00(0x8101D0), 001D1EA0(1) → the em_render stages; 0018B9C0 → `em_camera_0018B9C0` or, in 001AE6B0, `em_camera_0018B9C0_opening`; 001CB590 stores the current actor D_00275B44 = a0 per byte-matched func_001CB590, read back through r_00275B44; 001CB5A0 is the empty leaf; r_008102B9 returns 0x15, the value in all three captured RAM images). The 001AFD70 position is one legacy block per variant (`em_game_legacy_pool_gameplay` for mode 0, `em_game_legacy_pool_cutscene` for mode 1, in em_game.c), holding the retired monoliths' remaining world updates in their old relative order; the truck, director and panel updates and the collision-registry clears that used to precede the player update now run there, after 0015BCF0, as their pool owners do (0015BCF0 at 0x1AE628, 001AFD70 at 0x1AE64C). Effects of that move: the two registry clears change nothing, because neither registry has a runtime registrant (em_collision_moving_register/em_collision_blocker_register are called only from the em_collision.c self-tests); grate_update, the only binder of panel collision cell 18 (it registers no blocker), now binds it after 0015BCF0, so the player stage of the first world frame after a scene load or an in-block `em_game_scene_switch` (em_collision_load clears all bound cells) runs without cell 18; a director beat armed in frame N locks the player from frame N+1. The damage/vitals tick that design 4.5 assigns to w_0015BCF0 stays in `em_game_legacy_pool_gameplay` until S11b, with the port's death latch (g.go_state, its stand-in for the B9 write; canonical B9 has no port writer yet). The cutscene 0015BCF0 stays on the opening-player path (reported, no port code at that position; design risk 2). 0015C160, 001F0360, 001AAD00, the 001CB590 anim_bone_array_setup tail and walk mode 2 are explicit no-effect bindings reported once on stderr; every stage worker faults outside a variant or with an argument other than the original's. Interim until S11a: canonical 3B8D is published from `g.frame_selector` at the 0x1AE040 entry (the port's 3B8D writer runs inside the world frame, so frame N's write picks frame N+1's variant, as in the original). The status/game-over frozen frame and the test hooks moved to `em_game_legacy_variant_head` (the frozen frame is retired by S11b). `gameplay_frame`, `cutscene_frame` and `em_game_legacy_world_frame` are deleted, as are the S5 fault stubs `em_player_0015C160`/`em_render_001AAD00`. Verified against a HEAD build (same `git archive` snapshot): compare_frame_order PASS for idle04, walk04, st03 (first and last native windows of a newgame-control trace), cut02 and cut15, with one allow entry (`S10a-pool-legacy-block`: the original's per-node 001AFD70 dispatches are absent until S10b); cut07 (selector 3) has no native window because no port code writes 3B8D=3; aligned on "selector ≠ 0" in a one-off check, its 18 in-scope calls per frame also match. The comparator self-test now runs its mechanics with an empty allow list (the real list is still schema-checked). newgame-control PASS (locked 1300, 9.599989) with a byte-identical first-control BMP; the EM_CONTROL_STOP_TEST and EM_CONTROL_LOW_GAIT=1 runs are identical; 24 EM_CAPTURE BMPs are byte-identical (default, frames 0/1/2, aim 1-5, door, supply, examine 1/2, rise, orient, walk, locked 1/2, snow, snow frames 0/200, drawbridge, cam-print, EM_HUD_FORCE); 26 EM_*_TEST verdicts are identical (the 20 of run_suite plus six in scene_snow; the pre-existing DOOR/TRANSIT/SLIDER/CINE/PAUSE/CAMREGION and snow MOVE/CINE FAILs are unchanged); no scene fault in any run.
- **S10b landed (2026-09-23):** the actor pool is live (em_actor_pool.c, em_actor_roster.c and the new em_area11_bindings.c are in COMMON): state 0 resets it at w_001AFCA0, spawns the AREA11 roster at w_001B6990 (the S7 spawner over assets/scene_snow/roster.emro; the legacy load and the Continue restart commit 001AD360 step 4's area bytes 0x0B/0/0 first), the 001E55F0 weather node at w_001C1DC0 (interim: D_008106C8 has no writer until S12a, so the captured 0x20081910 selects D_00250F00) and the title node at w_001C5C50; the first gameplay 0015BCF0 after the wipe spawns 0015C420's seven 0018A880 nodes (0015C310's D_00810CA4 branch) and 001F0120(0x3B); owners' first ticks spawn their 001C5570 children (00219550 x6 and 00159210 from their C; 0x825940 and 0x827B10, and Roger's 001F0120(0x47), as measured interim spawns); every 001AFD70 position is the pool walk with per-node trace (callback, record tag, binding name) and spad 3B8A; each design-4.4 row is an adapter over a piece of the former S10a block (em_game.c: collision clears first, draw-list collector and the damage/weapon residue after the walk; the em_pickup 001C5680 light loop moved to the indicator node so its LCG draws follow the weather node as in the original) or an explicit reported no-port-code node (Roger, 001C5C90, r13, 001E2560, 0018A6B0 per D3, fans, dormant 00823CE0, prop, area title); scenes without a roster run the unchanged S10a block as one legacy_world node; D2: EmProgress in em_scene_state.h, canonical for D_00810788, D_00810860..B5F (em_pickup's taken bits migrated) and D_00810CA4..CA7 (em_pickup's CA4/CA6 migrated; test_continue_reset_reference now checks CA5/CA7). Verified: compare_frame_order PASS for idle04, walk04, st03, cut02 and cut15 in every candidate window of a newgame-control trace (66/66, 65/65, 67/67 events per frame) with the S10a entry deleted and five S10b entries (r13 extra; walk04 001EA240; opening 001BB0E0; cut02's paired 001E2560); newgame-control PASS (1300, 9.599989) with the stop, low-gait and re-entry variants, their BMPs and 26 EM_CAPTURE BMPs byte-identical to HEAD; 38 EM_*_TEST verdicts (20 run_suite scenes + 18 in scene_snow) identical; all 121 make test-* targets pass. The panel-cell question from S10a: the original's player stage also sees the panel one frame late (D_00275B7C is reset by 001AF8E0 and swapped at 001AAD00), so grate_update's first bind after the walk matches.
- **S11a landed together with S11b (2026-09-23, one commit; lead decision D4):** S11a: spad 3B8D and 3B91 have one storage, `EmSceneState` (g.frame_selector is deleted, with the S10a publication in w_001AD4D0 and the state-0 re-arm's clear): em_opening_runtime.c writes them as 001B82D0 does (ops 9..12 phase 0: 3B8D=2 at 0x1B8608, 3B91=0 at 0x1B861C; phase 3: 3B91=1 at 0x1B8778; op 5→4: reads 3B91 at 0x1B88DC, clears 3B91/3B8D), and em_script's `skip_request` is only a per-tick view the runtime publishes from the canonical byte (001BA1F0 reads 3B91 at 0x1BA284/0x1BA3EC); the private START/SELECT promotion is deleted, so the 3B91 1→2 promotion is only em_sf_001AE6B0's (0x1AE6E0); em_render_frame.c's area-title check and em_opening_control_test.c read canonical 3B8D. `em_frame_scene_input` (em_frame.c) is the one translation of step C into D_00810E74/E70 (001B5940's words, unswapped) and D_00810E50=4 (001B5F40's pad-state byte, sb at 0x1B604C), called at the start of every slot-0 tick; the bindings' E50=4 init and the classifier trace's pad-block substitution are deleted. New `make test-scene-no-shadow` (tools/test_scene_no_shadow.py, the G6 grep: no frame_selector, writers of 3B8D/3B91/E74/E70/E50 limited to their original writers' counterparts, one EmSceneState owner, 3B8D/3B91-annotated declarations only from a shrinking allow list: the trace record, em_script's view and the unwired interaction host/scan fields, WP-4/WP-6). tests/opening_runtime_test.c now runs the real em_sf_001AE5E0/001AE6B0 cores over a canonical state (walk → opening tick, 0018B9C0 → opening camera) and asserts that the skip press is promoted by 001AE6B0. Verified against a pre-step build of the same tree: newgame-control PASS (1300, 9.599989) with the stop, low-gait 1/2 and re-entry variants, all five BMPs and EM_FRAME_TRACE files byte-identical (3B8D per tick identical: 98 ticks at 0, 1299 at 2); a live START press during the opening (scratch probe, frame 400) skips identically (opening complete at frame 435 both, identical trace and BMP); opening runtime test output identical (normal 1293 actor frames, skip 288); compare_frame_order PASS for idle04, walk04, st03, cut02 and cut15; all 127 make test-* targets pass; make all has zero warnings. The S11a→S11b window (a START/TRIANGLE edge in gameplay faulting at the unbound w_0020E060) never shipped: S11b below closes it in the same commit.
- **S11b landed (2026-09-23, with S11a):** the frame machine acts on 001AE7E0. The bindings-only `s_classifier_shadow` flag and the `*frame_state != 1` guard are deleted from em_scene_bindings.c; w_001AD4D0 records the +B it entered with (`s_entry_state`) so that workers the original reaches from several states refuse or tell apart a call. **Status (r==2 → states 3/5):** w_0020E060 → `em_hud_status_open` (the original clears the 0xA0-byte block D_00810130, src/func_0020E060.c; the legacy screen's counterpart is shown/hub/no hover), w_0020CDC0 → `em_hud_status_tick` (the legacy navigation; returns 1 when the hub closes on TRIANGLE/START/CIRCLE, the hub's 0x830 edge mask in src/func_0020CDC0.c; on the close the adapter clears canonical B0 and C5), w_001FBC50 → `em_sfx_stop_all` (its translation), w_001AEDB0 → `em_frame_fade_full`, w_0018C0D0 (state 5 only) → `camera_commit_original(&g.cam, a1)`, w_001AEE40 → `em_frame_fade_flash` in state 5 (the state-0 area-entry call stays unmirrored), w_001D1C50 and w_001D1EA0(0) → the same render stages in state 3 (001D1C50's C4≠0 path still ends in 001D7C30, the point-light tick), r_00282157 → 0 (001FA0D0's disc-read phase is never mid-read at a tick boundary: the port's readers are synchronous). 001FABB0, 00119828 ×2, 001D2830, 001E0CC0, 001FAE70 and 001D1EF0 are reported no-effect bindings: the music keeps playing (H22 stays open for WP-5). em_hud no longer opens itself: its self-toggle, `s_menu_inhibit`/`em_hud_menu_inhibit` and `em_hud_is_open` are deleted, as are the status/game-over early-outs of `em_game_legacy_variant_head` (now test instrumentation only) and em_camera.c's `em_hud_is_open` gate; `em_hud_forced_update` keeps the EM_HUD_FORCE debug hook's navigation. The menu-inhibit byte B3 is canonical: `em_player_0015BCF0` writes it every gameplay frame from the legacy screen's former open gate (damage/death lock, examine sequence, opening runtime, door menu lock), an interim stand-in for 0015BA50's tail (its original conditions are listed in em_hud.h; WP-15). **Game over:** the damage/vitals tick moved from the 001AFD70 legacy block (`em_game_legacy_player_residue`, now the weapon update only) into `em_player_0015BCF0`, after the pose, followed by 0015CF90's B9 write (byte-matched: `+0x220 <= 0 && B9 == 0 → B9 = 1`; its other stores D_00810706/707/858/85C are not canonical yet, D2). em_player_damage.c's pd_phase-3 hand-off to the legacy GO_SCREEN is deleted: at fade substate 2 0x1AE040 state 1 calls 001AD140 (+9=2), and 001AD250 runs the byte-matched **001AD4E0 core** (the 0xF0 hold at +0x18, the CROSS skip D_00810E74&0x40 once D_0028A9A0==0, the 001AEE10/001AEDE0 fades), then 001ADF00 (D_00275BDC=1, 001AEBA0(0xFF) → `em_screen_fade_in`, 001AB790(001AC070)). D4 named the legacy GO machine as "w_001AD4E0", but em_sf_001AD250 calls the 001AD4E0 core directly (there is no w_001AD4E0 entry, and adding one would edit the S3 core's caller); the legacy machine therefore sits at that position as the core's workers: w_001FF080(0,0x27) sets `g.go_state = GO_SCREEN` (the em_hud_game_over module stand-in) and clears D_00275BD8 (no asynchronous module load in the port), w_001ABF90 → `em_render_001ABF90` (today's close-out with that stand-in), and w_001AB790(0x1AC070) replaces the task with the interim 001AC070 `em_game_legacy_continue_task_001AC070` (the legacy continue prompt; cursor init now D_00275BDC ? 1 : 0 per 001AC480 sub 0; option 0 runs the legacy Continue restart and reinstalls em_scene_task_001ACEC0 with the legacy load's bytes). The legacy GO_SCREEN/GO_SCREEN_OUT timing code, GO_HOLD_FRAMES, go_hold and the w_001AD4D0 Continue hook (`em_game_legacy_continue_restart`) are deleted. 001D2880, 001FA790(0,0x1B), 001FAB50 and the D_00810D38 store are reported no-effect bindings. The unported arms fault at their NULL workers: r==1 (state 2 / 0022A650, 001FB9F0; reachable only through the Q1-withheld SELECT or E50≠4), r==3 (state 6 / 001FF030/001FEFE0; CE has no port writer) and +9=3 (001AD740; 3B93 is never set in AREA11). **D5:** spad 3B92 is canonical: `s.cinematic_ready` is deleted; em_opening_runtime.c reads 3B92 in ops 9..12 phase 0 (lbu 0x1B85E8), writes 1 in phase 3 (0x1B874C) and 0 in the op 4 teardown (0x1B891C/0x1B8940), and the phase-0 `sh 3B84=0` (0x1B8610) is added; 3B92 is in test_scene_no_shadow's WRITERS/BYTE_TOKEN (with an ALLOWED entry for the unwired em_interaction_frame.h `ready`, WP-4); em_script.h's `skip_request` is relabelled as the per-tick view of canonical 3B91. **Verified** against a pre-step build of the same tree: newgame-control PASS (1300, 9.599989) with the stop, low-gait 1/2 and re-entry variants, all five BMPs byte-identical and the EM_FRAME_TRACE of the default run identical tick for tick; compare_frame_order PASS for idle04, walk04, st03, cut02 and cut15; new `EM_CONTROL_STATUS_TEST=1` (em_opening_control_test.c) presses START at first control and TRIANGLE 30 status frames later: PASS (status_frames=30, the world frozen: D_00810750, player position and camera eye unchanged through the open, the frames and the close frame; resumed_frames=11 with one variant per frame); its trace shows the original sequence tick for tick: state 1 r=2 (0020E060, 001FBC50, 001FABB0, 00119828 ×2, no variant) → state 3 +C0 (no calls) → state 3 +C1 (001D1C50, 001D2830(3,1), 0020CDC0, 001D1EA0(0)) → close frame (… 0020CDC0, 001E0CC0, 001AEDB0, 001D1EA0) → state 5 one frame (001AEDB0, 001D1EF0, 0018C0D0(0x8101E0,1), 001FAE70(1), 001AEE40(0x20)) → state 1 → 001AE5E0 (ORIGINAL_FRAME_ORDER Q7), and compare_frame_order against st14 PASSes on the state-3 frames (6/6 events per frame) once the native selector is aligned to st14's 2 (st14 was opened from the panel script with 3B8D=2; a gameplay open keeps 0, Q7) in a one-off copy. Game over, scratch probe (a lethal pending hit at first control): B9=1 on the next player stage, death clip, corpse hold, fade out, 001AD140 at fade 2, 001AD4E0 steps 0/1/2/3 on consecutive ticks, the 240-tick hold, fade out, step 4, 001ADF00, the continue prompt with cursor 1 (D_00275BDC=1), UP + START → option 0 restart → state 0 rebuild → the opening replays. All 128 make test-* targets pass (test-scene-no-shadow on this step's own file set: in the shared working tree it also sees two other lanes' uncommitted files, em_area_script.c and em_player_slide.h, which need WRITERS/ALLOWED entries when they land); make all of this step's file set over HEAD has zero warnings, and its binary passes newgame-control (paced, 9.599989) and EM_CONTROL_STATUS_TEST.
- **S12b landed (2026-09-23):** the AREA11 door goes through B8. **Door (em_door.c):** `door_bind_original` binds the manifest door to the exported original door descriptor `door_original/source.emdo` (source 0082A3C0, callback 001BC350, door id 0, destination row D_0024E140[0x0B] + 0 = 2/1/0/0) when the placement matches; the EM_DOOR_OPEN commit is then 001BC240 → 001BC150 (`em_door_transit_commit`, oracle `test_door_transit_reference`: 001AEDE0(4, 0) → `em_frame_fade_start_colour(1, 4, 0)`, B8 = 2, B7 = row[side], side 0 = the legacy `front`), and sub 5 is 001BC290 (clip advance; the snap close once B8 is clear). The legacy warp (at full black: computed far-side point, transit yaw, unconditional walk-out) is no longer reached in AREA11; it stays for the doors of the non-roster fixture scenes, whose replacement is not live. **State 4 (em_scene_bindings.c):** 0018AB00 → new `em_sf_0018AB00` (em_scene_task.c; D_008106C6 from the canonical D_00810CA4/CA7); w_001B07C0 accepts arg0 1 in state 4 (and only 0 in state 0), writes back the pending-damage drop (g.pd_pend_hp/inf; +0x00 has no port storage) and calls `em_door_room_move_arrival` (the walk-out when 001B07C0 wrote 5/1/0, the script teardown, the movement lock released otherwise; g.doorcam = 3 for the cleared D_008101E4) instead of the state-0 fixtures; 001B0460(1) keeps the S12a stand-in (its a0 arm needs D_008104E0 = player +0x230 at 0x10/0x12, which 001B07C0 has just stored 0); 0018D7B0(cam, 1) and 0018C0D0(cam, 1) are reported no-effect bindings (`UM_0018D7B0`, `UM_0018C0D0_STATE4`); 001C5C50 in state 4 re-arms the legacy area-title card. **Nodes (em_area11_bindings.c):** the weather node runs `em_weather` over its own `EmWeather` (new `em_snow_runtime_tick_actor`; B8 and D_0028A9A0 are passed, so B8 = 2 with fade 2 selects state 3 and the next call frees the node through 001AFC10); the area-title node runs its lifecycle from the .s (0 → 1, B8 ≠ 0 → 3, 2/3 → 001AFC10, the legacy card stopped with `em_hud_area_title_stop`). **Verified:** new `make test-room-move-reference` (executed 0018AB00 over CA4 × CA7, quick 2,600 of 65,536, full all; a headless New Game whose door commits on side 0 at first control, `EM_ROOM_MOVE_TEST`, logged per tick through EM_AREA_CHANGE_LOG: the 70 rows from the B8 = 2 row are identical to the original route capture 09_fence_door f407..f476 in B5..B9 and the eight fade bytes, the re-place is at +65 as f472 with the captured position and heading, 001AD010 replays through the executed original, the state-4 tick runs the nine state-4 calls then 001AE7E0/001AE5E0 with +B 4 → 1, D_008106C8 = 0x20089910 and D_008106C6 = 0 as in the capture's snapshot, one weather and one title node before and after as in the capture, the door closed and control returned at the re-place; `EM_ROOM_MOVE_TEST=side1` (no capture exists for side 1) re-places at entry 1 with heading 0, and its walk-out byte 1 runs the legacy walk-out, which releases the movement lock 13.1 u further on); newgame-control PASS (1300, 9.599989), BMP byte-identical to a pre-step build and its EM_FRAME_TRACE identical except the two renamed binding names; compare_frame_order PASS for idle04, walk04, st03 (gameplay windows) and cut02, cut15 with the same verdicts as the pre-step build; make all has zero warnings. Known limits: the door's open script is still legacy (no 3B8D = 2, no 001BBE40 snap), the camera re-seat is the legacy chase re-arm, the side-1 walk-out is the legacy walk-out, and the legacy card is keyed on the area byte only.
- **S12a landed (2026-09-23):** the load arms run through the cores. **New Game:** the frontend's EM_STARTUP_NEW_GAME is 001AC070 state 4 (D_00275BE0 = 0, 001AB790(001ACEC0)): `em_game_install_new` registers `em_scene_task_001ACEC0` with a cleared record, and the chain runs +8 = 0 `w_001AD1A0` (byte-matched 001AD1A0 translated in the bindings: +9++, D_00275BD8 = 1, 001FF080(0, 3) = module 3, resident, BD8 = 0; then 00200830/001D19D0, reported no-port-code, return 4) → +8 = 1 `w_001AD230` (`em_game_new_game_reset_001AF2C0`: em_pickup_reset + game_state_new_game, moved out of the install) → +8 = 3 001AD360, whose step 1 stores D_00275C78 = 0 and D_00821058 = 1 through `em_frontend_movie_select/_request` (the intro movie now starts there; the frontend's movie pump no longer installs the game or forces black) → 001AD250's 001AEDB0(0) → +9 = 5 001ADF50: `w_001AED80` = em_frame_fade_clear, `w_001FF080(1, 0)` = the native area read `em_game_legacy_area_load` (the former game_load_task body; first call also loads the player model and SFX registry, later calls re-read the scene) of D_00810700/701 (only 0x0B/0 is exported; anything else faults) with BD8 = 0 when it returns, and the veil state machine 0021B180/0021B550/0021B840 (new em_load_veil.{h,c}, byte-matched, EE-truncating floats; its 001D2830 calls and particles 0021B1B0/0021B500 are reported, not drawn) → +9 = 1 → state 0. The rebuild mirrors 001AEE40(4) (the captured flash), and **001B07C0(0)** is the byte-matched translation (new em_spawn_table.{h,c}) over the exported D_0024D650 window (tools/export_spawn_table.py → assets/spawn/spawn_table.emsp): position/heading into g.pos/g.yaw, 0x70003B40..5C into new canonical `spad3B40`, D_008106C8 (001B0250) into the canonical request word C8, C60 into em_pickup; 0015C1F0 reported; 001B0460 stood in by the legacy chase re-arm (`em_game_legacy_camera_rearm`, reported) until translated; arg0 != 0 (state 4, S12b) and D_00275BE0 == 1 are refused. The manifest spawn is kept only for scenes without an original roster (the EM_SKIP_STARTUP fixtures, which read their scene at install and enter at state 0 through `em_scene_bindings_fixture_loaded`); `game_load_task` and `em_scene_bindings_legacy_loaded` are deleted. **Continue** (001AC070 option 0) reinstalls the task with a cleared record and so runs the same route (movie at step 1 included); only the port's go_*/pd_* stand-ins and the legacy em_bgm_stop stay in continue_restart. **Area change:** `em_scene_request_area_change_001B0C60` (byte-matched 001B0C60: 3B8D = 3, 001B0C00(4), B8 = 1, B5/B7/B6; 3B8D writer added to test_scene_no_shadow) → 001AD010 at fade 2 → +9 = 5 → reload → state 0. Also: the weather node is the 001C1EA0 translation over canonical D_008106C8 (no longer interim), and **record 13's manager 008257A0** is translated (em_manager_008257A0.{h,c}: states 0/2/3; state 1, reachable only with event 0x30 set, faults) so it frees itself on the second world frame (Q3) and the allow entry S10b-record13-unbound is deleted; D_00810794 (event 0x3C) is a migrated D2 byte. **Verified:** new `make test-spawn-place-reference` (executed 0x1B07C0 + 001B0250 over every record of every room, one-past-end and out-of-window walks, D_00275BE0 {0,1,2} x arg0 {0,1}, 768 flag-arm cases and callee fail-stops; the window equals the opening/handoff/playable RAM and entry 0's placement equals the 17 fields the captures still hold; full sweep 6,864 cases PASS), `make test-manager-8257a0-reference` (executed overlay + 001BA1C0, 12,288 cases full), `make test-area-load-reference` (executed veil functions, and a headless New Game + EM_AREA_CHANGE_TEST run whose 24 chain ticks and the 001AD010 call are replayed through the S3 oracle extended to execute 001AD1A0: identical state and trace per tick; the New Game passes the 13 distinct task states of the PCSX2 capture newgame_samples.jsonl in order; 001ADF50 takes 8 ticks natively); newgame-control PASS (1300, 9.599989) with **census 49** at first control, stop/low-gait 1/2/re-entry/status variants identical to HEAD with byte-identical BMPs; EM_AREA_CHANGE_TEST PASS (player at entry 0: position and heading bits of the record); compare_frame_order PASS for idle04, walk04, st03, cut02 and cut15 in every candidate window of a newgame-control trace except the windows that include the first two world frames after the load, which hold record 13 as the original's do (it frees itself during the second; the traces were taken later); EM_CAPTURE office/snow fixture frames 0/1/2/5/60 byte-identical and all 38 fixture verdicts identical to HEAD; all 131 make test-* targets pass; make all has zero warnings. Not run live: a Continue (no scripted death-and-continue fixture). Known limits: the veil is not drawn; 001B0460 is a stand-in; AREA01 (the fan/Roger exit targets) is not exported.
- **S13 landed (2026-09-23):** the live level smoke (docs/LEVEL_SMOKE.md). New `src/game/em_level_smoke_test.{h,c}`, hooked beside em_opening_control_test (begin in `em_game_install_new`, after-frame at the end of `frame_close_out`, the failure exit in main.c; the frontend treats `newgame-level` as a New Game fixture): `EM_STARTUP_TEST=newgame-level` walks the main line of FIRST_LEVEL_ROUTE.md section 3 as 15 phases up to `EM_LEVEL_SMOKE_UNTIL` (first_control, status, battery, elevator_refusal, panel, elevator, boxes, slide, truck_preview, truck_crossing, cage_roof, crevice_prompt, crevice_jump, east_tower, roger; default all), driving pad input only. **first_control** asserts task +8/+9/+B/+C = 3/1/1/0, 3B8D = 0, census 49, area 0x0B/0 and no fault; **status** presses START at first control and TRIANGLE 30 frames later and asserts state 3 sub-step 1 with the world frozen and 3B8D = 0 on every status frame, then one gameplay variant per frame in state 1 with B0/C5 clear until the fade is idle. Every later phase reports NOT-LIVE with its route beat, original owners, the port binding its owner runs today (new `em_scene_bindings_pool_binding`) and the step it waits on; the run stops at the first NOT-LIVE phase. New `tools/test_level_smoke.py` checks every passing phase against the original captures through the run's EM_AREA_CHANGE_LOG: first control against status_04.json frame 0 (task bytes, spad 3B8C..93, B0/C5/CE, one 001AE5E0) and route 01_battery f0 (spad, D_008106B0..B9, area, fade block); the status open and close windows (two ticks before to three after +B becomes 3 and 5) against status_04.json tick for tick, and the exit fade against route 01_battery f484..f495 row for row; a phase that passes in process without a capture check fails. New `make test-level-smoke` (about 7 s). Found: the interim em_hud hub closes on the first state-3 frame after the TRIANGLE edge, while in the original the press-to-close (+B = 5) delay was two frames longer than the press-to-open (r == 2) delay (status_04, TRIANGLE both times: f200 → f204 against f10 → f12; the capture held each press four frames, the smoke taps one; the decomp's close is edge-triggered and phase 5 waits on 0020E0C0, the likely source of the extra latency); the smoke aligns on the +B = 5 tick until WP-5. After its last phase the run takes one more frame without input before quitting, so the last phase's post-frame fade (the next tick's start sample) is in the tick log; `EM_LEVEL_SMOKE_UNTIL=first_control`, `=status` and the default run each pass `make test-level-smoke`. Verified: the smoke PASSES first_control and status and reports 13 phases NOT-LIVE; the capture check fails on a one-byte change to the exit fade or to the state-5 tick (negative controls); newgame-control (and its status, area-change and room-move variants) unchanged; all make test-* targets pass; make all has zero warnings.

- **WP-4 landed (2026-09-23):** the AREA11 interaction host is live (docs/AREA11_INTERACTION_HOST.md). **Lifecycle (em_scene_bindings.c):** w_001B6990 loads it after the roster spawn (a host that cannot load faults at 00159210) and installs `player_use_set_hook(em_area11_interaction_host_use)`, `player_pose_set_stage_hook(em_area11_interaction_host_player)` and the step-F message service (`em_frame_set_message_service`, 001FCA10's position after the task); w_001AFCA0 (001AF8E0) and `em_game_shutdown` detach them and clear the host. **Owners (em_area11_bindings.c):** #26 and #27 run as above (4.4); the panel's first call gives the host its record address, which 00157F60 stores in D_008106D0. **Use (00160220's head, 00184BA0, 00183EF0):** on D_00810E74 & 0x40 (spad 0x3B76's default CROSS) the host scans the previous frame's published list, gated on 3B8D, D_0028A9A0 and D_008106EF, with the panel's type-24 and the elevator's selector-1 predicates; the winner's +0xB = 4, the shared-owner claim writes 3B8D = 3 and 001798D0 runs (`player_pose_use_accepted`). Only these two owners are published: the pickups, the door and Roger keep their legacy scans until WP-6/7/9 (W22). **Publication:** each owner's state-1 tail offers it (001B17A0: the 001B1630 cone/range gate on g.cam.eye/fwd, then 001B1B70), and w_001AAD00 swaps the list in (`em_area11_interaction_host_publish`). **Cutscene player stage:** w_0015BCF0 in 001AE6B0 runs `em_player_0015BCF0` unless the opening runtime owns the player, so the shared player worker (0015B130 takeover, scripted clips, 00182DF0 release) runs between 001AFD70(1) and 001AFD70(2) while 3B8D is 3 or 2. **0018B9C0** now decays D_008106EF in both variants (`camera_cooldown_0018B9C0`, src/func_0018B9C0.c). **Status requests:** w_0020E060 routes a screen opened on B0 != 0 (the panel's 00157F60: B0 = 1, B1 = 0x82, D_008106D0 = the panel) to the host's page layer (new `em_status_runtime_page_open`/`_page_tick`: 0020E060's reset and 0020CDC0 over the canonical B0/B1/C5/CC; 002149F0's exit writes 3B8D = 3), drawn at 001D1EA0(0) with no world flush; a START/TRIANGLE screen (B0 == 0) keeps the interim em_hud (WP-5). The status page's system cues (0/1/2/4/5/6/0xB/0xD) have no exported samples (WP-14) and are silent, reported once. **Canonical storage:** the host's EmInteractionFrame is a per-call view (view_load/view_store around every entry point) of spad 3B8D/3B8F/3B92/3B84, D_008106D4..DF/EF/F3 (EmSceneState), the camera bytes D_008101E1/E3/E4/E6 and D_008105F0 (g.cam) and the message phase; the running script's skip byte is 3B91 (script_load/store); D2 migrates D_0081084C (the power byte; `g.terminal_powered` and `em_game_set_terminal_powered` are deleted, `em_game_terminal_powered` reads D_00810841[D_00810700]) and D_0081083A (the elevator floor, the owner's `lower`). test_scene_no_shadow gains the host as a 3B8D/3B91/3B92 writer (em_area_script.c's frame_load/frame_store entries are left to the area-script lane, whose file it is); the em_interaction_frame.h/em_script.h/em_interaction_scan.h entries are relabelled as per-call views. **Correction (00828050):** its three add.s are the EE single-guard-bit add (em_pose_math.h `pose_add`), not a plain truncation: route 04's 150 carried player Y values end at 190.00061 (a truncating add gives 189.99832, which AREA11_ELEVATOR.md had claimed); em_elevator.c and test_elevator_reference.py's add.s now use that model, and the test replays the capture's 150 values. **Retired (scene_snow):** the em_examine terminal (the manifest's `terminal` examine line is skipped, `em_examine_set_terminal` and its powered/refusal branch are deleted), the legacy ride (`elevator_tick`, `elevator_descent_begin`, `em_game_elevator_start`, g.elev_state/pending/frame/rate) and the pool node's legacy examine tick; the grate had no interaction code left (grate_update is its pose and cell 18). The legacy interact-clip lock stays for the legacy pickup take (WP-6). **Verified:** `make test-level-smoke` runs elevator_refusal, panel and elevator live (battery is driven through the legacy pickup, NOT-LIVE) and matches routes 02 (f200..f381) and 04 (f183..f570) row for row, and route 03 in two windows (f231..f389 and f461..f680; the prompt window between them is not compared, the module-0x21 load wait of WP-5, and the panel's player Y is checked as retained, its camera Y with that offset), in the spad bytes, the cinematic camera byte, the letterbox, the message block, the power byte, the placement, the heading, the scripted player Y (the 150 carried values included) and the scripts' camera eye/target (H5), with negative controls; compare_frame_order over the smoke's EM_FRAME_TRACE: cut07 (selector 3, the panel after the menu: the cutscene player stage at its original position) and st14 (the panel's BATTERY status frames, selector 2) now have native windows and PASS, cut02 PASSes on the refusal's and the ride's selector-2 frames, idle04/walk04/st03 on gameplay windows around the interactions (the load window keeps its S12a record-13 note), cut15 as before; the host fixture keeps its callback counts (156, 118, 170, 285); newgame-control PASS (9.599989) and its status variant; all make test-* targets pass except test-scene-no-shadow, which fails at HEAD too on the slide lane's em_player_slide.h `scripted` (theirs to fix); make all has zero warnings. Known limits: the status page's module 0x21 loads instantly (the original waits 25 frames, route 03 f390..f414; WP-5), the player is not re-grounded on the elevator actor after a release (collision/floor lanes), the follow camera after a release is the port's (WP-16), and only the panel and the elevator are in the Use list (W22). **WP-4 fix round (2026-09-23):** 00827B10's state 0 now places the actor (overlay 0x827B54..0x827BF0: D_0081083A selects 190/230 for +0xB4 and the script heights, then 001C6380; the new `em_area11_interaction_host_elevator_state0`, called by node #27's first tick before the child spawn, re-derives the owner, the Use descriptor height and the elevator pose), so an AREA11 rebuild after the ride no longer draws the elevator at the manifest's 230 (fixture `elevator_state0_floor`); the logged message block is the kind/token the message command 001B7D60 case 0 stored (cleared by 001FC9B0) instead of a synthesized mode 2 and presenter-picked token (negative controls on kind and token fail); w_0020CDC0 passes D_00282157 through the same r_00282157 reader as 0x1AE040 state 3 (0: the disc-read phase; the page does not read it).
- **WP-5 landed (2026-09-23; PARTIAL: the module-load wait is open):** **Routes (em_scene_bindings.c, em_area11_interaction_host.c):** when the AREA11 host is loaded, w_0020E060 opens the host's page route for every r == 2 (0020E060's page reset; the panel owner is bound only for B0 != 0 with B1 & 0x80, since 0020CDC0 case 0 ignores a stale B1 without a request) and w_0020CDC0 ticks `em_status_runtime_page_tick` over the canonical B0/B1/C5/CC: the cold entry (001AED80(0), cue 0xB, 0020DFA0), the request map, the hub, the ITEM root and BATTERY page, and the 0020E0C0 exit (CC = 1, 0020E080's B0/C5 clears). Scenes without the host keep the legacy em_hud screen. **The hub:** the page core's hub phase runs `em_status_hub` (0020CDC0 phase 1 with 0020D930's hover) inside `em_status_runtime` (`em_status_runtime_bind_hub`; the host binds `panel/status_hub.emhs` + `status_hub_atlas.emha` from tools/export_status_hub.py): sub-state 0 calls 001AFEB0/001AFE60, 0020E020 (the shared trail reset), 001AFF10 + 0020E6F0 and 0020E250, sets message mode 4/phase 0/group 0 and draws nothing; each sub-state-1 frame runs 0020A7A0 (one D_002655A0 step, the hub tile 0x20045EE59D421E40), 001B0000, the hover (cue 5 on a change) and 00209DF0 (`em_status_hub_ui_prepare` with the runtime-owned UI+0x20 clock, zeroed by the 0020E060 memset), then the group-0 help line; the 0x830 edge enters phase 5 (cue 1); X (0x40) enters page 3/2/1/0 for hover 1/2/3/4 (cue 0, 0020CD40) or buzzes (cue 2, 0020CD80); ITEM (hover 4, phase 3 with t[0x10] = 0) runs through the page core, MAP/SPR4/DATABASE fault there (untranslated). The hub/ITEM/BATTERY adapters share the UI texture slot, so the page that draws marks the others' uploads stale. The hub inputs: D_00810858/5C = g.status.health/infection, D_008104E4 = g.pd_infected (player D_008102B0 +0x234), D_00810C60 = g.status, C7F = em_pickup's count of item 0x1B, CB2/CB7 = em_pickup's charge/capacity, CA4..CA7 = the canonical D2 bytes, CB4 = em_weapon_reserve; CA8..CB0 have no port storage and fault if 00209860 would read them. **Status models (lead decision (a)):** the model workers are translated, not inert: `em_status_models` (new) runs `em_status_scene_original` (0020E1E0/E250/E3A0/E460, 0020E6F0/EC80, 001F4BF0 with its rand() through the shared `em_random`, and the pool 001AFF10/001AFF90/001AF800/001AFEB0/001AFE60/001B0000) with its workers bound to the port (em_owner_services_original, em_player_pose for clips 0x1C2/0x0A, the VU0 forms of em_ee_float.h); the page events bind 0020DFA0 (001AFE60, 001029C0(D_00810610), D_00810624 *= -1), 001AFEB0 and 001AFE60; the 0020A7A0 part of the other lane's module is removed, so `em_status_background` is the one D_002655A0 owner. **Drawing (lead decision (b)):** new tools/export_status_models.py exports the menu player D_0028A57C with its skeleton and clips, and the glyph models of the letter bank D_0028A56C that the captured inventory spawns ('/', '@', '0', '1', '2', '8'), with texels from the status-hub capture's GS memory (ignored assets/status_models); any other glyph, variant or costume faults, as does the D_008104E4 == 1 glow sprite 001CD520. Each 001CB580 draw is the record's model with its node world matrices and 001CB4F0's lighting mode 1, drawn with em_gfx_draw_skinned on 0020DFA0's UI camera. em_gfx has an ordered 2D layer, `em_gfx_overlay_backdrop_flush` (Metal): `em_status_runtime_render` queues 0020A7A0's sprites, flushes them, draws the models (the runtime's new `hub_models_draw` hook) and prepares 00209DF0's layer, which end_frame draws after the 3D pass: the original's GS order. **Sine:** 0020A7A0's live sine is the translated original sinf 0011E2A8 (`em_sdk_math_original`, docs/SDK_MATH_ORIGINAL.md) over the ELF window D_0026C170..D_0026C658 (tools/export_sdk_math_tables.py -> assets/sdk_math_tables.emsm; STARTUP.md). **Battery pop-up (H7):** the legacy take posts 001C47A0's B0 = 1, B1 = type for the battery types 0x1B..0x1D, so the screen pops up on the ITEM page's BATTERY acquisition notice; other takes report the withheld request. **Audio (H22):** w_001FABB0 is the port's stream-release stand-in (not a translation of 001FABB0: the stream lanes are not live; corrected in the WP-8 fix round, STREAM_LANES.md "Still missing"); w_001FBC50 and w_001FAE70 pass 001FBC50's and 001FC280's 00119828(0/1, 0x1999, 0x1999) to w_00119828, which reports them (the port's streams have no per-channel gain); w_001FAE70 reads D_008104E4 and faults on the infected override (cue 0x18); cue 25 resumes through `em_opening_media_resume_music`. **Background orientation:** 00207E40 puts UV (0, 0) on the bottom vertex; rendering the three layers from the status-hub capture's D_002655A0 block in the four U/V orientations and correlating with hub.png gives 0.90/0.94 for the port's orientation and at most 0.31 for the others (STATUS_HUB.md). **Deleted:** the em_hud page views, EM_HUD_PAGE, the hover cue 4, the Found line, the host's interim hub. **Retired:** EM_CONTROL_STATUS_TEST (superseded by the smoke's status phase, CLAUDE.md rule 4). **Verified:** `make test-status-models` (the status-hub capture: at walk 10 the pool records 0..7 and all 27 node world matrices are bit-exact), `make test-status-scene-reference` and `test-status-scene-original`, `test-status-background-reference`, `test-sdk-math-original(-reference)`; the host fixture's hub scenario (sub-state 0 draws nothing; one 0020A7A0 step, the backdrop flushed before the seven model draws and one 00209DF0 per sub-state-1 frame; hover 4 + X -> ITEM and Back; the 0020E0C0 exit latency; X on hover 3 faults); the level smoke's status phase (D_002655A0 and UI+0x20 once per hub frame, the capture's seven model records drawn once per hub frame after the first, the status_04 close latency) and battery phase (the notice lasts route 01's 239 frames); the smoke's hub frame against hub.png (menu player box within 1 pixel). **Open (WP-5, lead decision (d)):** the module 0x1F/0x21 load wait: routes 01 and 03 wait 24 loader dispatches (the 001FF080 frame plus 23), the translated loader's minimum is 10 (steps 0, 1, 2 x3, 3, 4, 5, 7 and 0x63), so 14 are disc I/O (00200730 busy polls or D_00282157 gates) whose split the captures do not record; it needs a PCSX2 I/O probe of route 03 f391..f414 (slot 2's +9/+0xB, 00200730's v0 and D_00282157 per frame), then the translated 001FF080/001FF0D0 bound at the page core's LOAD with that I/O model. Until then the port loads instantly and the panel prompt consumes its request 7 ticks after it (original 30). Also open: the 0x1999 stream volume (H22), the non-battery takes (WP-6), and the runtime's own frame/queued path, which only the sanitizer fixtures use.
- **WP-6 landed (2026-09-23):** **Owners (em_area11_interaction_host.c, em_area11_bindings.c):** the host binds the seven AREA11 item owners (00219550 x6, 0015AFA0) to `em_pickup_original` at load (`bind_pickups`; a taken one, which 001B6660 did not spawn, is skipped) and binds their status/class/armed bytes into the interaction scene; whole-world teardown releases them (`em_pickup_original_unbind_all`). Each pool node #0..#6 runs its own owner: state 0 (00219550 spawns its 001C5570 child and keeps it as the owner's +0x2EC; 0015AFA0 runs 0015AC00's 001C6380 matrix and 001F1110), then `em_area11_interaction_host_pickup_tick` (`em_pickup_original_tick_one` over the shared frame view and the program's 3B91 byte; D_00810354 = g.pos[1]; D_008104A0/D_008104E6 passed as 0: no port writer). The completion writes the child's +4 = 3 and the child node frees itself (001C5680 state 3); the owner's free frees its node (001AFC10); a freed indicator head hands the aggregate to the next member. **Publication:** the owners' PUBLISH is the translated 001B17A0 (`em_owner_services_001B17A0`: 001B1630 on g.cam.eye/fwd, 001B1B70's interactive-list push; its class-4 cell push has no port counterpart). **Use:** the host's one 00184BA0 pass now evaluates the items with 00183EF0's selector-3 branch (`em_interaction_pickup_candidate`, 0019A910 mode 6 = the port's camera segment query with mask 6): lowest score wins, 2 commits at once, the winner claims the shared owner and 001798D0 runs. **Inventory:** the item block D_00810C60, C63..CA3, CA8..CB3, CB6..D1F is canonical D2 progress (lead decision D2; the counts, meters, battery bytes and the map/key arrays alias as in the original); em_weapon keeps C61/C62/CB4. 001C40B0 is translated from its .s (`em_pickup_items_original.c`, all cases; case 0x10 writes C62/CB4 directly through `em_pickup_set_weapon_ammo`, W13). **Takes:** 001B6EA0's families post their original requests; the battery's opens the BATTERY page (WP-5); the others (0x1E/0x1F, 0x10, key 0x32, map 0x08) fault in 0020CDC0 with a report naming the untranslated page. **Aura:** 001F1110/001F1180 translated (the .s: variants 4/2/1 run the facing test; the NEARMISS C has it inverted), gated on D_70003B92 read after the script step; the sprite draw (001F0A60) is reported, not drawn. **Deleted:** `pickup_trigger_scan`, the two-frame take, `inventory_add`, `em_pickup_update(_owners)`, `em_pickup_scan_dist/_release` (em_examine no longer arbitrates against it), `em_pickup_ammo_take` and em_game's collect hunk, `em_game_player_interact_anim` and its lock (g.interact_*), `em_game_legacy_pickup_update/_collect`. Scenes without a roster keep their placed items drawn and never taken. **Verified:** `make test-pickup-items-reference` (new: 001C40B0, 001F1110, 001F1180 against the original instructions; full sweep 675 + 85,571 cases), test-pickup-original, test-pickup-lights, test-area11-interaction-host (callback counts 156/118/170/285 unchanged; new `other_take` for 0x0B04/0B07/0B08/0B09), the level smoke with the battery phase live (`check_battery`: the take row for row with route 01 from the scan f125 to the post, then from the post to the page load; the post is 61 rows after the scan in the port and 64 in the original because the op00 sub8 settle starts from the port's follow-camera target, WP-16, and both post two rows after the settle ends), newgame-control PASS (displacement 9.599989), compare_frame_order over the smoke's EM_FRAME_TRACE: idle04, walk04 and st03 PASS in every gameplay window between the status phase and the battery scan (native indices 1319..1627, the seven item nodes each at its own position), cut02, cut15 and st14 PASS; cut07 now differs only by the battery owner g0.0 and its light child, which its source save state 07 still holds while the smoke's session (like route 01, where the g0.0 record is reallocated by f516) has taken the battery and freed both: with a scratch allow file naming those two nodes it PASSes (58/58 events a frame); the committed allow list is unchanged. test-player-states-host's CB6 assertion now expects the known result (D_00810CB6 is canonical). Open: cue 0x194 has no exported sample (WP-14, reported and dropped like the status cues); the aura sprite draw; the four untranslated take pages; the items' collision cells (001A2370, 001B1D20) are not in the port's collision world.
- **WP-8 landed (2026-09-23; PARTIAL: the stream lanes are not bound and their binding is BLOCKED on a lead decision, below):** **Step F (main.c, em_frame.c):** `em_message_live_install` loads `assets/message/message_data.emmd` (new `tools/export_message_data.py`: the ELF's `D_00264DD0` global and area-11 tables, `D_0026EC60`, `D_0026EC10`, `D_00264CD0`/`D_00264BF0`; the disc's global and AREA11 banks) and installs the one `em_message_service` as the step-F frame service at bring-up; its tick is 001FCA10 over canonical area/3B8F/`D_008106F4`/`D_008106F5`/`D_008106D4[12]` bytes, and its render draws the frame's glyph passes. `em_opening_media_render` is gone from step F. **Draw:** the service's `draw_line` is the translated 001FD950 draw prefix (`em_message_draw_original`) with the newly translated 001FC7B0/001CC1E0/001CBE10/001CC3B0 (`em_message_glyph_original`, docs/MESSAGE_GLYPH.md); the packed passes are drawn through the tall-font atlas (`em_hud_glyph_strip`); `em_hud_subtitle` is deleted. **Routes:** the AREA11 host's panel/terminal message hooks post through 001B7D60 case 0 (`em_message_live_post`) and its frame view keeps `D_002821B4` in the live block; w_001B6990/w_001AFCA0/`em_game_shutdown` set and clear the host hooks (the face talk 001D06E0 and the gate that holds step F while the status page layer presents its own mode-4 lines, a port stand-in: the original 001FCA10 has no gate) instead of swapping the frame service; the opening's op0C runs `em_message_live_op0c`, its op12 phase 0 the stream request 001FD4C0 (`em_message_live_stream_request`) and phase 3 waits for `D_008106F4 == 1`, its op-4 and 001B6BF0 paths store `D_002821B4 = 2`, and its actors' talk is the activity bytes `D_008106D4[0/1]` consumed as 001BA580 does; `w_001FC9B0` is `em_message_live_reset` (UM_001FC9B0 deleted). **Streams:** `em_scene_bindings_001FD470` (bit 0 → w_001FBC50, bit 1 → w_001FABB0, the port's stream-release stand-in, not a translation of 001FABB0) and `em_scene_bindings_001FA790` (lane 0 with the opening row's cue → `em_opening_media_audio_start`, anything else faults); `em_opening_media` is the lane-0 stand-in and follows `D_008106F4` at step H through `em_bgm_set_lane_service` (2 → 1 at once, 0 starts the sound); no voice lane runs, so 001FAAC0 on lanes 1/2 is empty and 001FA5A0 faults. **Deleted:** `em_panel_message`, the `em_opening_dialogue_*` clock, `em_hud_subtitle`, the `.emod` exports (`tools/export_interaction_message.py`, the dialogue halves of the opening/panel/elevator/Roger exporters) and `tests/opening_media_export_test.py`; the service oracle's cross-check against the deleted clock. **Verified:** see FIRST_LEVEL_AUDIT.md WP-8. **Fix round (2026-09-23):** the host passes the 001B7D60 delay word through unchecked; the opening's 001B82D0 phase 0 makes its two 00119828(0/1, 0, 0) calls (reported no-effect, `em_scene_bindings_00119828`); 001F9820 is translated into `em_stream_lanes_original` (with 0011A4B8/0011A4E8/0011A658) and oracle-checked, not bound. **Open:** the stream lanes' binding needs an IOP stream backend at the 001157F0 boundary, its buffer allocator and 0011A2B0, the stream-file reader and `sub_O_STREAM_MUSIC_DAT_1`, 001FB100 at step H, and the replacement of every legacy stream path (STREAM_LANES.md "Still missing"); **lead decisions (2026-09-23):** (a) split — the stream lanes and the IOP stream backend are WP-8b (FIRST_LEVEL_AUDIT.md), WP-8 is done as the service/glyph/routing part; (b) keep the labelled step-F gate until the mode-3/4 presenters are translated; (c) the opening's mouth talk now starts/stops one frame later than the old port, which is the original order (001BA580 consumes in the task, 001FD950 writes at step F); also open: the mode-3/4 presenters and the director/Roger/door requests (WP-10/9/7).

- **HK landed (2026-09-23; housekeeping: canonical storage, scene no-shadow, asset steps):** **D2 migrations (em_scene_state.h):** D_00810707, D_00810792..0x794 (events 0x3A/0x3B next to S12a's 0x3C) and D_00810813 join the migrated ranges; D_00810CC3 and D_00810CB6 were already canonical (WP-6) and lose their last copies. `g.cine_step` is deleted: the legacy director stand-in em_director.c reads and writes D_00810813 in the progress region (its completions store 0x10/0x20/0xFF, as 008253F0 does), and em_game_legacy_state0 no longer resets it at every area build (the original has no such writer: only 001AF2C0's memset clears it; em_director_original.h's state 1 reads the persistent byte). `g.opening_key_item_zero` is deleted: 00823E80's 001C4760(0, 1) (jal at 0x823F84) runs the one 001C4760 translation, `em_director_original_001C4760`, on D_00810CC3[0] through the new canonical binding `em_director_original_001C4760_scene` (DIRECTOR_ORIGINAL.md section 6 named that translation for the opening; em_director_original.c joins COMMON and OPENING_TEST_SRC), which the legacy director's beat-0 completion now also calls (001C4760(1, 1), 0x8255CC..0x8255D4; it was a flagged no-op). D_00810707 gets its original writer: `em_player_0015BCF0` stores +0x234 (g.pd_infected) there as 0015CF90 does (src/func_0015CF90.c), before the B9 test, and w_001B07C0 reads the canonical byte instead of the live +0x234. D_00810792/D_00810793 have no live port reader or writer (em_truck_original, em_director_original and em_roger are unbound; their binders take the canonical pointers, WP-12/WP-10/WP-9). **D_008106F1 / D_00810707 unified (PLAYER_STAGE_WORKERS.md 2):** EmPlayerStageScene.d8106F1/d810CB6, EmPlayerStageGlobals.d810707, EmPlayerMajor2Scene.d8106F1/d810707 and EmPlayerRecoveryScene.d8106F1 are pointers at the one canonical byte (0021C270 sets D_008106F1 in the middle of a stage and 00224B80 / 0021C440 / the +4 = 2 states read it after that call, which a per-struct copy would have missed); the stage workers' readiness, em_player_stage_end (now returning int), the Major2 context and the recovery routines that read D_008106F1 refuse before any write without it. The live stage view (em_player.c live_scene_load) points them at `em_scene_req_at(0x008106F1)` / `em_scene_progress_at(0x00810CB6)`. **G6 (tools/test_scene_no_shadow.py) passes on the committed tree:** em_area_script.c is a listed 3B8D/3B91/3B92 writer (001B82D0's frame view store and sub 6 teardown, 001B6E40), em_player.c's per-stage view load of 3B8D is a VIEW_LOADS entry, the declaration check matches names as well as comments and covers the new bytes, the per-call inputs of the unbound player, camera and collision list-pass lanes and 001B07C0's EmSpawnIo, EmRogerStory's value view (removed by WP-9) and three beat-table constants are ALLOWED entries with their removal step, `cine_step`/`opening_key_item_zero` are retired names, and REACHERS lists the files that name each migrated address. **docs/STARTUP.md** has one ordered list of every local export step (section "Local assets", 43 steps plus the `--node-class` EMCL re-export that is installed when collision binds), each classed required / live / live-behaviour / not-read-yet (with the lane that makes it required) / not first-level; the classes were measured by removing each of the 94 files the headless New Game route reads, one at a time (a missing interaction-host, roster or spawn-table asset latches a fail-stop fault, but the newgame-level run then hangs instead of exiting: reported, not fixed here). **Verified:** make all has zero warnings; newgame-control PASS (1300, 9.599989) and its EM_FRAME_TRACE byte-identical to the pre-step build; the level smoke's tick log byte-identical to the pre-step build and test_level_smoke.py PASS (6 live phases); test-continue-reset-reference now also executes 001C4760 (90 cases) and the opening slice through its call against em_director_original_001C4760_scene (a mutant fails); the stage-workers reference gains 16 fail-stop checks (a host without the D_008106F1 or D_00810707 pointer); floor, stage-workers, pose-host, major2, closure-10-12-19 and recovery references pass with the pointer layouts; frame order is unchanged, so compare_frame_order was not re-run.
- **Census L01 landed (2026-09-23; PARTIAL: the takeover and the prelude on the port's idle/walk stay stand-ins, below):** **Stage (em_player.c):** `player_states_stage` is one original player stage over the record, run by actor_update whenever the new STAGE mechanism (`EM_PLAYER_MECH_STAGE`: the stage workers, 0011A070, major[4] and major[6]) is engaged: the vitals view load (+220/+224/+228/+22C/+234/+20E from g.status / g.pd_*; +204 from g.loco_rate with the port's other mirrored bytes), 0015BA50's begin, its switch (the display's advance, then the +4 handler: +4 = 1 is `live_major1` = the takeover stand-in, then 0015B130 with the port's idle/walk callbacks as state[0]/[1]), the hand-back after a translated routine, 0015BA50's end (3B8F stored back only when the stage changed it), 0015BCF0's tail, the vitals store. `player_states_stage_begin/_end` and `live_stage_dispatch` are gone; `player_move` is the port's callbacks alone. **Binder (new em_player_stage_live.c):** at w_001AFCA0 (001AF5C0's position) `player_states_reset()` then `em_player_stage_live_bind()`: the host over `player_states_scene()` and canonical views loaded before every stage (D_008106C8, D_00810701, D_0081083C, D_00810C7E, the D_00810707 pointer; area 8 room 2 refused for the non-canonical D_00810770), D_00248C98 from `assets/player_clip_rates.emcr` (a missing export faults at 0x0015BA50), advance = the pose host's `player_pose_stage_advance` (em_player_pose_advance; player_pose_stage is now its advance plus `player_pose_stage_hook`), sound = em_sfx_play_at, sound_stop = the new `em_sfx_stop_track`, major[4] = 0015B530 (001837A0 bound, the other six fail-stop), major[6] = 0015D460 over em_frame_fade_start_colour, takeover = `player_pose_stage_hook`, and fail-stop workers for every callee without a live translation (PLAYER_STAGE_WORKERS.md section 2.1). **Retired:** em_player_damage.c's 0021C440 copy (with the 0021C350/0021C270 appliers and the flinch entry), its 0015D100 drain, the +20E countdown and the kill plane, `player_damage_tick`, and the constants and fields only they used. **D2:** D_0081083C (the grab-slot bits) is canonical (no port writer); G6 lists it. **Open:** the takeover (acquire / +4 = 4 tick / release) stays the interaction runtime's; under 0x70003B8D without that owner the port's idle/walk keep the stage (the prelude's 00174A50 needs 0017B490, L12); the canonical B3 byte stays the stand-in expression (0015BA50's busy is computed into the stage view only); 0015CF90 has no oracle; census: 10 of L01's 15 rows live (0015BCF0 tail only), 00182B30 / 00182D70 / 0015B530 / 001837A0 bound but unreached behind the takeover stand-in (verified-unbound), 0015CF90 unverified; outside AREA11 a port enemy hit now faults in the unbound +4 = 2 states (off the route, restored by L02). **Verified:** make all with zero warnings; newgame-control PASS (1300, 9.599989) with its EM_FRAME_TRACE byte-identical to the pre-L01 build (frame order unchanged, so compare_frame_order was not re-run); the level smoke PASS with its tick log byte-identical to the pre-L01 build; `tests/player_states_host_test.c` asserts the original 0015B130-around-idle order (the old assertion that the port's idle skips 0015B130 encoded non-original behaviour) and adds the takeover, prelude-gate and vitals cases; G6 passes. **Test retirement (rule 2, a mechanism no longer on the live path):** `tools/test_player_random_reference.py` loses its flinch-entry cases (the port's copies of 0021D800 case 0 and 0021D1A0, deleted with the 0021C440 copy); its footstep cases (00179B90, 00182430) stay. **Pre-existing failures, not from this step:** test-director-original, test-effect-kinds-reference, test-load-veil-particles-reference, test-main-loop-and-gap-reference, test-player-equipment-reference, test-script-host-workers-reference and test-shadow-original-reference assert 15 route beats or a beat-15 difference: they fail on the new `15_level_exit` capture (decomp commit 37a645d), not on port code.
- **Census L05..L08 landed (2026-09-24; PARTIAL: L05 BLOCKED on two untranslated workers, below):** **World (new em_collision_world.{h,c}):** one original collision world per area (AREA11 only), built at w_001AFCA0 before the player stage binds: the cell directory *0x70003250 (`area11_cells.bin`, a required export now), the rank view of the EMCL (the installed file carries the node class and the rank section, flags 7; STARTUP.md step 13), the walkers' one scratchpad state (`EmCollProbeState` + `EmCollSegmentFaceScratch`, zeroed at every build), the SDK context (the user's export, with D_0026C5D0 from its window; the export reader is now `em_sdk_math_original_load_export`, shared with the status background) and the class lists D_00275B54..BB8, reset at 001AF8E0's list half. **001AAD00:** `w_001AAD00` (both variants, roster scenes) runs `em_collision_world_close_out_001AAD00`: the nine list passes (`em_coll_list_passes_001AAD00_hooks` over the live lists; record memory, D_0024A740 and the 0x00823580 behaviour are fail-stop, D_0081070A 0 (read only behind the fail-stop behaviour); 0x70003B86/88 one storage with the scratchpad state) and then the list block. Its interactive list is the one store the host's Use scan and device lookup read (`published_view`); the host's own list swap (`em_area11_interaction_host_publish`) and the scene's `em_interaction_scene_offer` / `_publish` (a duplicate, incomplete 001B17A0) are deleted. **Owners:** the panel, the terminal and the items publish through the one `em_owner_services_001B17A0`, whose 001B1B70 pushes the owner's pool record (bound by its node's first call, `em_area11_interaction_host_bind_actor`; its +0x02 stored from the owner's class byte); the terminal re-transforms its cell (001A2370 over its 001C6380 matrix) at state 0 (0x827C04) and at the ride's completion (0x827E54: the new `EmElevatorHooks.retransform`, oracle-checked in test_elevator_reference's call order, where the carry 00828050 rebuilds only the matrix), and 00219550 at its state 0 (before its child spawn, which the node now issues after the host's state 0, as the original orders them). **Queries:** the scripted retarget's 0018D330 / 0018D910 queries (em_camera.c `interaction_camera_query`) are 0019A910(6) / 0019B7D0 over the world, and the items' 00183EF0 ray is 0019A910(6) with the item's pool record as its identity; scenes without an original world keep em_collision.c. The FLOOR mechanism's collision workers (0019AB20 ground, the flags-7 grid, 0019B6C0 / 0019B8C0 with 001A50A0 / 001A5C30, 00175640, 0019BC40 with the world's SDK sqrt/atan) are bound by `em_collision_world_bind_player`; FLOOR stays gated (SDK set, display, closure callbacks). **Blocked (L05):** the player's move/sweep probes (0019AD00 / 0019AFE0, em_coll_move_original) cannot bind: their grid pass 0019CB60 and hull lock 001A6440 have no translation (the census had them verified-unbound because the oracle hooks the originals; corrected to missing), so em_player.c keeps `em_collision_move_probe` and the fence door's `em_door_probe`. **Open:** the crates, drums, truck, prop 001C4820 and 0x825940 do not publish their cells (L25/L23/L35/L24), so the original walkers do not see them (the port's own walker never saw the crates either); the follow camera (L13) and the legacy weapon/enemy queries keep em_collision.c; em_actor_collision.c's query half (0019AB20 / 0019BC40, the FLOOR ground and column workers, and the original of 001764E0's 001760C0 column, which keeps em_collision.c) stays out of the live path: its prim tests are a second copy of em_coll_probe_original's 001A4030 (live under 0019A910), 001A4650 and 001A44B0 on the truncating float helpers, so they must be reduced to one and the module harmonized with its oracle first (EE_FLOAT_MODEL.md 5c; plus 0019C830's KNOWN INEXACT node order); D_0024A740 is not exported. **Verified:** make all with zero warnings; newgame-control PASS (1300, 9.599989) with its EM_FRAME_TRACE byte-identical to the pre-step build; the level smoke PASS with its tick log byte-identical to the pre-step build (its frame trace differs only in the armed battery record's class byte, now 0x87 as 00219550 writes it; compare_frame_order gives the same verdicts as the pre-step trace: idle04/walk04/st03 PASS on gameplay windows, cut02/cut15/st14 PASS, cut07 the known battery-owner difference); the new `make test-collision-world-capture` (the live cell directory equals route captures 00 and 04 byte for byte in uids 4, 19, 21..25, and the last frame's published class-4 list is beat 04's for the ported owners, in order; a negative control without the completion's 001A2370 fails); test-camera-interaction-fixture runs over the captured scenes' own cell directory and published list (the refusal overhead is exact now; its assertion is tightened from one ULP to equality); test-elevator-reference records 001A2370 in the call order; test-area11-interaction-host (callback counts 156/118/170/285 unchanged); G6 passes (the list passes' 3B8D view is now filled from em_scene_state()). **Test retirement (rule 2):** tests/interaction_scene_test.c loses its `em_interaction_scene_offer` assertions with the deleted function (its list comes from `em_interaction_list_push` / `_publish` now); the live 001B17A0 is covered by test_owner_services_reference. **Pre-existing failures, not from this step:** the seven beat-15 failures recorded under L01 (test-director-original, test-effect-kinds-reference, test-load-veil-particles-reference, test-main-loop-and-gap-reference, test-player-equipment-reference, test-script-host-workers-reference, test-shadow-original-reference).
- **Census L02 + L25 (2026-09-24; BLOCKED, nothing live changed):** the step could not bind FLOOR or the crate/drum owners faithfully. **FLOOR** needs, beyond its bound collision workers: the display (every closure state requests clips on its first frames; the live pose bank `assets/player_channels.empc` holds 14 clips, none of the FLOOR ones, `em_pose_bank.c` refuses the chained 0x73 / 0x5E, and the record-level 001749A0 / 001749F0 / 001C61D0 / anim_eval_skeleton must first get one owner, em_player_pose or em_pose_host_workers: display lane L12/L33); the SDK atan2f / sqrtf (met since the soft-float step, below); and every closure callback with its workers (about 450 slots over em_player_fall / _hang / _recovery / _ladder_climb / _closure_0e_18 / _closure_10_12_19 / _weapon_states_a / _b / _major2 and the reaction / slide adapters; the duplicate translations and the record-level 00174AC0 are settled by the one-owner step, below). **Crates/drums** need an AREA11 world model bank for 001B0EA0 (001C6120 / 001CA6E0 / 001AF780 / 001CB5B0), a decision on the 001CAA00 draw (001CA7B0 / 001CA940 / 001D1F80), the harmonized 0019AB20 query half, the split of the legacy enemy group with L24, and exports of D_002468B0 / D_00246A00 / D_00246A10 (CRATES_DRUMS_ORIGINAL.md "Status"). **Done:** `em_collision_world_bind_player` binds 00175CF0's tanf 0011E398 and atanf 0011DBB8 (`em_sdk_math_original_float_*` over the world's SDK context) into the gated FLOOR; the new `EmPlayerStatesBinding.sdk_fault` (the context's fault word) is cleared before and tested after every floor service and fall check, and the world's column wrapper keeps an earlier fault of the same service; the slot mislabelled `cosine` is `tangent` in em_player_floor.h / em_player.h, and test_player_floor_reference's hook of 0x11E398 is host tanf on both sides (it was host cosf); the FLOOR report names each missing SDK worker. **Verified:** make all with zero warnings; all 216 make test-* targets: 209 pass, and the 7 that fail are the pre-existing beat-15 failures recorded under L01 (test-director-original, test-effect-kinds-reference, test-load-veil-particles-reference, test-main-loop-and-gap-reference, test-player-equipment-reference, test-script-host-workers-reference, test-shadow-original-reference); test-player-floor-reference passes with the tanf hook; newgame-control PASS (9.599989); the level smoke PASS through elevator with its tick log byte-identical to the pre-step build (frame order unaffected, so compare_frame_order was not re-run); an isolated HEAD + step build has zero warnings; tests/player_states_host_test.c case 0b (the latch is cleared before a call and fails a call whose worker recorded a fault).
- **SDK soft float bound (2026-09-24; atan2f/sqrtf allowed, nothing live changed):** **Binding (em_collision_world.c):** the collision world's one SDK context `w.math` now has the four soft-float workers 00128350 / 0011DB90 / 0011FD78 / 00127758 (`em_sdk_soft_float_bind`). Their data comes from the new local export `assets/sdk_soft_float.emsf`: D_0024295C and the errno word it names, 0x00242670 and 0. `tools/export_sdk_math_tables.py` writes it from the user's ELF, and `em_sdk_soft_float_load_export` reads it. It is loaded once and kept across area builds, because the original initialises it only in the ELF image. A missing export faults the AREA11 build at 0x001AFCA0. So 0011E620 atan2f and 0011E748 sqrtf are complete, and `em_collision_world_bind_player` binds the gated FLOOR's `.atan2` / `.sqrt` to `_float_0011E620` / `_float_0011E748`. **Gate:** SDK_MATH_ORIGINAL.md 7 is lifted under option 1. The route census never hit 0011E420 / 0011E520 and hit the workers in beats 03 and 05, so the route's EDOM comes from 0011E620 or 0011E748; the call site is still unidentified (SDK_SOFT_FLOAT.md 5). No live caller reaches those tails yet: the column's sqrt argument is never negative, and FLOOR is gated. **Evidence:** test_sdk_soft_float_reference part 9 checks the export against the ELF and `playable_ee.bin`, checks the loader, and runs the route-RAM zero-vector / negative-root cases over a context built from the loaded export, which equal the original (quick 27,324; full 3,513,304 cases). tests/sdk_soft_float_test.c covers the loader's rejections. **Verified:** make all has zero warnings (private lane, and an isolated HEAD + FLOOR + step tree); all 216 make test-* targets pass; newgame-control PASS (9.599989); the level smoke PASS through elevator, and its tick log and smoke lines are byte-identical to the pre-step build. Frame order is unaffected, so compare_frame_order was not re-run.
- **One owner per original (2026-09-24; nothing live changed):** the closure modules' duplicate translations are reduced to one bound owner each. **em_player_fall** owns 0021D250, 0021D2E0 (its copy builds the 001EFD90 point in the scratchpad word 0x700038A0, as the original does) and 00179880 (`em_player_fall_00179880(p, at)`); the reaction lane runs them through a bridge over its workers (`em_player_reaction_0021D250` / `_0021D2E0`, so major2's `w0021D250` / `w0021D2E0` reach the same code), and its drop tails and the running jump's p+2E4 drop call the owner. `EmPlayerReactionWorkers` gained `scratch`, the fall lane's one `EmPlayerLandScratch`, and the reaction routines now store 0x70003A20 as the original does (002202C0, 001754E0, 0021D1A0). **em_player_ladder_climb** owns 0017FC80 (with 001885D0 / 001885F0), 00180420 and 00174AB0 (new export `em_player_ladder_climb_00174AB0`); **em_player_ladder_entry** owns 00180300 (new narrow entry `em_player_ladder_probe_00180300`). The closure states run 00180420 / 00180300 / 00174AB0 and the ladder entry runs 0017FC80 through one-routine owner contexts over their own workers, copying the scratchpad view back before every worker call; the ladder entry's `clip_001885D0` / `clip_001885F0` workers are gone. **00174AC0:** every closure module doc names `em_player_heading_record_worker(_result)` as its `heading` worker; test_locomotion_display_reference binds it as `EmLocoWorkers.heading` in its captured-image cases (001612D0 with the stick held; RAM and scratchpad, 0x70003A20 included, compared) and test_player_fall_reference into `EmPlayerLandWorkers.heading` (0017C580 / 00162DB0 / 00163B40), each against the original with 00174AC0 executing as original code. The live turn (em_player.c, em_player_heading.c, em_player_reversal.c) stays on mirrors until 001612D0 runs over the record. **Verified:** the owners' and callers' oracles execute the originals over the bridges (fall, reaction with the scratchpad now compared, running jump, closure 0E/18, ladder climb, ladder entry with D_002754D0 at its ELF value), each also in its EM_TEST_FULL=1 sweep; the game binary is byte-identical to the pre-step build (no changed module is in COMMON); make all with zero warnings; all 220 make test-* targets pass; newgame-control PASS (1300, 9.599989); the level smoke PASS through elevator (6 live phases; boxes onward NOT-LIVE as before; frame order unaffected, so compare_frame_order was not re-run). See FIRST_LEVEL_CENSUS.md rows 00174AB0 / 00174AC0 / 00179880 / 0017FC80 / 00180300 / 00180420. **Still more than one copy (other steps):** 001B1470 (reaction, recovery, fan, effect, stage workers, and the live host models), the SDK VU0 leaves, em_player_slide.c's inline 00179880 on its mirror actor, and the camera's legacy solvers (census L14).
- **Player display: one pose owner (2026-09-24; live):** **Owner:** the player's clip clock, node channels and skeleton live in the player record, worked by `em_pose_host_workers` (001749A0, 001749F0, 001C61D0, 001C63E0, 001C67E0 and the channel routines) and `em_player_stage_anim_advance` (001C64F0 with its chain step): new `em_player_record_pose.{h,c}` holds the storage (the raw bank `assets/player_clips_full.bank`, all 459 clips, at 0xD689C0; 21 node records at 0x7D5840..; the record mapped at 0x8102B0; D_00248C90's +0 column, new `assets/player_clip_row0.emch` from `tools/export_player_tables.py`; the globals with D_008106F3 at the canonical byte and 0x70003A20 at the stage host's word) and translates 0015BCF0's animate step (001C6DA0 / 001C68C0 / 001C6960 by +2F3, +303 and the row). **Host:** `em_player_pose_host.c` keeps its API over the record (frame-0 requests are 001749A0, source-frame requests 001749F0, releases 00182DF0's with the row column, the foot-stop begin 0017B910's anim_eval_skeleton) and publishes world palettes (the node matrices, +D0 in the model's trailing slot) without `palette_apply_placement`; `player_pose_load(bank, row0)` and `player_pose_attach` (0015C420's pose half at w_001AFCA0) replace the EMPC load. **Stage:** em_player.c drops the +20C mirror and runs the animate step after every stage; em_player_frame.c displays the record for takeover and translated-state stages (`player_states_record_display`); a non-idle/walk stage advances the record whatever a stand-in holds; em_player_stage_live.c binds the stage's clip workers to the record's host and declares `player_states_bind_display(1)`. `em_player_pose` stays for Roger, the status models and the unreached cinematic bank; `em_pose_chain` stays unbound. **Verified:** all 225 make test-* targets pass (the new `test-player-record-pose-reference`: the live module against the original over the captured records, every first-level clip's first frames and both chains into 0x5F / 0x72, the captured skeletons re-evaluated byte for byte; quick 4 images / 374 callbacks, EM_TEST_FULL=1 16 images / 1,968 cases / 126,720 callbacks, all exact); the first-control pose trace matches the original on all 56 callbacks; newgame-control PASS (1300, 9.599989) with its EM_FRAME_TRACE byte-identical to the pre-step build (compare_frame_order results unchanged); the level smoke PASS through elevator (6 live phases), its tick log differing from the pre-step build only by float ulps (at most 1.4e-4 in position) in ticks 1572..2187, from one foot-stop begin whose feet are now anim_eval_skeleton on the record; the takeover/foot-stop palettes differ from the old host composition by at most 9.2e-5 (14 clips x 40 frames); make all with zero warnings, also in an isolated HEAD + step tree. Retirements: none; `tests/player_states_host_test.c` now seeds +20C in its setup (the record owns it) and asserts `player_states_record_display`; `test_player_pose_host_reference.py` asserts that row 1's clip 0x0A is in the bank (the host still holds at low health: the +235 latch is not ported); `test_player_cinematic_reference.py` asserts that the release publishes the record's palette with no host placement.

**After S13, WP-4…WP-12 each replace one binding row and delete the matching legacy code:**
- WP-4: grate, elevator_tick and the examine terminal (landed 2026-09-23)
- WP-5: em_hud pages, Found, and the status_runtime shadow (landed 2026-09-23: pages, Found, the hover cue and the interim hub deleted in AREA11; the fixtures' runtime-frame path remains)
- WP-6: pickup_trigger_scan and the countdown (landed 2026-09-23, with the flat inventory add, the ammo queue and the interact-clip lock)
- WP-7: em_door walk/goto
- WP-8: em_panel_message, the opening's dialogue clock and em_hud_subtitle (landed 2026-09-23; the stream lanes remain unbound)
- WP-9: Roger
- WP-10: kCineBeats
- WP-11: fan and the exit
- WP-12: truck

A legacy *module* is deleted only when no roster-less scene or `EM_*_TEST` uses it. That is a user decision, because run_suite.sh is theirs. **WP-2 (H12) must land before WP-4.**

---

## 7. Risks

1. **One-tick shift (S9).** Every frame index moves. Re-baseline against the trace; never against the old port.
2. **Cutscene player stage.** The original runs 0015BCF0 in cutscenes, and the port never has. Binding the full path early could double-drive the verified opening, so the interim binding keeps the opening path.
3. **Aggregate adapters** (pickups on #0, enemies on #7, indicators on #39) approximate the interleave. The census guards contiguity, and the trace allow-list names each one.
4. **Migrating shadows.** These must each be deleted in the step that moves them:
   - g.frame_selector (deleted in S11a)
   - em_hud s_menu_inhibit (deleted in S11b: B3 is canonical; interim writer at the player stage)
   - em_door locks
   - em_status_runtime frame/queued
   - the host's recovery_lock versus EF
   - em_opening_runtime.c `s.cinematic_ready`, the private stand-in for spad 3B92 (deleted in S11b per D5, with the phase-0 `sh 3B84=0` at 0x1B8610 added and 3B92 in test_scene_no_shadow's WRITERS/BYTE_TOKEN)
   - g.cine_step (D_00810813) and g.opening_key_item_zero (D_00810CC3[0]) (deleted in HK), and the stage/Major2/recovery copies of D_008106F1 / D_00810707 (pointers since HK)
   - EmRogerStory (D_00810791/793/813, D_008107D8), a value view of the unbound Roger translation: its binder must point it at the progress bytes (WP-9; an ALLOWED entry in G6 until then)
5. **Oracle ISA gaps.** The variants and 001AFD70 use paddub moves and sq/lq, plus scratchpad addresses. Extend the base Oracle and validate it on 001AE7E0 first.
6. **Input latency.** The original pressed edge reaches 0x810E70/74 two frames after pad_set (pcsx2_session). An off-by-N shows up around menu opens and skips.
7. **em_game.c contention.** WP-1 (truck, director, Continue) and WP-2 also edit em_game.c. S5 must land first, and S8–S12 must be serialized with those lanes.
8. **Trace coverage.** Overlay behaviours are opaque except at traced main-ELF sites. Per-node comparison matches on callback and record, never on node address, because LIFO slot reuse makes addresses unstable.

## 8. Adjacent, not in WP-3 (measured)

- Main loop: the port lacks step H 001FB100 and step I 001B5B70 (the rumble countdown, which runs every frame). Assign these to WP-14/WP-17.
- 001D1C50 calls 0021B9A0 (the rumble channel) only when the selector is 0.
- Camera latency: 001D1C50 seeds the display list before 0018B9C0. Whether the world draw uses the previous frame's camera belongs to WP-13/WP-16.

## 9. Open questions (with the evidence that settles each)

- **Q1. DECIDED (lead, 2026-09-22): withhold.** Until 0022A650 is ported, the input translation clears
  SELECT (0x100) from the E74 word the classifier reads and logs one "unported: 0022A650 (SELECT)" line;
  E50 is written as 4 (analog DualShock) per S11a. Faulting would end the session on a single button
  press; withholding adds no invented behaviour (the button is inert and says so). Remove the mask in
  the step that ports 0022A650.
- **Q2.** Where the player's final palette is produced: 0015BCF0, or player+0x4C = 001CAA00 via 0015C160 after the walk. Settle by reading 001CAA00 → 001CA990 writes, and by a palette-write watchpoint against 001AFD70.
- **Q3.** Record 13: which call frees it, and when. Settle with a breakpoint on 001AFC10 with a0=0x7A96E0 from state 0 until the opening spawns.
- **Q4.** The spawners of 001E55F0, 001E2560 and 001EA240 (the table near 0x24CCC8). Settle with a breakpoint on 001AFA90 returning those nodes during state 0 and the first frames.
- **Q5.** The identity and effect of 0018A6B0 ×7 (read 0018A880 and 0015C420) and of 001E2560 (its label "turret AI" is unverified).
- **Q6.** Who sets 3B93 (there is no main-ELF byte store) and 3B92. Settle with a write watchpoint in the captures.
- **Q7.** Which 3B8D writer sets 2 during status (st14), and whether state 5 returns into the cutscene variant.
- **Q8.** Whether D_00810E50 is 4 for a DualShock in analog mode. Settle from a capture of 0x810E50 and a reading of 001B57E0/001B5940.

**Label corrections, for the lead to apply:**
- FINDINGS "actor registry" has the 001AFD70 modes swapped.
- FINDINGS "ENGINE FRAME ANATOMY": 001AFCF0 is not per-frame; 001AE7E0 is the frame classifier.
- FINDINGS s54: Start/Triangle are swapped.
- FINDINGS "one live actor": AREA11 has 49–52.
- The src/func_001B07C0.c header: it is spawn placement.
- ORIGINAL_FRAME_ORDER #30: it is the area title.
- em_game.h "0x1AE040 still undecompiled".
- em_game.c: the case-0 and :5523 comments.

---

## 10. Phase 1 landed (2026-09-22) — facts Phase 2 must use

Committed: S1 `em_scene_state.h`, `em_scene_workers.h`, `em_scene_classify.{h,c}`; S2 `em_scene_frame.{h,c}`;
S3 `em_scene_task.{h,c}`; S4 `em_actor_pool.{h,c}`; S6 `em_frame_trace.{h,c}` + `tools/compare_frame_order.py`;
S7 `em_actor_roster.{h,c}` + `tools/export_area11_roster.py`; S5 split (`em_player_frame.c`, `em_render_frame.c`,
`em_game_selftest.c`, now in COMMON). Only the S5 files are in COMMON; S8/S10b add the cores when they wire them.
Make targets: test-scene-classify(-reference), test-scene-frame-reference, test-scene-task-reference,
test-actor-pool, test-frame-trace, test-actor-census.

### 10.1 Interfaces as built (supersede section 3 where they differ)
- `em_sf_001AE7E0(const EmSceneState *, int16_t d0028A9A0)`: the fade halfword is read through a worker and
  passed in (D_0028A9A0 is not owned). Task bytes +8..+0x1F stay in `EmTask.user`; `em_scene_state.h` gives
  original-offset accessors over `user` (no second copy).
- Extra workers beyond section 3.2: stores `s_00821058`, `s_00275C78` (001AD360 step 1), `s_00810D38`
  (001ADF00); readers `r_00275B44`, `r_008102B9` (variant arguments).
- Q1 entry point: `em_sf_001AE040_q1` (bindings choose it and log `EM_SCENE_Q1_UNPORTED_MESSAGE` once).
- States 3/5 delegate to `em_status_frame` through a published/refreshed view.
- Task cores: 001ADF00 and 001AFCF0 take `(s, w)`; the rest `(s, user, w)`. 001AD360/001ADF50 return the
  original 0/4; others 0 or -1 on fault. Bind `w_001AD140`, `w_001AD010`, `w_001AFCF0` to the S3 cores.
- 001AD360 steps (from the .s): 0: 001D1EF0, 001FABB0, +A++ · 1: 001D1EF0; when D_00282157==0:
  D_00275C78=0 then D_00821058=1, +A++ · 2: 001D1EF0, +A++ · 3: +A++, +0x18(u16)=0, +0x10=0, falls into 4
  (+A 3->5 in one tick) · 4: 001D2830(3,1), 700=0x0B, 701=702=0, D_00810730[0x0B]=0, +A++ · 5: 001D1EF0,
  return 4 · +A>=6: return 0.
- Pool: calls take `EmSceneState*`; the walk writes spad 3B8A and latches faults into `scene->fault`. The walk
  trace hook is `EmActorTraceFn(ctx, caller, callee, actor_address, actor)` — S8/S10b need a small adapter
  into the frame-trace stream. The pool does NOT reset the 001AF8E0 class-list block D_00275B54..BB8: since
  census L07 the collision world resets it at the 001AFCA0 position (`em_collision_world_lists_reset_001AF8E0`). `EmActor` still lacks `flags2` (+0x2E,
  written by 001B6660/001B6990, untouched by alloc/free) — add it before S10b and store it on spawn.
- Trace contract (S6): core-internal jals 001ACEC0->001AD250 (0x1ACFF4) and 0x1AE040->001AE7E0 (0x1AE154) go
  through the trace hook; bindings set `w->trace = em_frame_trace_env_hook` when `em_frame_trace_env()` is
  non-NULL, call `em_frame_trace_tick_begin/end` around each task tick, `em_frame_trace_frame_state` at the
  0x1AE040 entry, `em_frame_trace_classifier` once per classifier run, and `em_frame_trace_node(callback,
  class, record, binding)` once per ticked node with the tracer's record tags ("area11[i]", "deferred[gG.J]").
- S5 stubs: `em_player_0015C160` and `em_render_001AAD00` have no port code and fault; the cutscene camera
  stage sits behind `em_opening_runtime_camera()`; damage/vitals and the death latch remain in the pool block
  (moving them is S10a/S11b). S10a left them in the 001AFD70 legacy block (`em_game_legacy_pool_gameplay`), after
  the player stage; S11b moved them into w_0015BCF0 (`em_player_0015BCF0`) with the B9 write of 0015CF90.

### 10.2 Open questions settled by the second PCSX2 trace (ORIGINAL_FRAME_ORDER.md section 6)
- **Q2** The player's final palette is produced inside 0015BCF0 (anim_eval_skeleton at 0x15BD64 in gameplay,
  001C6960 at 0x15BDBC in cutscenes; tail copies to 3B40/3B50). 0015C160 -> 001CAA00 only draws.
- **Q3** Record 13 (overlay 0x8257A0) frees itself via 001AFC10 on the second world frame after load; the
  slot 0x7A96E0 is later reused by 001BAC00's opening actor 001BB0E0.
- **Q4** 001E55F0, 001E2560 and 001EA240 all come through the effect allocator 001EF9D0: 001E55F0 from
  001C1DC0 (state 4) -> 001C1EA0 -> 001EFD20 (weather type 0x17); 001E2560 from 0015C420 -> 001F0120 (player)
  and from 001BA8E0 (Roger, opening actor); 001EA240 = footstep effect type 0x28 from 00187350 ->
  00187EE0 -> 001EFD90, about every 23 frames while walking.
- **Q5** 0018A6B0 x7 are the player's attached equipment models (0015C420: 0018A880(4,0) at player+0x18;
  0015C310: (0,0) at +0x20, (1,0), (1,0x10), (2,CA5), (2,CA6), (2,CA7)); each tick rewrites one bone's world
  matrix at a player bone and draws via 001CAA00. 001E2560 is a periodic head-bone sprite effect (60-99 tick
  countdown, +0x244 ramp 0->1.5 at 0.02/tick, GIF via 001CFBE0), ending on owner death/+4>=2 — not turret AI.
- **Q6** 3B92=1 and 3B91=1 are written by 001B82D0 (0x1B874C/0x1B8778) reached from the opening controller
  0x823E80 via 001BA1F0; cleared at script end (0x1B8940) and by 001AFCF0 at load. 3B93 is never nonzero in
  AREA11 (only AREA21.BIN stores it).
- **Q7 (partial)** No 3B8D writer runs during status state 3; the 2 is inherited from the panel battery
  script. Close: state 3 -> +B=5 -> one state-5 frame with no world variant -> state 1, which runs 001AE6B0 while
  3B8D=2 (14 frames until the panel script clears it). A status opened from gameplay keeps 3B8D=0.
- **Q8** E50=4 (pad initialised; analog DualShock ID 7) in every capture — the port's E50=4 is correct.
- Also settled: AREA11 has no passive hazard drain (D_008106C8=0x20081910, &0x60=0); 001FAE70 picks cue 25
  (29 during Roger, 63 in the opening), fade 270+((LCG>>16)&0x7F). Unsettled: R09 original clear colour.

### 10.3 Lead decisions
- **D4 (lead, 2026-09-23): S11a lands together with S11b.** No temporary withhold of E74 0x0810:
  a mask would be a non-original stand-in. The combined step binds the status arm (w_0020E060,
  w_0020CDC0 via the interim em_hud adapter until WP-5), B9 -> 001AD140 -> w_001AD4E0 for game over,
  clears the classifier shadow flag and the `*frame_state != 1` legacy guard, and is gated by the
  Start-press trace against st14.
- **D5 (lead, 2026-09-23):** the combined S11a+S11b step also makes spad 3B92 canonical: remove the
  `s.cinematic_ready` stand-in and add the phase-0 `3B84=0` write in em_opening_runtime.c, and add 3B92 to
  WRITERS/BYTE_TOKEN in tools/test_scene_no_shadow.py.
- **WP-5 decisions (lead, 2026-09-23):** (a) the status-model workers are translated, not left inert
  (inert would hide the shared-RNG draw 0020E6F0 -> 0020EC80 -> 001F4BF0 -> rand 00122BB8 when
  D_008104E4 == 1); em_status_scene_original is integrated without its duplicate 0020A7A0, and
  em_status_background stays the one D_002655A0 owner. (b) The hub models are drawn through the rig
  path from a local export of the letter bank D_0028A56C, and em_gfx gets an ordered 2D layer so
  0020A7A0's sprites draw before the models and 00209DF0's layer after them. (c) em_sdk_math_original
  lands with WP-5 (its targets and the sdk_math_tables export in STARTUP.md). (d) The module-load
  wait stays open until a PCSX2 I/O probe measures it; H7 stays PARTIAL.
- **D2 (2026-09-22): canonical game-progress storage.** The whole 0x640-byte block at D_00810700 that
  001AF2C0 resets (area bytes, D_00810730 table, D_00810758 slot table, D_00810778/788, the D_00810860
  per-area bits, D_00810B40, opening-complete D_00810811, power bits D_00810841, inventory D_00810C60..) gets
  ONE canonical owner inside `EmSceneState` (an `EmProgress` region addressed by original offset). Existing
  port mirrors (`g.opening_complete`, em_pickup counts/taken bits, `terminal_powered`, weapon ammo) become
  accessors over it in the step that first touches them; no step may add a second copy.
- **D3:** bind the seven 0018A6B0 nodes to the player's attached-equipment draw (Q5) rather than leaving them
  UNBOUND; the legacy weapon code keeps its position inside the player stage until WP-15 replaces it.
