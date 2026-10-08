# Port profiles — Original and Enhanced

User decision, 2026-09-23. When the port is done it ships **two profiles** built
from **one codebase**:

| | Original | Enhanced |
|---|---|---|
| Purpose | Faithful to the PS2 game in every way that can be measured | The user's improvements |
| Game logic | The translated original code | **The same code.** Differences are opt-in switches, listed below |
| Resolution | The exact framebuffer the GS draws, shown at 4:3. First level (measured 2026-09-26, decomp docs/CAPTURES_C7.md 5/5b): each field is one 512x224 PSMCT32 buffer (two buffers alternate) shown as 448 lines in field mode (SMODE2 FFMD=1, DISPLAY2 512x448, draw offset alternating by half a line with the field). How the Original profile presents the fields without CRT simulation (line-doubling each field vs combining the pair) is an open user decision | Native / higher resolution, any aspect ratio |
| Scaling | Nearest-neighbour integer or aspect-correct scaling, **no smoothing** | Filtering and AA of the user's choosing |
| Colours | GS-exact: the same blend, fog, alpha test, dither and clamp arithmetic, so the pixels match | May differ |
| CRT / scanline simulation | **None** (user: "no simulating CRT") | Not planned |
| Frame rate and timing | The game's own 59.94 Hz tick (all logic counts fields). Hardware timing is not reproduced: the disc drive answers at host speed and the PS2's slowdowns are not emulated (user, 2026-09-27). The recorded PS2 disc-drive timing is an optional switch, off by default (built: `EM_PS2_DISC_DRIVE_TIMING=1`, `src/em_settings.h`) | Higher display rates allowed, but logic and streamed audio stay at 59.94 Hz with rendering decoupled (the stream ring needs field pacing, IOP_STREAM.md "Clock domains") |
| Controls | The original DualShock 2 mapping and behaviour | Better controls (list below) |
| Content | Only what the shipped game reaches | May restore cut content (list below) |

## Rules

1. **Original is the default** and the only thing the fidelity work measures.
   A frame from the Original profile, taken at the same game state, should match
   the original's framebuffer pixel for pixel. The framebuffer-comparison harness
   (queued below) checks this.
2. **Enhanced never forks the logic.** Every enhancement is a named switch with
   an Original value. With every switch at its Original value, Enhanced is the
   Original profile. Rendering-only switches (resolution, filtering, AA,
   widescreen) are separate from switches that change gameplay (controls,
   camera, cut content).
3. **Enhancements are built on byte-faithful code, never in its place.** A
   gameplay enhancement is a patch applied on top of a translated original
   function, behind its switch. It is not a rewrite of that function. The
   Original path stays the verified translation.
4. **Enhancement work comes after fidelity work.** Nothing here starts until the
   user moves the goal past "first level faithful" (see `CLAUDE.md`), unless the
   user asks for a specific item.
5. **Cut content comes from the disc only.** Restoring it means wiring up code
   and data the original already ships, as documented in the decomp's
   `docs/CURIOSITIES.md`. It never means making up new content. Each restored
   item gets its own switch.

## The user's improvement list (sources)

- **Controls and QoL** — the "Future Enhancements" section of the port's
  `README.md` (the user's own file; read it, never edit it): slide down ladders,
  no inverted aiming, toggle between classic and modern camera, move while
  aiming, no full black fade on some doors, macOS window follows the dark/light
  theme, and subtitle fixes.
- **Cut and hidden content** — the decomp's `docs/CURIOSITIES.md`. Only
  entries of a restorable kind (hidden system, hidden or unreached UI,
  unreached content, cut content that left something on the disc) that are
  `decoded` can become switches. The candidates and their current status are
  listed in `docs/LAUNCHER_OPTIONS.md` (the 2026-09-27 review withdrew the old
  "light-based stealth system" premise and found that the "hidden animation
  entries", dogtags, diary and 7th config row are not cut content).
- **Presentation** — higher resolution, texture filtering, anti-aliasing,
  widescreen and higher frame rates, as described to the user on 2026-09-23.
- The decomp's old `PORT_DIFFERENCES.md` inventory was deleted on 2026-09-27
  (superseded by `FIRST_LEVEL_AUDIT.md` and `FIRST_LEVEL_CENSUS.md`).

## Queued work (after the first level is faithful)

1. **Framebuffer-comparison harness.** Take the original frame from the GS
   memory of PCSX2 snapshots made with the software renderer (the decomp's
   `build/s87/c7cap/fb2/<point>/displayed.bin`, 19 route points, recorded
   2026-09-26 by `tools/c7cap_partb.py fb2`; the hardware-renderer route
   snapshots hold no rendered frame). The software renderer is PCSX2's model
   of the GS, not real hardware. Render the port at the same
   game state in the Original profile. Diff the two pixel by pixel. This turns
   "looks like the original" into a number. **Built 2026-09-28**
   (`tools/test_fb2_pixels.py`, `make test-fb2-pixels`; GS_EXACT.md section
   10 has the method and the numbers per point). Since item 2 it compares
   the GS model's field word for word (with the GPU renderer it samples the
   Metal frame at the GS sample points).
2. **GS-exact Original rendering.** Render each field at 512x224 as the GS does, apply the GS blend, fog,
   alpha-test and dither rules exactly, and scale to 4:3 without filtering.
   **Built 2026-10-03** for the world frame (chain step GSFRAME; GS_EXACT.md
   section 9): the CPU GS model draws every world frame's GS packets into a
   512x224 field in GS memory, and the platform layer presents it through
   the field-presentation hook (the placeholder only; the user's choice is
   open). The status frames and the 2D overlay pass still draw with the GPU.
   Only the Metal backend (macOS) presents the field today: on the d3d12
   and Vulkan backends the Original profile refuses to start, and
   EM_GPU_RENDERER=1 (the GPU renderer) is needed there (GS_EXACT.md
   section 9, "Backends").
3. **Profile switch plumbing.** One settings struct with an Original value for
   every switch, chosen at launch. Started 2026-09-27: `src/em_settings.{h,c}`
   (`EmSettings`, `em_settings_original`, `em_settings()`), read from the
   environment by `em_settings_from_env` until the launcher sets it. Its
   switches so far are the PS2 disc-drive timing and, since 2026-10-03, the
   renderer (`gpu_renderer`: 0, the Original GS frame; 1, the GPU renderer
   the Enhanced resolution builds on; `LAUNCHER_OPTIONS.md`, BUILT).
4. **Enhancement items**, one switch each, in the order the user picks.

## Options and pending decisions

Every option, with its Original value, and every decision the user still has
to review (for example the presentation of the 512x224 fields) are listed in
`docs/LAUNCHER_OPTIONS.md`, the registry for the future launcher.
