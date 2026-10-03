# AREA11 particle hazard: original owner008235F0

The old port called this a steam emitter and invented two pale billboards,
a120-tick rise/fade cycle, position(452,279,278), and sound413 retriggers every
90ticks with radius300. None of those visual/timing choices follows the
original owner. The actual resident texture is an orange/yellow flame.

The flame draws from its original packets on the chain page (step FLAMESNOW,
2026-09-28; CHAIN_PAGE.md section 6.1): the owner's DRAW is 001D04B0 bound to
em_effects_live, whose 001CCF70 / 001CFA60 / 001CFBE0 put the flame's
sprite-program chain into page D_007635C0, and the page consumer runs the
sprite program's translation (em_vu1_page_programs.h) on it. The invented
sound retrigger stays removed. Since chain step A11FIX (2026-10-02) the
whole owner runs on its pool record, against the decomp's byte-identical C
(src/overlays/AREA11/func_overlay_AREA11_008235B0.c and
overlay_AREA11_func_00823540.c): its loop sound 001FC3C0 / 001FC520, its
publication 001B17A0, its +0x30 / +0x34 stores and the contact behaviour
0x823580 that the collision world's contact pass calls. Since chain step
DAMAGE (2026-10-02) the damage the contact deals runs too: 001EFE00(
0x80000027) at the player (the burn node) and 001A8660's knock-back
(below; DAMAGE.md sections 2 and 3).
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

Descriptor 00828340 contains 80 particles, flags 9, mode 2. Original 001D04B0
(an asm function in the decomp; its three calls read from the .s) runs 001CCF70(+0xD0 + 0x30) for
the depth key, 001CFA60(block, +0xD0, phase, seed) and 001CFBE0(key, 1,
D_00828340, block, 0). Mode 2 with kind 1 chooses the sprite program packet
0x00231770 (its lookup equals the snow program's 002342BC) and blend preset
2 (additive). Its VU59 parameters are phase, 1, 1e-6, seed.

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

The contact callback 0x823580 rejects bit 1 of the player's +0x00 or a
nonzero 0021BB00(D_008102B0) (0021BB00 is called only when the bit is
clear). On acceptance it calls 001EFE00(0x80000027, player), then writes the
player's +0x0F = 12 and the owner's cooldown +0x210 = 60
(em_area11_effect_contact, its callees as workers in that order). The
collision world's 001A8BE0 calls it through 001A8660 when the player's circle
(the radius and height of D_00275490, the block 0015C420 stores at the
player's +0x30) overlaps the flame's (+0x30 = the record's +0x1F0 block:
radius 7, height 15). The visual bounds are not used to invent damage.

The sprite program's emission is the snow program's without the near
weight (CHAIN_PAGE.md section 3). Its mode-2 GS state is the preset bank's
(additive Cs + Cd, TEST 0x53001: depth GEQUAL, no depth write); the page's GS
path draws it (CHAIN_PAGE.md section 5). The flame's TEX0 is the
descriptor's +0x70 row, a page texture (tools/export_page_textures.py /
export_disc_textures.py).

Effect fog is the render context's +0xA0 quadword, copied into 001CFBE0's
packet 4 at the owner's DRAW call (its walk position; 001D04B0 programs no
fog): with AREA11's light-rig entry 30 (near −209, far 304) the context holds
255 / 2048 / 151.1111145 / −0.4970760345, as the saved context does
(EFFECT_MANAGER.md 8.2).

## Sound

Original001FC3C0 retains a live handle, stops changed/dead sounds, and starts
or updates positional sound on `(global_frame + active_list_ordinal)%10==0`.
Owner008235F0 passes sound413, radius100 and volume4096 (001FBD50 passes its
own 4096, so the caller's f13 is not read). Its opening player distance
exceeds100, and the captured handle is−1. The old radius300 made the port
play sound during this otherwise silent opening view.

Since A11FIX the owner's SOUND call is em_sfx_loop_service (the verified
001FC3C0, em_sfx.h) over the record's +0xB0 and +0x20C, with the scratchpad
frame counter 0x70003B68 and the walk ordinal 0x70003B8A of EmSceneState;
STOP_SOUND is em_sfx_loop_release (001FC520). The decomp's audio capture
(CAPTURES_AUDIO.md, the `flame` beat, route 11, 16.5 units from the flame)
holds 0x413 in D_00281B78 / D_00281C38 in every frame, and the `cage_roof`
beat first holds it at f885: the original keeps this loop alive near the
flame. What reaches the speakers is the port's SPU2 voice model (FIRST_LEVEL_
AUDIT.md 1b item 1).

AREA11 bank0's script4/4 contains one note65 with velocity101 followed by
velocity0, not two independent notes. The second event calls the original
key-off path. Its VAG has loop-start block2 and loop-end1244, with ADSR
words33023/24523. The existing native one-shot WAV mixer discards loop and
envelope semantics. A new unconditional loop or guessed voice lifetime would
therefore be another fabrication. The sequencer and voice model the loop
plays through are em_sfx's (SFX_SEQUENCER.md); no loop or voice lifetime is
invented here. The ordinal comes from the live walk, not from the ordinal 17
of one capture.

## Reproduction and validation

Run on native macOS Python with the decomp environment's dependencies:

```sh
../Extermination/.venv/bin/python tools/export_area11_effect.py
# optional developer checks against the opening capture and its VU1 dump:
#   --ee ../Extermination/build/startup-reference/opening_ee.bin
#   --vu ../Extermination/build/weather_reference/original_vu1.bin
make test-area11-effect-reference
```

The exporter writes the ignored EMEF asset (placement, descriptor; its EMTX
texture is no longer read: the manifest's `area11effect <config>` takes an
optional second token of older manifests and ignores it), replaces the legacy
`steam` manifest line with `area11effect`, and records source/output hashes
under `build/area11_effect_reference`. A malformed or missing EMEF fails the
load; scene unload and re-entry release and reconstruct the effect state.

## Binding (live)

- **Owner.** Node 008235F0 (area11[7]) runs tick_effect
  (em_area11_bindings.c) -> em_area11_effect_runtime_tick ->
  em_area11_effect_tick over the record: +0x00 / +0x04 are EmActor's
  status / u04[0], the +0x1F0 block its scratch, +0x30 / +0x34 its w30 /
  w34 (state 0 stores the record's own +0x1F0 and 0x823580). A failed
  callee faults the scene at its address.
- **Sound.** 001FC3C0(self, +0x20C, 0x413, 100, 4096) =
  em_sfx_loop_service(+0x20C, 0x413, +0xB0, 100, 0x70003B68, 0x70003B8A);
  001FC520 = em_sfx_loop_release.
- **Publication.** 001B17A0 = em_area11_interaction_host_offer_001B17A0
  (001B1630 on the camera, then 001B1B70 when visible: the record goes on
  the class-0xD list, class 0xD, type +0x03 = 1).
- **Contact.** The close-out 001AAD00's 001A8BE0 walks the class-0xD list;
  for the flame (type 1) 001A8660 reads the player record (the live image,
  em_area11_bindings.c area_records) and the flame's record (its
  em_actor_pool_record_image), and the player's +0x30 block D_00275490
  (assets/collision_contact.emrg, tools/export_collision_contact.py). On an
  overlap it calls the +0x34 word through em_collision_world_bind_behaviour:
  flame_behaviour runs em_area11_effect_runtime_contact. Its 0021BB00 is
  em_player_0021BB00. Its 001EFE00(0x80000027, player) is
  em_area01_side_001EFE00 through the aim / fire composition, with the
  player record's close-out view of +0xA0 / +0xB0 for the call
  (em_area11_bindings.c flame_001EFE00; DAMAGE.md section 2); the node it
  spawns is the burn node 0022BBC0 (DAMAGE.md section 3). 001A8660's
  knock-back (when the player's +0x00 is 1) reads the table D_0024A740
  (assets/collision_knockback.emrg, tools/export_collision_contact.py) and
  stores the pending damage +0x224 on the player's one vitals storage. No
  recorded main-line route beat touches the flame (the closest is 16.5
  units away; contact needs 10); the DAMAGE side run dmg_flame does
  (LEVEL_SMOKE.md "The DAMAGE side runs").
- **Free.** States 2 / 3: 001FC520, then 001AFC10 (the pool's free); not
  reached in the first level.
- **DRAW.** em_effects_live_001D04B0(+0xD0 matrix, 1, D_00828340, the
  descriptor's 0x90 bytes, phase, seed) (em_effects_live.h). The descriptor
  bytes stay readable through em_effects_live_window until the next attach,
  so the page's REF of D_00828340 reads them.
- **Page.** em_chain_page_live walks the flame's chain with every other
  producer's (CHAIN_PAGE.md section 7).

## Verification

- `make test-area11-effect-reference`: the original-instruction oracle passes
  280 controller state / call cases (the DRAW call's arguments (1,
  D_00828340, phase, seed) and the +0x30 / +0x34 stores included) and 768
  contact cases (the callees in order: 0021BB00 only with bit 1 clear, then
  001EFE00(0x80000027, player); the stores; a failing 001EFE00 faults before
  them); the overlay's
  callback and descriptor bytes equal the opening capture's, and the opening
  VU1 dump's last effect holds that descriptor, the owner's matrix and the
  lookup.
- `make test-chain-page-reference`: every captured page holds the flame's
  sprite-program MSCAL; the translation equals the ORIGINAL microcode on it.
- The level smoke (check_chain_page): every world page reads the flame's
  descriptor once in the frames its 001D04B0 ran; in the camera-exact
  snapshots 10 and 14 the flame's 001CFBE0 packets 4 (the projection rows
  and the fog) and 1 (the matrix and the MSCAL; phase and seed masked) equal
  the capture's; the sampled pages re-walked with the original microcode
  draw the port's primitives.

The native owner now consumes its original initializer RNG call at the
original's position: in the rand() order audit (RAND_ORDER.md) its draw is
AE+1's, after the security gun's (bound on its owner since census L24).
Full actor scheduling remains unfinished. These
changes establish original-derived behavior and scoped proofs, not complete
first-level or pixel fidelity.
