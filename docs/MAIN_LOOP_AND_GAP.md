# The main loop 0x1AAE40, its vblank handler, and the 0x1050E4 gap

Lane "main-loop-and-gap" (2026-09-23), from the critic notes of
`FIRST_LEVEL_CENSUS.md` section 7.1: the main loop was missing from the census
(its entry runs once, before any census label), and the unlisted code in
0x1050E4..0x105148 was unexplained. This document covers what the original
does, the translation, how it is verified, how it binds into the live port,
and the corrections for the census. It contains addresses and descriptions
only, no original code or data.

Files (all new):

| File | What |
|---|---|
| `src/game/em_main_loop_and_gap.h/.c` | the translation: `em_mlg_001AAE40_init`, `em_mlg_001AAE40_frame`, `em_mlg_001AB140`, `em_mlg_001050E8` |
| `tools/test_main_loop_and_gap_reference.py` | the original-instruction oracle and the capture checks (the lane's test; depends only on the lane's files and the original) |
| `tools/test_main_loop_and_gap_frame_audit_reference.py` | a separate REPORT for the lead, not a make target: the live `em_frame.c` against the original steps (section 4) |
| `tests/main_loop_and_gap_frame_audit_test.c` | recording stubs that let the report run the live `em_frame.c` headless and log its call order |

## 1. Status: before and after

| Address | What | Decomp | Census before | After this lane |
|---|---|---|---|---|
| 0x1AAE40..0x1AAF24 | main loop, start-up part | NM (structural; the code is byte-identical by the decomp's hand check) | not in the census | **verified-unbound**: `em_mlg_001AAE40_init`; stand-in: `main.c` bring-up + `em_frame_init` |
| 0x1AAF28..0x1AB134 | main loop, one frame (steps A..W) | same symbol | not in the census | **verified-unbound**: `em_mlg_001AAE40_frame`; stand-in: `em_frame.c em_frame_step` (section 4) |
| 0x1AB140..0x1AB1D8 | vblank interrupt handler (alternate entry of the same symbol) | same symbol | not in the census | **verified-unbound**: `em_mlg_001AB140`; stand-in: none (`frame_pace_ntsc` only sleeps) |
| 0x1050E4 | one zero word (padding after 00105088) | in splat's `func_001050E8` data label | unlisted | not code |
| 0x1050E8..0x105147 | libmpeg: saturate 384 halfwords to bytes | `.word` listing only | unlisted | **boundary** (SDK libmpeg / IPU movie decode) with a verified translation `em_mlg_001050E8`, not needed live |

All rows are verified by `tools/test_main_loop_and_gap_reference.py`
(section 6). None is live yet: the module is not in the Makefile's COMMON list
and nothing calls it.

## 2. The main loop 0x1AAE40

Read from the decomp's `gs_readback_queue_run.c` (its NEARMISS is structural:
splat gives the loop and the handler one symbol) and the split listing, whose
delay slots place each store between two calls.

### 2.1 Start-up (0x1AAE40..0x1AAF24), runs once at boot

1. 001AB1E0 (IOP module bring-up). A nonzero result ends the program in a
   branch to itself at 0x1AAE58: the original hangs there forever. The
   translation returns `EM_MLG_HANG` and runs nothing after it.
2. 001FEE60, then the timer-0 mode register 0x10000010 = 0x83 (stored in the
   delay slot of the next call), then 001AB370, 001CCCC0, 001CCBD0(0, 0x3FFF, 0),
   001AB430, 001FB210, 001F9820, 001F9780, 00101548(0x1AB140) (installs the
   vblank handler), 001FF1E0(0), 001D0F20, 001CCB10, 001B5790, 00225CC0,
   001AB650 (task table reset), 001AB740(0, 0x1AB7E0) (the slot-0 boot task).
3. Wait until D_00810E88 is nonzero, i.e. until the handler has latched an
   odd field (0x1AAEFC).
4. Timer-0 count 0x10000000 = 0, then 001AED80(0) (transition clear), and
   fall into the loop top 0x1AAF28.

None of the start-up callees ran in the census (boot before the title is not
instrumented), except 001CCCC0, 001CCBD0, 001F9820, 001CCB10, 001AB740 and
001AED80, which the census saw from other callers.

### 2.2 One frame (0x1AAF28..0x1AB134)

The step letters are those of `ORIGINAL_FRAME_ORDER.md` section 1; "T0" is the
timer store between P and R, which has no letter there.

| Step | Site | What the original does |
|---|---|---|
| A | 0x1AAF2C | vblank flag D_00810E98 = 0 |
| B | 0x1AAF34 | 001D1AE0(D_00810E80) |
| C..I | 0x1AAF3C..0x1AAF6C | 001B57E0, 001AEBE0, 001AB6A0, 001FCA10, 001AEE70, 001FB100, 001B5B70 |
| J | 0x1AAF78 | r = 00100A60(0, 0); if r != 0: 0011B910, 0011B5E0, 0011B328, 0011AE88, 0011A9D8 |
| K, L | 0x1AAFB0, 0x1AAFB8 | 001D7410, 001AB590 |
| M, N, O | 0x1AAFD4..0x1AAFE8 | only when the byte D_00821058 == 1: 00203350 (blocking movie), 001D1C10(D_00810E80), 001AEE70 |
| P | 0x1AAFF0 | poll D_00810E98 until nonzero (the handler sets it) |
| T0 | 0x1AB010 | timer-0 count 0x10000000 = 0 |
| R | 0x1AB020 | 001AB4E0(halfword 0x70003B94, halfword 0x70003B96) |
| S | 0x1AB070, 0x1AB0C0 | 001015A8(env, 0x70003B70, 0x70003B72, flip) and 00101810(env, same, flip); env = 0x811070 / 0x8110F0 when D_00810E80 != 0, else 0x810F00 / 0x810F80; flip = (1 - D_00810E88) truncated to a halfword and sign-extended |
| T | 0x1AB0C8 | 0010BAA0(0) |
| U | 0x1AB0EC | 00100550(0x810EA0 + 40 * D_00810E80) |
| V | 0x1AB0F4 | 001D2300 |
| W | 0x1AB118 | field = D_00810E88 is read first, then D_00810E80 = 1 - D_00810E80 (halfword store in the call's delay slot), then 001D2580(field); counter 0x70003B64 += 1 in the back-branch's delay slot |

Every halfword the loop reads is sign-extended (the loop uses signed halfword
loads); the movie gate compares the unsigned byte with 1.

What the callees are (read, not assumed from labels):
- **00100A60(0, 0)** (splat label `dma_wait_and_submit`) waits until the
  VIF1 and GIF DMA channels (0x10009000, 0x1000A000) have stopped, VIF1_STAT
  (0x10003C00) shows no queued or active work, the VU1 busy bit of VPU-STAT
  is clear and the GIF status (0x10003020) is idle, with one poll counter
  limited to 0x1000000 across all the waits. It returns 0 when idle; on a
  timeout it prints a register dump through 00122B58 and returns -1.
- **The J block** runs only after such a timeout. Its five routines are
  hardware resets, not sound code: 0011B910 resets VIF1 (1 to VIF1_FBRST
  0x10003C10, 6 to VIF1_ERR 0x10003C20), 0011B5E0 does the same for VIF0
  (0x10003810 / 0x10003820), 0011B328 stores 1 to GIF_CTRL 0x10003000, and
  0011AE88 / 0011A9D8 set the VU1 / VU0 reset bits of the COP2 FBRST control
  register. The J-block call sites are their only callers.
- **001AB590** clears the ASP bits of the DMA channel 0, 1 and 2 CHCR
  registers when set (hardware; lane L34 has the translation).
- **0010BAA0** is a four-instruction kernel syscall stub with syscall number
  100 (the public EE syscall table lists 100 as FlushCache). The splat label
  `DisableDmacHandler` is wrong.
- **001D2580** stores its argument (the field bit) into word +0x98 of the
  render context D_00275670. It is a game-memory store, not packet building.
- **Timer 0**: a static scan of the boot ELF (addresses built from an upper-half constant) found only stores to 0x10000000 /
  0x10000010 (the three in this loop), no loads, so nothing in the game reads
  the timer the loop resets.

### 2.3 The vblank handler 0x1AB140

Installed by the start-up through 00101548. It disables interrupts and
re-reads the COP0 Status register until its EIE bit (bit 16) reads clear,
then increments D_00810E98 (the flag step P waits for) and D_00810E90 (the
vblank count), loads the 64-bit GS CSR 0x12001000 and stores its FIELD bit
(bit 13) into D_00810E88, calls 0010C710(D_00282184) (an EE kernel thread
wake-up; D_00282184 is 3 in every route capture) and re-enables interrupts.

Its return value: nothing writes v0 after the 0010C710 call (0x1AB1C0) up to
the return at 0x1AB1D4, so the handler returns 0010C710's result. 00101548
installs the handler with `AddIntcHandler(2, handler, -1)` (decomp
`func_00101548.c`), so that value goes to the EE kernel's INTC dispatch.
0010C710 returns the result of the kernel wake-up call when its argument
differs from the value its first kernel call returns, otherwise -1 or that
value (decomp `func_0010C710.c`).
The translation returns it through `em_mlg_001AB140`'s `v0` out-parameter,
and the oracle compares it.

### 2.4 Measured on the original route captures

Asserted by the test on every run (`route_checks`):

- In all 15 route snapshots (taken at the loop top), D_00810E90 - 0x70003B64
  is the same value, 9937: between the snapshots, every main-loop frame took
  exactly one vblank (no dropped or doubled frames on the route in PCSX2).
- In all 15, (D_00810E80, D_00810E88) is (0, 1) or (1, 0): the buffer index
  and the field bit stay locked in opposite phase, as one vblank per frame
  implies.
- D_00810E98 is 1 at every loop top (one handler run since step A), and the
  snapshot's `vsync_counter` equals D_00810E90.
- The counter 0x70003B64 advances by exactly one per traced frame over the
  12,439 route trace rows.

Also measured:

- The 17 PCSX2-traced frames of `ORIGINAL_FRAME_ORDER.md` all ran the 18 call
  sites B..W without the J block and without M/N/O.
- **J block (open question).** The census saw the five J-block routines
  (0011B910, 0011B5E0, 0011B328, 0011AE88, 0011A9D8) on nine labels (S0, S2,
  S3, 04, 07, 08, 10, 11, 13). Their only call sites in the boot ELF are the
  J block (0x1AAF88..0x1AAFA8), and no overlay under `extract/OVERLAY`
  calls them, so each of those nine census runs had at least one 00100A60
  timeout. 00122B58 (the register-dump printer) is NOT evidence of this: it
  appears on ten labels (also 01_battery) and has 71 call sites in the boot
  ELF. The constant vsync offset above shows that no timeout-length stall
  (00100A60 polls up to 0x1000000 times) happened on the route between the
  first and the last route beat. That supports the explanation that the
  timeouts are an artifact of the census pausing the EE at breakpoints
  while the GS/VIF paths ran on, not a stall of the original game. Not
  proven: no traced frame shows the J block.

## 3. The translation (`em_main_loop_and_gap.c`)

House style of `em_player_stage_workers.h` / `em_startup_load_gaps.h`:

- `EmMlgState` holds one pointer per global the loop and the handler read
  or write, named by address: D_00810E98, D_00810E90, D_00810E80,
  D_00810E88, D_00821058, D_00282184, scratchpad 0x70003B70/72/94/96 and
  0x70003B64. They point at the single storage other translations use
  (section 5, "Data"); a NULL pointer a routine needs fails it before any
  write, like a missing worker.
- `em_mlg_001AB140(w, s, v0)` returns 0 or -1 like the others and passes the
  handler's own return value (0010C710's result, section 2.3) out through
  `v0` (NULL discards it); `w0010C710` returns that result through an
  out-parameter.
- `EmMlgWorkers` has one worker per original callee (the 44 distinct call
  targets of the symbol, in call order) plus the loop's own boundaries:
  `io_store` (the timer-0 stores), `spin` (one pass of a polling loop, the
  only point where the loop can observe the asynchronous vblank), and for the
  handler `cop0_di` (disable + Status read), `gs_csr_load` and `cop0_ei`.
- Each routine checks, before its first write, that every worker it can reach
  is bound, else returns -1 (fail-stop); a worker fault returns -1 at once
  with the earlier writes kept in the original order.
- The vblank handler is a separate function the platform calls when a vblank
  is delivered. In the frame, the `spin` worker is where the native side
  delivers it (section 5).

`em_mlg_001050E8(dst, src)` clamps each of 384 signed halfwords to 0..255
(signed minimum with 0x00FF, then signed maximum with 0) and stores the low
bytes in order, 16 per pass. Each pass reads its 32 source bytes before
storing, like the original.

## 4. The live `em_frame.c` against the original

`em_frame.c` says it follows the 0x1AAE40 order. The separate report
`tools/test_main_loop_and_gap_frame_audit_reference.py` (NOT a make target
and not part of the lane's test, which depends only on the lane's own files
and the original) builds the live `em_frame.c` with the recording stubs of
`tests/main_loop_and_gap_frame_audit_test.c`, runs one engine frame in three
scenarios (ordinary; the movie arms in the task dispatch and ends at once;
the movie plays two more presentation steps), maps each call onto the step it
stands for (`em_gfx_begin_frame` B, `em_pad_unpack` C, `em_screen_fade_tick` D,
`em_task_dispatch` E, the message tick F, `em_transition_fade_tick` G and O,
the movie pump M, parity and counter W) and lists the deviations from the
translation's step list. The deviations are non-original behaviour to
remove, so nothing pins them: the report fails (exit 1) only if the steps
em_frame.c has run out of the original order, and exits 2 when the stubs or
the call map no longer fit the current em_frame.c. Result on 2026-09-23
(information, not an expectation; the H line updated by WP-8b, 2026-09-25):

- **Order**: the steps em_frame.c has are in the original order in all three
  scenarios.
- **Missing steps** (ordinary frame): A (vblank flag), I (001B5B70, in the
  report's stubs only: the game installs it), J (00100A60 and the reset block), K (001D7410), L (001AB590),
  P (vblank wait: `frame_pace_ntsc` sleeps outside the step), T0, R (001AB4E0),
  S (001015A8, 00101810), T (0010BAA0), U (00100550), V (001D2300) and the
  001D2580 call of W. In the movie scenarios N (001D1C10) is missing too.
- **Native-only calls** inside the step: `em_gamepad_poll`, `em_window_poll`,
  `em_input_pad`, `em_pad_raw` (host input before C), the field hook at the
  top of every step (the vblank's D_00810E90 and the IOP stream backend's
  field, em_stream_live; WP-8b), the message render (drawing, before G), and
  `em_gfx_end_frame` (present). Since WP-8b step H runs 001FB100's lane
  service 001F9CF0 after G (skipped while D_00821058 == 1).
- **Repeated while a movie plays**: the original blocks inside 00203350 (M)
  and the loop runs nothing else until it returns. em_frame.c instead runs B
  (begin frame) and C (pad unpack) again on every presentation step of the
  movie, plus the pump. The engine frame's A..L and W still run once.
- Movie completion: em_frame.c runs the second transition tick (O) after the
  pump returns, as the original does after 00203350, but not N.

Several missing steps are platform boundaries that need no native work (T0,
T, the J resets, L); the others (H, I, R's display offsets, V, W's render
context store) are game-visible and belong to other lanes (L34, L35, L31) or
to the render context.

## 5. Binding notes (for the lead)

**What it replaces.** `em_mlg_001AAE40_frame` replaces the body of
`em_frame_step` (em_frame.c) from `em_gfx_begin_frame` to `counter++`;
`em_mlg_001AAE40_init` replaces the engine part of `main.c`'s bring-up
(`em_frame_init`'s `em_task_init`, the slot-0 task install done by
`em_frontend_install`, the first transition clear). `em_mlg_001AB140`
replaces nothing live (the port has no vblank count, no field bit and no
vblank flag; `frame_pace_ntsc` only sleeps). The module is not in COMMON;
add `src/game/em_main_loop_and_gap.c` to the COMMON list only when it is
bound.

**Workers**, with what exists today:

| Worker | Bind to | State |
|---|---|---|
| w001D1AE0, w001D1C10 | `em_gfx_begin_frame` (GS/VIF packet boundary) | 001D1C10 has no port code; its callees 001CB5C0 / 001F0310 / 001D2830 are render work |
| w001B57E0 | `em_slg_001B57E0` (L34) or the current `frame_input_read` | live stand-in reads the pad via `em_pad_unpack` (001B5940, verified) |
| w001AEBE0, w001AEE70 | `em_screen_fade_tick` / `em_transition_fade_tick` + their draws | live, `test_fade_reference.py` |
| w001AB6A0 | `em_task_dispatch` | live, unverified (census) |
| w001FCA10 | the message-service tick | live (panel lines only, census) |
| w001FB100 | since WP-8b its 001F9CF0 call (em_stream_live at step H, skipped while D_00821058 == 1); the rest of `em_slg_001FB100` (L34: the output-mode commit, the D_00281B70 copy, 001FC6E0) not bound | partly live |
| w001B5B70 | the 001B5B70 translation in `em_owner_services_original` (L35) | verified-unbound |
| w00100A60 | "path idle": return 0 (the native renderer has no VIF/GIF/VU1 path to stall) | boundary; the J-block workers are then never called but must still be bound (fail-stop): bind them to a fault, since reaching them natively would be a bug |
| w001D7410, w001015A8, w00101810, w0010BAA0, w00100550 | the native renderer's frame submission / present (`em_gfx_end_frame`) | boundaries |
| w001AB590 | `em_slg_001AB590` (L34) or a no-op (DMA hardware) | boundary in substance |
| w00203350 | the movie pump: the worker must run the whole movie before returning, pumping window events and presenting each movie frame itself (em_frame.c's suspension then goes away) | boundary (IOP movie service) |
| w001AB4E0 | `em_slg_001AB4E0` (L34) | missing live; its output is the GS display environment (boundary) |
| w001D2300 | `em_background_gs` (L31) | verified-unbound; 001D2300 also passes 1 - D_00810E88 and 0x70003B70/72 to 001015A8 / 00101810, so whatever binds it must read those from the shared storage (see "Data") |
| w001D2580 | a store of the field into the render context word +0x98 | no port code |
| io_store | no-op (timer 0 is never read) | boundary |
| spin | deliver the pending ticks; if none, sleep to the next 59.94 Hz tick (the `frame_pace_ntsc` logic) and deliver it (see "Vblank delivery" below) | new |
| cop0_di / cop0_ei | Status 0 / nothing | boundary |
| gs_csr_load | FIELD alternating per delivered tick (see "Field" below) | boundary |
| start-up workers | `em_task_init` (001AB650), the slot-0 install (001AB740), `em_frame_fade_clear(0)` (001AED80); the rest are IOP/GS/sound/module bring-up boundaries | 001AB1E0 must report 0 natively |
| w0010C710 | the EE kernel thread wake-up; natively nothing, reporting its result through `*result` (0 is fine: the value reaches only the kernel's INTC dispatch) | boundary |

**Handler return value.** `em_mlg_001AB140`'s `v0` is the value the
original hands to the kernel's INTC dispatch (section 2.3). The native
platform has no kernel handler chain, so it may pass NULL; nothing in the
game reads the value.

**Data: one storage, shared.** `EmMlgState` points at the globals; the
binder must point it at the SAME storage that every other translation on
the live path uses for these addresses, never at a private copy. Readers and
writers outside this lane (found by symbol name in the decomp C and
listings; references through another symbol plus an offset are not
covered):

| Global | Other original code | Port view today |
|---|---|---|
| D_00810E90 (vblank count) | read by 001F9CF0 (the per-frame stream-lane service, reached from step H 001FB100) as an elapsed-vblank timer; read by 001FA790 (lane start), which stores it into the lane record | `em_stream_lanes_original` globals view, field `d810E90` |
| D_00810E88 (field) | read by 001D2300 (step V: 1 - field, like step S) | none (L31's `em_background_gs` does not read it) |
| D_00810E80 (buffer index) | cleared by 001AB430 (start-up); read by 001AEBE0 (D), 001AEE70 (G/O), 001CB800, 001CB8A0, 001CFBE0 | `em_head_sprite_original` (`d810E80`), `em_frame.c` (`parity`) |
| D_00821058 (movie byte) | set to 1 by 001AC3B0, 001AD360, 001B7A30; cleared by 001AB430 and 00203350 (the movie driver, step M); read by 001FB100 (H) and 001B7A30 | `em_startup_load_gaps` (`EmSlgSoundFrame.d821058`), `em_area_script` (`d821058` pointer), `em_scene_bindings` / `em_frontend_movie_request` (writer), `em_frame.c` (`movie_active`) |
| D_00282184 (thread id) | written by 001F9780 (start-up) | none |
| 0x70003B64 (counter) | cleared by 001AB430; read by game routines (e.g. 0015A2C0's every-128-frames test, 001C02E0) | `em_frame.c` (`counter`), `em_enemy.h` comment |
| 0x70003B70/72, 0x70003B94/96 | set by 001AB370 at start-up (0x800, 0x800, 0, 0 in every route capture); 3B70/72 read by 001D2300; 3B94/96 (display offset) adjusted by 00201F70 and 00201C50, referenced by 001AF150 | none |

**Vblank delivery.** The platform must call `em_mlg_001AB140` exactly once
per elapsed 59.94 Hz tick, including the ticks that elapse inside callees
during an overrun frame: it counts elapsed ticks against delivered ones and
delivers the pending ones at the next worker boundary (the binder's worker
wrappers) or `spin` pass, not one per `spin`. In `spin`, when none is
pending, it sleeps until the next tick and then delivers it. Headless runs
use a virtual clock (one tick per `spin` pass, none elapsing inside
callees). Undercounting would slow D_00810E90 (the stream-lane timers of
001F9CF0 / 001FA790) and could put D_00810E88 in phase with D_00810E80,
which changes the flip argument step S passes to 001015A8 / 00101810; the
route captures show the opposite-phase lock (section 2.4).

**Field.** `gs_csr_load` must return FIELD (bit 13) alternating once per
DELIVERED tick (not per frame), starting so that the start-up's odd-field
wait (0x1AAEFC) completes on an odd field.

## 6. Verification (`make test-main-loop-and-gap-reference`)

`tools/test_main_loop_and_gap_reference.py` runs the ORIGINAL instructions in
the shared EE interpreter (`test_player_slide_reference.EE`), extended with
the loop's hardware as recorded events (timer stores, the GS CSR load, COP0
disable/enable with a scripted Status) and the three MMI operations of
001050E8. It faults on any COP1/COP2 instruction (these routines have none,
so `tools/ee_float_model.py` is not needed).

- **Structure**, asserted on the ELF: the symbol's call targets are exactly
  the 44 hooked callees; its only return is the handler's; the gap facts of
  section 7.
- **Frame**: one pass from 0x1AAF28 back to 0x1AAF28 over the captured RAM
  of the 15 route beats and the 3 startup-reference captures that carry a
  scratchpad (the loop code in RAM is asserted equal to the ELF). Script
  classes: J result (0, 1, -1, extremes), movie byte (0, 1, 2, 0xFF), vblank
  delivered on polling pass 0 or 3, inside E (an overrun: no wait), twice
  inside M, or late inside V (changes the field W reads), handler Status
  retries, buffer index and field over 0/1 and odd halfwords, negative
  scratchpad halfwords, counter wrap, plus random scripts. Compared: every
  event (callee, arguments, hardware access, poll pass, handler step) in
  order, all state fields at EVERY event (so where each store sits against
  every call is checked on every run, not only by sampled faults), the
  return code, the final state; every byte the original stores outside its
  stack must be a compared field.
- **Faults**: a worker fault at event k gives -1, the original's first k+1
  events and the original's state at event k; a missing worker gives -1 with
  no event and no write (every worker of each routine); so does a NULL
  pointer to any global the routine uses, and the globals it does not use
  may be NULL (start-up and handler).
- **Start-up** over the ELF image: the hang, the odd-field wait (field on the
  first pass, after even fields, delivered inside 001AB740, already set,
  negative), random scripts.
- **Handler**: random CSR, Status retry counts, wrapping counters, and
  random 0010C710 results (0, -1, 3, 0xFF, 0x100, random): the handler's v0
  must equal the original's; on a fault it is not written.
- **Gap 001050E8**: boundary halfwords and random buffers; the original must
  store exactly 384 bytes at the destination and nothing else.
- **Captures**: the 17 PCSX2-traced frames (call sites in order; the J result
  is taken from the trace: nonzero only if the J block appears); the 15 route
  traces (12,439 rows: the counter advances by one per frame), snapshot
  counter = 0x70003B64, vsync counter = D_00810E90, and the facts of
  section 2.4 (one offset 9937; phases (0, 1) / (1, 0); D_00810E98 = 1).

Results (2026-09-23, after the review fixes): default run 1.3 to 2.4 s wall (under 1 s CPU), "mode quick: 44 callees hooked,
64 of 846 frame runs, 18 captured states, 1715 frame events, 11 start-up
runs, 40 handler runs, 13 gap runs, 17 PCSX2-traced frames, 12439 route
counter rows over 15 beats", all equal. `EM_TEST_FULL=1`: 40 s CPU (2 min 19 s wall on a loaded machine), 7,614 frame
runs (217,638 events, with a fault sweep at every event of every run), 67
start-up runs, 400 handler runs, 301 gap runs, all equal.

Oracle sensitivity was checked by mutating a scratch copy of the translation:
swapping H and I, storing the timer before the wait, a signed movie gate,
clamping at 256 in 001050E8, reordering the handler's CSR load and thread
call, storing the timer mode after 001AB370, and an unsigned buffer index in U
were each detected. After the review fixes, four store-order mutants (A
after B; the counter increment before the 001D2580 call; the flipped index
stored after that call; the handler's D_00810E90 increment after the CSR
load) each fail 62..114 comparisons in the default run, starting with the
base script of the first route beat; a handler returning 0 instead of
0010C710's result fails 37. Two mutants are not detectable from the loop's inputs
and outputs: reading the field after storing the flipped index (no call in
between), and dropping the halfword truncation of the flip argument (the
handler always leaves D_00810E88 at 0 or 1 before step S). The translation
keeps both as the original does.

Makefile target (to be added by the lead; the module joins COMMON only when
bound):

```make
.PHONY: test-main-loop-and-gap-reference
test-main-loop-and-gap-reference:
	python3 tools/test_main_loop_and_gap_reference.py
```

The test builds its library (the translation) under
`build/b7-main-loop-and-gap/` (or `build/$EM_LANE/`); the report builds
`em_frame.c` with the audit stubs there too.

## 7. The 0x1050E4..0x105148 gap

- 0x1050E4 is one zero word after 00105088's return.
- 0x1050E8..0x105147 is **one** routine (0x60 bytes). Its only return is at
  0x105140. Its body contains the 16-byte constant at 0x105130 (eight 0x00FF
  halfwords). Execution falls through the constant, whose four words decode
  as shifts into the zero register, so they have no effect.
- Its only callers are two calls inside 001041E8 (at 0x1042FC and 0x104334),
  a libmpeg routine (splat label `sub_intra_skip_MB`) that calls it in two of
  its three cases (its flags +0x130 and +0x13C) and calls 00105088 in the
  third. 001050E8 is the single-source variant of 00105088, which adds two
  halfword sources before the same clamp.
- 0x105130 is loaded by 00105088 and 001050E8 only.
- 001041E8 ran only in S0_title in the census (the title movie). The census
  did not arm 0x1050E8, so whether it executed there is not measured.

This is SDK MPEG decode work. The port plays remuxed movies through the OS
decoder (`tools/export_movie.py`, `em_movie_mac.m`), so it is a boundary; the
translation exists only to prove the identification.

## 8. Corrections for FIRST_LEVEL_CENSUS.md (for the lead)

1. Add **0x001AAE40** (main loop; decomp NM structural; first label S0_title,
   every label): verified-unbound, module `em_main_loop_and_gap`
   (`em_mlg_001AAE40_init`, `em_mlg_001AAE40_frame`), test
   `test_main_loop_and_gap_reference.py`; stand-in `em_frame.c em_frame_step`
   with the deviations of section 4. The step list in section 3.1 of the
   census should gain this row.
2. Add **0x001AB140** (vblank handler, alternate entry of the same symbol;
   runs every vblank): verified-unbound (`em_mlg_001AB140`); stand-in none.
3. Critic note "Unlisted code: the 0x1050E4..0x105148 gap" is wrong in two
   details: the gap holds **one** routine, 0x1050E8, with a single return at
   0x105140 (there are no returns at 0x1050F8 or 0x105118), and 0x105130 is
   loaded by 00105088 and 001050E8, not by 00104D78 or 00104E48 (those two
   load nothing from the gap). Add **0x001050E8** as a boundary row (SDK
   libmpeg; substitute: em_movie_mac.m; a verified translation exists), and
   re-arm it in the next census pass.
4. Reclassify **0011B910, 0011B5E0, 0011B328, 0011AE88, 0011A9D8** from
   "EE sound library" to a GS/VIF/VU path-reset boundary (section 2.2): they
   reset VIF1, VIF0, the GIF, VU1 and VU0, and run only after 00100A60's
   timeout. Their appearance on nine census labels (their only callers are
   the J-block sites) means the path sync timed out in those runs; 00122B58
   (on ten labels, 71 call sites) is not evidence of it. The cause is open;
   the constant route vsync offset points to a census artifact (section 2.4).
5. Name fixes for the boundary rows: 00100A60 is a path-idle wait with a
   timeout (not a DMA submit); 0010BAA0 is the syscall-100 stub (not
   DisableDmacHandler).
6. **001D2580** is listed as "GS/VIF packet build" but is a single store of
   the field bit into render-context word +0x98 (game memory); classify it
   by what reads that word.
7. **001AB590** (missing, lane L34) is DMA-register housekeeping; natively
   it is a boundary in substance.
8. Section 6 "Census coverage": the 0x1AAE40 blind spot can be closed by
   arming the loop top 0x1AAF28 (it is hit every frame), as the route
   stepper already does.

## 9. Limits

- The oracle executes the loop, the handler and 001050E8; every callee is a
  scripted hook. Nothing here verifies what the callees do; each belongs to
  its own lane.
- Interrupt delivery is scripted at polling passes and callee boundaries. On
  the PS2 a vblank can land between any two instructions; the loop's only
  shared state with the handler is D_00810E98 and D_00810E88, which the loop
  reads at the points compared here, so earlier or later delivery within a
  callee is equivalent to delivery at that callee's boundary.
- The PCSX2 trace frames carry no RAM, so for them only the native call-site
  order is compared (with the trace's movie byte and J result).
- The em_frame.c report measures call order only; the stubbed modules'
  behaviour is not claimed, and it is not part of the lane's test.
- The handler's v0 is compared, but what the EE kernel does with it
  (handler-chain control) is outside the game and not modelled.
- The "Data" table lists references by symbol name only; code that reaches
  these globals through another symbol plus an offset is not covered.
- Not measured on the original: which frames of the census runs timed out
  in 00100A60, and whether 001050E8 executed in S0.
