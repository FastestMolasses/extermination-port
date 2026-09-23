# Port profiles — Original and Enhanced

User decision, 2026-09-23. When the port is done it ships **two profiles** built
from **one codebase**:

| | Original | Enhanced |
|---|---|---|
| Purpose | Faithful to the PS2 game in every way that can be measured | The user's improvements |
| Game logic | The translated original code | **The same code.** Differences are opt-in switches, listed below |
| Resolution | The exact framebuffer the GS draws (512x448 for the first level; measure other modes per mode), shown at 4:3 | Native / higher resolution, any aspect ratio |
| Scaling | Nearest-neighbour integer or aspect-correct scaling, **no smoothing** | Filtering and AA of the user's choosing |
| Colours | GS-exact: the same blend, fog, alpha test, dither and clamp arithmetic, so the pixels match | May differ |
| CRT / scanline simulation | **None** (user: "no simulating CRT") | Not planned |
| Frame rate | The original's frame rate and pacing | Higher rates allowed |
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
- **Cut and hidden content** — the decomp's `docs/CURIOSITIES.md`. Examples: the
  light-based stealth system, hidden animation directory entries, passcode
  keypads, dead code shipped on the disc, the 15 named RECON dogtags, the
  infection diary, the unlabelled 7th config row, and cutscene multi-actor
  tracks. Each entry's decode status is recorded there. Only `decoded` entries
  can become switches.
- **Presentation** — higher resolution, texture filtering, anti-aliasing,
  widescreen and higher frame rates, as described to the user on 2026-09-23.
- The decomp's `docs/PORT_DIFFERENCES.md` (2026-06-11) is the old inventory of
  where the port differs from the engine. It is mostly about fidelity bugs, not
  enhancements, and like every older claim it has to be checked.

## Queued work (after the first level is faithful)

1. **Framebuffer-comparison harness.** Take the original frame from the GS
   memory in PCSX2 snapshots (`tools/pcsx2_session.py` snapshots and the route
   captures in `../Extermination/build/s87/route/`). Render the port at the same
   game state in the Original profile. Diff the two pixel by pixel. This turns
   "looks like the original" into a number.
2. **GS-exact Original rendering.** Render at 512x448, apply the GS blend, fog,
   alpha-test and dither rules exactly, and scale to 4:3 without filtering.
3. **Profile switch plumbing.** One settings struct with an Original value for
   every switch, chosen at launch. Rendering switches go first.
4. **Enhancement items**, one switch each, in the order the user picks.
