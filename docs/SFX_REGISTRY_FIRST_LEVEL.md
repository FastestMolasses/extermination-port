# First-level sound census and registry export (WP-14)

Lane "sfx-registry-complete", 2026-09-25. Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Many first-level sounds were silent because `tools/export_sfx_registry.py`
only exported the ids of the decomp's hand-written scene presets
(`tools/gen_sfx_registry.py` SCENES). For an id that is not in the registry,
`em_sfx` does nothing: 001FBD50 returns -1, so for example the slide's +31B
stayed -1 and 0016CD70 requested 0x12E again on every motion tick. This
lane lists every sound id the first level can request, with evidence for
each one, and exports all of them for the AREA11 scope (11,0) through the
same original path (`docs/SFX_PITCH.md`, `docs/SFX_SEQUENCER.md`).

**Result.** The census has 270 ids. 64 were already exported by the presets.
The other **206 were missing and are now exported**: 191 audible, 13 ABSENT
(the original itself plays nothing for them in AREA11) and 2 UNSUPPORTED
(listed in section 4). The registry grows from 71 entries and 56 samples to
277 entries (259 audible, 16 absent, 2 unsupported) and 141 samples. The 71
original preset entries and 56 samples, and all 262 entries and 136 samples
of the previous round, are byte-identical in the new export.

Round 3 (2026-09-25) made the census follow the code the route reaches, not
only the code it ran: the scan (section 1, CONST) now takes the callees,
installed behaviors and function tables of every included function to a
fixpoint, and a completeness check in the port test fails when an id
reachable that way is not exported. It found 18 ids the earlier census
missed (section 2, last six rows).

## 1. Where the ids come from

The census lives in the decomp's `tools/gen_sfx_registry.py`
(`FIRST_LEVEL_GROUPS`, `first_level_ids()`, scope `FIRST_LEVEL_AREA = "11.0"`).
The port exporter appends the census ids after the presets
(`scene_ids()`), so the preset entries keep their sample indices. No matched
decomp code was touched. The census uses three kinds of evidence:

- **ROUTE.** The original requested the id on the recorded route. The new
  decomp tool `tools/sfx_request_probe.py` replays route_census's segments
  in the hidden PCSX2: the startup segment (title, New Game, the opening,
  200 idle frames) and route beats 00..14, 14,373 frames in total. It sets
  persistent breakpoints on 001FB9F0 (a0 = id), 001FBD50 (the positional
  entry, a1 = id, with its caller) and 001FC580 (the impact queue), and
  records v0 at the return address. It found 1,273 requests and 82 distinct
  ids in 11.0 (plus 0x5DD at the title, area 0.0). Output:
  `../Extermination/build/sfx_probe/report_A.json` (ignored).
- **CONST (reachability scan).** A constant id at a sound call or tail
  jump: 001FB9F0 a0, 001FBD50 or 001FC580 a1, 001FC3C0 a2.
  `sfx_request_probe.py scan` reproduces this evidence, and the port test
  runs the same scan (section 3):
  - **Seeds:** every function route_census executed (1,184,
    `build/s87/census/route_functions.json`) plus its `PORT_BOUND` list (40
    functions the census cites, 35 of them not executed on the route; the
    closure below reaches almost all of them anyway).
  - **Closure, to a fixpoint:** from every included function the scan
    follows its direct calls and jumps, every address-taken function (an
    upper/lower immediate pair that forms a function start: a behavior or
    callback the code installs) and every function-pointer table such a
    pair addresses. This takes in the direct successors and siblings of the
    included states: the player dispatch 0015B130 (route) calls every
    handler of its state table jtbl_0026D3B0, so all player states and
    their workers are included, and the gun's 00186A60 installs the impact
    marker 0018ABA0. Function starts and sizes come from
    `build/s87/census/candidates.json` (boot and the AREA11 overlay).
    Calls through a register whose target is held only in data the scan
    does not address are not followed; the route seeds cover the ones the
    route ran.
  - **Trace:** at each jal or j into a sound entry the id register is traced
    back (the delay slot, then linearly backwards; no control-flow graph)
    to a constant, a constant plus rand5 (the result of 00179B90, 0..4), or
    the function's own argument register. An argument makes the function a
    forwarding entry whose callers are traced in turn (001F02C0, whose
    caller 001F3620 gives 0x16A). A call (jal or jalr) clobbers the
    caller-saved registers, so the trace stops there unless it follows a
    saved register (s0..s7, fp); it also stops at a load or any other
    computation.
  - **Result** (`build/sfx_probe/scan.json`, ignored): 1,971 reachable
    functions, 311 sound call sites, 147 constant ids and 7 derived DATA
    ids (below), 151 distinct ids, **all exported**. The 7 sites whose id
    is loaded or computed each have a DATA rule (`DATA_RULES`): derived
    from original data in the capture (001FC280, 0016F600, 001EF940),
    documented by a census group (00182430, 0018A180), the preset (the
    door pair 001B8020, 0x401/0x402), or the 001FC580 queue flush
    (001FC6E0, whose ids are the queued ones). The rand5 families
    (00182A70, 00182AB0, 00182AF0, 0016AE40, 00169730, 0016D130, 0016DE40,
    the gear layer) now resolve as constant plus rand5.

  Each value was also checked against the decomp C wherever the function
  has C. The tail jumps are 00187DC0 (0x86), 00187EA0 (0xA8) and the status
  cue thunks 0020CD40/60/80/A0 (0, 1, 2, 4).
- **DATA.** An id that original code computes from data the first level
  can present:
  - **Footsteps and landings.** 00182430 plays the block for +23A, plus the
    tier offset (tier 2: +5, tier 3: +0xA), plus rand5. It then plays the
    gear sound, 0x138 + rand5. 00182870 plays the landing id for the
    surface. 00179B90 is rand5: `rand() & 7`, with 5..7 folded to 0..2.
    The surfaces that can reach +23A in AREA11 come from three sources:
    - the captured AREA11 grid (3,099 nodes): attributes below 0x1E are
      0, 3, 4 and 5; attributes in 0x5A..0x77 are 0x5A and 0x5D;
    - the static cells, `D_0024D7C0[11][0]` kinds below 0x1E: 0, 3, 4, 8,
      0xB and 0xD;
    - the published class-4 owners' +0x54 in beats 00..14: 0, 3, 4, 0xB
      and 0xD. Kind 0x46 passes neither walker gate.

    Surfaces 0xB and 0x5D use the default block. This gives the families
    for surfaces 0 (0x10), 3 (0x43), 4 (0x54), 5 (0x65), 8 (0x87), 0xD
    (0xDC) and 0x5A (0x76). Each family has 15 step ids and its landing
    ids. Surface 0x5A has one landing id (0x85) because 00182870 plays
    nothing for the second argument there.
  - **rand5 families** in the player's closure states: 00182AF0 (0x100+),
    00182A70 (0x109+, the ladder), 0016AE40 (0x112+), 00182AB0 (0x11B+),
    00169730 (0x124+) and 0016D130 / 0016DE40 (0x13F+). The scan now
    resolves these itself (constant plus rand5).
  - **0018A180:** 0x180 + (rand & 1), which gives 0x17F..0x181.
  - **0016F600:** the holster table `D_00248680[+0x275]`, six sub-unit
    entries read from the capture (0x168, 0x5DE, 0x5DF, 0x5E0).
  - **001EF940:** effect records with a sound, read from the capture. Of
    the global table's 0x78 records only 0x27 (0x14A) and 0x44 (0x14B)
    have +0x24 other than -1. The AREA11 table 0x2595B0 (36 records, to the
    end of the table region 0x259C70) has none. Open: 001EF9D0's special
    ids 0x80000026/2C/67 write 0x18C..0x193 (or -1) into +0x24 of the
    global table's record 0 (EFFECT_ORIGINAL.md), and that record is played
    only when effect 0x80000000 is spawned; no scanned code spawns it by a
    constant id, but the data-driven spawners (001B41F0, 0014CDD0, ...)
    were not enumerated.
  - **001FC280, the room-ambient selector** (the route runs it in S1, S2,
    01, 03 and 09). Its id is the high half (sra 16, so 0xFFFF is -1:
    none) of word +0x20 in `D_0024D650[area][room] + D_00810702 * 0x30`.
    It forces 0x44E when the area is 0xB and event flag 0x30
    (`D_00810788`) is 0xFF.
    - **AREA11's own table has 4 records** (0..3, all none): the room
      tables are packed, and AREA13's room-0 table starts 0xC0 after
      AREA11's (0x24C850 → 0x24C910). What earlier rounds read as "AREA11
      entries 4..9 = 0x44F" are AREA13's records. The route captures hold
      entries 0 and 2 (the door re-places at entry 2 or 1), and the probe
      logged no 001FC280 request in 11.0.
    - The scan's rule (`_room_ambient`) is deliberately conservative: it
      takes records 0..3 plus every constant any reachable function stores
      to `D_00810702` (2, 3, 4, 5, 7, 8, 9, 10: 0016D130, 0016DE40,
      001963A0 and anim_frame_top_a), whose area gating this lane did not prove. Those
      records give **0x44F, exported for 11.0 as ABSENT** (the 11.0 remap
      is 0xFF; the original 001FB9F0 returns -1 over both captures). The
      door loader 001AD010 stores the door row, which for AREA11's door is
      2 or 1.
    - **0x44E is excluded** because it is revisit-only. Event flag 0x30 is
      0 in every capture through beat 15 (the port test asserts it is not
      0xFF in both AREA11 captures). FIRST_LEVEL_AUDIT.md INV-08 has the
      reason: within AREA11, only manager 1's own script sets the flag,
      and that script waits on the flag first. The only op06 record that
      sets it is in AREA17. Beat 15 has already left AREA11: it is area 1.0
      with cached ambient 0x44E from area 1's own records. That request
      belongs to the next level's scope, not 11.0.

Every id the original requested on the route is in the census or the
AREA11 preset (`sfx_request_probe.py report` prints
`outside_census: []`). The probe also found an id that the first static
pass missed: 0x86, from the tail jump in 00187DC0 that the floor service
reaches (beats 08 and 10).

## 2. The ids that were missing and are now exported

`*` = requested by the original on the route (ROUTE evidence as well).

| Group | Added ids |
|---|---|
| Footsteps (00182430) and landings (00182870), 92 | 0x10..0x14, 0x1F*, 0x20; 0x43*..0x46*, 0x47..0x4C, 0x4D*, 0x4E, 0x4F*, 0x50..0x53; 0x63*, 0x64*; 0x65*..0x67*, 0x68, 0x69*..0x6D*, 0x6E, 0x6F*..0x75* (0x74: 00182870(p, 0) on surface 5, from the climb's 0017DEB0); 0x76*, 0x77..0x79, 0x7A*..0x7C*, 0x7D..0x80, 0x81*, 0x82*, 0x83, 0x84*, 0x85; 0x87..0x96; 0xDC*..0xE0*, 0xE1..0xEA, 0xEB*, 0xEC* (0xEB/0xEC: 00182870 on the crates' surface 0xD) |
| Player rand5 families, 30 | 0x100..0x104, 0x109*..0x10D* (ladder), 0x112..0x116, 0x11B..0x11F, 0x124..0x128, 0x13F..0x143 |
| 0018A180, 2 | 0x180, 0x181 |
| Holster table, 3 | 0x5DE, 0x5DF, 0x5E0 (ABSENT, section 4) |
| Climb, drop, ladder, slide, reversal, 14 | 0x12B* (crate climb), 0x12C*, 0xFE, 0xFF, 0x107* (ladder), 0x10E, 0x10F, 0x123, 0x187, 0x12E* (slide loop), 0x137 (reversal skid), 0x13D, 0x86*, 0xA8 |
| Hang, ledge, fall, weapon states, damage, 18 | 0x119, 0x120, 0x121, 0x122, 0x134, 0x105, 0x106, 0x186, 0xCA, 0xDB, 0x17A, 0x12F, 0x148, 0x150, 0x154, 0x15A; 0x5DC, 0x5DD (ABSENT, section 4) |
| AREA11 owners, pickups, 5 | 0x19C, 0x19D, 0x19E, 0x19F (001551B0 / 00156620, some queued through 001FC580), 0x194* (the item pickup 00219550) |
| AREA11 overlay owners, 8 | 0x455* (truck; frame 110 of the fall per TRUCK_ORIGINAL.md), 0x451* (fan 00827630), 0x423, 0x425, 0x426, 0x427; 0x424, 0x428 (UNSUPPORTED) |
| Flame loop service 001E3D90, 2 | 0x411, 0x412 |
| Effects (001EF940), 2 | 0x14A, 0x14B |
| Frame machine, status pages, message presenter, 14 | 0xC, 0xD (001AE040), 0xA, 0xB*, 0x6*, 0x5, 0xE, 0xF, 0x182, 0x0*, 0x1*, 0x2, 0x4* (the UI cues), 0x8C9 (ABSENT) |
| Room ambient 001FC280, 1 | 0x44F (ABSENT; see section 1) |
| Player states 17 and 23, 3 (round 3) | 0x110 (0016AC50, state 17: entered from 0015D4C0 on surface 0x1E and from the route's 0021C440; hands off to state 18); 0x135, 0x136 (0016BF80, state 23: 0x136 in cases 1/3, 0x135 in 2/3; entered from state 22 0016BC40 case 3, 0015B130's special-mode arm and 0021C440). 0x135 is a sustained loop with no key-off: its requester stops the track |
| Gun impact marker 0018ABA0, 3 (round 3) | 0x188, 0x18A, 0x18B (0x189 was in the office preset). The behavior every gun shot's 00186A60 / 001861C0 installs: +0x2E bit 0x10 (or the 0x300 flag arms) selects 0x18A / 0x18B, otherwise 0x188 / 0x189; the member is (00122BB8() >> 12) & 1 |
| Hit application 001B41F0, 2 (round 3) | 0x15B (queued through 001FC580), 0x1B1 (0x15D was in the office preset); called by the shot paths 00186A60 / 001861C0 / 00189FE0 and the projectiles 0018AF50 / 0018B3E0 |
| Debris clatter 001F3620, 0 new exports (round 3) | 0x16A (type 3) is in the census now; the office preset already exported it globally. Found through the forwarding thunk 001F02C0 |
| SPR4 screen sub-modules, 1 (round 3) | 0x17B (00217090 / 002177B0 / 00217FA0 / 00218640 / 00218D90, the state 4..8 modules of the status page 00211970) |
| Passcode keypads 002072C0, 6 (round 3) | 0x8C6, 0x8C7, 0x8C8, 0x8CA, 0x8CB, 0x8CD (ABSENT, section 4): the keypad callback 00207350 that 002072C0 installs (status pages 4/5 of 0020CDC0, opened on the request D_008106C5) and its helper 002072A0 |

The preset already carried: 0x454 (truck), 0x452/0x453, 0x413, 0x19A,
0x1A0/0x1A1, 0x3EE/0x3EF, the door pair 0x401/0x402, the snow block
0x54..0x62, 0x15..0x1E, 0x97, the gear layer 0x138..0x13C, and the office
preset's global weapon and damage ids (0x146..0x169, 0x179, 0x17D..0x17F,
0x168; since round 3 also 0x189, 0x15D and 0x16A, which the census now
lists).

Probe notes:
- 0x413 (the flame) was requested 812 times on the route. 808 of those
  requests went through 001FBD50 while 001FBF50 refused the gains (out of
  range), so no track started.
- 0x3EE was the one request where 001FB9F0 itself returned -1. It is
  exported ABSENT.

## 3. How the export is checked

- **Existing entries byte-identical.** The 71 previous entries (header and
  operations) and the 56 previous samples are identical to the previous
  export. They were compared with a parse of both EMSR files. The previous
  registry is kept in `build/sfx_registry_census/pre_census.emsr`.
- **The original bank on the user's disc**
  (`make test-area11-sfx-reference`):
  - The census scope must resolve every census id: in 11.0, or globally.
  - The bank binding check is unchanged: in both AREA11 captures, the RAM
    headers equal the bound container banks from `extract/`.
  - **All 139 AREA11-reachable samples equal the SPU RAM the original
    loaded** in the opening capture, including their loop points. The
    other 2 of the 141 samples come from the office region.
  - Every audible entry runs in lockstep with the original 001FB9F0 +
    001152D8. Pitch, both volume words, SPU address and ADSR must equal the
    original's commands for every key-on, and every track and voice field
    must agree on every tick.
  - Full run (`EM_TEST_FULL=1`, about 6 min): 1,355 entry × request cases,
    2,080 A0 voices and 257 entries with SPU2-model feedback (round 3).
  - Quick run: every preset entry plus a covering sample of 12 census
    entries (every bank and every loop, key-off and portamento form).
  - **Every non-AUDIBLE entry against the original**, whatever its reason.
    The test runs the original 001FB9F0(id, 0x1000, 0x1000, 0x1000) over
    both AREA11 captures (playable and opening). A result of -1 must be
    ABSENT, and a track must be UNSUPPORTED: 16 entries, 32 runs. The
    native driver must also refuse every UNSUPPORTED entry.
  - **Census completeness.** When `../Extermination/build/s87/census`
    exists (ignored, local), the test runs `sfx_request_probe.scan` in
    process over the playable capture, with the exported 11.0/global ids
    as the census. It fails when a reachable id (constant, rand5, forwarded
    or derived from original data) is not exported, when a loaded or
    computed id site has no DATA rule, or when a DATA rule names no census
    group. Otherwise it prints SKIP. Mutants run through a wrapper that
    bypasses only the installed-registry staleness guard: dropping 0x44F
    (a DATA id off the route), 0x135, or 0x188 + 0x18A from
    `FIRST_LEVEL_GROUPS` each fail with "reachable ids not exported for
    11.0". The scan's trace stops at jalr as at jal (checked on synthetic
    sequences: a clobbered a1 gives no constant, a saved s0 survives).
  - **Endless loops.** 0x135 keys on a sustained looping tone and never
    keys it off; its requester stops the track. The lockstep passes stop it
    with a soft 0011A070 (pass A 60 ticks after its last event, pass B at
    tick 120) and then require the usual settle; the random scenario leaves
    such entries out, since nothing in its plan would stop them.
  - The exporter's refused slots are checked in both captures. Group 3
    bank 0 names handle 3, and that handle's `D_0027C6C0` header fails
    00119EA0's SShd check.
- **The original's own results on the route.** When
  `../Extermination/build/sfx_probe/report_A.json` exists, every id the
  original requested in 11.0 must be exported. An id whose 001FB9F0
  returned a track must be AUDIBLE, and one that returned -1 must be
  ABSENT. The test reads only the report's 11.0 counts (`by_area`), so a
  request made in another area cannot mix in. Over the route's 464
  original starts, none contradicts the export.
- `make test-area11-sfx` (native loader, scopes, refusals, mix against the
  independent SPU2 model) passes with the 277-entry registry.

## 4. Not exported as audible (with reason)

- **0x424, 0x428** (the 00825940 owner; UNSUPPORTED, modulation). These
  tones carry flag 0x20 (00115850 voice +0x14). The native driver
  (`em_sfx_bank.c`) has no modulation yet. The original does start a track
  for them (checked in the test).
- **0x5DC, 0x5DD, 0x5DE, 0x5DF, 0x5E0** (ABSENT). Their records name
  group 3, bank 0. In both AREA11 captures, `D_00281D50` gives handle 3
  for that slot. The handle's `D_0027C6C0` record is in use, but its header
  (0x1800030) has no SShd magic at +0xC, so 00119EA0 returns -1. The
  original 001FB9F0 therefore returns -1 for all five over both captures,
  and in AREA11 it plays nothing for them. The exporter's AREA11 binding
  lists this slot as refused (`refused_slots` in `sfx_registry.json`) and
  exports these ids as ABSENT. The native driver refuses them the same way
  the original does, and they are not counted as unsupported cues. (The
  first round exported them as UNSUPPORTED "unbound bank"; the reviewer's
  oracle run showed that was wrong.) They are requested by:
  - the title menu (001AC480 / 001AC7F0: 0x5DC..0x5DF; the probe saw 0x5DD
    at the title, area 0.0);
  - the weapon stance workers 00171320..001723D0 (0x5DC/0x5DD);
  - the holster table (0x5DE..0x5E0).

  The title's scope (0,0) has no binding in this exporter. At the title,
  group 3 holds a real bank, and there these ids can play.
- **0x8C9** (001FDDB0, the mode-3 cue presenter): ABSENT. The 11.0 remap
  is 0xFF.
- **0x44F** (001FC280): ABSENT. The 11.0 remap is 0xFF. See section 1.
- **0x8C6, 0x8C7, 0x8C8, 0x8CA, 0x8CB, 0x8CD** (the passcode keypads
  00207350 / 002072A0): ABSENT. The 11.0 remap is 0xFF, as for 0x8C9; the
  original 001FB9F0 returns -1 over both captures.

## 5. Open items for other lanes

- `docs/SFX_PITCH.md` ("Current export (71 entries …)") and
  `docs/SFX_SEQUENCER.md` ("71 entries") still give the old counts. The
  current counts are 277 entries (259 audible, 16 absent, 2 unsupported)
  and 141 samples. The census rows in `FIRST_LEVEL_AUDIT.md` /
  `FIRST_LEVEL_CENSUS.md` that say "not in the exported registry (WP-14)"
  can now be closed: 0x12E, 0x74, 0x12B, 0xEC, the footsteps, 0x455, and
  the ladder's 0x107/0x10E/0x10F.
- With 0x12E exported, 001FBD50 can return a track for the slide, so the
  record's +31B should hold it as the original's does instead of 0016CD70
  requesting 0x12E again every tick. This lane did not check that in a
  live run.
- Modulation (0x424/0x428) belongs to the driver.
- The title bank (group 3) needs a title scope binding before
  0x5DC..0x5DF can play at the title.

## 6. Regenerate

```
# decomp repo (optional; the route evidence, about 30 min, hidden PCSX2)
.venv/bin/python tools/sfx_request_probe.py run --segments all
.venv/bin/python tools/sfx_request_probe.py report
.venv/bin/python tools/sfx_request_probe.py scan     # reachability + completeness, < 1 s; exit 1 on a gap
# port repo
python3 tools/export_sfx_registry.py
make test-area11-sfx-reference test-area11-sfx
```
