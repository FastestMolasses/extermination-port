# AREA01 flicker-light owner

2026-10-03. Standalone, original-instruction verified and unbound. The
AREA01 frame gate stays in place. No live host, Makefile or first-level
behavior is changed by these files.

`em_area01_light_owner.c/.h` adds the missing owners of boot functions
001C4FA0 and 001C50B0. Both original C bodies are byte-matched. The original
instruction body additionally fixes the draw-vector stack address: its
0x40-byte frame places that vector at caller sp minus 0x10.

001C4FA0 dispatches on actor +3 and reads the corresponding canonical story
or inventory byte. 001C50B0 uses that predicate before setup and before each
active frame. Setup calls the existing model worker, stores its byte result,
chooses the original color and point-light amplitude from area/sub/parameter,
optionally calls the point-light worker, and scales the color vector. Active
frames consume exactly one random value, compute the original EE float
jitter, clamp the three draw components to the original [0,255] interval and
call the draw worker with a stack vector. Teardown releases a light handle
unless it is -1, then frees the actor. These clamps are in the original;
they are not native safety substitutions.

The module uses the existing `EmA01Math` address and worker contract. It
owns no global/actor memory. The caller provides the original actor address
and entry stack address, and maps the canonical data. External worker calls
remain mandatory and fail-stop through the math core; the predicate is
called directly because this file is its sole translation owner. Float
operations use `em_ee_float.h` throughout. No script, light, model, random
or draw behavior is substituted here.

All external workers are entered with original stack pointer `sp - 0x40`.
The math dispatcher carries no separate stack argument, so its host context
must supply this value when calling stack-aware existing workers such as
001C5050 and 001F5F60. Their own frame allocation then remains theirs.

## Binding dependencies

| Worker | Existing translation / service |
|---|---|
| 001F5490, 001F5F60 | `em_area00_fx_exit.c` |
| 001C5050 | `em_area00_world_001C5050` in `em_area00_world.c`; forwards the original 001D7FA0 result |
| 001D7FA0 | `em_point_light_register` over `em_rcl_point_lights()`; `em_effects_live.c:w_room_001D7FA0` preserves the returned handle |
| 001D80B0 | `em_rcl_001D80B0` / `em_frh_001D80B0` over the same canonical pool |
| 00122BB8 | shared canonical random generator |
| 001AFC10 | `em_actor_pool_free_001AFC10` |
| 001028B8, 001028D0, 00102900 | existing SDK vector owners through the shared dispatcher |

001C5050 was checked before implementing: it already exists and was not
duplicated. Its decomp C declares void, but the original caller consumes the
forwarded result; the existing AREA00 translation explicitly preserves that
result. The effect-only `w_001D7FA0` discards the handle and is insufficient
for this caller. No decomp source was edited.

The live adapter must supply these original-layout fields from canonical
owners, with store-back before external workers and refresh after them:

| Original bytes | Native source / binding requirement |
|---|---|
| actor +3, +4, +0D | `EmActor.model`, `u04[0]`, `param` |
| actor +20 | persistent point-light handle; `EmActor` has no direct field, so retain it in the actor's canonical controller |
| actor +44 | model entry supplied by 001F5490; retain it in the canonical model/controller owner |
| actor +80..8F, +B0..CF | `EmActor.f80`, `pos`, `rot`; preserve float bits |
| 00810700..701 | canonical scene area/sub bytes |
| 00810C87, 0081075D, 0081076D, 00810770 | canonical story/inventory bytes used by the predicate |
| caller sp-10..sp-1 | temporary vector read by 001F5F60 during the call |

The native pool's record-image helper does not supply +20 or +44; a zeroed
image would lose the light handle and model. The standalone oracle's full
RAM image is a test fixture, not a proposed live second actor arena.

## Verification and limits

Run `python3 tools/test_area01_light_owner_reference.py`, or prefix it with
`EM_TEST_FULL=1`. The native library is compiled with C11, warnings as
errors and contraction disabled. The test executes the original ELF
instructions over the AREA01 arrival capture
`Extermination/build/c10/exit/exit_01_movie_arrival/`: the light actor is
present at 0x7B0F50 in state 0. Later AREA01 end-of-beat captures no longer
hold this callback, so they cannot replace that arrival fixture.

The oracle checks the two original code ranges against the pinned ELF.
It runs the real 001C4FA0 inside 001C50B0 and checks every external worker's
call order and consumed arguments, the full actor and relevant globals at
each worker boundary, the original worker-entry stack, the draw vector,
every final original/native write
and the predicate result. Synthetic cases cover setup acceptance/refusal,
all predicate arms, teardown handle boundaries, parameter/mode branches,
callee writes that force the owner to re-read state, signed random results,
and EE float edge values. Full mode expands the predicate type-byte space
and randomized float cases. The test's worker stubs are explicitly test
boundaries; they do not verify the workers themselves.

Quick: **161 cases / 216 worker boundaries**. Full: **2,309 cases / 2,536
worker boundaries**. Both also check six fail-stop contracts: null owner
context, null predicate context, null result, an already-latched fault,
unmapped input and an unbound reached worker. Timing and final results are
recorded in the parent binding report.

This proves a standalone translation on the tested inputs. It does not
prove the future live binder, light allocation/drawing, pixels, arbitrary
aliasing of an actor with global bytes, or every possible worker failure.
