# Original AREA11 auxiliary point light

The previous port invented a faint steam light at `(452,279,278)` with
intensity 14 and tuned warm RGB. The original light is an independent room
resource. `001F6D60` selects the auxiliary placement list `0025D5A0` for
AREA11/sub0; `001F6E40` releases old handles and registers that list through
`001F6640` and `001D7FA0`. The earlier exporter only inspected the primary
selector `001F6760`, so its claim that the snow area had no room lights was
incomplete. The nearby overlay actor `008235F0` does not register this light.

The single record uses color preset 2 at `0026EB90`. It places a type 1 light
at original binary32 coordinates `(451.600006,279,277.200012)`. Registration
uses multiplier 1 and adder 0. Adoption scales the complete color vector by 128,
giving RGB `(1658.879883,414.719971,103.679993)` and intensity 414.719971.
These position/color bytes equal the immutable original opening snapshot.
There is no authored radius; `001D8340` uses inverse squared distance, with
the denominator clamped to at least 1.

`em_point_light.c` preserves the original 32-slot active/staging lifecycle:

- `001D7BB0` clears active type/handle/position/color fields and the counters.
  It preserves the other slot bytes. Registration writes staging slots.
- `001D7C30` first updates active slots. Intensity becomes
  `max(0,adder + intensity*multiplier)`; RGB is multiplied by the multiplier.
- Type 1, multiplier 1 lights outside key 0F00 consume two original RNG outputs.
  Each angle adds `-.001 + .002*(float(random)*2^-31)` and clamps to
  approximately ±.03141593 radians. The original SDK polynomial and matrix
  multiplication build the X/Y rotation; host sine/cosine are not substituted.
- Other active types/multipliers and key 0F00 use identity rotation. Pending
  lights are adopted afterward into the first inactive slots. Adoption zeros
  angleXYZ while preserving angleW and the slot's prior matrix. New lights
  therefore begin their random update on a later frame.

The body light fold follows original instructions: seed the direction with
weighted camera fill, add each rotated offset scaled by10k, add light color
scaled by2k, then normalize the final direction. Here
`k = .1*intensity/max(distance_squared,1)`. Face lighting continues to bypass
this fold through its separate original rig.

Native integration (since the lighting step, 2026-10-02, audit 1b item 4)
runs the original chain at run time, where the original does: the area
entry's 001D19E0 (0x1AE040 state 0; `um_001D19E0` in em_scene_bindings.c)
calls 001D7BB0, whose field writes are `em_point_light_reset` over the
render context's pool and whose tail, 001F68B0 then 001F6E40, is
`em_effects_live_room_lights`: em_effect_kinds' 001F68B0 / 001F6E40 / 001F6850 /
001F66F0 / 001F6640 and the selectors 001F6760 / 001F6D60 over the ELF's
lists window D_0025AD80..D_0025D800 and the presets D_0026EB70 (the one
copy, from `effect_tables.emet`; the records' +0x24 handles persist across
area loads, starting from the ELF's 0), with 001D7FA0 =
`em_point_light_register` and 001D80B0 = `em_rcl_001D80B0`
(em_frh_001D80B0 over the context). 001F68B0's latch bytes come from the
canonical progress bytes; a key whose case reads one that is not canonical
faults. In AREA11 001F68B0 selects nothing and 001F6E40 releases the list
record's handle (the ELF's 0: 001D80B0 finds no slot after the reset) and
registers the one record above. The pool ticks at the `001D1C50` phase
(001D7C30, keyed by D_00810700 / D_00810701 as it runs): after ordinary
player update and before pooled actors, or before actor updates during the
opening. The shared `em_random_next` supplies its RNG calls. The offline
`tools/export_point_lights.py` / `point_lights.emlp` / `em_point_light_load`
and the manifest's `pointlights` line (now ignored) are retired. The guessed steam light registration
is removed. The separate particle owner is now recovered in
[AREA11_EFFECT.md](AREA11_EFFECT.md); its old steam billboards and audio
retriggers were fabricated. Nearby original sound/contact binding remains
unfinished.

## Validation

`tools/test_point_light_reference.py` executes the original EE instructions,
including both SDK VU0 rotation helpers, directly from the owner's ELF. It
compares 256 complete 32-slot updates, 780 RNG calls, 256 dynamic folds and 40
registrations, including capacity and allocator wrap. The area entry runs
as the original 001D7BB0 (its reset, 001F68B0 and 001F6E40 over the ELF's
lists) against the port's chain wired as em_effects_live_room_lights wires
it, for every key either selector names plus 0x0B00 and 0x0F00, each latch
byte 0 and 0xFF, twice in a row (60 entries, 120 registrations): the whole
pool and the whole lists window are equal. All active/staging slot bytes are
compared. Live, the level smoke's check_room_lights compares the pool's
counters and active slots with the first-control capture and every aligned
route snapshot.

The saved original angles reconstruct all 64 bytes of the captured flicker
matrix. Using the captured light and Dennis's actual bone 1 anchor reproduces
the unique current body DMA color row at `002FE060`, all 12 RGB bytes. This
row distinguishes round-to-nearest EE DIV.S from truncation; the latter does
not match (the test keeps that negative control). Since 2026-09-27 the oracle
runs COP1 and VU0 on the measured EE model (docs/EE_FLOAT_MODEL.md section 5)
and em_point_light.c computes the tick's and the fold's COP1 sites (traced:
001D7C80, 001D7D8C/90, 001D7E1C/20, 001D85AC..001D85FC) through
em_ee_float.h; the flicker matrix is em_owner_services' 001029C0 / 00102B08 /
00102BB0, and the tick returns their status (a fault). The fold's VU0 sums
stay per-operation truncation (EE_FLOAT_MODEL.md section 5c).

```
make test-point-light test-point-light-reference
```

The lists come from `effect_tables.emet` (tools/export_effect_tables.py),
which the effects already need. ASan/UBSan tests cover staging/adoption,
RNG gates, zero-distance normalization, capacity and the reset.

## Remaining scope

The call order is audited (RAND_ORDER.md): the sway's two draws sit where
the original's do in every frame of the opening and of the aligned route
windows, and the level smoke's check_sway runs the original 001D7C30 over
the port's own pool and draws on sampled ticks (the pool it writes is the
port's). The values differ from a capture's at a snapshot, because the
port's stream reaches it from its own route; check_owner_units therefore
folds the port's own pool through the original 001CAA00 (whole lighting
rows equal). Native starts the original initializer and consumes the shared RNG; it does
not replay an arbitrary captured angle as a universal initial state. The
original angle-update formula and supplied-state output are verified even
when the native global stream differs. Camera-fill composition and native
bone arithmetic retain their separate rounding dependencies. Opening Dennis,
Roger and equipment pass the original body-light eligibility gate; eligibility
for other native actor classes remains unaudited. Legacy primary lamps in
other scenes retain their previous path. Texture/fog/raster behavior is
separate from this light input correction.
