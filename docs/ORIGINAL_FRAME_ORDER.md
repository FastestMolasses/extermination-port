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
| 19 | 0x7A8E10 | 4 | overlay 0x823E80 | area11[10] key item | - | - (state 02: 001B7B30 via 0x1BA248) |
| 20, 21 | 0x7A9100, 0x7A93F0 | 9 | overlay 0x823CE0, 0x8253F0 | area11[11], [12] | - | - |
| 22, 23 | 0x7A99D0, 0x7A9CC0 | 4 | 00156620 | area11[14], [15] nest fixtures | 001CAA00 | 001CAA00 |
| 24 | 0x7A9FB0 | 4 | overlay 0x823FF0 | area11[16] | - | - |
| 25 | 0x7AA2A0 | 4 | overlay 0x8251E0 | area11[17] | - | - |
| 26 | 0x7AA590 | 4 | 00159210 | area11[18] (FINDINGS calls it a grate; the audit calls it a panel) | 001CAA00 | 001CAA00 |
| 27 | 0x7AA880 | 4 | overlay 0x827B10 | area11[19] examine switch | - | - |
| 28 | 0x7AAB70 | 4 | 001C4820 | area11[20] | 001CAA00 | 001CAA00 |
| 29 | 0x7AAE60 | 12 | 001E55F0 | runtime | - | - |
| 30 | 0x7AB150 | 8 | 001C5930 | runtime (equipment controller) | - | - |
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
