# AREA01 world model bank selection

## Scope and ownership

`em_area11_boxes_bind_world_bank(path, resource_word)` selects an exported
world model bank during area reset. `resource_word` is the canonical module
loader's `EmStatusSceneLoader.d28A490[0x43]`: the original `D_0028A59C`, not a
constant inferred from an area filename. The export's table address must
match exactly. Invalid input, a failed read/parse, a mismatched token, or a
current-generation owner still holding a model returns failure and leaves
the old bank intact. A successful bind expires borrowed world-bank/model
views. The caller must release those external borrowers before rebinding;
the adapter can check its own tracked owners only.

The sole existing owner remains `em_area11_boxes`: the parsed model bank,
`em_owner_services` translations, and the shared `001AF710` bone-slot stack
used by Roger and the other binders. Original callers `001B0FD0`,
`001B0EA0`, and `001C6120` continue through their existing translations;
there is no AREA01 copy of the allocator or model lookup. Parsing happens
before replacement, and skeleton pointers are rebased into the stable bank
storage after the parser's temporary result is installed.

`em_area11_boxes_reset` drops owners and resets the shared slots; it keeps
the selected world bank until an explicit bind succeeds. The unchanged
first-use fallback still selects the AREA11 export. The separate global
library (`D_0028A56C`, Roger's resource table), table-less library models,
and Roger exports keep their existing semantics. Bank selection does not
change them; the existing area reset still clears this adapter's library
view.

The nonallocating `em_area11_boxes_owner_fields` projects an already bound
world/library owner's canonical `+0x40`, `+0x44`, and `+0x4C` words. It
returns absent for an unbound or stale generation and does not create a
model. The existing `owner_world` and `owner_slot` APIs expose the same
owner's world matrix, bone records, and original slot addresses.

## Caller integration

After `001AFCA0` has reset the actor pool and `em_area11_bindings_reset`
(and the arrival adapter) has released the previous area, before any owner
allocates a model:

1. Obtain the current `EmModuleLoader` and its `EmStatusSceneLoader`.
2. Read `ld->d28A490[0x43]` after the area load has completed.
3. Call `em_area11_boxes_bind_world_bank` with
   `assets/scene_snow/world_models.emwm` for AREA11 or
   `assets/area01/world_models.emwm` for AREA01 and that word.
4. Propagate failure through the scene fault path. Continue owner creation
   only after a successful bind.

The bank-selector lane does not change scene bindings or Makefile targets;
those integrations are coordinated separately. No source capture is a live
input. Only the user's exported EMWM is opened by this API.

## Loader overlap correction

The new AREA11-to-AREA01 loader check exposed lost resident data in
`em_module_loader.c:region_for`. A shorter read at the same base retained
the old region's larger size. A later nested read then intersected that
obsolete extent, causing the whole region to be freed, including the
AREA01 top resources that the new read had not overwritten. Both
`em_module_loader_memory` and `memory_rest` returned no view at the top
resource base: this was data loss, not merely an unavailable contiguous
view.

The corrected storage replaces exactly the incoming interval and preserves
untouched prefixes/suffixes. It prepares the new mapping before changing
the old one, so allocation/capacity failure leaves existing memory intact.
Each new read retains its exact extent, preserving the DMA consumer's
boundary. The existing 16-region limit and fail-stop behavior remain.
`002009E0` is a store into existing memory, not a new disc delivery:
its BSS clear first uses a containing canonical allocation, preserving its
untouched guard bytes and extent, and allocates only if that span is not
already mapped. The AREA01 state full oracle checks the original clear,
32 untouched guard bytes, and 416 init cases over 16 captures (570,775,296
compared bytes). The original-instruction loader test compares every modelled store,
callee entry, frame state, delivered span, and DMA payload after the
transition. The formerly missing top span now remains readable and equal.
The memory APIs still return a contiguous view within one region; they do
not promise to concatenate adjacent independently delivered regions.

## Evidence

The new `tools/test_world_model_bank_reference.py` and narrow
`tests/world_model_bank_bridge.c` compare the actual shared adapter with
original instructions from the pinned boot ELF and the user's captures.
The bridge supplies no substitute model/allocator logic. Unexpected calls
to its out-of-scope Roger resource provider abort.

| Check | Quick | Full |
|---|---:|---:|
| Parsed model views compared byte-for-byte | 64 | 416 |
| Skeleton records compared to original resource bytes | 121 | 681 |
| Original `001C6120` executions (four masked IDs per model) | 256 | 1,664 |
| Original `001B0FD0` / `001B0EA0` executions | 128 | 832 |
| Allocated bone records compared, including matrices | 242 | 1,362 |
| Invalid-input / active-owner bind rejections | 24 | 24 |
| Relocated loader-token rejection | 1 | 1 |
| Additional AREA01 captures | 0 | 16 |

Quick checks AREA11 -> AREA01 -> AREA11 selection and both original owner
initializers for every model. Full repeats all AREA01 models against every
available sub-0 capture returned by `export_area01_common.captures`,
including the first-level exit arrival. Both check the library view and
library word remain unchanged across a successful world-bank selection,
and validate owner metadata projection for every world-owner bind.

The native and original loader execute module 3 in 8 dispatches, then
AREA11 in 13, AREA01 in 17, and a direct AREA11 return in 13. The sound
transfer is an explicitly supplied boundary: its returned container end
comes from the delivered file's length expression, independently on both
sides. Four sound-boundary calls occur across the four area loads below;
this does not establish sound-transfer timing or sound residency.

| Delivered bank | Canonical table | Models | EMWM resource span bytes |
|---|---:|---:|---:|
| AREA11, after module 3 | `0x01335F40` | 21 | 544,928 |
| AREA01, after AREA11 | `0x01781140` | 22 | 288,448 |

For each accepted selection, the loader's slot `0x43`, original loader's
word, capture word, and EMWM header agree. Every exported span byte equals
both native delivery and the original loader's delivered memory. This
checks the actual loader-to-bank identity, not just the filename/header.

A direct AREA01 -> AREA11 load without reloading module 3 produces table
`0x01336CC0` in **both** original and native execution. The fixed-address
AREA11 export is rejected. Reloading module 3 (8 dispatches) and then
AREA11 (13 dispatches) restores `0x01335F40`; its bank is accepted and
byte-equal. The selector does not invent a relocation or silently reuse
old handles. A broader route that legitimately uses another placement
needs corresponding exported-resource/address handling.

Observed runs (all PASS):

- New bank quick: 4.1 seconds; full: 4.83 seconds.
- Existing owner-services quick: 120 bind cases, 12 bone initializations,
  16 placements, 200 SDK sets, 416 publication cases, 24 draws, 176 rumble
  cases, 18 countdown cases, 40 captured owners, and 4 captured palette
  uploads; 1.3 seconds.
- Existing owner-draw quick: 700 culling cases, 160 packet cases,
  84 lookups, 60 captured owner draws, and 32 indicator draws; 1.4 seconds.
- Existing module-loader quick: 120 read cases, 25 polls, 12 DMA cases,
  184 packet/restore cases, and 3 additional whole modules; 7.9 seconds.
  Full: 5,544 read cases and 45 additional whole modules; 12.1 seconds
  after the BSS store-path correction.
  Both retain the 13 page-module checks, 16,000 GS-block comparisons,
  17 ITEM-atlas sprite checks, pinned loads, measured-drive checks,
  and sanitizer harness checks reported by that suite.

Logs are ignored outputs under `build/level2/oracles`: `model_bank.log`,
`model_bank_full.log`, `module_loader_bank_fix.log`,
`module_loader_bank_fix_full.log`, `owner_services_bank.log`, and
`owner_draw_bank.log`. The new structured report is
`build/level2/oracles/model_bank/report.json`. Reports contain counts and
metadata, not original instruction/data dumps.

This evidence establishes bank selection, original owner initialization,
and the loader handoff on the tested transitions. It does not establish
AREA01 actor scheduling, gameplay closure, final draw composition, or a
completed live first visit; those need the separate adapter integration.
