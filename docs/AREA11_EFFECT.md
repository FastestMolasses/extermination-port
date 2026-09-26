# AREA11 particle hazard: original owner008235F0

The old port called this a steam emitter and invented two pale billboards,
a120-tick rise/fade cycle, position(452,279,278), and sound413 retriggers every
90ticks with radius300. None of those visual/timing choices follows the
original owner. The actual resident texture is an orange/yellow flame.

The new visual path uses the original placement, particle descriptor, lookup
table, texture, controller phase and GS projection. It removes the invented
sound retrigger. **Nearby sound and contact reactions remain unfinished.**
The original auxiliary point light is a separate room table; this actor does
not register it. See [AREA11_POINT_LIGHT.md](AREA11_POINT_LIGHT.md).

## Original provenance

The MWo3 loader maps the whole AREA11 module to00823500. Its64-byte header
remains present; canonical overlay labels that place file40 at823500 are
40bytes too low for runtime analysis. The exporter checks original callback
bytes at fileF0 against captured EE008235F0 before reading data.

Placement record7 begins at AREA11 file6FD4, carries constructor001551B0 and
class13/sub1, and supplies position(452.2999878,278.6000061,277.6000061).
All three rotation fields are zero. Captured actor007A8540 has the expected
callback and position; its world matrix matches the exported identity plus
translation in all64bytes. Nonzero authored rotation is rejected by this
bounded exporter instead of passing through an unverified rotation helper.

Descriptor00828340 contains80 particles, flags9, mode2. Original001D04B0 calls
001CFA60/001CFBE0 with kind1 and the owner's phase/seed. This chooses micro
program00231770, whose first1912 instruction bytes are identical to the
already recovered snow generator. Its VU59 parameters are phase,1,1e-6,seed.
The80 scalar lookup values come from original ELF002342BC.

The immutable opening VU1 dump's last effect is this actor: all144 descriptor
bytes,64 transform bytes and80 lookup scalars agree. Its final24 particle
scratch records match the readable generator in all1152 position/color/size
bytes. These observations establish effect identity; they do not establish
whole-game RNG ordering or universal frame phase.

## Controller and submission

State0 constructs the world matrix, installs contact callback00823580,
sets half-extents7/15/7, clears cooldown and phase, sets sound handle−1,
and draws one shared random value for the seed. It immediately falls through
to state1. Each active update submits particles at the **old phase**, adds
0.025 with original scalar operation order, and subtracts1 if phase reaches2.
It services the sound, updates flags1/2 around the contact cooldown, then
calls001B17A0 for common actor spatial/category publication. States2/3 stop
the sound and release the owner.

The contact callback rejects target flag2 or the player's0021BB00 predicate.
On acceptance it requests attached effect80000027, writes target+0F=12 and
sets owner cooldown60. This callback is translated and tested, but native
candidate selection, effect80000027 and player reaction semantics are not
connected. The visual bounds are not used to invent damage.

The00231770 sprite projection matches the snow GS projection except that it
omits snow's extra near-camera attenuation. It uses the original guard band,
FTOI4 corner/depth quantization, plus-corner ST(0,0), minus-corner ST(1,1),
and integer color after fog. Its mode2 GS state is additive Cs+Cd with depth
test enabled and depth writes disabled. The separate texture occupies native
particle slot1; snowfall retains slot0. Metal uses the quantized GS corners
and maps depth to the native scene's existing convention.

Effect fog is the render context's +0xA0 quadword, copied at the owner's
DRAW call (its walk position; 001D04B0 programs no fog, its 001CFBE0 copies
the context's): with AREA11's light-rig entry 30 (near −209, far 304) the
context holds 255 / 2048 / 151.1111145 / −0.4970760345, as the saved
context does (EFFECT_MANAGER.md 8.2).

## Sound boundary

Original001FC3C0 retains a live handle, stops changed/dead sounds, and starts
or updates positional sound on `(global_frame + active_list_ordinal)%10==0`.
Owner008235F0 passes sound413, radius100 and volume4096. Its opening player
distance exceeds100, and the captured handle is−1. The old radius300 made
the port play sound during this otherwise silent opening view.

AREA11 bank0's script4/4 contains one note65 with velocity101 followed by
velocity0, not two independent notes. The second event calls the original
key-off path. Its VAG has loop-start block2 and loop-end1244, with ADSR
words33023/24523. The existing native one-shot WAV mixer discards loop and
envelope semantics. A new unconditional loop or guessed voice lifetime would
therefore be another fabrication. The original sound-service callback stays
explicitly unbound pending sequencer/ADSR and active-list scheduling recovery.
The ordinal17 in this one capture is not installed as a universal constant.

## Reproduction and validation

Run on native macOS Python with the decomp environment's dependencies:

```sh
../Extermination/.venv/bin/python tools/export_area11_effect.py \
  --ee ../Extermination/build/startup-reference/opening_ee.bin \
  --gs ../Extermination/build/startup-reference/opening_gs.bin \
  --vu ../Extermination/build/weather_reference/original_vu1.bin
make test-area11-effect-reference test-area11-effect-runtime
```

The exporter writes ignored EMEF/EMTX assets, replaces the legacy `steam`
manifest line with `area11effect`, and records source/output hashes under
`build/area11_effect_reference`. Required malformed/missing effect assets fail
loading; scene unload and re-entry release/reconstruct the effect state.

The original-instruction oracle passes280 controller state/call cases and768
contact cases. Projection passes1135 cases/67120 compared output bytes.
The real-asset runtime passes400ticks/30360 submissions under ASan/UBSan,
including re-entry and cleanup. Shared snow projection/runtime and lamp
oracle regressions also pass. Finite arithmetic is covered; exceptional VU
arithmetic and full GS rasterization are outside these tests.

Frozen native opening frame280 exits0 with the unchanged camera134.5,
actor half-tick269 and script PC829140 witness. Its player, rifle, subtitle
and scene remain intact. A separate three-frame GPU fixture renders the
original flame beside a blue slot0 control, proving texture-slot isolation
and visual submission. That fixture uses a synthetic close camera solely
for plumbing; it is not a captured original gameplay view. Images, binaries,
logs and hash receipt remain ignored under `build/area11_effect_reference`.

The native owner now consumes its original initializer RNG call, but full
actor scheduling and global RNG call ordering remain unfinished. These
changes establish original-derived behavior and scoped proofs, not complete
first-level or pixel fidelity.
