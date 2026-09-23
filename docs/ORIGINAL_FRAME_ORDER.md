# Original per-frame call order (AREA11)

This is measured ground truth for the live scene coordinator (FIRST_LEVEL_AUDIT
WP-3). It contains addresses, call order and owner mapping only.

## How it was measured

- The original game ran in the MCP-enabled PCSX2, driven by
  `Extermination/tools/pcsx2_session.py` from the owner's save states.
- A breakpoint was placed on every `jal`/`jalr` site of the functions below,
  plus the entries of 001ACEC0, 0x1AE040 and 001AFD70 and the return point of
  001AE7E0. At each hit the tracer recorded the PC, the callback register and
  the current actor `*(0x275B44)`. It stopped when the main-loop top 0x1AAF28
  hit again.
- Traced functions: the main loop 0x1AAE40, 001AB6A0, 001ACEC0, 001AD250,
  0x1AE040, 001AE5E0, 001AE6B0, 001AFD70, 0015C160, 001D1C50, 001AAD00,
  001A8970 and 001A8660. The trace also covered every `jalr` in the main-ELF
  bodies of the owner behaviours that were seen.
- Raw traces are local and gitignored, in
  `Extermination/build/s87/frame_trace/`:
  - `idle04`, `walk04`, `st03`, `cut02`, `cut07`, `cut15` and `st14` `.json`
  - `frame_order_summary.json`
  - the tracer `trace.py`
- Frames traced (with the main-loop counter 0x70003B64):

  | Save state | Situation | Counter | Frames |
  |---|---|---|---|
  | 04 | Idle | 4085 | 3 |
  | 04 | Stick held for 12 frames (the player moved) | 4097 | 3 |
  | 03 | Movement locked | 3965 | 2 |
  | 02 | Opening cinematic, selector 2 | 2940 | 2 |
  | 07 | Selector 3 | 8119 | 2 |
  | 15 | Roger encounter, selector 2 | 4030 | 3 |
  | 14 | Status hub | 8060 | 2 |

  Within each situation, every frame had the same order.

## 1. Main loop 0x1AAE40 (one frame)

The letters are the steps in FINDINGS "ENGINE FRAME ANATOMY". Every traced
frame, in every state, ran this order:

| Step | Call site | Callee |
|---|---|---|
| A | 0x1AAF28 | clear vsync flag (loop top) |
| B | 0x1AAF34 | 001D1AE0 |
| C | 0x1AAF3C | 001B57E0 input |
| D | 0x1AAF44 | 001AEBE0 fade |
| E | 0x1AAF4C | 001AB6A0 task dispatch. Only slot 0 ran: `jalr` 0x1AB704 → 001ACEC0 → 001AD250 (0x1AD2D4) → 001AD4D0 → `j` 0x1AE040 |
| F | 0x1AAF54 | 001FCA10 |
| G | 0x1AAF5C | 001AEE70 |
| H | 0x1AAF64 | 001FB100 |
| I | 0x1AAF6C | 001B5B70. **Runs every frame; not gated.** |
| J | 0x1AAF78 | 00100A60. It returned 0 in every frame, so the VU block 0011B910…0011A9D8 was never called |
| K | 0x1AAFB0 | 001D7410. Called every frame; its gate is inside it |
| L | 0x1AAFB8 | 001AB590 |
| M/N/O | 0x1AAFD4 / 0x1AAFE0 / 0x1AAFE8 | 00203350, 001D1C10, 001AEE70. **Never ran**: D_00821058 was 0 in every traced frame, and all three sit in one block gated on D_00821058 == 1 (the movie driver) |
| P | about 0x1AAFF0 | vsync wait |
| R | 0x1AB020 | 001AB4E0 |
| S | 0x1AB070, 0x1AB0C0 | 001015A8, 00101810 |
| T | 0x1AB0C8 | 0010BAA0(0) |
| U | 0x1AB0EC | 00100550 |
| V | 0x1AB0F4 | 001D2300 |
| W | 0x1AB118 | 001D2580, then the counter is incremented |

The whole world frame (sections 2 and 3) runs inside step E. That is before
audio (F, H), the second fade (G) and the vsync wait.

## 2. In-game frame machine 0x1AE040

In these tables, `task` means `*(0x70003B6C)`, and the "selector" is the
scratchpad byte 0x70003B8D.

| Situation | Task state byte `task[0xB]` | Sub-step `task[0xC]` | 001AE7E0 result | Selector | Variant |
|---|---|---|---|---|---|
| Gameplay (states 03, 04, idle and walking) | 1 | 0 | 0 | 0 | 001AE5E0 (call site 0x1AE2A4) |
| Opening, Roger encounter, state 07 | 1 | 0 | 0 | 2 or 3 | 001AE6B0 (call site 0x1AE2B4) |
| Status hub (state 14) | 3 | 1 | not called | 2 | none |

In every state-1 trace, D_00275BD8 was 0 and neither D_008106B8 nor
D_008106B9 was set.

In the status hub (state 3, sub-step 1) the frame is:

1. 001D1C50
2. 001D2830(3,1)
3. 0020CDC0
4. 001D1EA0

No actor, player or camera update runs. The whole world is frozen.

## 3. World-frame variants

The two variants call their functions in different orders. The coordinator
must reproduce both.

**Gameplay, 001AE5E0:**

1. Increment counters 0x810750 and 0x70003B68.
2. 001CB590(player 0x8102B0) at 0x1AE620.
3. **0015BCF0** player update at 0x1AE628.
4. 001CB5A0.
5. **001D1C50** render chain at 0x1AE638.
6. 001C1D00(0x8101D0) camera at 0x1AE644.
7. **001AFD70(0)** at 0x1AE64C. It walks every node.
8. **0015C160** at 0x1AE654.
9. **001F0360** at 0x1AE65C.
10. 001CB590(0x8101E0).
11. 0018B9C0.
12. 001CB5A0.
13. 001AAD00 at 0x1AE688.
14. 001D1EA0(1) at 0x1AE690.

**Cutscene, 001AE6B0:**

1. Scratchpad bookkeeping.
2. **001D1C50** at 0x1AE744.
3. 001C1D00 at 0x1AE750.
4. **001AFD70(1)** at 0x1AE758. It skips class-1 nodes.
5. **001F0360** at 0x1AE760.
6. 001CB590(player).
7. **0015BCF0** at 0x1AE780.
8. 001CB5A0.
9. **001AFD70(2)** at 0x1AE790. It visits class-1 nodes only.
10. **0015C160** at 0x1AE798.
11. 001CB590(0x8101E0).
12. 0018B9C0.
13. 001CB5A0.
14. 001AAD00 at 0x1AE7C4.
15. 001D1EA0(1) at 0x1AE7CC.

The player is updated by 0015BCF0, while 0015C160 calls `+0x4C`. In the
walks, the node class is `node[2] & 0x1F`.

Key differences for the coordinator:

- **Gameplay:** player → render chain → camera → all owners.
- **Cutscene:** render chain → camera → non-class-1 owners → 001F0360 →
  player → class-1 owners.
- 001F0360 runs **after** 0015C160 in gameplay, but **before** the player in a
  cutscene.
- In every mode, the class-1 player children tick after 0015BCF0 and before
  0015C160.
- 001AFD70's two cutscene walks together visit each node exactly once. For
  example, state 15 has 42 nodes in mode 1 plus 7 in mode 2, which is 49.

### 0015C160 (player post-step)

This is identical in every trace:

1. 001CB590(player).
2. 001DA6A0, because player `+0x214` == 0 and D_00810771 != 1.
3. `jalr` at 0x15C1D4 to player `+0x4C` = **001CAA00**, with a0 = 0x8102B0.

### Render chain 001D1C50: no owner callbacks

001D1C50 calls these in order:

1. 001D2830(4,0) at 0x1D1C60.
2. 001B0070 at 0x1D1C8C. Bit 0x80 was clear in every trace.
3. Gameplay (selector 0) only: 0015D2F0 at 0x1D1D88, then 0021B9A0 at
   0x1D1DC0.
4. 001D2830(6,1) at 0x1D1DCC.
5. 001D2960.
6. `copy_qw4` twice.
7. **001D7C30** at 0x1D1E78.
8. **001D30A0** at 0x1D1E80.

The status hub instead takes the D_008106C4 != 0 path: 001D2830(6,0) at
0x1D1C7C.

001D2830, 001D7C30 and 001D30A0 are not owner walkers. Their static subtree
(43 functions) contains **no `jalr`**, and no indirect call fired at run
time. Owners do their own per-frame model work from inside their behaviour,
through their `+0x4C` anim-mode method (next section).

001AAD00's two indirect sites, 0x1A8A2C and 0x1A8734, never fired.

## 4. Owner walk 001AFD70: every callback, in list order

Where the owners come from:

- The list head is D_00275BC0 and the next link is `+0x1C`.
- The callback is the owner's `+0x10`, called at 0x1AFE1C with a0 = the
  owner. 001CB590 sets the owner as the current actor `*(0x275B44)` first.
- List order is spawn order:
  1. the deferred-spawn registry D_0024D820[11] (0x2759A8 → sub 0x2759A0,
     group 0), in record order;
  2. then placement table 0x82A3C0, in record order;
  3. then actors spawned at run time.
- New actors are appended at the tail.
- `rec` is matched by behaviour, uid and position.

The table below is gameplay, state 04, counter 4085: 49 nodes, all ticked by
`001AFD70(0)`. The last two columns list the nested `jalr` fired from inside
each callback. The first is the same idle frame; the second is the Roger
encounter frame, state 15.

| # | Owner | Class | Callback `+0x10` | Record | Nested, idle | Nested, Roger |
|---|---|---|---|---|---|---|
| 0–5 | 0x7A5640…0x7A64F0 (step 0x2F0) | 4 | 00219550 | deferred g0.0–g0.5 (item pickups, cond 1) | 001CAA00 only for the pickups near the view (g0.0, g0.3) | 001CAA00 (g0.1) |
| 6 | 0x7A67E0 | 7 | 0015AFA0 | deferred g0.6 (pickup) | 001CAA00 | - |
| 7 | 0x7A6AD0 | 4 | overlay 0x825940 | deferred g0.7 (door-position creature) | - | - |
| 8 | 0x7A6DC0 | 4 | overlay 0x827490 | deferred g0.8 (husk) | - | - |
| 9 | 0x7A70B0 | 5 | 001BC350 | area11[0] room-move door | 001CAA00 | 001CAA00 |
| 10, 11 | 0x7A73A0, 0x7A7690 | 4 | overlay 0x827630 | area11[1], [2] | - | - |
| 12–15 | 0x7A7980…0x7A8250 | 4 | 001551B0 | area11[3]–[6] crawlers | 001CAA00 | 001CAA00 |
| 16 | 0x7A8540 | 13 | overlay 0x8235F0 | area11[7] | - | - |
| 17 | 0x7A8830 | 10 | overlay 0x8237E0 | area11[8] (Roger) | - | 001B7B30 via 001BA1F0 (0x1BA248) |
| 18 | 0x7A8B20 | 8 | 001C5C90 | area11[9] attachment. It moves; the record position is 0 | 001CAA00 | 001CAA00 |
| 19 | 0x7A8E10 | 4 | overlay 0x823E80 | area11[10] opening controller (drives the opening script through 001BA1F0 → 001B82D0; see trace 2) | - | - (state 02: 001B7B30 via 0x1BA248) |
| 20, 21 | 0x7A9100, 0x7A93F0 | 9 | overlay 0x823CE0, 0x8253F0 | area11[11], [12] | - | - |
| 22, 23 | 0x7A99D0, 0x7A9CC0 | 4 | 00156620 | area11[14], [15] nest fixtures | 001CAA00 | 001CAA00 |
| 24 | 0x7A9FB0 | 4 | overlay 0x823FF0 | area11[16] | - | - |
| 25 | 0x7AA2A0 | 4 | overlay 0x8251E0 | area11[17] | - | - |
| 26 | 0x7AA590 | 4 | 00159210 | area11[18] (FINDINGS calls it a grate; the audit calls it a panel) | 001CAA00 | 001CAA00 |
| 27 | 0x7AA880 | 4 | overlay 0x827B10 | area11[19] examine switch | - | - |
| 28 | 0x7AAB70 | 4 | 001C4820 | area11[20] | 001CAA00 | 001CAA00 |
| 29 | 0x7AAE60 | 12 | 001E55F0 | runtime | - | - |
| 30 | 0x7AB150 | 8 | 001C5930 | runtime area-title actor (spawned by 001C5C50) | - | - |
| 31–37 | 0x7AB440…0x7AC5E0 | **1** | 0018A6B0 | runtime player children (7) | 001CAA00 each | 001CAA00 each (in `001AFD70(2)`) |
| 38 | 0x7AC8D0 | 12 | 001E2560 | runtime | - | - |
| 39–45 | 0x7ACBC0…0x7ADD60 | 12 | 001C5680 | indicator children at deferred g0.0–g0.5 and g0.7 | 001CACB0 via 001F54E0 (0x1F5620) | same |
| 46 | 0x7AE050 | 12 | 001E2560 | runtime | - | - |
| 47 | 0x7AE340 | 12 | 001C5680 | indicator at area11[18] | 001CACB0 | same |
| 48 | 0x7AE630 | 12 | 001C5760 | indicator at area11[19] | 001CACB0 | same |

Notes on the list:

- **Record 13** (class 9, overlay 0x8257A0) was not in the list in any traced
  state.
- **Walking** (walk04) appends one node at the tail: #49, 0x7AE920, callback
  **001EA240**, at the player's feet.
- **Opening** (state 02, 52 nodes) appends two opening-script actors with
  callback **001BB0E0**: node 0x7A96E0 (class 9), and node 0x7AE920 (class 8,
  which calls 001CAA00). A third appended node is 001E2560.
- **State 03** frame 3965 still had the two 001BB0E0 opening actors (51
  nodes). They were gone on the next frame (49 nodes).
- Every nested `jalr` is either an owner's `+0x4C` method, reached from inside
  its behaviour, or the script step 001B7B30.
  - 001CAA00 is the default static method. 001CACB0 is the indicator method.
  - 001B7B30 is called from 001BA1F0 at 0x1BA248.
- Whether an owner calls `+0x4C` in a given frame depends on the owner's
  state. Pickups call it only when they are near or visible.
- Overlay behaviours (0x82xxxx) have no static disassembly here. Their
  internal indirect calls appear only when they pass through a traced
  main-ELF site.

## 5. Contradictions and corrections

- The FINDINGS table is right that steps M, N and O are all gated. None of
  them ran during gameplay, cutscenes or the status hub.
- Step I (001B5B70) is not gated.
- WP-3 gameplay order is confirmed. Two calls are missing from it:
  001AAD00 and 001D1EA0(1).
- WP-3 cutscene order "001AFD70(1) → player → 001AFD70(2)" is correct but
  incomplete:
  - 001D1C50 and 001C1D00 run **first**.
  - **001F0360 runs between 001AFD70(1) and the player.**
- The render-chain "walkers" 001D2830, 001D7C30 and 001D30A0 invoke no owner
  callbacks. Owners are not walked twice per frame.
- The header comment of `anim_frame_top_b.c` describes a title/attract
  machine with states such as "load game". In game, state 3 sub-step 1 is
  the status-hub frame (0020CDC0). Treat those labels as wrong.
- FINDINGS calls the gameplay frame a "ONE live actor" idle room. That is
  false for AREA11: 49 to 52 owners tick every frame.

## 6. Open questions settled (trace 2)

Measured on 2026-09-22 with the same `pcsx2_session.py` driver. Method:
exec breakpoints on 001AFA90 (entry and its return 0x1AFBB8), 001AFC10,
0018A880, 001EF9D0, 001EFD90, every call site of 001AE5E0/001AE6B0/0015BCF0/
0015C160, and every main-ELF store to 0x70003B8C..0x70003B93 (68 sites found by
scanning resident code for `lui 0x7000` + byte stores). Values were also read
straight from the owner's 12 save states. Raw output (local, gitignored):
`Extermination/build/s87/frame_trace2/` (`inventory.json`, `spad_boot.json*`,
`boot_hits.jsonl`, `palette_*.json`, `alloc_*.json`, `children_04.json`,
`status_*.json`) and the scripts that produced them.

Method note: a scratchpad **write memcheck** on 0x70003B8C..0x70003B93 caught
some stores (001AFCF0, 001B82D0@0x1B8608) but silently missed others
(001B82D0@0x1B874C/0x1B8778). Use exec breakpoints on the store sites for
scratchpad bytes; EE-RAM memchecks were reliable.

### New-game load, from state 01 (title, counter 1306)

NEW GAME (Cross), intro movie skipped with START, AREA11 loads. One frame
(counter 1769→1770, task `+B` 0→1: states 0 and 4, falling into state 1)
does all static spawning, in this order:

1. 001AFCF0 (from 0x1AE040 at 0x1AE094) zeroes 3B93, 3B8C, 3B8D, 3B8E, 3B8F,
   3B91, 3B92 (stores 0x1AFD04..0x1AFD38).
2. 001B6660: deferred registry g0.0–g0.8 → nodes 0x7A5640…0x7A6DC0.
3. 001B6990 (run as a task function from 001AB6A0's `jalr` at 0x1AB70C):
   placement records area11[0]..[20] → 0x7A70B0…0x7AAB70, **including
   record 13 at 0x7A96E0** (class 9, callback overlay 0x8257A0).
4. 001C1DC0 (0x1AE040 state 4, call at 0x1AE0B4) → 001C1EA0 (0x1C1E58) →
   001EFD20(0x80000017, …) → 001EF9D0 → 001AFA90(0xC): **node 0x7AAE60,
   callback 001E55F0** (#29).
5. 001C5C50 → 001AFA90(8): node 0x7AB150, callback 001C5930 (#30,
   area title).

Next frame (1770→1771), from the first player update
(0015BCF0 → 0015BA50 → 0015C420):

- 0015C420 → 0018A880(4,0) → 0x7AB440 (stored at player+0x18);
  0015C310 → 0018A880(0,0) (player+0x20), (1,0), (1,0x10), then with
  D_00810CA4 = 0xFF the three-row branch: (2,D_00810CA5=5), (2,D_00810CA6=0),
  (2,D_00810CA7=7). D_00810CA6 ≠ 4, so no (1,0x15). That is exactly the seven
  class-1 nodes #31–#37, in list order.
- 0015C420 → 001F0120(player,0x3B) → 001EF9D0(0x80000010) → 001AFA90(0xC):
  **node 0x7AC8D0, callback 001E2560** (#38), `+0xD`=0x3B, `+0x24`=player.
- 001C5570 spawns the indicator children (#39–#44, #47); overlay 0x825940 (g0.7)
  spawns #45 itself; overlay 0x827B10 (area11[19]) spawns #48 at 0x827C20.
- The Roger owner (overlay 0x8237E0, inside the 001AFD70 walk) → 001BA8E0
  (0x1BA928) → 001EF9D0 → **node 0x7AE050, callback 001E2560** (#46),
  `+0xD`=0x47, `+0x24`=Roger 0x7A8830.

### Q3 — record 13

Freed at counter **1771→1772** (the second world frame, gameplay variant,
`001AFD70(0)` walk) by **its own behaviour**: overlay 0x8257A0 calls
001AFC10 at 0x8258E0 with a0 = 0x7A96E0, node state `+4` = 3. Nothing else
frees it. The slot is then reused at counter 1797→1798 by 001BAC00 (script
op 0x14 spawn) for the opening actor (class 9, callback 001BB0E0), together
with 0x7AE920 (class 8, 001BB0E0) and a third 001E2560 node 0x7AEC10
(`+0xD`=0x47, `+0x24`=0x7A96E0, via 001BB0E0 → 001BAD40 → 001BA8E0).

### Q4 — spawners

| Callback | Spawn chain | When |
|---|---|---|
| 001E55F0 | 001C1DC0 → 001C1EA0 → 001EFD20(0x80000017) → 001EF9D0 → 001AFA90 | area load, state 4 |
| 001E2560 | 001F0120 / 001BA8E0 → 001EF9D0(0x80000010) → 001AFA90 | player init (0x3B), Roger and each opening actor (0x47) |
| 001EA240 | 0015BCF0 (0x15BDD8) → 00187350 → 00187EE0 → 001EFD90(0x80000028) → 001EF9D0 → 001AFA90 | walking only; `+0xD`=5; one node every ~23 frames at the player's feet |

All three come out of the effect allocator 001EF9D0; the table near 0x24CCC8
maps the effect type (0x80000000|n) to the callback.

### Q5 — identities (measured per tick, state 04)

- **0018A6B0 ×7** are the player's attached equipment models. Each owns one
  bone node and a model pointer at `+0x44`. Its tick rewrites that bone's world
  matrix (`+0x90`) at a player bone (flavour 4 sits on player bone 14; the
  others share one bone position), then draws through `+0x4C` = 001CAA00.
  Flavour/variant (`+3`/`+0xD`): (4,0), (0,0), (1,0), (1,0x10), (2,5), (2,0),
  (2,7). The (2,n) variants are D_00810CA5..CA7.
- **001E2560** is a periodic sprite effect anchored on the owner's
  **bone 7** (the highest bone, the head), offset (1.5,−0.6,0) for the
  player and (1.8,−0.5,0) for Roger. A countdown of 60–99 ticks, then a
  ramp `+0x244` 0→1.5 at 0.02 per tick. While ramping it rewrites its
  position `+0xB0..B8` and `+0x100..108` and emits a GIF packet through
  001CFBE0. It ends (state 3) when the owner's health `+0x220` ≤ 0 or
  its `+4` ≥ 2. It fires no projectile: the "turret AI" label is wrong.
  "Breath vapour" fits (cold area, mouth offset), but is not proven.
- **001E55F0** is effect type 0x17, created from the area weather flags
  (001B0070 bits). Its per-frame work goes to 001E67C0 (weather tile
  emitter) or 001E5AC0.
- **001EA240** is effect type 0x28, the footstep effect, spawned from the
  player's animation events (00187EE0).

### Q2 — where the player's bone palette is produced

The player's bone world matrices (bone nodes' `+0x90`, the skinning palette)
were sampled at every call site:

- **Gameplay (state 04, idle and walking):** all 21 matrices change only
  across the call at **0x15BD64 (anim_eval_skeleton)** inside 0015BCF0.
- **Cutscene (state 02):** 20 of the matrices change only across the call at
  **0x15BDBC (001C6960)** inside 0015BCF0.
- In both variants they are unchanged across 001D1C50, 001C1D00, the
  001AFD70 walk(s), 0015C160 and its `jalr` 0x15C1D4 → 001CAA00.
- The scratch hip and Euler at 0x70003B40/0x70003B50 are written by the
  0015BCF0 tail copies at 0x15BF68/0x15BF78.
- Player `+0xA0..+0xCF` changes across 0015BA50 (0x15BD14), 0x15BD64 and
  the tail copy at 0x15BF58.

So the palette and the hip/Euler publication both belong to the 0015BCF0
stage. 001CAA00 (via 0015C160) only consumes them for the draw.

### Q6 — 3B92 and 3B93

- **3B92 = 1** is written by 001B82D0 at **0x1B874C** (with 3B91 = 1 at
  0x1B8778), called through 001BA1F0 (0x1BA250) by the opening controller
  overlay 0x823E80 during the 001AFD70 walk, at counter 1797→1798. That is
  the same beat in which the opening actors are spawned.
- **3B92 is cleared** by the script end 001B82D0@0x1B8940 (seen for the
  panel 00159210 and the switch 0x827B10) and by 001AFCF0@0x1AFD38 at load.
- **3B93** is 0 in all 12 save states. Its only main-ELF stores are the
  zero stores 001ADF00@0x1ADF18 and 001AFCF0@0x1AFD04. Of the 19 `OVERLAY/`
  modules, only AREA21.BIN stores it (0x827148 and 0x827154, from `$v0`).
  **Nothing in AREA11 ever sets 3B93.**

### Q7 — 3B8D during status

- **No store to 3B8C..3B93 runs while task state 3 is active.** This was
  checked over a 190-frame status session opened with Triangle from state 04,
  and over the st14 close.
- Opened from gameplay (state 04), 3B8D stays 0. The close then goes state 3
  → `+B`=5 → state 5 (one frame, no world variant) → state 1 → **001AE5E0**.
- States 08/12/14 were opened from the panel 00159210's battery interaction
  and carry **3B8D=2 from that script**. After Triangle, state 5 returns
  into **001AE6B0** for 14 frames, until the panel script
  (00159210 → 001BA1F0 → 001B82D0@0x1B8938) clears 3B8D. It also clears
  3B92@0x1B8940, and 00182DF0@0x182EE8 clears 3B8F. The next frame is
  001AE5E0.
- The store that put 2 there for the panel was not reproduced, because no
  save state precedes the panel interaction. In the opening, 3B8D=2 comes
  from 001B82D0@**0x1B8608** (value from `$a0`), called by 0x823E80 through
  001BA1F0 at counter 1772→1773. Then:
  - 00182D64@0x182D88 sets 3B8F=1 (player 0015B130 path);
  - 001B81D0@0x1B82B4 sets 3B8F=2.
- 001AE7E0 checks C5/B0 before 3B8D. A script can therefore open status
  while 3B8D≠0. The static D_008106B0/C5 byte-store sites outside the status
  stack are 0x157F38..0x158020 and 0x1C4744..0x1C47BC.

### Q8 — D_00810E50

In all 12 save states:

- **E50 = 4**, E51 = 1, E52 = 1;
- libpad state E4C = 6 (stable);
- current pad ID **E54 = 7** (analog DualShock).

001B5F40 is a pad-setup state machine:

- 0: read the ID. If digital with no mode table → 4. Otherwise lock analog
  mode → 1.
- 1: wait → 0 with E51 = 1.
- 2: → 4.
- 4: ready. Read the pad through 001B5940. State 7 (disconnect) → 0.

E50 = 4 therefore means "pad initialised". An analog DualShock reaches it,
so the port's E50 = 4 is correct.

### Audit items

- **P31:** D_008106C8 = **0x20081910** in every AREA11 capture (states
  02–15). **& 0x60 = 0**, so AREA11 has no passive hazard drain.
- **SI-27/AM-21/INV-24 (001FAE70 branch):**
  - D_008104E4 = 0 and D_00810D38 = 0 in every capture. The cue-0x18
    override is not taken.
  - The cue is (0x20081910 >> 8) & 0x7F = **0x19 = 25**, with a fade of
    270 + (LCG >> 16 & 0x7F).
  - `arg0=1`: channel release, then start cue 25.
  - `arg0=0`: restart only if channel 0 is idle (D_00282154==0) or holds
    another cue. For example, D_00282178 = 29 during the Roger encounter
    (st15) and 25 during gameplay.
  - 001FAE70 reads no weapon id.
- **R09 (frame clear colour):** not settled. The captures do not expose it
  cheaply: the GS privileged BGCOLOR is not in the save-state blob that was
  read. The gameplay screenshot has scene content at the top rows
  (7,7,18), not a clear. The cutscene letterbox bars are (0,0,0).
