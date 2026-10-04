# AREA01 SFX resources

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

## Recorded evidence and conditional callers

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

## Original bank and sample identity

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

## Private installation and verification

`assets/sfx` in the level2 worktree was a symlink into the main checkout.
It is now a real private directory; its two registry files are private,
and ten unchanged top-level entries remain symlinks to their original
files/directories. All 15 files / 4,668,860 bytes under the original main
SFX directory have identical before/after hashes. The installed registry
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
