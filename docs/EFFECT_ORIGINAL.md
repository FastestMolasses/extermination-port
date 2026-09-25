# Effect spawn chain, generic puff driver 001EA240, ring decals 001F0460

Status: 2026-09-23, lane `effect-puff-original`. The translation and its oracle are done. The module is **not
wired** into the frame. The coordinator binds it (see "Binding").

Files:
- `src/game/em_effect_original.{h,c}`: the native translation.
- `tools/test_effect_original_reference.py`: the original-instruction oracle, including the capture checks.
- `tests/effect_original_test.c`: the native contract test (ASan/UBSan). It needs no disc data.

## Scope

| Function | Decomp state | Translated from |
|---|---|---|
| 001EFD90 spawn at (pos, rot) | byte-matched C | C, checked against the .s |
| 001EFD20 spawn at pos, rot = 0 | byte-matched C | C |
| 001EF9D0 effect allocator | NEARMISS | the .s (the C's logic agrees) |
| 001EF940 effect sound | byte-matched C | the .s. The C prototype omits a1, which 00102948 copies |
| 001D80E0 / 001D8100 point-light presets | byte-matched C | C. 001D7FA0 is a worker |
| 001F0460 ring decal slots | byte-matched C | C |
| 001EA240 effect driver | byte-matched C | C, with the float op order from the .s |
| 001CCF70 projection / depth key | NEARMISS | the .s |
| 001CD390 look-at rows | NEARMISS | the .s |
| SDK leaves: 001029C0, 00102918, 001026A0, 00102760, 00102718, 001031E0, 00102948, copy_qw4, 001029E8, 00102A60, 00102BB0, 00102B08, 00102C58, 001B1470, 0011E860, float_to_int 001281C0 + unpack 001278C0 | mixed; 00102A60/BB0/B08 are handwritten asm | the .s |

Every float operation follows `docs/EE_FLOAT_MODEL.md` bit for bit:
- EE add/sub use the pre-trim; all EE results are truncated; div.s rounds to nearest; DAZ, FTZ and saturation
  apply;
- each VU0 lane uses the operand clamps of its own original form, which the source names by its VU_FORMS key.

The C float helpers in `em_effect_original.c` are an integer-exact port of `tools/ee_float_model.py`. The test
checks them against the recorded PCSX2 vectors directly.

## Identity (read from the ELF and the captured RAM)

- **Entity table.** `D_00259C70` = 0x257C90. That is 0x78 records of 0x30 bytes, followed by the per-area tables
  `D_00259C74[0..22]`; AREA11 (`D_00810700` = 11) uses 0x2595B0. The whole region 0x257C90..0x259C70 is
  identical in the ELF and in every route snapshot, so it is static data. It is loaded from the user's ELF by
  `em_effect_original_load_tables`.
- **Record fields.**
  - +0: 001AFA90 class;
  - +4: copied to node +3;
  - +8: copied to node +0xD (the subtype);
  - +0xC: callback (0 means "no effect");
  - +0x10: colour, handed to 001D7FA0;
  - +0x20: kind (1: 001D80E0, 2: 001D8100, 4: throttled 001D7FA0);
  - +0x24: sound id (-1 = none);
  - +0x28/+0x2C: 001FBF50 f12/f13.
- **Ids with callback 001EA240 (subtype = +8; handler = `D_00255434[subtype*2]`).**

  | id | subtype | handler | route use |
  |---|---|---|---|
  | 0x80000028 | 5 | 001EC3F0 | footstep surface 5 (snow), climb |
  | 0x80000065 | 0x24 | 001EC470 | slide (snow, every 8 ticks), footstep 0x5A |
  | 0x80000049 | 0x20 | 001EBF10 | truck (spawn frames and counts: TRUCK_ORIGINAL.md "Effects") |
  | 0x80000033 | 1 | 001EAD70 | reversal (+23A 5/6) |
  | 0x80000005 | 2 | 001EAF00 | footstep surface 6 |
  | 0x80000011 | 0xA | 001EC1F0 | footstep surface 0 |
  | 0x80000015 | 0xD | 001EBD20 | crate (CRATES_DRUMS_ORIGINAL) |
  | 0x8000001D | 9 | 001EAF80 | footstep 0x5B |
  | 0x8000005F | 0x22 | 001EB600 | crate area-0x15 arm |
  | 0x80000066 / 67 / 68 | 0x25 / 0x26 / 0x27 | 001EC5F0 / 001EC820 / 001EB980 | footstep 8 / 0x5C / 7 |
  | 0x8000000E | 7 | 001EBBB0 | spawned by 001F0460 preset 0 |

  All of them have class 0xC and kind 0 except 3/0x19/0x1A/0x30/0x42/0x60 (kind 1) and 9 (kind 2).
- **Live nodes in the route snapshots.** Walking the `D_00275BC0` list gives:
  - beat 05: one subtype-5 footstep;
  - beat 08: eight subtype-0x20 truck puffs;
  - beat 12: four subtype-5 nodes;
  - no other beat has any.

## Behaviour (all numbers read from the original code)

**001EF9D0(id, pos, f12)**
1. **Base.** With the sign bit set: base = `D_00259C70`, index = id & 0x7FFFFFFF. Otherwise: base =
   `D_00259C74[D_00810700]`, index = id. The record is at base + index*0x30 (32-bit wrap). A record +0xC of 0
   returns 0.
2. **Special ids.** This applies only to ids 0x80000026/2C/67.
   - When f12 == 1.0 (c.eq with DAZ), it writes a state code to **base + 0x24**. That is the **first record of
     the table**, not the selected one; the .s stores through s1. The code is:
     - id 26: 0x18E + rand % 4;
     - id 2C: 0x18C + rand % 2;
     - id 67: 0x192 + rand % 2.
     A negative C remainder writes nothing.
   - When f12 != 1.0, the same ids write -1 there.
3. **Alloc.** 001AFA90(record +0). A NULL node returns 0. Otherwise:
   - node +3 = rec +4, +0xD = rec +8, +0x10 = rec +0xC, +0x38 = 1;
   - pos == NULL returns the node;
   - otherwise the kind dispatch runs:
     - kind 4: when |clock 0x70003B68 − D_00275C38| ≥ 13 (0011E860), stamp D_00275C38 and call
       001D7FA0(pos, rec+0x10, 1, 0.95, −0.05);
     - kind 2: 001D8100 → 001D7FA0(pos, color, 0, 0.95 (0x3F733333), −0.05 (0xBD4CCCCD));
     - kind 1: 001D80E0 → (0, 0.6 (0x3F19999A), −1.0);
   - then **001EF940(record, pos) for every kind**.

**001EF940(record, pos)**
- It does nothing when D_008101E4 == 3 or rec +0x24 == −1.
- Otherwise it copies pos into a scratch record at +0xB0. 001FBF50 reads only +0xB0..+0xBC of that record.
  It then calls 001FBF50(scratch, &a, &b, 0, rec+0x28, rec+0x2C).
- If that returns nonzero, it calls 001FB9F0(rec+0x24, 0x1000, a, b).
- On the route only records 0x27 and 0x44 carry a sound, and neither is a 001EA240 type.

**001EFD90(id, pos, rot)** calls 001EF9D0(id, pos, **rot[3]**). It then sets node +0xB0 = pos, +0xC0 = rot and
+0xBC = 1.0.

**001EFD20(id, pos)** calls 001EF9D0(id, pos, 1.0). It then sets +0xB0 = pos, +0xC0..+0xCC = 0 and +0xBC = 1.0.

**001EA240(node).** D_00275C34 = node + 0x1F0 and D_00275C30 = node. The state byte +4 selects the path:

- **2/3:** 001AFC10(node).
- **other than 0..3:** return.
- **0 (seed):**
  1. The accumulator and limit are set by subtype:
     - 0x13: 0.0 / 6.0;
     - {0x2A,0x24,9,0x1B,0x22,0x21,0x1F,0xE,0x29,0x1A,0x19,0x28,0x1D,0x1C,0x17,0x16,4,0x15,0x14}: 0.2 / 1.5;
     - 0x20: 0.0 / 2.0;
     - else: 0.0 / 1.5.
  2. Step = `D_00255430[sub*2]`. seed = rand. fraction = cvt(rand) / 2³¹. +0xC = +9 = 0, +4 = 1.
  3. The direction block runs by subtype:
     - {0x29,0x28,0x1D,0x1C,0x17,0x16,4}, when +0x38 ≠ 0: rot.x and rot.y each get
       (π·(120·(rand/2³¹) − 60))/180, then each is wrapped by 001B1470. Then identity, 00102C58(rot),
       00102918(pos).
     - {0x25,1,5,0xB,0xA}: identity, euler(rot), 00102BB0(π), translate(pos), +0x104 += 0.5.
     - {0xE,0x24,9}: identity, euler, 00102BB0(π). Then
       scratch 0x700038A0 = (0, 0, −3.5·(D_008102E8/0.8), 1) is multiplied by the matrix (001026A0).
       Then translate(pos), and row 3 xyz += scratch xyz.
     - {0x26,0x1B,0x18}: 00102760(rot, rot), 001CD390(M, rot), translate(pos).
     - {0x23,0}: the same, then 001F0460(0, M) when +0x38 ≠ 0.
     - {0x1E,0x13,0x1A,0x19}: nothing.
     - else, when +0x38 ≠ 0: identity, euler(rot), translate(pos).
  4. It falls into state 1.
- **1 (run):**
  1. Fog range (0021B9A0 is the fog / depth-range programmer, docs/PACKET_CHAIN.md): {0x28,0x1D,0x1C} call
     0021B9A0(2,1,100) and (3,1,100);
     {6,0x29,0x1A,0x19,0x17,0x16,4,0x15,0x14,3} call (2,1,20) and (3,1,20).
  2. depth = 001CCF70(node + 0x100).
  3. w+4 = w+0. Then an indirect call handler(node+0xD0, depth, D_00275C34).
  4. The work block is **re-read through D_00275C34**. w+0x54 += step.
  5. If limit == 0: when acc > 2.0, acc −= 1.0. Otherwise: when acc > limit, +4 = 3.
  6. The same fog-range set plus {0x28,0x1D,0x1C} calls 0021B9A0(1, 0, 0).

**00102A60/00102BB0/00102B08** are the Z/Y/X rotations.
- If f12 < 0: sincos(π/2 + f12, neg); otherwise sincos(π/2 − f12). This is 001029E8, a 4-term odd polynomial
  from D_00241100 with the cosine taken by vsqrt(1 − s²).
- The four rotation rows are built lane by lane from a zero vector. Then out[i] = rows · in[i] for all four rows.
- **00102C58** applies Z, then Y, then X.

**001CD390(out, v)**
1. The scratchpad 0x70003610 = (x, 5+y, z or 5+z, 1). The +5 on Z applies only when x == z == 0.
   0x70003600 = (x, y, z, 1).
2. 3620 = cross(3610, 3600). 3630 = cross(3620, 3600). Both are normalized (w = +0).
3. out = identity with rows 0/1/2 xyz = 3620/3630/3600.

**001CCF70(pos)**
1. v = clip · (pos.xyz, 1), using the `001CD370(0)` matrix = ctx+0x2240.
2. VCLIPw.xyz: if any flag is set, the result is 0xFFFFFF and nothing is written.
3. Otherwise v = K · p, with K = scratchpad 0x70003AC0.
4. Q = 1/v.w (the reciprocal divide form). xyz *= Q.
5. w = clamp(fog.z + fog.w·v.w) to [0, fog.x]: an ACC multiply-add, then a min against fog.x and a max against 0.
   fog = ctx+0xA0.
6. Converted to 12.4 fixed point → 0x70003600..0F.
7. D_00275C04 = float_to_int(unscaled v.w).
8. It returns the 12.4 z.

001CCF70 and 001CD390 emit **no GIF packets**. They compute the depth key, the screen vector and the look-at
rows. The oracle compares those bit for bit. The packets come from the per-subtype handlers (001CFB50 →
001CFBE0 → 001D0540), which are workers here. Subtype 5's handler 001EC3F0 loads its own VU registers
(001D0540 reloads the four VU0 registers it reads), so no VU state leaks from 001CCF70 into it.

**001F0460(n, src)**, presets 0..6 (jtbl_0026E9C0):
- Every preset supplies:
  - a tag (+0x50);
  - a parameter vector (+0x40);
  - count, stored as count·60 at +0x58;
  - a ring limit.
- Preset values:
  - 0: tag …2078, (8,8,8,64), count 3, limit 32. It also calls 001EFD20(0x8000000E, src row 3) first.
  - 1/2: tag …218C, (48,48,48,64), count 3, limit 20.
  - 3: tag …2080, (255,0,0,64), count 3, limit 32.
  - 4: tag …2078, (48,32,16,80), count 20, limit 32.
  - 5/6: tag …2078, (16,16,16,64), count 3 and 2, limit 32.
- `D_0081F950[n]` += 1 and wraps to 0 at the limit. The slot is D_0028F700+0x4DBEC0 + n·0xC00 + i·0x60, and
  src is copied into it (copy_qw4).
- The ring is consumed by 001F0720 (age + four-packet draw) and reset by 001F03D0. Both are outside this lane.

## Verification

`python3 tools/test_effect_original_reference.py` (default ~3.5 s; `EM_TEST_FULL=1` ~17 s).

The oracle is our own bounded EE/VU0 interpreter:
- It runs the ORIGINAL instructions of the pinned ELF for every function above.
- Arithmetic comes from `tools/ee_float_model.py`.
- Memory is a route snapshot (`../Extermination/build/s87/route/<beat>/eeMemory.bin` + `scratchpad.bin`).
- Workers are recorded as calls with scripted results.

After every call, three checks run:
- every byte the original changed must be a modelled byte (or stack);
- every modelled byte must equal the native value: node fields, work block, table, D_00275C30/34/38/04,
  scratchpad 3600..363F and 38A0..38AF, ring indices and all 7×32 slots;
- the worker call sequences, with argument bits, must be equal.

| Part | quick | full |
|---|---|---|
| native float helpers vs recorded PCSX2 vectors (EE add/sub/mul/div/cvt/compares, VU lanes of every form, vdiv, vsqrt, vftoi4, vmax/vmini) | 5,066 | 28,441 |
| native float helpers vs the model on random + boundary bits (all clamp-flag combinations) | 417 | 8,017 |
| 001B1470, float_to_int | 72 / 165 | 612 / 1,515 |
| 001CD390 (both guard arms, signed zeros) | 46 | 406 |
| 001CCF70 on the captured view (clipped and visible), plus 4 synthetic fog quads reaching the 255 and 0 bounds | 100 (+40 bounded) | 800 (+1,138) |
| spawn chain over every sign-bit id with a record + area-11 ids, with specials, NULL pos, refused alloc, sound gate | 180 of 545 | 545 |
| kind-4 throttle edges (12/13, wraps, INT_MIN) | 9 | 9 |
| 001F0460 all presets, ring starts 0..0x40 (wraps, limit 20/32), preset 0's nested spawn | 51 | 51 |
| 001EA240 synthetic: every 001EA240 subtype spawned by 001EFD90 and driven to its free | 16 nodes / 576 ticks | 43 / 5,463 |
| 001EA240 crafted state-1 ticks: limit edges, endless decay clamp, free states, fog-range subtypes | 60 | 124 |
| 001EA240 non-finite translations (ACC clamp keeps VCLIP finite) | 18 | 18 |
| 001EA240 lockstep from every **captured** live node until it frees | 13 nodes / 389 ticks | same |
| em_point_light_register vs the original 001D7FA0 with the kind 1/2/4 argument sets | 12 | 12 |

**Capture evidence.** The check is independent of the model:
- For each of the 13 captured live nodes, the native state-0 recomputation from the node's own +0xB0/+0xC0
  reproduces its captured +0xD0 matrix **bit for bit**. That matrix was computed by the original in PCSX2.
- The captured accumulator lies on the EE-add trajectory of its step from 0.0.
- A deliberately wrong rotation sign fails this check alone.

**Mutation check (this session).** 27 distinct single-point bugs were injected into the native file, and the
default run was repeated after each one. The bugs covered:
- the jitter constant, the EE pre-trim, the base-vs-record +0x24 store and the rotation sign;
- div rounding, ring life and limit, the limit comparison and the fog min lane;
- the seed values, the sound gate and argument order, the kind-4 edge, the look-at bias and the normalize w;
- VU-vs-EE add, sqrt rounding, the ftoi fraction, D_00275C04, seed_copy and the euler order;
- the 9/0xE/0x24 scale, preset 0's spawn id, the decay step, the fog-range set and the state-2 free.

26 of them fail the test. Three needed the boundary, throttle and fog-quad cases this doc lists: the limit
comparison, the kind-4 edge and the fog lane.

The one that passes swaps the sincos clamp flags. It is unobservable, because every lane of 001029E8 is finite
for any input: the EE argument is saturated.

`tests/effect_original_test.c` covers:
- fail-stop: NULL workers, negative results, ids past the tables, area index ≥ 23, a missing area table,
  subtypes ≥ 0x2B, a freed node, a negative ring index, a 001B1470 loop that cannot end;
- the worker protocol;
- one full footstep life cycle.

## Boundaries (workers; a NULL or negative result faults)

| Worker | Original | Data |
|---|---|---|
| `w_001AFA90` | pool alloc | class; returns the node (NULL = refused, not a fault). The module writes only the modelled bytes. |
| `w_00122BB8` | game rand | v0 |
| `w_001D7FA0` | point-light register | pos, rec+0x10 colour, type, fa, fb. Wrap `em_point_light_register` (verified against 001D7FA0 here and in test_point_light_reference) and **discard its result**; see "Full point-light pool" below. |
| `w_001FBF50` / `w_001FB9F0` | positional sound gains / submit | pos copy, rec+0x28/+0x2C; then (id, 0x1000, a, b) |
| `w_0021B9A0` | fog / depth-range programmer (em_packet_chain_0021B9A0) | mode, scale, bias |
| `w_handler` | `D_00255434[subtype]` draw handler | handler address, node (a0 = node+0xD0), depth, work block (= D_00275C34) |
| `w_001AFC10` | pool free | node |

**Full point-light pool.** 001D7FA0 first reads the live count at D_00275670 +0x214. When it is already 0x20
it returns -1 at once and writes nothing (no slot, no allocator bump). None of this module's callers use that
result: 001D80E0 and 001D8100 are tail calls whose own callers treat them as void, and 001EF9D0's kind-4 call
drops v0. So in the original a full pool just loses the light and the spawn carries on. The module, however,
treats **any negative worker result as a fault** (WORKER_FAILED at 0x001D7FA0). `em_point_light_register`
returns -1 when `pending_count >= EM_POINT_LIGHT_COUNT` (32), so binding it directly would turn the original's
silent drop into a fail-stop. The adapter must discard the result:

```c
static int w_001D7FA0(void *ctx, const float pos[4], const float color[4], int32_t type,
                      float fa, float fb)
{
    /* 001D7FA0 returns -1 when the pool is full; its callers ignore it. */
    (void)em_point_light_register((EmPointLightPool *)ctx, pos, color, type, fa, fb);
    return 0;
}
```

(`ctx` here is whatever the coordinator hands the workers; the point is only the discarded result.)

Faults:
- **BAD_INDEX:** an address outside 0x257C90..0x259C70 (the original would read on); an area index ≥ 23; a
  subtype ≥ 0x2B; a negative ring index; a freed node.
- **UNMEASURED:** a 001B1470 loop that can never end (|angle| ≳ 1e8 or NaN); a non-finite VCLIP lane. The second
  cannot happen in 001CCF70, because form (15,3) clamps ACC. It stays as a guard.

**Not measured.** VCLIPw is not part of EE_FLOAT_MODEL. Both sides use the IEEE comparison of DAZ'd finite
values: x > |w| / x < −|w|.

## Binding (for the coordinator chain; nothing is wired)

**Blocked on the render-context views (Effects step, 2026-09-24).** The
`view` below has no live producer: the port has no canonical render-context
block, and no live code writes 0x70003AC0 or context +0x2240. The driver
calls 001CCF70 and the handler on every state-1 tick, so binding the spawns
without the view would fault at the first footstep puff. What has to exist
first is in EFFECT_MANAGER.md 5.0. em_effect_original.c is in COMMON only
for the SDK leaves that the live player closure uses (001026A0, 00102760).

The coordinator owns one `EmEffectOriginal`:
- `tables` from `em_effect_original_load_tables(elf)`. It is the same ELF buffer em_director_original reads.
- `globals` mirroring D_008101E4, D_00810700, D_00275C38, the 0x70003B68 clock, D_008102E8, and the scratchpad
  words.
- `decals` (7 lanes of 32 slots).
- `view`: the render context +0x2240 clip matrix, +0xA0 fog quad, and the 0x70003AC0 camera matrix, refreshed
  wherever the original writes them.
- `workers` as above.

**Callers to bind** (each existing worker slot, the original call site, the data):

| Caller (module, worker slot) | Original | Call |
|---|---|---|
| footstep `em_player_floor.h` `effect` (00187EE0) | 001EFD90(id, foot − 1.5, actor+C0) | `em_effect_original_001EFD90(e, id, pos4, rot4, &node)` |
| footstep `decal` | 001F0460(1, M), M built by 00187EE0 (identity, 00102BB0 yaw, 00102B08 pitch, row 3 = position) | `em_effect_original_001F0460(e, 1, M)` |
| climb `em_player_climb.h` `effect` (0x80000028) | 001EFD90(id, pos, p+C0) | same as footstep |
| slide `em_player_slide.h` `effect` (0x80000065 every 8 ticks) | 001EFD90(id, p+B0, p+C0) | same |
| the walk's skid `em_locomotion_display.h` `effect(id, p)` (0x80000033 / 0x80000012) | 001EFD90(id, p+B0, p+C0) | the binder supplies the record's +0xB0/+0xC0 (em_player_closure_live.c `lw_effect`) |
| truck `em_truck_original.h` `effect` (0x80000049) | 001EFD20(id, pos) | `em_effect_original_001EFD20(e, id, pos4, &node)` |
| drum `em_drum_original.h` `effect_matrix` (preset 4) / `effect` | 001F0460 / 001EFD20 | `_001F0460` / `_001EFD20` |
| crate `em_crate_original.h` `effect` | 001EFD90(id, pos, rot) | `_001EFD90` |
| area weather node (em_area11_bindings `spawn_001EF9D0`, 0x80000017) | 001EFD20 | can move to `_001EFD20` (same record bytes, now loaded from the ELF) |

**Important data details.**
- **The rotation's 4th lane (actor +0xCC) is load-bearing.** 001EFD90 passes rot[3] as f12, and for
  0x80000067 (footstep surface 0x5C) f12 == 1.0 selects the random state write. It is also copied to node
  +0xCC. The floor/climb/slide worker signatures pass `rotation[3]`. The adapter must hand this module the
  actor's full +0xC0 quadword, not a 3-vector padded with a made-up w. pos[3] does not matter, because +0xBC is
  overwritten with 1.0.
- **Node allocation.** `w_001AFA90` must return a pool record whose `EmEffectOriginalNode` view starts in
  state 0 (001AFA90 hands out records in state 0). The adapter maps the node's modelled fields onto the pool
  record: +3, +4, +9, +0xC, +0xD, +0x10, +0x38, +0xB0..+0x10F, and the work block at +0x1F0/+0x244/+0x24C.
  The node's per-tick callback is then `em_effect_original_001EA240(e, node)` from the 001AFD70 walk: callback
  0x1EA240, roster row in em_actor_roster.c.
- **Handlers.** `w_handler` receives the subtype's original handler address. The route handlers 001EC3F0,
  001EC470, 001EBF10 and 001EC1F0 are translated in em_effect_kinds (`em_effect_kinds_handler`), with their
  001CFB50 / 001D0540 since the Effects step (docs/EFFECT_KINDS.md 2.1a); the other subtypes' handlers are not
  (they fault UNTRANSLATED). The handlers reach 001CFB50 and 001CFBE0 (em_head_sprite_original) with depth `n`
  and the work block's +0x54/+0x5C/+4. Both read the render-context views (EFFECT_MANAGER.md 5.0).
- **Frame order.** ORIGINAL_FRAME_ORDER Q4 (measured):
  0015BCF0 (0x15BDD8) → 00187350 → 00187EE0 → 001EFD90(0x80000028) → 001EF9D0 → 001AFA90. That is one node
  about every 23 frames while walking, at the player's feet. The node then ticks in the 001AFD70 walk.
  D_00275C30/34 are published per tick for the handler.

## Open items

1. Translate the handlers 001EC3F0 (5), 001EC470 (0x24), 001EBF10 (0x20), 001EAD70 (1), and those of
   2/0xA/0xD/9/0x22/0x25..0x27/7, together with 001CFB50/001CFBE0/001D0540. Those are the actual packets.
2. The ring consumer 001F0720 (age + draw) and the reset 001F03D0.
3. The effect types for crate/drum breaks (0x0A 001F2BA0, 0x13 001E4CE0, 0x1C/0x2E 001E4610) belong to the
   separate breakable-fx lane.
4. VCLIPw on non-finite operands. It is unreachable here and was not measured.
