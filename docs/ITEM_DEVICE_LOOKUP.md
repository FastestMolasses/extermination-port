# BATTERY device lookup

`em_item_device_find()` implements the battery-item branch of original
`00185420`/`00184D20`. Its input is the actual frozen published interaction
list, in the same order as original `D275B5C`, with each actor's current
status, class, subtype, shape, armed byte and descriptor. It does not build
or sort that list, move the player, arm an owner, or perform nearest-object
arbitration.

`00185420` returns the first eligible entry. It requires status bit0,
class bit7 and armed byte0. For battery item1B/1C/1D, `00184D20` admits
class4 or6 with subtype14,22,23,24,25,26 or2C, then evaluates the original
shape and facing test. Shapes0/1/2 use the pi/4 facing limit; shapes3/4
use pi/2, with the original lower-height allowance and near-distance
exception. Shape5 uses the fixed14-unit planar radius and4-unit height
band. Unknown shapes take the original shape0 branch.

The native function uses the recovered SDK integer square root and original
atan2 coefficients. Every scalar add/product is rounded separately; the
original angle wrap converts negative pi to positive pi. Host yaw magnitudes
above16 radians are rejected so invalid huge input cannot stall a repeated
subtraction loop. The verified first-level data lies within that domain.

`make test-item-device-reference` executes both original functions and their
SDK calls for2,764 predicate and ordered-list cases. It covers accepted and
rejected type/shape combinations, three battery IDs, geometry/facing,
active/armed gates, and every permutation of a four-device list. It proves
the pure helper, not correctness of a host's published-list construction.

The current status adapter owns an optional associated type24 panel. The
host may report available only when this lookup's first result is that
same panel. A null lookup may produce the original no-device banner. A
different eligible result—including any result while the adapter has no
associated panel—must remain an explicit unsupported-owner fault until
its real dialog and callback are bound. It must not be reported as an
empty lookup or silently substituted with the nearby panel.
