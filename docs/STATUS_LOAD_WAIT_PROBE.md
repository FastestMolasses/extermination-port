# Module-load wait probe: the 24 loader dispatches of the status BATTERY page

Date: 2026-09-23 (session s87, lane "emu-exit-and-io"). This closes the "next step" of
`STATUS_SCENE.md` section 3: which of the 24 loader dispatches between the ITEM root's
001FF080(0, 0x21) and the root seeing D_00275BD8 == 0 are disc-I/O time, and how they are
split. Everything below was measured on the ORIGINAL game in the hidden PCSX2; it names
addresses and values only.

## 1. Method

Tool: `../Extermination/tools/load_wait_probe.py` (decomp repo, new). It replays route beats
01 (battery pick-up, the ITEM pop-up) and 03 (panel, the BATTERY prompt) of
`tools/route_capture.py` from their recorded source snapshots (slot 04 and
`build/s87/route/02_elevator_refusal/state.p2s`) with the same closed-loop pad driver, and
records per frame:

- slot 2's record 0x28A790: +0 slot state, +8, +9 (001FF830 step), +0xA, +0xB (001FF3F0
  sub-step), +0xE module, +0x16 chunk index;
- D_00282157 (the loader's read gate: 001FF0D0 does nothing while it is nonzero);
- D_00275BD8 (the "module load pending" byte the ITEM root waits on).

Two modes:

| Mode | What it adds | Perturbs the run? |
|---|---|---|
| `boundary` | Nothing: the fields above are read at the main-loop top only. | No. Every run reproduced the recorded route trace row for row (03: 686/686 twice; 01: 517/517). |
| `breakpoints` | EE breakpoints at 001FF080 (a0/a1), 001FF0D0 (record and D_00282157 at the dispatch), 00200780 (read offset and size), 00200730 entry plus its return site (the poll's return value v0). | Sometimes. Route 01: 517/517 rows identical, so its per-dispatch values are direct observations of the unperturbed run. Route 03: the chunk read finished after 1 busy poll instead of 8, the wait took 17 dispatches and the trace diverged from row 408. |

So mid-frame EE pauses can shift PCSX2's disc timing; the frame counts below come from the
unperturbed runs only (both boundary runs of 03, both runs of 01, and the recorded traces).

Outputs (ignored): `../Extermination/build/s87/loadwait/{01_battery,03_panel_power}/probe_*.json`.
Print the tables with `.venv/bin/python tools/load_wait_probe.py table --beats 01,03 --mode boundary`
(or `--mode breakpoints`).

## 2. The measured wait

In both beats the whole beat contains exactly **one** 001FF080 call: state 0, module 0x21
(the BATTERY page), from the ITEM root (return address 0x20F0F8, inside 0020EE50). D_00282157 is **0 at every dispatch and at
every frame boundary** of both waits, so the read gate never closes here; every frame
dispatches the loader once.

### Route 03, f391..f414 (boundary mode; counters 5381..5404)

"After" is the record at the frame boundary following that frame's dispatch. The poll column
is the return value of 00200730 implied by the transition (0 stays, 1 advances); route 01's
breakpoint run observes the same values directly (below). The boundary runs record no
per-dispatch events (`probe_boundary.json` has none), so they cannot see read sizes or
offsets: the sizes and offsets in the "Dispatch does" column (marked †) are the values of
route 01's **unperturbed breakpoint run** (00200780's arguments), which issues the same
three reads. Route 03's own (perturbed) breakpoint run shows the same three reads; only its
chunk-poll timing differs (section 1). The step / sub-step transitions and the other columns
are route 03's boundary values.

| Frame | Dispatch does | After: +9 / +0xB / +0x16 | 00200730 | D_00282157 | BD8 |
|---|---|---|---|---|---|
| f391 | step 0: buffer pick, **header read** kicked (0x800 bytes at byte offset 0x10800 = 0x21 << 11 †) | 1 / 0 / 0 | — | 0 | 1 |
| f392..f397 | step 1: poll | 1 / 0 / 0 | **0 (×6)** | 0 | 1 |
| f398 | step 1: poll | 2 / 0 / 0 | 1 | 0 | 1 |
| f399 | step 2: 001FF3F0 sub 0 → 1: **chunk read** kicked (0x50800 bytes = 161 sectors, byte offset 0xE4BC000 †) | 2 / 2 / 0 | — | 0 | 1 |
| f400..f407 | step 2: 001FF3F0 sub 2: poll | 2 / 2 / 0 | **0 (×8)** | 0 | 1 |
| f408 | step 2: poll | 2 / 3 / 1 | 1 | 0 | 1 |
| f409 | step 2: sub 3 (section fix-up, count reaches 0, reports 1) | 3 / 0 / 1 | — | 0 | 1 |
| f410 | step 3: **payload read** kicked (0 bytes, offset 0xE50C800 †) | 4 / 0 / 1 | — | 0 | 1 |
| f411 | step 4: poll | 5 / 0 / 1 | 1 | 0 | 1 |
| f412 | step 5: kind 1, step = 7 | 7 / 0 / 1 | — | 0 | 1 |
| f413 | step 7: sections and relocations, +8 = 0x63 | +8 = 0x63 | — | 0 | 1 |
| f414 | +8 = 0x63: BD8 = 0, slot 2 idle | idle | — | 0 | **0** |

The root sees BD8 == 0 in f415 (first state-5 row, as in STATUS_SCENE.md).

**BD8 rises one frame before the 001FF080 call.** D_00275BD8 is already 1 at the boundary
after f390 (route 03) and after f193 (route 01), the frame in which the status UI bytes at
0x810130 also change (…0200 0000 → …0300 0500), while the 001FF080(0, 0x21) request is in
f391 / f194 (its breakpoint event, both routes). So the wait's BD8 == 1 span starts one
frame before the load request; the probe did not identify the writer of that first 1.

### Route 01, f194..f217 (breakpoint mode, unperturbed; boundary mode identical)

The same sequence shifted by 197 frames (counters 4279..4302): request and header kick at
f194; header polls f195..f200 return **0 six times** and f201 returns 1; chunk kick at f202
(same offset and size); chunk polls f203..f210 return **0 eight times** and f211 returns 1;
fix-up f212; zero-byte payload kick f213; its poll f214 returns 1 at once; step 5 f215; step 7
f216; BD8 = 0 in f217. The return values were read from v0 at 00200730's return site in
001FF830 (step 1 and step 4: return sites 0x1FFA30 and 0x1FFAD8) and 001FF3F0 (sub 2: 0x1FF4D8);
no poll returned 2.

### Split of the 24 dispatches

| Part | Dispatches |
|---|---|
| Header read busy (00200730 returns 0) | **6** |
| Chunk read busy (00200730 returns 0) | **8** |
| Polls that complete (return 1): header, chunk, payload | 3 |
| State work without I/O: step 0, 001FF3F0 sub 0/1, sub 3, step 3, step 5, step 7, 0x63 | 7 |
| **Total** | **24** |

The 14 I/O frames are therefore 6 + 8, with no gated frames. The native minimum of 10
(STATUS_SCENE.md) is the last two rows.

## 3. The resulting I/O model

This is a runtime-timing property (the lead's decision in STATUS_SCENE.md section 3), not
game code; it belongs in the adapter behind the loader's 00200780 / 00200730 workers:

1. **00200780 (read kick)** records the request and a completion frame
   `kick_frame + busy + 1`, where `busy` is the measured busy count of that read.
2. **00200730 (poll)** returns 0 before the completion frame and 1 from it on (2 is never
   observed on the route). The measured runs poll every frame, so "busy polls" and "busy
   frames since the kick" are the same number here; counting frames since the kick (not
   polls) keeps the disc progressing if a D_00282157 gate ever skips a dispatch, as the
   hardware would.
3. **Measured busy counts** (the only ones this probe supports):
   - module 0x21 header, 0x800 bytes: **6**;
   - module 0x21 chunk 0, 0x50800 bytes: **8**;
   - zero-byte payload read: **0** (completes at the first poll).
4. **D_00282157** stays 0 through both waits; the model needs no gate for this load. It is
   not always 0: in AREA11 play it is nonzero in short bursts (22 of the first 344 rows of
   route beat 15, values 1 and 2, a few frames each; FIRST_LEVEL_EXIT.md), and 001FF0D0
   skips its dispatch while it is nonzero. A module load overlapping such a burst would take
   longer; that case was not observed and is not measured.

Limits:
- Two data points (1 sector and 161 sectors, both starting from whatever head position the
  preceding streaming left) do not separate seek time from transfer time, so they must not be
  extrapolated to other modules or chunk sizes. Other loads need their own measurement with
  this tool (boundary mode).
- The counts are PCSX2's emulated disc timing (this build, the user's rebuilt ISO), not a
  hardware measurement. Four unperturbed runs agree exactly; the one perturbed breakpoint run
  shows that host-side pauses can change them.
- The area load of beat 15 (001FF080(1, 0) → 001FFCD0, AREA01) is recorded frame by frame in
  `FIRST_LEVEL_EXIT.md` section 3 (boundary sampling only, no per-poll values).

## 4. Reproduce

Decomp repo, `.venv` python, repo root. The emulator runs hidden and is closed after each
beat; no save-state slot is written.

```
.venv/bin/python tools/load_wait_probe.py run --beats 01,03 --mode boundary
.venv/bin/python tools/load_wait_probe.py table --beats 01,03 --mode boundary
.venv/bin/python tools/load_wait_probe.py run --beats 01 --mode breakpoints
.venv/bin/python tools/load_wait_probe.py table --beats 01 --mode breakpoints
```
