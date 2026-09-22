# ITEM cursor SDK arithmetic

The live status host can bind `EmItemMath` through
`em_item_sdk_math_bind()`. Its `EmItemSdkMath` context must remain alive
while the status runtime uses those workers. The initializer copies the
19 original atan coefficients from the already loaded interaction resource;
it does not generate a replacement table.

The UI uses SDK sine `0011E2A8` and cosine `0011DE90`, which are different
from the camera matrix SDK's trigonometric implementation. The native
module retains their quadrant selection, `0011C7B0` reduction and
`0011D770`/`0011CCC8` kernels, with an explicit rounding step for each EE
scalar sum and product. The supported input domain is the finite
`[-float(pi), float(pi)]` interval actually supplied by the UI's atan2.
The larger SDK range reducer is deliberately outside this module's scope.

Square root follows the integer digit-by-digit body `0011CB90`, including
its final tie-even correction. It accepts finite nonnegative binary32
inputs and preserves signed zero. The wrapper `0011E748` adds no error
path for those inputs. Both the trigonometric workers and square root
return NaN for inputs outside their documented domains.

The atan worker reuses `em_interaction_sdk_atan2()` and its original
coefficient ordering. For a neutral stick, the outer `0011E620` wrapper
returns positive zero and writes error `0x21`. The native worker records
that error in its explicit UI math context; it does not modify host libc
errno or install a process-global SDK error service. Error state persists
through later ordinary calls, as in the original wrapper.

`make test-item-sdk-math-reference` executes the user's pinned original
ELF directly in a bounded instruction oracle. It compares:

- 2,962 sine/cosine results across 1,481 angles, including reduction and
  cancellation boundaries and signed zero;
- 922 square-root results, including positive subnormals and large finite
  inputs;
- all four signed-zero atan2 combinations through the full wrapper,
  software double conversions, default error helper and errno store;
- 1,183 raw stick-byte pairs through the complete `001B62C0` SDK call
  chain, with all four output floats compared by bits;
- 43 complete cursor ring callbacks and 22,016 fixed-point triangles,
  including the original software float-to-integer helper. Only the
  final draw-mode service is intercepted.

These checks extend the earlier trail test, which deliberately supplied
the same host libm values to both sides. The new test has no host libm
substitution in the original numerical call chain. Its remaining limits
are the bounded EE arithmetic model and final GS/Metal rasterization;
the checks do not establish general emulator equivalence.

The same runner executes the fixed `001D2610(0)` scope-reset path,
including `001D2590` and the original SDK tangent. Across twelve mode,
flag and fog-range combinations, it submits projection distance
`480.3695983886719` (binary32 `0x43F02F4F`) to `001D25F0`, then submits the
unchanged rig `+F8/+FC` values to the fog setter `0021B970`. This is a
verified constant for argument zero, not a general scope-zoom substitute.
