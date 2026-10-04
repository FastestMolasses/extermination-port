# AREA01 audio

The AREA01 SFX resources and the positional audio adapters.

Contents:

- [AREA01 SFX resources](#area01-sfx-resources)
- [AREA01 positional audio and shared scratch](#area01-positional-audio-and-shared-scratch)

## AREA01 SFX resources

The live registry now includes the existing AREA01 sub-0 exporter’s complete
1,000 area-paged IDs, plus global failed-grab cue `01AC`. The registry has
1,278 entries: 663 audible, 577 originally absent, and 38 unsupported,
using 188 samples. AREA01 contributes 403 audible, 561 absent, and 36
unsupported entries. This is finite table coverage, not a claim that every
entry is requested on the first visit.

`tools/export_sfx_registry.py` still uses the original `001FB9F0` resolver,
existing sound-bank binding, and EMSR v2 writer. It appends AREA01 after the
old scopes, with the old arrival ambient first, then `01AC`. All previous
278 serialized entries, their JSON provenance, the pitch ladder, and all
143 previous samples including loop-body PCM remain exactly equal. The new
entries add 45 samples. AREA11 and AREA02 scope selection remains explicit.

The native EMSR entry cap is raised from 1,024 to 2,048 to accommodate the
larger valid registry. No mixer, sequencer, loader format, or bank-selection
algorithm changes. The existing scene binding already calls
`em_sfx_set_area(1, 0)` for AREA01.

### Recorded evidence and conditional callers

The first-visit input is `../Extermination/build/s87/census/a01_delta.json`
and its 16 `runs/A01` files, with the sampled traces under `route_a01`.
The census logs function entry, not sound ID arguments. A static backward
constant trace inside the 867 functions tagged with phase `area01` finds
85 sound call sites: 75 constant sites covering 57 distinct IDs, eight
computed sites, and two forwarding sites. All 57 constants now resolve to
a global or AREA01 registry entry. This is not a complete request trace or
a proof of every computed/script-supplied ID.

| ID | Original caller evidence | Scope and source bank |
|---|---|---|
| `0411` | `001E3D90`, variant `+0D = 0`, call site `001E44A4` | AREA01 `(1,0)`, group 2 / slot 0, area row 0 |
| `0412` | Same owner, variant 1, site `001E44C8` | AREA01 `(1,0)`, group 2 / slot 0, area row 0 |
| `0413` | Same owner, variant 2, site `001E44EC` | AREA01 `(1,0)`, group 2 / slot 0, area row 0 |
| `044E` | `001FC280`, original arrival placement’s ambient field | AREA01 `(1,0)`, group 2 / slot 0, area row 0; already exported |
| `08A9` | `00826200`, conditional phase 1 at site `00826348` | AREA01 `(1,0)`, group 4 / slot 1, area row 2 |
| `01AC` | Failed-grab path `001C2770`, traced sites `001C2FA8` / `001C2FE0` | Global `(-1,-1)`, group 1 / slot 1, global row 1 |

`0411..0413` and `08A9` are area-paged IDs under the original remap tables,
not global entries. The flame variants are present in every one of the
15 AREA01 route endpoint images: five variant-0, seven variant-1, and five
variant-2 owners per image. `001E3D90` and `001FC3C0` are reached in all
16 first-visit census beats. Conversely, both bridge halves remain at
lifecycle 1 / phase 0 in all 19,798 sampled AREA01 rows; these rows do not
show the phase-1 `08A9` call. Its export covers the original conditional
branch, without claiming that first-visit playback was recorded. The
`01AC` sites are likewise conditional inside a reached caller.

The eight computed sites belong to footsteps `00182430`, script sound
dispatcher `001B6D70` (three), door sound `001B8020`, effect sound
`001EF940`, room ambient `001FC280`, and queued-sound flush `001FC6E0`.
Their area-paged possibilities are now exported; this audit does not infer
all their global IDs from function-entry evidence.

### Original bank and sample identity

The existing AREA01 binding is verified across all 16 available AREA01
images, including the first-level arrival. Driver table `00281D50` and
handle records `0027C6C0` select the retained global container
`extract/chunk00/f05_id05.bin` for group 1 handles 0..2. The AREA01 nested
upload section `extract/chunk05.n0/f00_id44.bin` supplies group 2 handle 4
and group 4 handles 5/6. Every bank header matches its source except the
48 driver-written per-track channel bytes at `+0A`. Group 3’s invalid
header remains refused.

The independent EMSR reader compares all 1,000 AREA01 entries with the
already-verified AREA01 catalog, normalizing only local sample indices.
The 172 samples sourced from the retained global and AREA01 containers
are checked inside their original bank body regions, with 1,194,688 ADPCM
bytes hashed against source and independently decoded. Loop markers and
all AREA01 repeat-body PCM match the existing catalog. The catalog’s
independent RAM derivation and mutation evidence are in
`AREA01_ASSETS.md`, “Sound banks.” No source capture is loaded by native
playback, and no AREA01 SPU-residency or hardware-waveform equality is
claimed.

The 36 AREA01 refusals remain explicit: 23 require modulation and 13 are
the already-documented tail-table IDs `09B7..09C3` with unbound records.
Running original `001FB9F0` over captured driver state makes 11 of those
tail IDs refuse; `09B8` and `09B9` find handle-0 scripts. The exporter keeps
all 13 unsupported because they name no valid captured area bank. They
are not silently relabeled as ordinary absent sounds or given guessed
waves. No recorded request for them is claimed. The existing mixer’s dry
reverb, linear interpolation, and documented-semantics ADSR boundaries
still apply.

### Installation and verification

The registry is a local, ignored export (`assets/sfx/sfx_registry.emsr`
and `.json`; rerun `tools/export_sfx_registry.py`). When it was first
generated on the level-2 branch, all 15 files / 4,668,860 bytes of the
previous SFX directory kept identical hashes apart from the registry. The installed registry
SHA-256 is `aa224124ef6aef8a4c4e34e68151a85e3bc4a43349f0fa0df8891c1a91d3efe4`.
All exports and manifests remain ignored local artifacts.

`tools/test_area01_sfx_registry.py` uses the existing original-instruction
SFX oracle and native driver. It runs original `001FB9F0` / `00119EA0`
dispatch, then the existing full sequencer comparison for supported
entries, including track/voice state, pitch, gain, sample address, ADSR,
key-on/key-off commands, and lifetime. The hardware command sink is the
explicit boundary; no audio device is opened.

Quick verification passed 1,001 native entry lookups, 64 original
dispatches, and six sequencer cases / ten key-ons in 5.00 seconds against
the installed private registry.
Full verification (`EM_TEST_FULL=1`) passed 16,016 original dispatches
across all 16 images and 404 supported sequencer cases / 839 key-ons in
225.41 seconds. The 13 unbound IDs produced the same result in every
image: 176 refusals and 32 track starts, all retained as unsupported.
Both modes verify the 16 capture bank bindings and four loader boundary
cases: valid 2,048 entries, refused 2,049 entries, truncated data, and extra
data.

The existing AREA11 original quick regression passes with the expanded
registry: 48 panel dispatch cases, 216 pitch cases, 92 registry/request
cases and 112 key-ons, all 270 first-level census IDs and 82 recorded
request IDs still resolved, and 140 samples equal the recorded SPU RAM.
The existing native runtime regression passes its 28 malformed registries,
scope/refusal/lifetime checks, and independent 44.1/48/96 kHz dry-mix
comparisons under ASan/UBSan. Logs:
`build/level2/sfx-registry-area11-original-quick.log` and
`build/level2/sfx-registry-area11-runtime.log`.

Receipts: `build/level2/sfx-registry-{quick,full}.log`,
`build/level2/sfx-registry/report-{quick,full}.json`, `caller-audit.json`,
`private-install.json`, and `main-manifest-before.json` in that directory.
These are resource and standalone driver proofs. Guarded native arrival
and route playback remain separate integration evidence.

## AREA01 positional audio and shared scratch

`em_area01_audio_services_call` binds `001FBD50`, `001FBF50`, and
`001B1380`, plus loop service `001FC3C0` and release `001FC520`, to the existing `em_player_misc_workers` and
`em_script_host_workers` translations. It adds no sound bank, voice table,
spatial-audio algorithm, or persistent scratch copy. `EmArea01Call` supplies
the original argument/register words. The existing SDK table/context supplies
the original math workers.

The adapter reads the original object's `+B0` quadword and the canonical
listener `00810360`, camera yaw `0081027C`, and camera eye `008105D0` views.
The mono option is borrowed through `em_stream_live_output_mode()` from
`S.lanes.state.mono` (`0028215B`); an unbooted/faulted owner refuses the view.
No stereo default is substituted. Its call-local typed scratch is loaded from
and published to the canonical `70003400` (64 bytes), `70003600` (16), and
`70003610` (16) views at each native-worker boundary.

`001FBD50` submits through `host.worker` at `001FB9F0`, after publishing those
scratch words. The host must suspend native actor/player/collision views,
call the existing SFX track owner, resume the views, and return the actual
voice result. The submit callback owns voices and must not change these
scratch spans. `001FBF50` writes the supplied gain outputs in the original
order, including when both output pointers are identical. `001B1380` reads
only the two XYZ vectors and returns the existing original pan-side result.

Missing spans, incorrect argument counts, misaligned QW source/scalar output,
or gain-output aliases with an input or touched scratch span refuse before
invocation. RAM mirror addresses are normalized for this alias check.
Unsupported aliases are not silently evaluated against stale copied inputs.
The adapter returns 0 handled, 1 unknown entry, or -1 for refusal/fault.

`001FC3C0` and `001FC520` borrow the existing SFX service through
`em_sfx_loop_service_bound`. Its requested/snapshot arrays, track atomics,
registry lookup, gain requests, stop and start remain `em_sfx.c`'s sole state.
`em_sfx_service_step` is still the sole cadence/handle algorithm. Its optional
store callback runs at every original handle store (including unchanged
values), and a negative gain-provider result stops before following voice,
handle or table writes. The AREA01 gain callback uses the same exact
`001FBF50` translation as the other positional adapter: starts use 4096,
updates use the caller's f13. The callback publishes canonical shared scratch
before returning. Actual handle stores request writable canonical memory at
that point; no optimistic writable pointer is obtained on a no-store path.

Keep actor/player/collision views active for these two loop calls. They invoke
only the existing SFX owner and exact gain/math providers, so no projected
native actor/player/collision worker runs inside the boundary. Unsupported
handle values and missing reached spans fail. `001FC520` with -1 does not
request a writable handle span. No current voice or registry is reconstructed
from captures. The binding does not add missing sound-bank exports.

### Canonical scratch lifetime

`EmCollSegmentFaceScratch` retains four contiguous quadwords at
`70003600/3610/3620/3630`: box minimum, box maximum, delta, and relative
position. The collision translations still read/write XYZ; the fourth words
are preserved bytes, not invented vector constants. The collision byte
provider exposes all 16 bytes of each vector and bounded joined views of
`3600..363F`. The latter lets the existing `001CD520` sprite borrow its
32-byte `3600/3610` working span. Compile-time offsets prove that the four
arrays are contiguous; each subrange has exactly the same pointer as the
corresponding independent vector. Requests crossing either end still
refuse. No second collision state or allocator is introduced.

`em_camera_live_scratch_bind` borrows the matrix at `70003400` and vectors at
`70003600/3610/3630`; `em_aim_fire_runtime_scratch_3600_bind` borrows all four
contiguous vectors. Binding changes pointers only. Detaching copies the last
shared bytes back to each pre-existing legacy owner and restores its default
storage. Mixed NULL camera arguments refuse. Their narrow getters expose
current owned/borrowed spans so the caller can preserve existing bytes before
binding. AREA11's unbound behavior stays unchanged.

Install aliases at the end of scene state-0 `w_001AFCA0`, after collision,
player/pose, camera, aim, effects and render owners have been reset/attached,
and before `001B07C0` or any following player/camera worker. Save the prior
pose pointers for detach. Copy the existing RCL `70003400..347F` bytes into
the draw matrix owner; preserve camera's current `3600`, `3610`, `3630`
quadwords and aim's current `3620` quadword in the collision face owner.
Bind pose, RCL, camera, and aim to these owners. Before a later area load, `area_read` calls `area01_scratch_detach` before
`em_game_legacy_area_load`: that loader unloads the existing player pose
host, so waiting for state 0 would leave no host from which to preserve the
last shared bytes. The detach copies current shared bytes back to the saved
pose pointers, restores those pointers, then detaches camera, aim, and RCL.
The same helper runs idempotently at the beginning of `w_001AFCA0` for a
state-0 reset without a new load, before collision reset. This ordering also
covers a same-AREA01 door reload; the first AREA11-to-AREA01 arrival alone
does not exercise the already-bound detach path. No capture, zero
seed, or late first-pool-walk installation participates in this lifetime.

The established native owners previously had separate scratch storage; the
handoff preserves their existing bytes and thereafter native call order
determines the single last writer. Relevant original writes are explicit:
pose `001C84D0` and `001CA0A0` write all four `3600` words before their QW
consumers; camera `001B0460` writes all four before Euler use; audio copies
full `3600/3610` before arithmetic; camera `00198440` writes `3610` before
each read; camera specials writes `3630` through its full vector subtract.
Collision's `3620/3630` operations consume XYZ and preserve W. This is a
bounded proof for these workers, not a claim that every untranslated scratch
consumer is covered.

Camera follow/specials use call-local typed views. Their segment callbacks
publish the typed view before canonical collision and reload it afterward
when aliases are installed. Without this nested boundary, the outer camera
store would overwrite the collision worker's actual last writes. Existing
cross-camera callbacks already publish/reload; `001DD980` updates render
context fields through `001DD950` and does not write this shared scratch.

#### Mechanism scratch 38A0..38FF

The AREA01 mechanism `001C02E0` reached `001028D0(38E0,38B0,38A0)` after
its allocation and pose setup. The former composite exposed the player
closure's explicit aim regions only, so `38E0` was missing even though
aim/fire already owned `38C0..38FF` in `scratch_38C0`. Adding another
scratch buffer would have hidden that ownership error.

`em_camera_live_scratch_38_bind(A0,B0,C0)` instead borrows the player
closure's existing `L.land.s38A0` and `L.foot_38B0` quadwords and aim/fire's
existing 64-byte span from `em_aim_fire_runtime_scratch_38C0()`. It copies
and initializes nothing. The camera leftovers, camera commit, aim-camera
memory resolver, and follow/special typed-view boundaries all use these
same pointers. `EmCamLeftScratchAliases` contains pointers only. The
camera's first 96 private bytes are inactive while bound; no worker reads
or writes them. Its remaining `3900..3A3F` words retain their existing
camera ownership. The separate legacy `3A20` overlap is outside this change.

The live resolver authoritatively owns requests overlapping `38A0..38FF`
and delegates to `em_camera_live_scratch_bytes`; incompatible spans fail.
An A0 or B0 query cannot cross its 16-byte owner boundary. C0..FF is one
contiguous 64-byte owner, so both E0 and F0 quadword requests succeed.
Empty, wrapping and out-of-range requests refuse. Bind requires three
non-null aligned pointers, or three null pointers for detach; replacing
a live binding with different pointers before detach refuses.

Root integration uses the same early `w_001AFCA0` handoff as the earlier
scratch aliases: find A0/B0 in the configured player's writable aim-region
descriptors and borrow C0 from its existing owner, before any subsequent
player/camera work. `area01_scratch_detach` detaches these aliases before
`em_game_legacy_area_load` unloads the player owner. Detach preserves all
96 last-written bytes in the camera's legacy backing and drops its
borrowed pointers. Aim/fire retains its own C0..FF storage throughout.

No captured or zero-filled initializer is added. On the reached mechanism
path, `001C02E0` writes all four A0/B0 words before vector use, then the
original SDK writes all four E0 words before normalization/dot reads; it
also writes all four F0 words before that vector's normalization/dot reads.
The existing aim/fire writers likewise fill the scratch they consume.
This establishes the reached writes' provenance; it does not invent an
initial value for an as-yet-untranslated scratch consumer.

### Proof and limits

`tools/test_area01_audio_services_reference.py` executes the pinned original
ELF's gain, pan, SDK and math instructions. Only voice submission at
`001FB9F0` is an explicit test boundary returning 27 on both sides. A sparse
synthetic finite fixture compares every source, gain output, touched scratch
byte, submit argument, and return word. Quick/full run 120/900 chains. The
cases include mono values 0/1/2, flat low-byte behavior, radii 0/18/300/800,
multiple attenuation scales, and identical gain-output pointers. Refusal
checks preserve all observed bytes for missing writable scratch, aliases
(including EE RAM mirrors), and misaligned output. The test does not claim
an audio-mixer or complete AREA01-frame comparison.

`tools/test_area01_collision_view_reference.py` additionally seeds all four
native W lanes from each captured original scratch image and compares them
after every original collision-worker call. Quick/full cover 1,095/37,365
calls across 15 captures, plus 4,560 view-boundary checks; existing XYZ,
result and state comparisons remain intact.

`tools/test_area01_scratch_alias.py` runs an ASan/UBSan contract over actual
camera, aim, and pose owners. It proves pointer identity, exact span bounds,
pose-to-camera propagation, camera-to-owner stores, detach preservation, and
the nested camera/collision publish/reload order. Its nested collision stub
only checks the boundary contract; the collision body is proved separately
by the original-instruction comparison above.

The extended scratch contract also poisons the inactive camera bytes,
checks separate A0/B0/C0 owner identities, same-binding idempotence,
replacement/mixed-null and cross-owner refusals, camera typed-view
publication, and detach preservation. Its original SDK comparison runs
320/2,000 quick/full calls through the actual memory adapter against
these split owners, comparing all 96 bytes and dot returns. It includes
E0/F0 destinations, in-place normalization and source/destination aliases.
Receipts are `build/level2/scratch-alias38-{quick,full}.log`.

The camera leftovers and commit original oracles also run with
`EM_CAMERA_SCRATCH_ALIASES=1`. Leftovers use the alias resolver over their
original-address fixture; commit uses disjoint alias arrays and poisons
its inactive backing. Default runs keep the existing unbound contract.
Their receipts are `build/level2/scratch-camera-{leftovers,commit}-{quick,full}.log`,
`scratch-camera-alias-{quick,full}.log`, and
`scratch-camera-commit-alias-{quick,full}.log`.

Receipts: `build/level2/audio-services-{quick,full}.log`,
`build/level2/collision-view-quad-{quick,full}.log`, and
`build/level2/scratch-alias.log`.


Loop service proof extends `test_area01_audio_services_reference.py`: quick/full
now pass 160/1,000 loop/release chains in addition to 120/900 gain/pan/submit
chains. The original executes `001FC3C0`, `001FC520`, `001FBDB0`, `001FBD50`,
`001FBF50`, SDK and math instructions; original status/start/stop/request
calls are explicit device-boundary observations. Native tests use the actual
`em_sfx.c` tables, track atomics, registry and loop owner. The fixture's device
ensure returns success without opening audio hardware. Tests compare all
source/output/shared-scratch bytes, the handle, every requested-table word,
and native start/request gain words and stop states. They cover cadence,
missing snapshot IDs, changed IDs, no free tracks, live repan, out-of-range
sources, mono modes, release, f13=1/512/4096, and denied same-value handle
stores. Fixture voice states are controlled test data, not runtime seeds.

Shared worker and pre-existing AREA11 evidence is in
LEVEL2_RUNTIME.md ("AREA01 shared worker binding"); SFX regression receipts are
`build/level2/sfx-loop-owner-{quick,full}.log` and
`build/level2/sfx-loop-owner-runtime.log`. An AREA01 sound ID still requires
its correct globally or AREA01-scoped exported bank; worker equality alone
does not establish audible or whole-route support.

The completed existing SFX full regression passed 3,000 original loop
service cases, 1,500 voice-table cases, 1,355 registry/request combinations,
and 9,813 sequencer ticks. The native runtime ASan/UBSan test also passed.
