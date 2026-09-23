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
| #0–5, #6 | pickups 00219550 ×6, 0015AFA0 | one group adapter on #0: legacy `em_pickup_update` (Found line included); the other nodes no-op, marked `group:#0`. The census asserts #0–6 are contiguous. | WP-6: `em_pickup_original_tick` per uid; take writes B0/B1 |
| #7, #8 | 00825940, 00827490 | group adapter on #7: legacy `em_enemy_update` (husk, crates and drums; interleave approximate) | WP-18: per node |
| #9 | door 001BC350 (r0) | legacy `em_door_update`; goto re-expressed as B5..B8 + fade (S12b) | WP-7: `em_door_original_runtime_tick`; 001BC150 → B7, B8=2 |
| #10–11 | fans 00827630 (r1/r2) | static (WP-1 stops the spin) | WP-11 |
| #12–15 | crates 001551B0 | covered by #7's group | WP-18 |
| #16 | flame 008235F0 (r7) | `em_area11_effect_runtime_tick` | — |
| #17 | Roger 008237E0 (r8) | UNBOUND (drawn statically as today) | WP-9: `em_roger_runtime_tick(rt, player, 1)` |
| #18 | equipment 001C5C90 (r9) | UNBOUND | WP-9 |
| #19 | opening controller 00823E80 (r10) | `em_opening_runtime_tick`; its camera is `em_opening_runtime_camera()` at the 0018B9C0 stage | WP-10 unifies it with em_script |
| #20 | manager 00823CE0 (r11) | dormant no-op, traced (it waits on D_00810788) | — |
| #21 | manager 008253F0 (r12) | legacy `director_tick` | WP-10 |
| — | manager 008257A0 (r13) | UNBOUND (see §4.2) | overlay oracle |
| #22–23 | drums 00156620 | covered by #7's group | WP-18 |
| #24, #25 | truck 00823FF0, trigger 008251E0 | legacy `em_truck_update` on #24 (static after WP-1) | WP-12 |
| #26 | panel 00159210 (r18) | legacy `grate_update` | WP-4: `em_area11_interaction_host_panel_tick` |
| #27 | terminal 00827B10 (r19) | legacy `em_examine` terminal and `elevator_tick` | WP-4: `em_area11_interaction_host_elevator_tick` |
| #28 | prop 001C4820 (r20) | render-only | — |
| #29 | 001E55F0 | `em_weather` via `em_snow_runtime_tick` | — |
| #30 | 001C5930 | legacy em_hud area title | area-title translation (0x12C ticks, 3B8D suppression, B8→state 3) |
| #31–37 | 0018A6B0 | UNBOUND | identify first (0018A880/0015C420) |
| #38, #46 | 001E2560 | UNBOUND | identify first |
| #39–45, #47, #48 | 001C5680, 001C5760 | group adapter on #39: `em_props_indicators_tick` and the pickup lights | per child (WP-6) |

**Non-roster scenes** (office and drawbridge, used by every `EM_*_TEST` and by `tests/run_suite.sh`) keep one `legacy_world`
node. It holds today's exact legacy call order, so their outputs are unchanged.

### 4.5 Non-pool positions (stage workers)

| Worker | Binding |
|---|---|
| w_0015BCF0 | `em_player_0015BCF0`: actor_context/actor_update/pose finish, damage/vitals, and the B9 write per 0015CF90. In cutscenes it is bound to the current opening-player path until WP-9/WP-15. |
| w_001D1C50 | point_light_tick, fog apply, GS setup. The port's name `render_chain_build` for this is a wrong label. |
| w_001C1D00 | render_env_init (area 0x1500 GIF arm, 001E0CF0, 001D5370) |
| w_0015C160 | player post-step: palette/+0x4C. Open question Q2: where player_pose_finish_palette belongs. |
| w_001F0360 | FX managers |
| w_0018B9C0 | camera_update and em_sfx_listener |
| w_001AAD00 | `em_interaction_scene_publish` plus the class-list swap and the nine close-out hooks |
| w_001D1EA0(a) | draw flush: world when a=1, overlays |

Owners **offer** inside their behaviour (the 001B1B70 position). The player's Use scan (00160220 inside 0015BCF0) reads the previous frame's published list.

001FCA10 (the message service, WP-8) stays at main-loop step F in em_frame.c, not in the coordinator.

---

## 5. Flows

- **Status (state 3).**
  1. Classifier r==2 comes from B0/C5, or from START/TRI when fade==0, 3B8D==0 and B3==0.
  2. The r==2 arm runs with no world frame.
  3. State 3: +C0 waits for audio busy; +C1 runs 001D1C50 (C4≠0 path), 001D2830(3,1), w_0020CDC0 and 001D1EA0(0). **The world is frozen: no owner, player or camera ticks** (trace st14).
  4. State 5 returns to state 1.

  w_0020CDC0 is interim-bound to a legacy em_hud adapter, which must clear canonical B0/C5 when it closes. In WP-5 it is rebound to `em_status_runtime` stepping the canonical `EmStatusFrame` view; its private `frame`/`queued` shadow is deleted. The H22 audio (001FBC50, 001FABB0, 00119828 ×2, 001FAE70(1)) comes out of the core.
- **Room move.**
  1. The door writes B7, sets B8=2 and starts the fade-out.
  2. While B8 is set the classifier returns 0 and the world keeps ticking.
  3. At substate 2, 001AD010 sets 702=B7 and +B=4.
  4. Next tick, state 4 (001AFCF0 clears B8, 001B07C0(1) re-places the player from D_0024D650[area] entry 702, camera, fade-in 001AEE10(4,0), 001C5C50) falls into state 1 and draws a world frame in the same tick.
  5. The pool is kept. The old 001C5930 left because B8≠0.
- **Area change.**
  1. 001B0C60 (fan or Roger) writes B5..B8 with B8≠2.
  2. At substate 2, 001AD010 sets 700/701/702 and +9=5.
  3. 001ADF50 runs over at least 3 ticks. Native: w_001FF080 begins the scene load of (700,701); w_0021B550/0021B840 poll and clear BD8.
  4. Then +9=1, +B=0 → state 0 on the next tick. That tick is the full rebuild: pool reset calls every `release`, then the player wipe, overlay install, roster spawn and 001B07C0(0). It draws no world frame.
- **Game over.** Death sets B9. The world runs until fade==2, then 001AD140 → +9=2 001AD4E0 → +9=4 001ADF00 → the task is replaced with 001AC070. The legacy GO_* machine is the interim w_001AD4E0 and is retired when 001AD4E0 and 001AC070 are translated.
- **New Game.**
  1. The frontend registers `em_scene_task_001ACEC0`.
  2. +8 goes 0 → 1 (001AD230 = 001AF2C0 reset) → 3.
  3. +9=0: 001AD360 (area 0x0B.0.0; D_00821058=1 starts the intro movie).
  4. +9=5: load. +9=1: state 0, then state 1, with the selector set by the opening controller.

  Moving the port's movie start from em_frontend to 001AD360 step 1 is part of S12 (it overlaps WP-17 SI-09/SI-10).
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
| **S11b Classifier acts** | em_scene_bindings.c, em_hud.{c,h} (external open; self-toggle removed; menu inhibit → B3), em_player_frame.c (B9 write; the go_state early-out is removed), em_game.c | r==2 → state 3/5 with w_0020CDC0 = the em_hud adapter (clears B0/C5). Game over via B9 → 001AD140 → w_001AD4E0 = the legacy GO machine → 001ADF00. The unported arms (r==1, r==3, +9=3) fault. | test_status_frame_reference, test_scene_frame_reference, EM_PAUSE_TEST, EM_DEATH_TEST, newgame-control, a Start-press trace against st14 (world frozen) | em_hud_is_open and go_state early-outs; the em_hud self-open |
| **S12a Area change / load** | em_scene_bindings.c (w_001FF080, w_0021B550/0021B840, w_001AD360), em_game.c (game_load_task becomes the begin/poll workers), em_frontend.c (movie start moves to 001AD360 step 1), new em_spawn_table.{h,c}, tools/export_spawn_table.py, tools/test_spawn_place_reference.py | native 001ADF50 over at least 3 ticks; state-0 rebuild; 001B07C0(0/1) from the exported D_0024D650 | Spawn placement: executes byte-matched 0x1B07C0 against the captured tables; entry 0 must match handoff/playable. An `EM_AREA_CHANGE_TEST` posts B5..B8 and asserts the tick-by-tick +9/+A/+B sequence against S3's oracle. New Game census = 49 at first control. | game_load_task; the invented spawn placement |
| **S12b Legacy door through B8** | em_door.c | legacy goto → B7, B8=2 + fade; state 4 re-places | door self-tests; a room-move trace (tick sequence: B8 set, fade to 2, +B=4, state 4 → 1 in one tick) | the in-frame `em_door_goto_pending` path for AREA11 |
| **S13 Live smoke** | new em_level_smoke_test.{h,c} (hooked beside em_opening_control_test) | `EM_STARTUP_TEST=newgame-level`, `EM_LEVEL_SMOKE_UNTIL=first_control\|status\|battery\|panel\|elevator` | At S13, first_control (+B=1, selector 0, census 49) and status open/close must pass. Later phases report NOT-LIVE until WP-4/5/6. | — |

**Phase 2 status.**
- **S8 landed (2026-09-22):** slot 0 runs `em_scene_task_001ACEC0` (src/game/em_scene_bindings.c) through the S3/S2 cores (001ACEC0 → 001AD250 → w_001AD4D0 → `em_sf_001AE040_q1`); `game_load_task` seeds +8=3, +9=1, +B=0 (legacy load, retired by S12a); state 0 binds 001AFCA0 to the moved native re-arm (`em_game_legacy_state0`, plus spad 31F4=0), 001AFCF0/001AD140/001AD010 to the cores, and the ten state-0 callees without port code (001FC9B0, 001B07C0, 001B6990, 001D19E0, 001C1DC0, 00199C50, 001AEE40, 001FAE70, 001C5C50, 001D1EF0) to explicit no-effect bindings reported once on stderr; `legacy_state0_frame` keeps the same-tick fall-through; both variants bind to one legacy worker (`em_game_legacy_world_frame`); the traced classifier is a shadow over the canonical state with E74/E70 from `em_frame_pad_block()` (never acted on; canonical E50=4 per Q8 until S11a); 3B90/C4 are forwarded to `em_frame_screen_fade_gate` (C4 stays 0, so drawing is unchanged); the Continue restart is serviced at the 0x1AE040 entry (`em_game_legacy_continue_restart`, retired by S11b). `ingame_frame_machine`, `game_sub_machine` and `game_task` are deleted. Verified against a pre-step binary built from HEAD (build/S8-before): 19 EM_CAPTURE BMPs and the newgame-control BMP byte-identical, newgame-control PASS (locked 1300, 9.599989), 20 EM_*_TEST verdicts identical, EM_FRAME_TRACE valid (the comparator matches idle04 through 0x1AE040 → 001AE5E0, then diverges at the monolithic legacy worker, as expected until S10a). Known S8 limit: the trace's selector is canonical 3B8D (0), not `g.frame_selector`, so cutscene ticks show target 001AE5E0 until S11a.

**After S13, WP-4…WP-12 each replace one binding row and delete the matching legacy code:**
- WP-4: grate, elevator_tick and the examine terminal
- WP-5: em_hud pages, Found, and the status_runtime shadow
- WP-6: pickup_trigger_scan and the countdown
- WP-7: em_door walk/goto
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
   - g.frame_selector
   - em_hud s_menu_inhibit
   - em_door locks
   - em_status_runtime frame/queued
   - the host's recovery_lock versus EF
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
  into the frame-trace stream. The pool does NOT reset the 001AF8E0 class-list block D_00275B54..BB8: assign
  it to the 001AAD00/class-list owner at the 001AFCA0 position. `EmActor` still lacks `flags2` (+0x2E,
  written by 001B6660/001B6990, untouched by alloc/free) — add it before S10b and store it on spawn.
- Trace contract (S6): core-internal jals 001ACEC0->001AD250 (0x1ACFF4) and 0x1AE040->001AE7E0 (0x1AE154) go
  through the trace hook; bindings set `w->trace = em_frame_trace_env_hook` when `em_frame_trace_env()` is
  non-NULL, call `em_frame_trace_tick_begin/end` around each task tick, `em_frame_trace_frame_state` at the
  0x1AE040 entry, `em_frame_trace_classifier` once per classifier run, and `em_frame_trace_node(callback,
  class, record, binding)` once per ticked node with the tracer's record tags ("area11[i]", "deferred[gG.J]").
- S5 stubs: `em_player_0015C160` and `em_render_001AAD00` have no port code and fault; the cutscene camera
  stage sits behind `em_opening_runtime_camera()`; damage/vitals and the death latch remain in the pool block
  (moving them is S10a/S11b).

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
- **D2 (2026-09-22): canonical game-progress storage.** The whole 0x640-byte block at D_00810700 that
  001AF2C0 resets (area bytes, D_00810730 table, D_00810758 slot table, D_00810778/788, the D_00810860
  per-area bits, D_00810B40, opening-complete D_00810811, power bits D_00810841, inventory D_00810C60..) gets
  ONE canonical owner inside `EmSceneState` (an `EmProgress` region addressed by original offset). Existing
  port mirrors (`g.opening_complete`, em_pickup counts/taken bits, `terminal_powered`, weapon ammo) become
  accessors over it in the step that first touches them; no step may add a second copy.
- **D3:** bind the seven 0018A6B0 nodes to the player's attached-equipment draw (Q5) rather than leaving them
  UNBOUND; the legacy weapon code keeps its position inside the player stage until WP-15 replaces it.
