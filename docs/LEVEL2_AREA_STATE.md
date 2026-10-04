# AREA01 canonical overlay state and initialization

`em_area01_state.c/.h` binds the delivered AREA01 overlay data and BSS and
translates runtime `00823A50`, together with the AREA01 paths through its
caller `001E7780`. It owns only the six boot globals at `00275C18..00275C2F`.
There is no second overlay arena, captured-state initializer, replacement
effect-grid owner, or new BSS clear.

The existing `em_area01_sys_001E7D20` remains the grid owner's translation.
Existing AREA01 exit/render translations also use these global pointers and
records. No existing port translation of AREA01's `00823A50` or the shared
`001E7780` was found. The AREA11 initializer `em_slg_008237C0` and other
areas' initializers are different functions and remain unchanged.

## Original load and initialization evidence

The user's `extract/OVERLAY/AREA01.BIN` has MWo3 id 2, loaded at `00823500`.
Its header specifies text size `54C0`, data size `4300`, BSS size `14AC80`,
and BSS boundary `0082CD00`. Header plus text plus data is the actual file
size `9800`. Therefore the loaded data spans `00828A00..0082CCFF` and BSS
spans `0082CD00..0097797F` (**1,354,880 bytes**). The existing
`assets/area01/overlay_data.emsc` data is byte-identical to the file's data
section; this provider uses the same bytes already delivered by the loader.

Original `001FFCD0` state 1, after a successful overlay read, calls
`002009E0(load base, file size)`. That function reads header word `+14`,
calls `FlushCache(2)`, and clears that many bytes beginning after the file
through `00121A28`. Existing `em_module_loader.c:w_overlay` implements this
clear in the loader's allocation. Full BSS zeroing is therefore evidenced by
the original loader, not inferred from omitted export bytes. The new module
neither repeats it at scene rebuild nor manufactures a zero-filled fallback.

Original `001D0660` calls `001F0310` and then `001E7780`. For AREA01, the
latter performs these stores in order:

1. Clear `00275C24`, `00275C28`, `00275C2C`, `00275C18`, `00275C1C`,
   and `00275C20`.
2. For keys `0100` and `0101`, call runtime `00823A50` (decomp source
   `func_overlay_AREA01_00823A10.c`). Its ordered stores are `C2C = 1`,
   `C28 = 20`, `C20 = 0082CD00`, `C24 = 0`, `C18 = 0`,
   and `C1C = 00836D80`.
3. The closing record loop clears `0082CD00 + 54` and `+58`. No other
   BSS byte is cleared by this dispatcher.

Other AREA01 sub bytes have no dispatch target: the six globals remain
zero. This scoped wrapper rejects other area bytes before writing; it does
not implement the other areas' dispatch handlers.

## Ownership and integration

The only shared loader edits are the declaration in `em_module_loader.h`
and implementation in `em_module_loader.c` of
`em_module_loader_memory_mutable`. It returns the same bounded, already
allocated span as the existing read-only accessor. It never allocates,
clears, loads, or copies memory. Existing loader behavior is unchanged.

`EmArea01State` stores the six globals and a memory-provider binding. Its
`bytes` API exposes exactly three regions: those globals, overlay data, and
overlay BSS. Views of data/BSS alias the loader. The overlay data includes
script records that original script commands modify; future AREA01 script
hosts must resolve those records through this canonical view rather than
load another independently mutable copy of `scripts.emsc`.

Every query reacquires the loader span and checks the loaded overlay header.
Missing spans, a replaced overlay, empty/wrapping/out-of-region queries,
and latched faults produce no view. Bind/init failures latch their first
original address; subsequent execution refuses until the host explicitly
detaches the binding. Unknown-range queries alone do not latch a fault, so
a composite resolver may consult other canonical owners. A failed store
keeps the original stores that preceded it.

Exact scene integration for the parent binding change:

1. Keep one zero-initialized `EmArea01State` in the scene binding, and a
   memory callback that returns
   `em_module_loader_memory_mutable(ctx, address, size)`.
2. In `w_001AFCA0`, after `em_area11_bindings_effects_attach` (the existing
   `001F0310` binding), replace the AREA01 `001E7780` boundary comment with
   `em_area01_state_bind` over the live module loader followed by
   `em_area01_state_001E7780` using canonical area/sub bytes. Report either
   failure through `em_scene_fault` with the state's fault address. Do not
   clear BSS or reload an EMSC there.
3. This completes before `w_001AFCA0` returns, hence before the frame
   machine calls `001AFCF0`, player spawn `001B07C0`, placement spawn
   `001B6990`, and the later `001C5C50` spawn in that rebuild. The pool reset
   still precedes the effects and overlay initialization.
4. The future SYS/render/overlay binders must use the same global/data/BSS
   views. Reacquire any region arrays before execution after a module load;
   do not retain raw pointers while the loader can replace allocations.
   Detach before closing the loader or discarding this scene binding. A
   same-overlay scene rebuild may rebind without resetting its memory.
5. Keep the AREA01 world-frame guard until every reached owner and worker
   is bound. This initialization does not claim a playable arrival.

No `em_scene_bindings.c` or `Makefile` edits are part of this module. Add
`src/game/em_area01_state.c` to the native build and a reference target
running `python3 tools/test_area01_state_reference.py` during integration.

## Verification

`tools/test_area01_state_reference.py` builds the actual state provider and
actual loader with a test-only allocation bridge. The oracle executes the
original `001E7780` and `00823A50` instructions with no external worker
stubs on those paths. The user's pinned boot ELF and extracted overlay are
checked against the code in each capture before execution. The sources are
the arrival beat `build/s87/route/15_level_exit` and all 15 AREA01-resident
`build/s87/route_a01` end snapshots; `a01_07` is excluded because its
snapshot has already entered AREA00.

- Quick: **16 captures, 160 initialization cases**, direct init and
  dispatcher sub bytes 0, 1, 2, 255, each with recorded and poisoned
  BSS/global initial values. **219,528,960 state bytes** compared.
- Full: the same cases plus all 256 sub-byte values, **416 cases** and
  **570,775,296 state bytes** compared.
- Every case compares the ordered original stores, all 24 global bytes,
  the whole overlay data and BSS, the unchanged loaded file, and boundary
  guard bytes. Poisoning exposes omitted stores and excessive clears.
- A separate original `002009E0` execution runs its real `00121A28` memset;
  only `FlushCache` is a host boundary. The existing native loader clears
  exactly the same **1,354,880 bytes**, preserving the file and guard.
- API checks cover exact loader-pointer alias identity, moving a loader
  allocation, retained script/BSS modifications across rebuilds, malformed
  headers, missing/truncated spans, query bounds, unsupported area, partial
  stores before a missing-BSS fault, and refusal after a latched fault.

Both modes pass. Receipts are `build/level2/area-state/quick.log` and
`full.log`. The existing module-loader quick oracle also passes (8.3 s;
`build/level2/area-state/module-loader-quick.log`), including whole loads
and New Game at both drive settings. The targeted no-disassembly scan,
Python compilation, warning-clean bridge build, and diff check pass.
These are initializer/storage checks; the grid owner's existing
oracle proves its separate update logic. No original bytes are embedded in
the new source or tests.
