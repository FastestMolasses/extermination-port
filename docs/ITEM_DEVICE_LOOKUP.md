# Item device lookup (00185420 / 00184D20)

`em_item_device_find()` is original `00185420` with its predicate
`00184D20` (both byte-matched decomp C). Its input is the frozen published
interaction list, in the order of original `D_00275B5C`, with each actor's
current status, class, subtype, shape, armed byte and descriptor. It does
not build or sort that list, move the player, arm an owner, or arbitrate by
distance.

`00185420(item)` returns the first entry with status bit 0, class bit 7 and
armed byte 0 that `00184D20(D_008102B0, entry, item)` accepts. The
predicate's first stage crosses the class (`+2 & 0x1F`) with the item id and
the subtype (`+3`):

| Class | Item | Subtypes accepted | Caller |
|---|---|---|---|
| 4 | 0x1B..0x1D (the batteries) | 0x14, 0x22..0x26, 0x2C | BATTERY 002149F0 (`0x1B` + slot) |
| 4 | 0x20 (the infection cure) | 0x36 (the AREA02 bed, 00159620) | HEALING 002160B0 kind 2 |
| 4 | 0x25 / 0x26 / 0x27 | 0x21 / 0x49 / 0x35 | EVENT 00215870 (`0x23` + entry) |
| 6 | 0x1B..0x1D | as class 4 | BATTERY |
| 6 | 0x23 | 0x2F, 0x12 | EVENT |
| 6 | 0x24 | 0x13, 0x12, 0x2F | EVENT |

Every other pair rejects (class 6's 0x26 / 0x28 arm included), so every
item id is defined. Until 2026-10-30 only the batteries were, and any other
id fail-stopped (the cure from the menu near the AREA02 bed:
LEVEL2_CRASHES.md "AREA02 (lane B2)").

The second stage is the shape and facing test. Shapes 0/1/2 use the pi/4
facing limit; shapes 3/4 use pi/2, with the original lower-height allowance
(17) and the near-distance exception (7); shape 5 uses the fixed 14-unit
planar radius and 4-unit height band. Unknown shapes take shape 0's branch.
Shape 1's class-0xA arm is unreachable (the first stage admits only classes
4 and 6).

The native function uses the recovered SDK integer square root and the
original atan2 coefficients; every scalar add and product is rounded
separately (em_ee_float.h); the angle wrap turns -pi into +pi. Host yaw
magnitudes above 16 radians are refused, so invalid huge input cannot stall
the repeated-subtraction wrap.

`make test-item-device-reference` (about 2 s) executes both original
functions and their SDK calls for 7,594 cases:
- every item id 0x00..0x2F (and 0x7F, 0xFFFFFFFF) against the classes 4, 6,
  5 and 0xA and every subtype the predicate names (102 accepted pairs);
- the shapes, facing and ranges for the batteries and for the cure 0x20 at
  a class-4 subtype-0x36 owner;
- the active and armed gates, a refused owner ahead of the bed, and every
  permutation of a four-device list.
It proves the pure helper, not a host's published-list construction.

Hosts:
- AREA01 / AREA02 (em_area01_interaction_live, `00185420` on the live
  composition's list): any owner found is the page's device. HEALING kind 2
  hands it to 0015C750 (the bed); BATTERY reads the panel or the console.
  The menu uses are compared row for row with fork recordings in the
  revisit harness (a02_s1 at the bed, a01r_s2 with no device;
  LEVEL3_BINDING.md "2026-10-30, lane B2").
- AREA11 (em_area11_interaction_host `page_find_device`): the battery panel
  is the only owner the host can name. Another owner found faults (an
  unsupported owner); no owner gives the original's no-device banner.
